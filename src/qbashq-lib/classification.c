// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <math.h>
#ifdef WIN64
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#endif

#include "../shared/unicode.h"
#include "../shared/utility_nodeps.h"
#include "../shared/QBASHER_common_definitions.h"
#include "../utils/dahash.h"
#include "QBASHQ.h"
#include "classification.h"

static char *lyrics_prefixes[] =
  {
    "printable ",
    "show me ",
    "original ",
    "show me ",
    "show me the ",
    "what are the ",
    "a song with the ",
    "music and ",
    "song ",

    ""
  };

static char *lyrics_suffixes[] =
  {
    "of ",
    "to ",
    "for ",
    ""
  };

static char *words_suffixes[] =
  {
    "to the song ",
    "to song",
    "to",
    ""
  };


static char *lyrics2_prefixes[] =
  {
    " printable",
    " full",
    //" english",
    " original",
    " clean",
    " music and",
    " in the",
    ""
  };


static char *lyrics2_suffixes[] =
  {
    //" in english",
    //" english",
    " to print",
    " clean version",
    " and music",
    " by",
    ""
  };


static char *final_removals_anywhere[] =
  {
    //"in english",
    "clean",
    "song",
    "original",
    "words",
    "lyrics",
    ""
  };


static char *final_removals_at_tail[] =
  {
    " clean version",
    //" in english",
    " to print",
    ""
  };


// In the segment-specific rules section we may assume that qstring has been lower-cased, has had non-operator punctuation removed,
// has had leading and trailing blanks removed, and is null-terminated.


#define MAX_PATTERN 100

int apply_lyrics_specific_rules(char *qstring) {
  // If the qstring includes one of many patterns identified by Developer2 (email of 18 Feb 2015) then 
  // remove the lyrics-intent-specific parts of qstring and return non-zero.
  // We assume qstring has been lower cased and space-normalized
  char pattern[MAX_PATTERN + 1], *r, *w, *ss, *tail;
  int yes = 0, p, s;

  if (0) printf("Applying lyrics-specific rules\n");

  // Step 1: first look for one specific double pattern:
  if (!strncmp(qstring, "a song with ", 12) && (tail = (char *)tailstr((u_char *)qstring, (u_char *)" in the lyrics")) != NULL) {
    *tail = 0;  // Null out the tail bit
    substitute((u_char *)qstring, (u_char *)"a song with ", (u_char *)"", NULL, FALSE);
    yes = 1;
  }

  if (!yes) {
    // Step 2: Look for a set of patterns at the beginning of qstring.  We build up the patterns by 
    // combining a prefix with the word 'lyrics' and appending a suffix.  E.g.
    // "what are the " + "lyrics" + "for"

    for (p = 0;; p++) {
      r = lyrics_prefixes[p];
      w = pattern;
      while (*r) *w++ = *r++;
      strcpy(w, "lyrics ");
      ss = w + 7;
      for (s = 0;; s++) {
	r = lyrics_suffixes[s];
	w = ss;
	while (*r) *w++ = *r++;
	*w = 0;
	if (0) printf("Built up pattern A: '%s'\n", pattern);
	if (!strncmp(qstring, pattern, (w - pattern))) {
	  // Got a match
	  yes = 1;
	  break;
	}
	if (lyrics_suffixes[s][0] == 0) break;
      }
      if (yes) break;
      if (lyrics_prefixes[p][0] == 0) break;
    }
  }


  if (!yes) {
    // Step 3: Didn't find any of those, try "song words "
    strcpy(pattern, "song words ");
    if (!strncmp(qstring, pattern, 11)) yes = 1;
    if (!yes) {
      // Step 4:  Look for "words of", "words for" "words to", etc.
      strcpy(pattern, "words ");
      ss = pattern + 6;
      for (s = 0;; s++) {
	r = words_suffixes[s];
	w = ss;
	while (*r) *w++ = *r++;
	*w = 0;
	if (0) printf("Built up pattern B: '%s'\n", pattern);
	if (!strncmp(qstring, pattern, (w - pattern))) {
	  // Got a match
	  yes = 1;
	  break;
	}
	if (words_suffixes[s][0] == 0) break;
      }
    }
  }

  if (yes) {
    // Remove the triggering pattern.  There may be other clean up to do later. 
    substitute((u_char *)qstring, (u_char *)pattern, (u_char *)"", NULL, FALSE);  // Zap out the pattern
  }
  else {
    // Now try for matches at the tail of qstring
    u_char *tail;
    // First try things like music and lyrics
    for (p = 0; lyrics2_prefixes[p][0] != 0; p++) {
      r = lyrics2_prefixes[p];
      w = pattern;
      while (*r) *w++ = *r++;
      strcpy(w, " lyrics");
      if (0) printf("Built up pattern C: '%s'\n", pattern);
      tail = tailstr((u_char *)qstring, (u_char *)pattern);
      if (tail != NULL) {
	yes = 1;
	*tail = 0;  // Strip it off the tail
	if (0) printf("Non-NULL tail\n");
	break;
      }
    }
    if (!yes) {
      // now try things like lyrics in english
      strcpy(pattern, " lyrics");
      ss = pattern + 7;

      for (s = 0; lyrics2_suffixes[s][0] != 0; s++) {
	r = lyrics2_suffixes[s];
	w = ss;
	while (*r) *w++ = *r++;
	*w = 0;
	if (0) printf("Built up pattern D: '%s'\n", pattern);
	tail = tailstr((u_char *)qstring, (u_char *)pattern);
	if (tail != NULL) {
	  // Got a match
	  yes = 1;
	  *tail = 0;  // Strip it off the tail
	  break;
	}
	if (lyrics2_suffixes[s][0] == 0) break;
      }
    }
  }

  if (0 && yes) printf("already matched\n");
  if (!yes) yes = substitute((u_char *)qstring, (u_char *)"youtube lyrics", (u_char *)"", NULL, FALSE);
  if (!yes) yes = substitute((u_char *)qstring, (u_char *)"lyrics by", (u_char *)"", NULL, FALSE);
  if (!yes) yes = substitute((u_char *)qstring, (u_char *)"lyrics", (u_char *)"", NULL, TRUE);

  // Now a final clean up if we triggered a Yes
  if (yes) {
    if (0) printf("Final cleanup\n");

    for (p = 0; final_removals_anywhere[p][0] != 0; p++) {
      substitute((u_char *)qstring, (u_char *)final_removals_anywhere[p], (u_char *)"", NULL, TRUE);  // Zap out the pattern
    }
    for (p = 0; final_removals_at_tail[p][0] != 0; p++) {
      tail = (char *)tailstr((u_char *)qstring, (u_char *)final_removals_at_tail[p]);
      if (tail != NULL)  *tail = 0; // Zap out the pattern
    }


  }
  return yes;
}


