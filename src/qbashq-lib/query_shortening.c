// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>


#include "../shared/unicode.h"
#include "../shared/utility_nodeps.h"
#include "../shared/QBASHER_common_definitions.h"
#include "../utils/dahash.h"
#include "QBASHQ.h"
#include "query_shortening.h"


static int all_digits(u_char *wd) {
  while (*wd) {
    if (!isdigit(*wd++)) return 0;
  }
  return 1;
}


#if defined(WIN64) || defined(__APPLE__)  // Annoying that Windows, MacOS and GNU don't agree about how to
                                          // make a reentrant comparison function for qsort_[rs]()
static int winmac_cmp_freak(void *context, const void *ip, const void *jp) {
  // Sort into order of descending frequency
  // Windows version -- different name to Linux
  int *i = (int *)ip, *j = (int *)jp;
  u_ll *freaks = (u_ll *)context;
  if (freaks[*i] < freaks[*j]) return 1;
  if (freaks[*i] > freaks[*j]) return -1;
  return 0;  
}

#else

static int cmp_freak(const void *ip, const void *jp, void *context) {
  // Sort into order of descending frequency
  int *i = (int *)ip, *j = (int *)jp;
  u_ll *freaks = (u_ll *)context;
  if (freaks[*i] < freaks[*j]) return 1;
  if (freaks[*i] > freaks[*j]) return -1;
  return 0;  
}
#endif