static char *carousel_removals_anywhere[] =
  {
    "names of",
    "names",
    "list of",
    "list",
    "best",
    "greatest",
    "by",
    "famous",
    "popular",
    "with",
    "actor",
    "most",
    "authored",
    "new",
    ""
  };


int apply_carousel_specific_rules(char *qstring) {
  // If the qstring includes one of many patterns then 
  // remove the carousel-intent-specific parts of qstring and return non-zero
  // We assume qstring has been lower cased and space-normalized
  int yes = 0, p, substitutions;

  if (0) printf("Applying carousel-specific rules\n");

  for (p = 0; carousel_removals_anywhere[p][0] != 0; p++) {
    substitutions = substitute((u_char *)qstring, (u_char *)carousel_removals_anywhere[p], (u_char *)"", NULL, TRUE);  // Zap out the pattern
    if (substitutions > 0) {
      yes = 1;
    }
  }

  return yes;
}


int apply_magic_songs_specific_rules(char *qstring) {
  // According to Developer2's interpretation of Fang Zhang's rules, the word lyrics (regardless of case)
  // should only be removed at the beginning XOR the end, not in the middle.
  int yes = 0;
  BOOL verbose = FALSE;
  char *r, *w;
  if (verbose) printf("BEFORE: %s\n", qstring);
  if (!strncmp(qstring, "lyrics ", 7)) {
    w = qstring;
    r = w + 7;
    while (*r) *w++ = *r++;
    *w = 0;
    yes = 1;
    if (verbose) printf("AFTER: %s\n", qstring);
    return yes;
  }
  if ((r = (char *)tailstr((u_char *)qstring, (u_char *)" lyrics")) != NULL) {
    *r = 0;
    yes = 1;
    if (verbose) printf("AFTER: %s\n", qstring);
    return yes;
  }
  if (verbose) printf("AFTER: %s\n", qstring);
  return yes;
}


int apply_magic_movie_specific_rules(char *qstring){
  int yes = 0;
  yes = substitute((u_char *)qstring, (u_char *)"movie about", (u_char *)"", NULL, FALSE);
  if (!yes) yes = substitute((u_char *)qstring, (u_char *)"movie that", (u_char *)"", NULL, FALSE);

  if (yes) {
    // We know it's a magic_movie, apply Developer2's query treatments....
  }

  return yes;
}


int apply_academic_specific_rules(char *qstring){
  int yes = 0;
  return yes;
}


int apply_wikipedia_specific_rules(char *qstring){
  int yes = 0;
  return yes;
}


int apply_amazon_specific_rules(char *qstring){
  int yes = 0;
  return yes;
}


void classifier_validate_settings(query_processing_environment_t *local_qenv, book_keeping_for_one_query_t *qex) {
  // Various combinations of options don't make sense when operating as a classifier.  Let's make sure
  // everything is set appropriately.
  // 
  // Note that the local_qenv may in fact be the global one.
  int mld;

  //local_qenv->max_to_show = 1;  // No. leave that to the driver

  local_qenv->auto_partials = FALSE;
  if (!qex->query_contains_operators && (local_qenv->classifier_mode == 1 || local_qenv->classifier_mode == 3)) {
    // The lexical similarity function is essentially query-length divided by a denominator.  The denominator
    // is a sum including document-length.  No candidate will be included if this fraction is less than 
    // local_qenv->classifier_threshold.  We can use this constraint to set a value of max_length_diff to quickly 
    // eliminate candidates which can't make it.

    // Problem is that the qwd_cnt should include all the words in a phrase because they match multiple 
    // words in the document.  Also, with disjunctions and nested disjunctions and phrases the calculation becomes too
    // difficult.


    mld = (int)(qex->qwd_cnt / local_qenv->classifier_threshold + 0.999999) - qex->qwd_cnt;
    if (0) printf("Max_length_diff changed from %d to %d for %d query words and threshold of %.3f\n",
		  local_qenv->max_length_diff, mld, qex->qwd_cnt, local_qenv->classifier_threshold);
    if (mld < local_qenv->max_length_diff) {
      local_qenv->max_length_diff = mld;
    }
  }
}


double get_global_idf(query_processing_environment_t *qoenv, u_char *wd) {
  // NOTE: wd is looked up case-sensitively, assuming wd is UTF8-lowercased prior to call

  // Since version 1.5.0 we use a field in the .vocab file to get a quantized idf and make no use at
  // all of the .global_idfs file.

  byte *vocab_entry, lwd[MAX_WD_LEN + 1];
  double idf, N;
  u_ll ig1, ig2;
  byte qidf; 

  N = (double)(qoenv->ixenv->dsz / DTE_LENGTH);  // Relatively quick way to determine no. of documents
  strncpy((char *)lwd, (char *)wd, MAX_WD_LEN);
  lwd[MAX_WD_LEN] = 0;

  vocab_entry = lookup_word(wd, qoenv->ixenv->vocab, qoenv->ixenv->vsz, qoenv->debug);
  if (vocab_entry == NULL) idf = log(N);   // Same as a term which occurs only once.
  else { 
    vocabfile_entry_unpacker(vocab_entry, MAX_WD_LEN + 1, &ig1, &qidf, &ig2);
    idf = get_idf_from_quantized(N, 0XFF, qidf);
  }
  qoenv->global_idf_lookups++;
  if (0) printf("global_idf(%s) = %.4f\n", wd, idf);
  return idf;
}



#define MATCHES_WORD_IN_PHRASE (u_char *)1000


static int term_match(u_char **dwds, int dwd_cnt, int *d, u_char *qterm, int debug) {
  // Try to match qterm (which may be a complex term like '["hey jude" "love me do"]'
  // against the document words starting at dwds[d].  Return the number of document
  // words matched and, if necessary, update d to point to the last word matched.
  u_char *q, qsave, *wdst;
  int local_d = *d, dwds_matched = 0;

  if (debug >= 2) printf("Trying to term_match '%s' in candidate and '%s' in query. local_d = %d dwd_cnt = %d\n", dwds[*d], qterm, local_d, dwd_cnt);

  if (qterm[0] == '[') {
    // We need to break the disjunction into its component elements and test them individually
    q = qterm + 1;
    while (*q && *q != ']') {  // loop around the words and phrases to the end of this disjunction
      if (*q == '"') {
	// This element of the disjunction is a phrase
	q++;  // skip over the quote
	while (*q && *q != '"') {  // Loop over the words in this internal phrase
	  if (*d >= dwd_cnt || dwds[*d] <= (u_char *)MATCHES_WORD_IN_PHRASE) {
	    *d = local_d;
	    if (0) printf("come to the end of the dwds array or to an already matched word. dwd_cnt = %d\n", dwd_cnt);
	    return 0;   // --------------------------------------->
	  }
	  wdst = q;
	  if (0) printf(" .. head of loop within internal phrase.  Looking at '%s'.  *d = %d dwd_cnt = %d\n", wdst, *d, dwd_cnt);
	  while (*q && *q != ' ' && *q != '"') q++;
	  qsave = *q;
	  *q = 0;
	  if (*d >= dwd_cnt) {
	    *d = local_d;  // Reset the words array pointer because we've failed to match
	    if (0) printf("Come to the end of the dwds array in internal phrase. dwd_cnt = %d\n", dwd_cnt);
	    dwds_matched = 0;
	    // Keep going because another element of the disjunction might match
	  }

	  if (!strcmp((char *)wdst, (char *)dwds[*d])) {
	    *q = qsave;
	    if (0) printf("Matched one word within internal phrase(%s, %s)\n", dwds[*d], wdst);
	    (*d)++; // Try to move to the next word
	    dwds_matched++;
	  }
	  else {
	    *d = local_d;
	    *q = qsave;
	    if (0) printf("Failed to match one word within internal phrase(%s, %s)\n", dwds[*d], wdst);
	    dwds_matched = 0;
	    // Keep going because another element of the disjunction might match
	  }

	  *q = qsave;
	  if (qsave != '"') q++;
	}  // End of loop over internal phrase 

	if (dwds_matched > 0) {
	  // We've matched all the words in the phrase
	  if (0) printf("term_match returning from within disjunction: %d\n", dwds_matched);
	  (*d)--;  // We're pointing one beyond the phrase end.
	  return dwds_matched;   // --------------------------------------------------->
	}
	q++;
      }  //
      else {
	// This element of the disjunction is a word
	wdst = q;
	while (*q &&* q != ']' && *q != ' ') q++;
	qsave = *q;
	*q = 0;
	if (!strcmp((char *)wdst, (char *)dwds[local_d])) {
	  *q = qsave;
	  if (debug >= 1) printf("Matched(%s, %s)\n", dwds[local_d], qterm);
	  *q++ = qsave;
	  return 1;   // --------------------------------------->
	}
	*q++ = qsave;
      }
    }  // End of loop around the words and phrases to the end of this disjunction
  }
  else if (qterm[0] == '"') {
    // We have to try to match each of the words in the phrase with each of the document words
    if (0) printf("We've encountered a phrase %s\n", qterm);
    q = qterm + 1;
    wdst = q;
    while (*q && *q != '"') {
      if (*q == '[') {
	// We've encountered a disjunction within the phrase
	BOOL success = FALSE;
	if (0) printf("We've encountered a disjunction within the phrase %s\n", q);
	if (*d >= dwd_cnt) {
	  *d = local_d;
	  if (0) printf("come to the end of the dwds array. dwd_cnt = %d\n", dwd_cnt);
	  return 0;   // --------------------------------------->
	}
	q++;
	while (*q && *q != ']') {   // Loop over words within the disjunction
	  wdst = q;
	  while (*q && *q != ' ' && *q != ']') q++;  // Skip to the end of the word.
	  qsave = *q;
	  *q = 0;
	  if (0) printf("KOMPARING %s and %s\n", wdst, dwds[*d]);
	  if (!strcmp((char *)wdst, (char *)dwds[*d])) {
	    // We've got a match:  Restore the old *q and skip forward to the close ]
	    *q = qsave;
	    while (*q && *q != ']') q++;
	    dwds_matched++;
	    success = TRUE;
	    *q = qsave;
	    break;   // ------->
	  }
	  // *q = qsave;  This spurious line caused the production SEGFAULT 24 Aug 2016
	  if (*q) q++;
	}
	if (!success) return 0;  // ---------------------------------------------->
	if (*q) q++;
	if (*q == ' ') q++;
	wdst = q;
	if (0) printf("Success with the disjunction.  Now looking at '%s'\n", q);
	(*d)++; // Try to move to the next word
      }
      else {
	while (*q && *q != ' ' && *q != '"') q++;
	qsave = *q;
	*q = 0;
	if (*d >= dwd_cnt || dwds[*d] <= (u_char *)MATCHES_WORD_IN_PHRASE) {
	  *d = local_d;
	  if (0) printf("come to the end of the dwds array or to an already matched word. dwd_cnt = %d\n", dwd_cnt);
	  return 0;   // --------------------------------------->
	}
	if (0) printf("KOMPARING2 %s and %s\n", wdst, dwds[*d]);
	if (!strcmp((char *)wdst, (char *)dwds[*d])) {
	  *q = qsave;
	  if (0) printf("Matched one word(%s, %s)\n", dwds[*d], wdst);
	  (*d)++; // Try to move to the next word
	  dwds_matched++;
	}
	else {
	  *d = local_d;
	  *q = qsave;
	  if (0) printf("Failed to match one word(%s, %s)\n", dwds[*d], wdst);
	  return 0;   // --------------------------------------->
	}
	*q = qsave;
	if (*q) q++;
	wdst = q;
      }
    }  // End over loop over phrase
    // We've matched all the words in the phrase
    if (0) printf("       try-to_match returning %d\n", dwds_matched);
    (*d)--;  // We're pointing one beyond the phrase end.
    return dwds_matched;
  }
  else {
    // In the simplest case (single query word) all we need is a strcmp()
    if (strcmp((char *)dwds[local_d], (char *)qterm)) return 0;
    else return 1;
  }

  return 0;
}