void create_candidate_generation_query(query_processing_environment_t *qoenv,
					      book_keeping_for_one_query_t *qex) {
  // If query shortening is in force, just make cg_qterms a copy of qterms.
  // Otherwise, apply a series of heuristics to try to reduce the length of
  // the query to the desired level.  Don't go shorter than the desired level,
  // and leave compound terms alone:
  //    1. Remove non-existent words
  //    2. *** NO LONGER DONE, BECAUSE IT'S NOW FASTER TO LEAVE THEM IN.  Remove repeated words
  //    3. Remove words which are all digits
  //    4. Remove the words with the highest occurrence frequency
  // 
  int t, u, distinct_terms = 0;
  byte *vocab_entry;
  u_char *r, *w;
  BOOL explain = (qoenv->debug >= 1), repeated;
  qex->shortening_codes = 0;

  // Count the number of DISTINCT words.  That's now how the threshold is determined.
  for (t = 0; t < qex->qwd_cnt; t++) {
    r = qex->qterms[t];
    repeated = FALSE;
    if (r[0] != '"' && r[0] != '[') {
      // Look for repetitions of top level single-word terms
      for (u = t - 1; u >= 0; u--) {
	if (!strcmp((char *)qex->qterms[t], (char *)qex->qterms[u])) {
	  repeated = TRUE;
	  break;  // Out of the inner loop;
	}
      }
    }
    if (! repeated) distinct_terms++;
  }	
  

  if (qoenv->query_shortening_threshold == 0
      || distinct_terms <= qoenv->query_shortening_threshold) {
    // Not shortening or no need to shorten
    for (t = 0; t < qex->qwd_cnt; t++) {
      qex->cg_qterms[t] = qex->qterms[t];
    }
    qex->cg_qwd_cnt = qex->qwd_cnt;
  } else {
    BOOL *zap;
    byte ig1;
    int newt, u, v, *freaki;
    u_char *wd;
    u_ll occurrence_count, ig2, *freaks;
;

    qex->cg_qwd_cnt = qex->qwd_cnt;
    if (explain) printf("     Going to try to shorten from %d to %d terms\n",
			qex->cg_qwd_cnt, qoenv->query_shortening_threshold);
    
    zap = (BOOL *)cmalloc(qex->qwd_cnt * sizeof(BOOL), (u_char *)"qshort zap", FALSE);
    freaks = (u_ll *)cmalloc(qex->qwd_cnt * sizeof(u_ll), (u_char *)"qshort freaks", FALSE);
    freaki = (int *)cmalloc(qex->qwd_cnt * sizeof(u_ll), (u_char *)"qshort freaki", FALSE);
    for (t = 0; t < qex->qwd_cnt; t++) {
      zap[t] = FALSE;
      freaks[t] = 0;
      freaki[t] = t;
    }

    //    1. Remove non-existent words 
    for (u = 0; u < qex->qwd_cnt; u++) {
      wd = qex->qterms[u];
      if (*wd == '"' || *wd == '[') continue;  // Never zap phrases or disjunctions
      vocab_entry = lookup_word(wd, qoenv->ixenv->vocab, qoenv->ixenv->vsz, qoenv->debug);
      if (vocab_entry == NULL) {
	// Term not found.  Zap it!
	zap[u] = TRUE;
	qex->shortening_codes |= SHORTEN_NOEXIST;
	if (explain) printf("     Zapped non-existent term %d\n", u);
	--qex->cg_qwd_cnt;
	--distinct_terms;  // Don't break cos we want to remove ALL non-existent words
	freaks[u] = 0;
      } else {
	// Save the occurrence frequency to avoid extra lookups.
	vocabfile_entry_unpacker(vocab_entry, MAX_WD_LEN + 1, &occurrence_count, &ig1, &ig2);
	freaks[u] = occurrence_count;
      }
    }

    if (distinct_terms > qoenv->query_shortening_threshold) {
      //    3. Remove words which are all digits
      for (u = 0; u < qex->qwd_cnt; u++) {
	if (zap[u]) continue;  // Already zapped
	wd = qex->qterms[u];
	if (*wd == '"' || *wd == '[') continue;  // Never zap phrases or disjunctions
	if (all_digits(wd)) {
	  zap[u] = TRUE;
	  qex->shortening_codes |= SHORTEN_ALL_DIGITS;
	  if (explain) printf("     Zapped all-numeric term %d\n", u);
	  --qex->cg_qwd_cnt;
	  if (--distinct_terms <= qoenv->query_shortening_threshold) break;
	}
      }
    }


    if (distinct_terms > qoenv->query_shortening_threshold) {
      //    4. Remove the words with the highest occurrence frequency
#ifdef WIN64
      // Annoying that Windows, MacOS and Linux don't agree on how to do reentrant qsort.
      qsort_s(freaki, qex->qwd_cnt, sizeof(int), winmac_cmp_freak, (void *)freaks);
#elif defined(__APPLE__)
      qsort_r(freaki, qex->qwd_cnt, sizeof(int), (void *)freaks, winmac_cmp_freak);
#else      
      qsort_r(freaki, qex->qwd_cnt, sizeof(int), cmp_freak, (void *)freaks);
#endif
       
      for (u = 0; u < qex->qwd_cnt; u++) {
	v = freaki[u];
	if (zap[v]) continue;  // Already zapped
	if (freaks[v] < 100) break;  // Don't want to zap rare terms?
	zap[v] = TRUE;
	qex->shortening_codes |= SHORTEN_HIGH_FREQ;
	if (explain) printf("     Zapped high frequency (%llu) term %d\n", freaks[v], v);
	--qex->cg_qwd_cnt;
	if (--distinct_terms <= qoenv->query_shortening_threshold) break;
      }
    }
 
 
    // Set up the shortened (candidate generation query) by copying over
    // all the terms that haven't been zapped.  Also create qex->candidate_generation_query
    // by concatenating all the unzapped terms (with spaces).
    // 
    newt = 0;
    for (t = 0; t < qex->qwd_cnt; t++) {
      if (!zap[t]) {
	qex->cg_qterms[newt++] = qex->qterms[t];
      }
    }
    qex->cg_qwd_cnt = newt;
    free(freaki);
    free(freaks);
    freaks = NULL;  // It's non-local
    free(zap);
  } // end of shortening branch of else

  w = qex->candidate_generation_query;
  for (t = 0; t < qex->cg_qwd_cnt; t++) {
    r = qex->cg_qterms[t];
    while (*r) *w++ = *r++;
    *w++ = ' ';
  }


  *(--w) = 0;  // remove last space and null terminate
  if (explain) printf("     Shortened query {%s} has %d terms\n",
		      qex->candidate_generation_query, qex->cg_qwd_cnt);
}