double classification_score(query_processing_environment_t *qoenv, book_keeping_for_one_query_t *qex,
			    unsigned long long *dtent, u_char *dc_copy,	size_t dc_len, int dwd_cnt,
			    byte *match_flags, double *FV, u_int *terms_matched_bits) {
  // dc_copy is the copied, case-folded and substituted content of a document matching 
  // the query represented by qwds (an array of query words) and qwd_cnt (how many words there are in the query.)
  // This function first breaks up the document content into words then finds the segment of the document which 
  // best matches the document and then, from it, calculates a lexical similarity
  // score (in the range 0 - 1) between the query and the document which can be thresholded to give a yes
  // or no classification

  // Classifier_mode 1 - DOLM score
  // Classifier_mode 2 - DOLM score using IDFs rather than counts
  // Classifier_mode 3 - Jaccard score
  // Classifier_mode 4 - Jaccard score using IDFs rather than counts.

  //
  // The DOLM score is calculated as Q / (D + I + M + S) where 
  //  Q is the number of query words matched by this document
  //  D is the number of words in this document
  //  I is the number of non-query words inserted into what would otherwise be a phrase match
  //  M is the number of query words not matched by this document
  //  S is the number of swaps needed to put the words in the best match segment into the same order as the query.


  // qwds[i] normally represents the i-th word in the query.  However, depending upon the mode it may represent
  // a more complex term.  E.g. 
  //	  - "a simple phrase"
  //	  - "a [complex phrase]"
  //	  - [a simple disjunction]
  //	  - [a "complex disjunction"]

  // match_flags returns a bitmap reporting the type of match encountered. E.g. MF_PHRASE
  // FV is an array of six doubles recording the features used in classification:
  //   0 - Q
  //   1 - D
  //   2 - I
  //   3 - M
  //   4 - S
  //   5 - rectype score.
  //   6 - static score.
  //   7 - Jaccard DOLM  (using counts or IDFs depending upon mode)
  //   8 - Non-Jaccard DOLM (using counts or IDFs depending upon mode)%
  // This function expects FV to be zero on entry

  u_char **dwds = NULL, *doc, **qwds = qex->qterms;
  int d = 0, q = 0, effective_q = 0, span_start = 0, span_end = 0, index_within_span = 0, 
    iqdolm = 0, dwds_matched = 0, iI = 0, actual_dwd_cnt = 0;
  double Q = 0.0, D = 0.0, I = 0.0, M = 0.0, S = 0.0, dolm = 0.0, rslt = 0.0, 
    MWT = 1.0,   // MWT is the penalty for a missing word.
    thresh = (float)(qoenv->classifier_threshold * qex->segment_intent_multiplier);
#if 0
  double denom = 0.0, denom_limit = 0.0;
#endif
  
  BOOL found = FALSE, explain = (qoenv->debug >= 1);
  double score_from_doctable = 0.0, rectype_score = 0.0;
  u_int thisbit;
  //if (qoenv->classifier_mode == 2) test_get_global_idf(qoenv);


  *match_flags = 0;
  *terms_matched_bits = 0;

  if (explain) {
    printf("classification_score( dwds = %d, qwds = %d, thresh = %.1f) -- ", dwd_cnt, qex->qwd_cnt,
	   qoenv->classifier_threshold);
    show_string_upto_nator(dc_copy, '\n', 0);
  }

  if (dwd_cnt <= 0 || qex->qwd_cnt <= 0) {
    if (0) printf("POQ: %d % d\n", dwd_cnt, qex->qwd_cnt);
    return 0.0;  // These conditions probably arise from an earlier error
  }


  if (dc_len > MAX_RESULT_LEN) return 0.0;


  // ====================== This block of code copied from extract_text_features() and modded ==================

  dwds = (u_char **)malloc(dwd_cnt * sizeof(u_char **));  // MAL0007
  if (dwds == NULL) {
    return 0.0;   // Malloc failed is a very serious error, but what can we do?
  }

  if (explain) printf("classification_score(%s): dwd_cnt = %d\n",
				dc_copy, dwd_cnt);

  if (0) printf(" classy dc_copy = '%s'\n", dc_copy);
  // dc_copy is already case folded
  actual_dwd_cnt = utf8_split_line_into_null_terminated_words(dc_copy, dwds, dwd_cnt,
							      MAX_WD_LEN,
							      FALSE, FALSE, FALSE, FALSE);
  if (actual_dwd_cnt != dwd_cnt) {
    int doclen_inwords;
    if (qoenv->debug >= 1) {
      printf("Warning: dwd_cnt, expected %d, got %d in '%s'\n",
	     dwd_cnt, actual_dwd_cnt, dc_copy);
      doc = get_doc(dtent, qoenv->ixenv->forward, &doclen_inwords, qoenv->ixenv->fsz);

      show_string_upto_nator(doc, '\t', 0);

      for (d = 0; d < actual_dwd_cnt; d++) printf(" %3d: %s\n", d, dwds[d]);
      exit(0);  // Can't exit except in debug mode.
    }
    dwd_cnt = actual_dwd_cnt;
  }


  if (0) {
    printf("Doc split into: \n");
    for (d = 0; d < dwd_cnt; d++) printf("  %s\n", dwds[d]);
  }
  // =====================================================================================================
  Q = 0;
  D = 0;
  if (qoenv->classifier_mode == 2 || qoenv->classifier_mode == 4) {
    // Note: dwds[i] have been lower-cased.
    for (d = 0; d < dwd_cnt; d++) D += (float)get_global_idf(qoenv, dwds[d]);
  }
  else {
    D = (float)dwd_cnt;
  }
  iI = 0;
  I = 0;
  M = 0;
  S = 0;
  span_start = dwd_cnt;
  span_end = -1;

  //  ---- These nested loops find a span of words in the document which includes one occurrence of each
  // of the words in the intersection of the query and the document.   Unfortunately this block of code
  // needs to be replaced so as to find the best span rather than the first one.   However, in the meantime,
  // we can use it to develop the rest of the machinery.
  effective_q = 0;

  thisbit = 1 << (qex->qwd_cnt - 1);  
  for (q = 0; q < qex->qwd_cnt; q++) {
    if (explain) printf("Query term[%d] = '%s'. Thisbit= %X\n", q, qwds[q], thisbit);
    found = FALSE;
    for (d = 0; d < dwd_cnt; d++) {
      if (dwds[d] <= (u_char *)MATCHES_WORD_IN_PHRASE) continue;   // Avoid looking at a document word which has already been matched
      if (0) printf("  Attempting to match against doc word %d '%s'\n", d, dwds[d]);
      // term_match() takes care of matching complex terms e.g. disjunction containing phrase(s)
      // as well as simple word matching
      if ((dwds_matched = term_match(dwds, dwd_cnt, &d, qwds[q], qoenv->debug))) {
	//    ***NOTE: The value of d is potentially increased by the term_match() call.  ***
	// We've found the first occurrence of this query term in the doc.  Mark the corresponding
	// document words so we don't match them again if we have repeated query words
	int w;
	if (explain) printf("dwds_matched = %d, d = %d, Q = %.0f\n", dwds_matched, d, Q);
	// mark the matched word, or the last word in a matched phrase with the effective index within the query
	dwds[d] = (u_char *)(long long)effective_q;  // This is the index of the matching word within the query (excluding missings), cast as a pointer
	effective_q++;
	for (w = 1; w < dwds_matched; w++) {
	  dwds[d - w] = MATCHES_WORD_IN_PHRASE;  // Mark the leading words in the phrase
	}
	found = TRUE;
	if (d > span_end) span_end = d;
	w = d - dwds_matched + 1;
	if (w < span_start) span_start = w;
	if (0) printf(" ....... matched! dwds_matched = %d\n", dwds_matched);
	break;
      }
    }

    if (found) *terms_matched_bits |= thisbit;
    thisbit >>= 1;
    if (qoenv->classifier_mode == 2 || qoenv->classifier_mode == 4) {
      if (found) Q += (float)get_global_idf(qoenv, qwds[q]);
      else M += (float)get_global_idf(qoenv, qwds[q]);
      if (0) printf(" g_g_i(%s) = %.4f\n", qwds[q], get_global_idf(qoenv, qwds[q]));
    }
    else {
      if (found) Q += dwds_matched; else M++;
      if (0) printf("       Q = %.3f, M = %.3f, dwds_matched = %d\n", Q, M, dwds_matched);
    }
  }

  // At this point, we have:
  //  - accurate values for D, Q and M
  //  - Indexes of a sub_sequence of the doc containing the first occurrence of each of the query words which are matched.
  // Now lets count I and S

#if 0  // Not sure that this saves any worthwhile time
  denom_limit = Q / thresh + (float) 0.1;  // Once the denominator exceeds this value we can take an early exit (0.1 is a safety allowance)
  denom = D + M;
  if ((qoenv->classifier_mode == 1 || qoenv->classifier_mode == 3)
      && denom > denom_limit) {
    if (explain) printf(" - Early exit because %.3f (= %.3f + %.3f) > %.3f (= %.3f / (%.3f + 0.1))\n",
				  denom, D, M, denom_limit, Q, thresh);
    free(dwds);
    return 0.0;   // --------------------------------------------------> 
  }
#endif

  if (explain) printf("  found a span from %d to %d\n", span_start, span_end);

  index_within_span = 0;
  for (d = span_start; d <= span_end; d++) {
    // dwds[d] is one of three things:
    //	  Case 1. a pointer to the original document word in dc_copy  (an unmatched word)
    //	  Case 2. the index of a word within the query (cast as a pointer), e.g. 3
    //    Case 3. a code MATCHES_WORD_IN_PHRASE (cast as a pointer)
    if (dwds[d] >= dc_copy) {
      // It's still a pointer to the document word, so it must be an insertion.
      if (qoenv->classifier_mode == 2 || qoenv->classifier_mode == 4) {
	// Note: dwds[i] have been lower-cased.
	I += (float)get_global_idf(qoenv, dwds[d]);
	qex->op_count[COUNT_TLKP].count++;
      }
      else {
	I++;
      }
      iI++;
      index_within_span++;
    }
    else if (dwds[d] == MATCHES_WORD_IN_PHRASE) {
      // It's one of the leading words of a phrase.

    } else {
      // It's a query word occurrence 
      if ((long long)dwds[d] != (index_within_span - iI)) {
	// This is half of an out-of-order pair.   Don't know what the penalty should be when we're summing 
	// IDFs
	S += 0.5;
	if (explain)printf("d = %d index_within_span = %d dwds[d] = %lld iI = %d, S = %.4f\n",
				     d, index_within_span, (long long)dwds[d], iI, S);
      }
      index_within_span++;
    }
  }


  free(dwds);    // FRE0007


  if (qoenv->classifier_mode == 3 || qoenv->classifier_mode == 4) {
    // Jaccard and weighted Jaccard
    dolm = Q / (D + M);
    if (explain) {
      printf("Jaccard_score: %.3f = %.3f / (%.3f + %.3f)  Q/(D + M) -- ",
	     dolm, Q, D, M);
      show_string_upto_nator(dc_copy, '\t', 0);
      printf("\n");
    }

  }
  else {
    MWT = 6.0 - Q;  // The shorter the query, the greater the penalty for missing a word.
    if (MWT < 1.0) MWT = 1.0;  // ... but the penalty can't go below  1.0

    dolm = Q / (D + I + MWT * M + S);
    if (explain) {
      printf("Classification_score: %.3f = %.3f / (%.3f + %.3f + %.0f * %.3f + %.3f)  Q/(D + I + MWT * M + S) -- ",
	     dolm, Q, D, I, MWT, M, S);
      show_string_upto_nator(dc_copy, '\t', 0);
      printf("\n");
    }
  }

  if (dolm < thresh) return 0.0;  // --------------------------------------------------->

  // Set match flags
  if (M == 0) {
    *match_flags |= MF_FULL;
    if (S == 0) {
      *match_flags |= MF_SEQUENCE;
      if (I == 0) {
	*match_flags |= MF_PHRASE;
	if (Q == D) *match_flags |= MF_FULL_EXACT;
      } 
    }
  }
  else if (M == 1) *match_flags |= MF_RELAX1;
  else if (M == 2) *match_flags |= MF_RELAX2;

  if (Q == 1 && (!(*match_flags & MF_FULL_EXACT))) *match_flags = MF_FULL;  // Don't allow single word queries to be SEQ or PHRASE matches.

  score_from_doctable = get_score_from_dtent(*dtent);
  if (0) printf("   cf_coeffs: %.5f %.5f %.5f\n", qoenv->cf_coeffs[0], qoenv->cf_coeffs[1], qoenv->cf_coeffs[2]);
  if (qoenv->cf_coeffs[1] == 0.0 && qoenv->cf_coeffs[2] == 0.0)  {   // Don't do this if psi or omega are non zero.
    // quantize the dolm score to the  range 0 - 99 then map that to 0.00 to 0.99
    // and add in the static score divided by 100
    iqdolm = (int)floor(dolm * 99);
    rslt = (((double)iqdolm + score_from_doctable) / 100.0);
    if (0) printf(" IQDOLM %d, %.5f\n", iqdolm, score_from_doctable);
  }
  else {
    rslt = dolm;
    if (qoenv->ixenv == NULL) {
      if (qoenv->debug >= 1) {
	printf("Damn and blast!  qoenv->ixenv is Null\n");
	exit(1);  // Can't exit except in debug mode
      }
      rectype_score = 0;
    } else {
      rectype_score = get_rectype_score_from_forward(dtent, qoenv->ixenv->forward,
						     qoenv->ixenv->fsz, qoenv->extracol);
    }
  }
  if (0) printf("   result:  %.5f\n", rslt);

  FV[0] = Q;
  FV[1] = D;
  FV[2] = I;
  FV[3] = M;
  FV[4] = S;
  FV[5] = rectype_score;
  FV[6] = score_from_doctable;
  FV[7] = Q / (D + M);
  FV[8] = Q / (D + I + M + S);

  if (explain) printf("terms_matched_bits = %X\n", *terms_matched_bits);
  return rslt;
}




static double bag_similarity(byte *s1, byte*s2) {
  // See page 77 of Leskovec, Rajaraman & Ullman, Mining of Massive Datasets  http://mmds.org/
  // We choose the version which results in a similarity of 1.0 if the two strings are identical:
  // The size of the intersection is the sum of the minimum frequencies of each byte in the two strings
  // The size of the union is the sum of the maximum frequencies of each byte in the two strings
  //
  // Although byte rather than character oriented, this should work fairly well for non-ASCII UTF-8
  byte map1[256] = { 0 }, map2[256] = { 0 }, *p;
  int b, intersection_size = 0, union_size = 0, min, max;
  double rslt;

  if (s1 == NULL || s1[0] == 0) {
    if (s2 == NULL || s2[0] == 0) return 1.0;
    else return 0.0;
  }
  if (s2 == NULL || s2[0] == 0) return 0.0;

  // Make the two character maps
  p = s1;
  while (*p) map1[*p++]++;
  p = s2;
  while (*p) map2[*p++]++;

  // Scan the character maps to compute bag intersection and union	
  for (b = 33; b < 256; b++) {  // Ignore spaces and control characters
    min = map1[b];
    if (map2[b] < min) {
      min = map2[b];
      max = map1[b];
    }
    else max = map2[b];
    union_size += max;
    intersection_size += min;
  }
  rslt = (double)intersection_size / (double)union_size;
  if (0) printf("bag_similarity(%s, %s) = %.4f\n", s1, s2, rslt);
  return rslt;
}


static int test1bagsim(byte *s1, byte *s2, double expected) {
  double diff = bag_similarity(s1, s2) - expected;
  if (fabs(diff) > 0.001) {
    printf("Error: bag_similarity(%s, %s) gave %.4f not %.4f\n",
	   s1, s2, bag_similarity(s1, s2), expected);
    return 1;
  }
  return 0;
}


int run_bagsim_tests() {
  int errs = 0;
  errs += test1bagsim((byte *)"", NULL, 1.0);
  errs += test1bagsim((byte *)"", (byte *)"a", 0.0);
  errs += test1bagsim((byte *)"abcdefghijklmnopqrstuvwxyz", (byte *)"abcdefghijklmnopqrstuvwxyz", 1.0);
  errs += test1bagsim((byte *)"abcdefghijklmnopqrstuvwxyz", (byte *)"a b c d e f g h i j k l m n o p q r s t u v w x y z ", 1.0);
  errs += test1bagsim((byte *)"abcdefghijklmnopqrstuvwxyz", (byte *)"abcdefghijklmopqrstuvwxyz", 25.0 / 26.0);
  // Reversed strings should score identically
  errs += test1bagsim((byte *)"abcdefghijklmnopqrstuvwxyz", (byte *)"zyxwvutsrqponmlkjihgfedcba", 1.0);
  errs += test1bagsim((byte *)"abcdefghijklm", (byte *)"nopqrstuvwxyz", 0.0);
  errs += test1bagsim((byte *)"abcdefghijklmn", (byte *)"nopqrstuvwxyz", 1.0 / 26.0);
  errs += test1bagsim((byte *)"abcdefghijklm", (byte *)"abcdefghijklmnopqrstuvwxyz", 0.5);

  if (errs) return -75;
  if (0) printf("successfully completed all bagsim tests.\n");
  return 0;
}


static u_char *code_flags_and_terms_which_matched(query_processing_environment_t *local_qenv,
						  book_keeping_for_one_query_t *qex, 
						  candidate_t *candy, u_char *doc) {
  // Caller's responsibility to free the returned string.
  //
  // Now also responsible for displaying the featre
  size_t space_needed = 15, code_len, space_needed_for_field_3 = 0,
    space_needed_for_jo = 0;
  int q;
  u_char *rezo = NULL, *w, *code;
  u_int termbits = candy->terms_matched_bits, bit_selector;

  if (local_qenv->include_extra_features) {  // ------------- extra features -----------
    space_needed += FV_ELTS * 12;   // Allow for sign, 3 digits before decimal point, 5 digits after, and tab, plus a safety margin of 2.
  }

  bit_selector = 1 << (qex->qwd_cnt - 1);
  if (0) printf("   termbits=%X bit selector = %X\n", termbits, bit_selector);
  // Find out how much space we need and allocate that via rezo
  
  for (q = 0; q < qex->qwd_cnt; q++) {
    if (termbits & bit_selector)
      space_needed_for_field_3 += (strlen((char *)(qex->qterms[q])) + 2);
    bit_selector >>= 1;
  }

  // If we were generating a JO query instead of listing the terms which matched
  // we'd need space as per:
  space_needed_for_jo = strlen((char *)qex->qcopy) + 13; // Allow for "JO: lyrics " plus a couple extra
  if (space_needed_for_jo > space_needed_for_field_3)
    space_needed_for_field_3 = space_needed_for_jo;
  space_needed += space_needed_for_field_3;

  // Insert the code
  if (local_qenv->extracol > 0)
    code = extract_field_from_record(doc, local_qenv->extracol, &code_len);
  else code = NULL;
  if (code != NULL) {
    if (0) printf("      CODE '%s'\n", code);
    space_needed += code_len;
    rezo = (u_char *)malloc(space_needed);
    w = rezo;
    strcpy((char *)w, (char *)code);
    w += code_len;
    free(code);
  }
  else {
    rezo = (u_char *)malloc(space_needed);
    w = rezo;
  }
		
  *w++ = '\t';
  // Insert text version of the match flags followed by a TAB
  if (candy->match_flags & MF_FULL_EXACT) {
    strcpy((char *)w, "EXACT");
    w += 5;
  }
  else if (candy->match_flags & MF_PHRASE) {
    strcpy((char *)w, "PHRASE");
    w += 6;
  }
  else if (candy->match_flags & MF_SEQUENCE) {
    strcpy((char *)w, "SEQ");
    w += 3;
  }
  else if (candy->match_flags & MF_FULL) {
    strcpy((char *)w, "AND");
    w += 3;
  }
  else if (candy->match_flags & MF_RELAX1) {
    strcpy((char *)w, "MISS1");
    w += 5;
  }
  else if (candy->match_flags & MF_RELAX2) {
    strcpy((char *)w, "MISS2");
    w += 5;
  }  
  else {
    strcpy((char *)w, "WEAK");
    w += 4;
  }
  *w++ = '\t';

  // The next field in the result string has (until 01 December 2016) always
  // been a comma-separated list of the query terms which were matched.  However,
  // in order to generate an alternate query path which could be fed into JO,
  // we're now implementing the possibility of generating an augmented query
  // which adds the word lyrics to the original query (unless it already had
  // intent words).

  // We'll need to play around with the triggering conditions for this
  if (local_qenv->generate_JO_path 
      && (!qex->vertical_intent_signaled)
      && (candy->match_flags & MF_FULL_EXACT)) {

    // Currently only defined for classifier_segment lyrics
    if (!strcmp((char *)local_qenv->classifier_segment, "lyrics")) { 
      strcpy((char *)w, "JO: ");
      w += 4;
      strcpy((char *)w, (char *)qex->query_as_processed);
      w+= strlen((char *)qex->query_as_processed);
      strcpy((char *)w, " lyrics");
      w += 7;
    }
  } else {
    // Go round the query terms again and copy the term strings in
    bit_selector = 1 << (qex->qwd_cnt - 1);
    for (q = 0; q < qex->qwd_cnt; q++) {
      if (termbits & bit_selector) {
	space_needed = strlen((char *)(qex->qterms[q]));
	strcpy((char *)w, (char *)(qex->qterms[q]));
	w += space_needed;
	*w++ = ',';
	*w++ = ' ';
      }
      bit_selector >>= 1;
    }
  }

  // This is the extra column for Steve as agreed in our meeting on 04 May 2016
  // It contains a count of the words in the query as actually processed.  Steve's
  // consumer code expects there to be no leading spaces.
  q = qex->qwd_cnt;
  if (q > 999) q = 999;
  *w++ = '\t';
  if (q > 100) {
    int dig[3];
    dig[2] = q % 10;
    q /= 10;
    dig[1] = q % 10;
    q /= 10;
    dig[0] = q % 10;
    *w++ = (byte)(dig[0] + '0');
    *w++ = (byte)(dig[1] + '0');
    *w++ = (byte)(dig[2] + '0');
  }
  else if (q > 10) {
    int dig[2];
    dig[1] = q % 10;
    q /= 10;
    dig[0] = q % 10;
    *w++ = (byte)(dig[0] + '0');
    *w++ = (byte)(dig[1] + '0');
  }
  else {
    *w++ = (byte)(q + '0');
  }

  *w = 0;


  if (local_qenv->include_extra_features) {  // ------------- extra features -----------
    // Guaranteed not to overflow the generous FV_ELTS * 12 bytes allocated, including null termination.
#ifdef WIN64
    sprintf_s((char *)w, (size_t)(FV_ELTS * 12), "\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f",
	      candy->FV[0], candy->FV[1], candy->FV[2], candy->FV[3], 
	      candy->FV[4], candy->FV[5], candy->FV[6], candy->FV[7], candy->FV[8]);
#else
    sprintf((char *)w, "\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f",
	    candy->FV[0], candy->FV[1], candy->FV[2], candy->FV[3], 
	    candy->FV[4], candy->FV[5], candy->FV[6], candy->FV[7], candy->FV[8]);
#endif
  }


  return rezo;

}

void classifier(query_processing_environment_t *local_qenv, book_keeping_for_one_query_t *qex,
		byte *forward, byte *doctable, size_t fsz, double score_multiplier) {
  // I think this is only called if there is at least one candidate.
  //
  // We've run saat_relaxed_and and put the candidates in the result blocks
  // of candidatesa.  Possibly_record_candidate() has actually calculated the classification
  // score and recorded it in the .score members of the candidates (assuming the DOLM exceeds
  // the specified threshold.)

  // Ignore rank_only stuff.

  // When relaxation_level > 0, we have multiple sorted lists which must be merged,
  // otherwise it's a straight copy from the candidates array 

  candidate_t *candidates, *candidates_to_use = NULL;
  int r, s, doclen_inwords, showlen, rb, best_rb, total_candidates = 0, *pos_in_rb;
  unsigned long long *dtent;  // Excluding the signature part
  docnum_t d;
  byte *doc, *what2show, *details = NULL;
  double best_score, highest_score;


  if (0) local_qenv->debug = 2;

  pos_in_rb = (int *)malloc((local_qenv->relaxation_level + 1) * sizeof(int));  // MAL 00222
  if (pos_in_rb == NULL) {
    return;   // Malloc failed
  }
  for (rb = 0; rb <= local_qenv->relaxation_level; rb++) {
    if (local_qenv->debug >= 1) 
      printf("Classifier: %d candidates in result block %d  for %d word query\n", 
	     qex->candidates_recorded[rb], rb, qex->qwd_cnt);
    total_candidates += qex->candidates_recorded[rb];
    pos_in_rb[rb] = 0;
  }

  if (total_candidates < 1) {
    free(pos_in_rb);
    return;
  }

  qex->tl_returned = 0;



  for (r = 0; r < total_candidates; r++) {
    // Each iteration copies one result

    // Find the result block with the best candidate.
    best_rb = 0;
    best_score = -1.0;
    // Note:  unused entries in result blocks have zero scores
    for (rb = 0; rb <= local_qenv->relaxation_level; rb++) {
      candidates = qex->candidatesa[rb];
      s = pos_in_rb[rb];
      if (s < local_qenv->max_to_show && candidates[s].score > best_score) {
	best_rb = rb;
	best_score = candidates[s].score;
	candidates_to_use = candidates;
      }
    }


    s = pos_in_rb[best_rb];
    d = candidates_to_use[s].doc;
    dtent = (unsigned long long *)(doctable + (d * DTE_LENGTH));
    doc = get_doc(dtent, forward, &doclen_inwords, fsz);
    details = code_flags_and_terms_which_matched(local_qenv, qex, candidates_to_use + s, doc);
    if (local_qenv->debug >= 1) printf("Details:  %s\n", details);
    if (local_qenv->include_result_details) {
      what2show = what_to_show((long long)(doc - forward), doc, &showlen, local_qenv->displaycol, details);
      if (details != NULL) free(details);
      details = NULL;
    }
    else
      what2show = what_to_show((long long)(doc - forward), doc, &showlen, local_qenv->displaycol, NULL);
    if (what2show != NULL)  {  // Could be NULL in case of memory failure in what_to_show
      qex->tl_docids[qex->tl_returned] = d;
      qex->tl_suggestions[qex->tl_returned] = what2show;  // That's in malloced storage (MAL2006)
      qex->tl_scores[qex->tl_returned++] = candidates_to_use[s].score * score_multiplier;
      if (0) printf("    r = %d, s = %d, best_rb = %d.  Score: %.4f\n", r, s, best_rb, candidates_to_use[s].score);
    }
    pos_in_rb[best_rb]++;
    if (qex->tl_returned >= local_qenv->max_to_show) break;
  }

  if (qex->tl_returned > 1) {
    highest_score = qex->tl_scores[0];
    if (local_qenv->debug >= 1) printf("Highest: %.4f.  Returned: %d\n", highest_score, qex->tl_returned);


#ifdef SIGNAL_AMBIGUITY
    // We don't check for AMBIGUITY any more because it causes Developer2 problems.
    // Instead leave it to the consumer
#define SCORE_GAP_THRESH 0.25
#define BAG_SIMILARITY_THRESH 0.75
    byte *top_candidate;
    top_candidate = qex->tl_suggestions[0];
    if (highest_score < 0.99) {
      // A score of 0.99 + indicates an exact match.   In that case, we can confidently show the first suggestion
      // because the searcher can't complain too loudly about an exact match, even if there are multiple songs (etc.) with 
      // the same name.   Hopefully the static score can help us choose the best one.
      // 
      // However, if highest_score < 0.99 there is no exact match.  In this case if there is a result returned which 
      // has both a sufficiently similar score to the top result and is sufficiently different textually to it, then we should signal ambiguity.

      for (r = 1; r < qex->tl_returned; r++) {
	double score_gap = highest_score - qex->tl_scores[r];
	if (local_qenv->debug >= 1) printf("Score gap: %.4f, bag_similarity: %.4f\n", score_gap, bag_similarity(qex->tl_suggestions[r], top_candidate));
	if (score_gap > SCORE_GAP_THRESH) break;
	if (bag_similarity(qex->tl_suggestions[r], top_candidate) < BAG_SIMILARITY_THRESH) {
	  // We have ambiguity!  Modify the top result to warn of it.
	  size_t len;
	  byte *old = qex->tl_suggestions[0];
	  len = strlen((char *)old);
	  qex->tl_suggestions[0] = (u_char *)malloc(len + 13);
	  if (qex->tl_suggestions[0] == NULL) {
	    // Curses - leave well alone!
	    qex->tl_suggestions[0] = old;
	  }
	  else {
	    strcpy((char *)qex->tl_suggestions[0], "AMBIGUOUS: ");
	    strcpy((char *)qex->tl_suggestions[0] + 11, (char *)old);
	    break;
	  }
	}
      }
    }
#endif

  }
  free(pos_in_rb);  // FRE 00222
}


double get_rectype_score_from_forward(u_ll *dtent, byte *forward, size_t fsz, int rectype_field) {
  int doclen_inwords;
  byte *doc, *field;
  double s = 0.0;
  size_t rectype_len;
  doc = get_doc(dtent, forward, &doclen_inwords, fsz);
  field = extract_field_from_record(doc, rectype_field, &rectype_len);
  if (field == NULL) return 0.0;
  if (!strcmp((char *)field, "T") || !strcmp((char *)field, "AT") || !strcmp((char *)field, "TA")) s = 1.0;
  if (0) printf("   Field: '%s' -- %.5f\n", field, s);
  free(field);
  return s;
}
