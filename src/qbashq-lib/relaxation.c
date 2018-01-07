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
#else
#include <errno.h>  
#endif

#include "../shared/QBASHER_common_definitions.h"
#include "../shared/utility_nodeps.h"
#include "../utils/dahash.h"
#include "QBASHQ.h"
#include "saat.h"


#if 0  // Not used any more
static int curdoc_parator(void *context, const void *vip, const void *vjp) {
	int *ip = (int *)vip, *jp = (int *)vjp;
	saat_control_t *pl_blox = (saat_control_t *)context;
	if (pl_blox[*ip].curdoc < pl_blox[*jp].curdoc) return -1;
	if (pl_blox[*ip].curdoc > pl_blox[*jp].curdoc) return 1;

	return *ip > *jp;  // Comparison of last resort.
}
#endif


static inline void sort_terms_by_curdoc(FILE *out, int qwd_cnt, int *tpermute, saat_control_t *pl_blox) {
  register int k, l, tmp;

  // tpermute is an integer array of dimension qwd_cnt.
  // Sort it so that it's elements reference term blocks in order of increasing
  // current document number
  // Now sort by increasing document number of corresponding terms.  Note that an
  // exhausted postings list gets a curdoc value of a huge number
  // Note that attempting to speed this up by counting swaps and stopping when no swaps
  // occurred in the inner loop was totally counter-productive.  It halved the
  // throughput of the program!

  for (k = 0; k < (qwd_cnt - 1); k++) {
    for (l = k + 1; l < qwd_cnt; l++) {
      if (pl_blox[tpermute[l]].curdoc < pl_blox[tpermute[k]].curdoc) {
	tmp = tpermute[l];
	tpermute[l] = tpermute[k];
	tpermute[k] = tmp;
      }
    }
  }

  if (0) {
    for (k = 0; k < qwd_cnt; k++) {
      l = tpermute[k];
      fprintf(out, "%d  %d  %lld\n", k, l, pl_blox[l].curdoc);
    }
  }
}


static inline void sort_terms_by_freq(FILE *out, int qwd_cnt, int *fpermute, saat_control_t *pl_blox) {
  int k, l, tmp;
  int *ocptrl, *ocptrk;
  byte *dek, *del;
  // fpermute is an integer array of dimension qwd_cnt.
  // Sort it so that it's elements reference term blocks in order of increasing term frequency

  for (k = 0; k < (qwd_cnt - 1); k++) {
    for (l = k + 1; l < qwd_cnt; l++) {
      del = pl_blox[fpermute[l]].dicent;
      dek = pl_blox[fpermute[k]].dicent;
      if (del != NULL && dek != NULL) {
	ocptrl = (int *)(del + (MAX_WD_LEN + 1));
	ocptrk = (int *)(dek + (MAX_WD_LEN + 1));

	if (*ocptrl < *ocptrk) {
	  tmp = fpermute[l];
	  fpermute[l] = fpermute[k];
	  fpermute[k] = tmp;
	}
      }

      // Leave order unchanged if terms are disjunctions or phrases (dicent == NULL)
    }
  }

  if (0) {
    for (k = 0; k < qwd_cnt; k++) {
      l = fpermute[k];
      ocptrl = (int *)(pl_blox[l].dicent + (MAX_WD_LEN + 1));
      fprintf(out, "%d  %d  %d\n", k, l, *ocptrl);
    }
  }
}



#if 0  // No longer used but might be useful in future
#define MAX_N MAX_WDS_IN_QUERY

static inline long long nCr(int n, int r) {
	// Calculate the number of combinations nCr of n things taken r at a time given 
	// allowable values of n and r.  Return -1 if values are outside allowed ranges
	// 
	// nCr = n! / (r! x (n-r)!)
	// 
	// For efficiency and to reduce risk of integer overflow, we find the largest of the factors
	// in the denominator and cancel it out against the numerator.  E.g. in 10C8 we cancel out
	// 8! giving 10 x 9 and divide by 2! giving 45.

	long long rslt = 1;
	int i, hf, lf;

	if (n == r) return 1;  // This works for 1C1
	if (n <= r || r < 1 || n < 2 || n > MAX_N) return -1;  

	if ((n - r) > r) { hf = n - r;  lf = r; }
	else { hf = r; lf = n - r; }
	// Multiply the non-cancelled factors in the numerator
	for (i = n; i > hf; i--) rslt *= i;
	// Divide by the smaller factor in the denominator
	for (i = lf; i > 1; i--) rslt /= i;
	return rslt;
}

#endif   // No longer used but might be useful in future

static inline int count_one_bits(unsigned int x) {
	int cnt = 0;
	while (x) {
		if (x & 1) cnt++;
		x >>= 1;
	}
	return cnt;
}


void saat_relaxed_and(FILE *out, query_processing_environment_t *qoenv, book_keeping_for_one_query_t *qex,
		      saat_control_t *pl_blox, byte *forward, byte *index, byte *doctable, size_t fsz,
		      int *error_code) {
  // Implements relaxed saat functionality
  //  - attempts to insert up to max_candidates_to_consider candidates into the candidates array
  //  - Returns zero if there are no words in the query (obviously)
  //  - Returns zero if there are more than MAX_WDS_IN_QUERY words
  //  - works even if there is only one word in the query.
  //  - assumes that plists have been setup by saat_setup()
  // If (allow_self) the input query itself can be included in the candidates
  // otherwise it's filtered out.

  // Important details of the algorithm.  (Very small changes can make huge differences to running speed!)
  //
  // pl_blox is the array of control blocks for the top-level terms.  It has qwd_cnt elements.  For brevity,
  // let's call qwd_cnt 'q' and that top-level terms are numbered from 0 to q-1
  // This function uses two permutation arrays to reorder pl_blox
  //	 - fpermute reorders the terms by increasing collection frequency.  Notes:
  //	     a. This permutation is calculated only once
  //       b. This permutation is ineffective for queries containing disjunctions and phrases
  //		 c. If such terms are encountered fpermute order is sort of undefined
  //       d. Maybe phrases should be put at head of list (assume low frequency) and disjunctions at tail (assume high freq, high cost)
  //   - tpermute reorders terms by increasing index of the document they currently reference.  Notes:
  //       e. This permutation is currently calculated each time a new candidate is considered. 
  //	     f. Re-calculation of tpermute definitely pays off by reducing the number of calls to saat_skipto() and
  //          by increasing the magnitude of the skips (allowing more benefit to be obtained from skip blocks)
  //       g. It might be possible to work out how to maintain tpermute[t] without resorting from scratch each
  //          time but I haven't done so yet.
  //
  // The algorithm starts by choosing the first candidate 'c'.   It does this by setting up tpermute and
  // choosing element q-m-1 as the candidate term, where 'm' is the relaxation level.  This is the right
  // choice because we may be able to advance all the terms to the left to the same document number as 
  // the q-m-1th term and we don't care about the ones to the right.  Let the candidate document number be 'd'.
  // If we choose a document number higher than d we may miss matches.  If we choose one lower than d
  // then we will make unnecessary calls to saat_skip().  Note that if d == EXHAUSTED, we can stop
  // because we can't find any matches.
  //
  // The main loop of the algorithm executes the following steps (a few details left out for clarity):
  //     S1: Try to advance all the terms to document d.  Count the number of terms 'x' which do not have d in their
  //         postings lists and break out of this process as soon as x > m.  The order in which the terms are processed
  //         here makes a big difference to speed. Here are throughput figures for different choices: 
  //            fpermute:			6389
  //			  tpermute:			2409
  //            query order:		2470
  //			  reverse fpermute:	 625
  //			  reverse_tpermute: 1540
  //
  //     S2: If x <= m  possibly record d as a result and check for successful early termination.
  //
  //     S3: Try to skip forward all the terms which are currently positioned on d,  to document d+1.  Otherwise
  //         we may repeatedly accept d.
  //
  //	   S4: Select a new candidate.  Recompute tpermute and use tpermute[q-m-1] as the next candidate

  int total_recorded = 0, k, l, candid8, code = 0, t = qex->tl_saat_blocks_used, pivot,
    curdoc_ranking[MAX_WDS_IN_QUERY], fpermute[MAX_WDS_IN_QUERY], u, m = qoenv->relaxation_level,
    terms_missing, terms_exhausted = 0, it_was_recorded, candidates_considered = 0, skips = 0,
    rbn = qoenv->relaxation_level + 1, rb_to_use;

  docnum_t candidoc;
  long long possibles = 0;  // For enforcing a timeout on this thread.
  u_int rbit, terms_matched_bits;
  BOOL finished = FALSE;

  *error_code = 0;
  if (qoenv->debug >=2)
    printf("SAAT_RELAXED_AND: qwd_cnt = %d, cg_qwd_cnt = %d, tl_saat_blocks_used = %d, query_as_processed='%s'\n",
	   qex->qwd_cnt, qex->cg_qwd_cnt, qex->tl_saat_blocks_used, qex->query_as_processed);


  if (qex->cg_qwd_cnt < 1 || qex->cg_qwd_cnt > MAX_WDS_IN_QUERY) {
    //saat_relaxed_and(): invalid parameters");
    *error_code = -100058;
    return;  // ----------------------------------->
  }

  u = t - m;   // u is minimum number of terms which must match
  // m is the number of terms which may be missing
  if (u < 1) {
    u = 1;  
    m = qex->tl_saat_blocks_used - u;
  }


  // Special test when we're classifying.
  if (qoenv->classifier_mode > 0) {
    // m must be <= (1 - threshold) * qwd_cnt.   Otherwise partial matches can't reach the threshold.
    // NOTE: This inequality depends upon the exact lexical similarity formula.  For some
    // formulae this may be too
    // strict or not strict enough.
    int max_m = (int)floor((1.0 - qoenv->classifier_threshold) * (double)qex->cg_qwd_cnt);
    if (0) printf("Shaping threshold.  M = %d, max_m = %d\n", m, max_m);
    if (m > max_m) m = max_m;	
  }

  pivot = u - 1; 

  if (qoenv->debug >= 2)
    fprintf(out, "saat_relaxed_and().  qex->cg_qwd_cnt = %d. R_level was %d, is %d.  "
	    "Min terms = %d.  Looking for up to %d candidates.\n", 
	    qex->cg_qwd_cnt, qoenv->relaxation_level, m, u,
	    qoenv->max_candidates_to_consider);
  for (l = 0; l < qex->tl_saat_blocks_used; l++) {
    fpermute[l] = l;
    curdoc_ranking[l] = l;
  }

  if (qex->cg_qwd_cnt > 1) {
    // 12 July 2017:  I don't understand why one sort is followed by another
    sort_terms_by_freq(out, qex->tl_saat_blocks_used, fpermute, pl_blox);  // This ordering is static
    sort_terms_by_curdoc(out, qex->tl_saat_blocks_used, curdoc_ranking, pl_blox);
  }
  // First candidate is the m-th highest docnum referenced by a plist control block  (the pivot)
  // That candidate is curdoc_ranking[u - 1] i.e 
  candid8 = curdoc_ranking[pivot];
  if (0) printf("Initial candid8 is %d, pivot = %d, t = %d, m = %d, u = %d\n",
		candid8, pivot, t, m, u);


  if (pl_blox[candid8].curdoc == CURDOC_EXHAUSTED) {
    if (qoenv->debug >= 1)
      fprintf(out, "Exhaustion(A): candidates considered: %d; skips = %d\n", candidates_considered, skips);
    return;  // No matches possible
  }

  if (qoenv->debug >= 2) {
    fprintf(out, "  saat_relaxed_and(): First candidate is %lld (%d): ", pl_blox[candid8].curdoc, candid8);
    show_doc(doctable, forward, fsz, pl_blox + candid8);
  }
	

  // This outer loop must continue until either:
  //   A. more than m postings lists are exhausted, or
  //   B. The slots for recording candidates at all allowed levels of relaxation have been filled. 

  while (!finished) {  // -----------------------  The big outer loop --------------------------------
    //  ===============================================================================================
    //  =============== Step 1:  Skip all the other lists forward to same doc as candid8  =============
    //  ===============================================================================================

    // Note: candid8 is the number of a term in the query.  The corresponding candidate document number
    // is candidoc = pl_blox[candid8].curdoc

    if (qoenv->debug >= 2) {
      fprintf(out, "HEAD OF WHILE saat_relaxed_and(): Candidate doc is %lld.  candid8=%d, tl_saat_blocks_used=%d. posting_num=%lld\n    ",
	      pl_blox[candid8].curdoc, candid8, qex->tl_saat_blocks_used, pl_blox[candid8].posting_num);
      show_doc(doctable, forward, fsz, pl_blox + candid8);
    }

    candidates_considered++;
    // For a single term query, the conditional inside this loop will never be executed
    terms_missing = 0;  // How many terms are not matched by this candidate.
    terms_exhausted = 0;
    terms_matched_bits = 0;
    candidoc = pl_blox[candid8].curdoc;
    for (k = 0; k < qex->tl_saat_blocks_used; k++) {     // ---------------  loop through all the postings lists ----------------
      l = fpermute[k];      // Using curdoc_ranking here rather than fpermute reduces throughput by a factor of 2.6 
      // on a test set of 10000 queries using relaxation_level=0
      rbit = 1 << (qex->tl_saat_blocks_used - l - 1);
      if (l == candid8) terms_matched_bits |= rbit;
      else {
	// saat_skipto will do nothing if curdoc is already pointing at or beyond the target but better to 
	// save a call
	if (pl_blox[l].curdoc > candidoc) code = 1;
	else if (pl_blox[l].curdoc == candidoc) code = 0;
	else {
	  code = saat_skipto(out, pl_blox + l, l, pl_blox[candid8].curdoc, DONT_CARE, index, qex->op_count, 
			     qoenv->debug, error_code);
	  if (*error_code < -200000) {
	    if (qoenv->debug >= 1) fprintf(out, "Exit due to error in saat_skipto\n");
	    return;  // ------------------------------------->
	  }
	  skips++;
	}

	if (qoenv->debug >= 2) fprintf(out, "    Skipped term %d.  Code is %d\n", l, code);

	if (code == 0) {
	  terms_matched_bits |= rbit;

	}
	else {
	  //  +1 - desired doc,wpos not found, blok has moved to a posting past there
	  //   0 - desired doc,wpos found
	  //  -1 - desired dow,wpos not found, and list is exhausted.
	  if (qoenv->debug >= 2) fprintf(out, "List %d exhausted or doesn't match this doc.\n", l);
	  terms_missing++;
	  if (terms_missing > m) break;
	  if (code < 0) terms_exhausted++;
	}
      }
    } // ---------------  end of for loop through all query words ----------------


    if (terms_exhausted > m) {
      if (qoenv->debug >= 1) fprintf(out, "Exhaustion(B): candidates considered: %d; skips = %d\n", candidates_considered, skips);
      return;  // TOO MANY LISTS EXHAUSTED ---------------------------------------->
    }


    if (qoenv->debug >= 2)
      fprintf(out, "saat_relaxed_and(): terms_missing = %d.  Terms_matched_bits = %X\n", 
	      terms_missing, terms_matched_bits);

    //  =======================================================================================
    //  ======================== Step 2:  Deal with a match if we have one ====================
    //  =======================================================================================

    if (terms_missing <= m) {   //  ............... Prima facie acceptable candidate found  ................

      // In this code block we: 
      //   1. Possibly record the candidate in a result blocks.  
      //   2. When we record a result, we check:
      //      a. Whether this result block is now full,
      //      b. If so, whether we can now reduce the relaxation level,
      //      c. If so, whether we have now finished.
			 
      rb_to_use = terms_missing;

      if (qoenv->report_match_counts_only) {
	//  --------------------- Special behaviour activated when max_to_show == 0 ------------------
	if (terms_missing == 0) {
	  qex->full_match_count++;  // Only count full matches.
	  if (0) printf("FMC:  %lld\n", qex->full_match_count);
	}
      } else if (qex->candidates_recorded[rb_to_use] < qoenv->max_candidates_to_consider || qoenv->classifier_mode) {  // ..................................  Test on RB .....
	// Acceptable degree of  match, and we haven't filled up all the slots at this level of
	// match, or we're doing the classifier pseudo-heap thing.

	if (qoenv->debug >= 1) {
	  byte *doc;
	  u_char *p;
	  int dc_len;
	  fprintf(out, "Match found in saat_relaxed_and(): rb_to_use = %d, candid8 = %d\n", rb_to_use, candid8);
	  fprintf(out, "       Match with %d terms missing [terms_matched bits = %X, m = %d, rb_to_use = %d] is %lld (%d): ",
		  terms_missing, terms_matched_bits, m, rb_to_use, pl_blox[candid8].curdoc, candid8);
	  doc = get_doc((unsigned long long *)(doctable + pl_blox[candid8].curdoc * DTE_LENGTH), forward, &dc_len, fsz);
	  if (doc == NULL) {
	    fprintf(out, " NULL (error)\n");
	  }
	  else {
	    fprintf(out, "[off = %llx] ", (long long)(doc - forward));
	    p = (u_char *)doc;
	    show_string_upto_nator(p, '\n', 0);
	  }
	}
	if (qoenv->debug >= 1) printf("About to P_R candidate in RB[%d]: %d\n",
				      rb_to_use, qex->candidates_recorded[rb_to_use]);


	if (0) {
	  byte *doc;
	  u_char *p;
	  int dc_len;
	  doc = get_doc((unsigned long long *)(doctable + pl_blox[candid8].curdoc * DTE_LENGTH), forward, &dc_len, fsz);
	  if (doc == NULL) {
	    printf("CANDIDATE: NULL (error)\n");
	  } else {
	    printf("CANDIDATE: [docno = %lld, off = %llx] ", pl_blox[candid8].curdoc, (long long)(doc - forward));
	    p = (u_char *)doc;
	    show_string_upto_nator(p, '\n', 0);
	  }
	}
	  

	  
	// If we're doing BM25 scoring we need to compute the TFs of each query term
	if (qoenv->rr_coeffs[5] > 0.0) {
	  rbit = 1;
	  for (k = 0; k < qex->tl_saat_blocks_used; k++) {
	    int tftmp = 0;
	    if (terms_matched_bits & rbit)
	      tftmp = saat_get_tf(out, pl_blox + k, index, qex->op_count, qoenv->debug);
	    if (0) printf("Query term %d, tf = %d\n", k, tftmp);
	    pl_blox[k].tf = tftmp;
	    rbit <<= 1;
	  }
	}

	it_was_recorded =
	  possibly_record_candidate(qoenv, qex, pl_blox, forward, index, doctable,
				    fsz, pl_blox[candid8].curdoc, 
				    rb_to_use, terms_matched_bits);
	if (0) printf("Done P_R candidate\n");

	if (it_was_recorded) {
	  int stopping_condition = 2;

	  //  ------------------------ Stopping condition rules are different in CLASSIFIER MODES ------------------------------------
	  if (qoenv->classifier_mode) {
	    // There are two different early termination conditions, one which applies to the highest scoring candidate
	    // (slot 0 in result block 0) and the other to the lowest candidate in the most relaxed result block
	    candidate_t *candies;
	    if (qoenv->classifier_stop_thresh1 < 1.0) {
	      candies = qex->candidatesa[rb_to_use];
	      if (candies[0].score > qoenv->classifier_stop_thresh1) return;  // classifier ----------------------------->
	    }

	    if (qoenv->classifier_stop_thresh2 < 1.0 && rb_to_use == (rbn -1)) {
	      // We have to apply this test to all the result blocks
	      // We only terminate if all the result blocks are fully populated and none have a score below
	      // thresh2.
	      int r;
	      BOOL no_lower_score_found = TRUE;
	      for (r = 0; r < rbn; r++) {
		candies = qex->candidatesa[r];
		if (0) printf("THRESH2: Checking result block %d. Lowest score = %.3f\n",
			      r, candies[qoenv->max_to_show - 1].score);
		if (candies[qoenv->max_to_show - 1].score <= qoenv->classifier_stop_thresh2) {
		  no_lower_score_found = FALSE;
		  break;
		}
	      }
	      if (no_lower_score_found) {
		if (0) printf("THRESH2: We're going to abandon ship\n");
		return;  // classifier -------------->
	      }
	    }
	  }
	  else {  // .................................. If not classifier mode .......................
	    total_recorded++;
	    if (qoenv->debug >= 1) fprintf(out, "saat_relaxed_and(): match with %d terms missing recorded in rb %d. tot rec: %d\n",
					   terms_missing, rb_to_use, total_recorded);

	    // Have we finished by finding the required number of results?

	    if (stopping_condition == 0 || m == 0) {
	      // Stop when the first tier is full
	      if (qex->candidates_recorded[0] >= qoenv->max_candidates_to_consider) {
		if (qoenv->debug >= 1) fprintf(out, "Stopping: candidates considered: %d; skips = %d\n",
					       candidates_considered, skips);
		return;  // FILLED ALL THE FULL MATCH SLOTS -------------------------------------------------------------->
	      }
	    }
#if 0
	    else if (stopping_condition == 1) {
	      // Stop when total matches reaches required level
	      if (total_recorded >= qoenv->max_candidates_to_consider) {
		if (qoenv->debug >= 1) fprintf(out, "candidates considered: %d; skips = %d\n",
					       candidates_considered, skips);
		return;  // GOT ENOUGH RESULTS ----------------------->
	      }
	    }
#endif
	    else {  //  -------------- Non-trivial stopping condition 
	      finished = TRUE;
		
	      if (qex->candidates_recorded[rb_to_use] >= qoenv->max_candidates_to_consider) {
		// We've just filled up a result list.  Can we now tighten up the relaxation level?
		if (m && rb_to_use == m) {
		  if (qoenv->debug >= 1) fprintf(out, "Shrinking relaxation_level to %d\n", m - 1);
		  m--;
		}
	      }

	      for (k = 0; k < rbn; k++) {
		if (0) fprintf(out, "Result block %d - recorded = %d / %d\n",
			       k, qex->candidates_recorded[k], qoenv->max_candidates_to_consider);
		if (qex->candidates_recorded[k] < qoenv->max_candidates_to_consider) {
		  finished = FALSE;
		  break;
		}
		if (0) fprintf(out, "result block = %d; matches recorded  = %d\n", k, qex->candidates_recorded[k]);
	      }
	      if (finished) {
		if (qoenv->debug >= 1)
		  fprintf(out, "Stopping: candidates considered: %d; skips = %d.  Got enough results.\n",
			  candidates_considered, skips);
		return;  // FILLED ALL THE SLOTS AT ALL THE LEVELS ----------------->
	      }
	    }   //  -------------- Non-trivial stopping condition

	    // =============== Can we ease off on the relaxation level? ================================
	    // Reducing the relaxation level once we've recorded enough weaker matches seems to pay off,
	    // at least for relaxation_levels of 2 or more.   E.g. for relaxaton_level=3 it increases QPS
	    // from an average of 100.7 to 122.4 (+22%) and for RL=2 from 567 to 722 (+27%)  The effect would probably be
	    // bigger for longer queries.
	  }
	}  // ..................................  End of if (it_was_recorded) .....
	else if (0) fprintf(out, "It was NOT recorded.\n");
      }  // .................................. End of complex if (rb >= 0 ....)  .......................
    }  //  ...............End of  Prima facie acceptable candidate found  ................

    //  =============== Step 3:  Advance all the terms referencing the current candidate  =============

    // I think that, regardless of whether we found a match or not, we just skip forward candid8 and all 
    // the other terms whose docnums are the same as candidate (otherwise we can potentially record the 
    // same document more than once, each with a different degree of mismatch).
    // It turns out that only doing this for the case where we had a viable candidate gives the right answer
    // but is slower:  e.g. 6281 QPS v. 6374 QPS
    // if (terms_missing <= m) {


    candidoc = pl_blox[candid8].curdoc;

    for (k = 0; k < qex->tl_saat_blocks_used; k++) {
      if (pl_blox[k].curdoc == candidoc) {    // Whether this is <= or == makes a huge difference to speed!!
	// E.g. 1707 QPS with <= cf. 6374 with ==
	code = saat_skipto(out, pl_blox + k, k, candidoc + 1, DONT_CARE,
			   index, qex->op_count, qoenv->debug, error_code);
	if (*error_code < -200000) {
	  if (qoenv->debug >= 1) fprintf(out, "Error return from saat_skipto(B)\n");
	  return;  // ------------------------------------->
	}
	skips++;

	if (qoenv->debug >= 2) fprintf(out, "  saat_relaxed_and(): Advanced term %d to (%lld, %d). Code is %d\n",
				       k, pl_blox[k].curdoc, pl_blox[k].curwpos, code);
      }
    }


    //  =============== Step 4:  Choose a new candidate by sorting curdoc_ranking again  =============

    // We could possibly avoid the sort by taking note of which lists had been advanced and therefore
    // needed to be shuffled.  That actually turns out to be quite messy and could be slower.  Accordingly,
    // the below gains a lot of speed by identifying quite a few special cases in which the loops can be
    // profitably unrolled.
    // That made the following QPS differences on the test_queries_fullword_10k.txt set
    // r_mode=0 r_level=1: 1762 -> 3663
    // r_mode=0 r_level=2:  435 -> 3188
    // r_mode=0 r_level=3:  156 -> 2782
    // r_mode=1 r_level=3:  133 ->  799

    candid8 = 0; 
    if (qex->tl_saat_blocks_used > 1) {
      if (m == 0) {
	// Just find the highest
	int k;
	for (k = 1; k < qex->tl_saat_blocks_used; k++) {
	  if (pl_blox[k].curdoc > pl_blox[candid8].curdoc) candid8 = k;
	}
      }
      else if (m == 1) {
	// Find the second highest h2.
	int k, tmp, h2 = 0, h1 = 1;
	if (pl_blox[h2].curdoc > pl_blox[h1].curdoc) { h2 = 1; h1 = 0; }
	for (k = 2; k < qex->tl_saat_blocks_used; k++) {
	  if (pl_blox[k].curdoc > pl_blox[h2].curdoc) {
	    h2 = k;
	    if (pl_blox[h2].curdoc > pl_blox[h1].curdoc) { tmp = h2; h2 = h1; h1 = tmp;}
	  }
	}
	candid8 = h2;
      }
      else if (m == 2) {
	// Find the third highest h3.
	int k, tmp, h1 = 0, h2 = 1, h3 = 2;
	if (pl_blox[h3].curdoc > pl_blox[h2].curdoc) { tmp = h3; h3 = h2; h2 = tmp; }
	if (pl_blox[h2].curdoc > pl_blox[h1].curdoc) { tmp = h2; h2 = h1; h1 = tmp; }
	if (pl_blox[h3].curdoc > pl_blox[h2].curdoc) { tmp = h3; h3 = h2; h2 = tmp; }

	for (k = 3; k < qex->tl_saat_blocks_used; k++) {
	  if (pl_blox[k].curdoc > pl_blox[h3].curdoc) {
	    h3 = k;
	    if (pl_blox[h3].curdoc > pl_blox[h2].curdoc) { tmp = h3; h3 = h2; h2 = tmp; }
	    if (pl_blox[h2].curdoc > pl_blox[h1].curdoc) { tmp = h2; h2 = h1; h1 = tmp; }
	    if (pl_blox[h3].curdoc > pl_blox[h2].curdoc) { tmp = h3; h3 = h2; h2 = tmp; }
	  }
	}
	candid8 = h3;
	//if (qoenv->debug >= 1) fprintf(out, "New candidate is term %d.  Curdocs are %lld, %lld, %lld\n",
	//	candid8, )
      }
      else if (m == 3) {
	// Find the third highest h4.
	int k, tmp, h1 = 0, h2 = 1, h3 = 2, h4 = 3;
	if (pl_blox[h4].curdoc > pl_blox[h3].curdoc) { tmp = h4; h4 = h3; h3 = tmp; }
	if (pl_blox[h3].curdoc > pl_blox[h2].curdoc) { tmp = h3; h3 = h2; h2 = tmp; }
	if (pl_blox[h2].curdoc > pl_blox[h1].curdoc) { tmp = h2; h2 = h1; h1 = tmp; }
	if (pl_blox[h4].curdoc > pl_blox[h3].curdoc) { tmp = h4; h4 = h3; h3 = tmp; }
	if (pl_blox[h3].curdoc > pl_blox[h2].curdoc) { tmp = h3; h3 = h2; h2 = tmp; }
	if (pl_blox[h4].curdoc > pl_blox[h3].curdoc) { tmp = h4; h4 = h3; h3 = tmp; }

	for (k = 3; k < qex->tl_saat_blocks_used; k++) {
	  if (pl_blox[k].curdoc > pl_blox[h3].curdoc) {
	    h3 = k;
	    if (pl_blox[h4].curdoc > pl_blox[h3].curdoc) { tmp = h4; h4 = h3; h3 = tmp; }
	    if (pl_blox[h3].curdoc > pl_blox[h2].curdoc) { tmp = h3; h3 = h2; h2 = tmp; }
	    if (pl_blox[h2].curdoc > pl_blox[h1].curdoc) { tmp = h2; h2 = h1; h1 = tmp; }
	    if (pl_blox[h4].curdoc > pl_blox[h3].curdoc) { tmp = h4; h4 = h3; h3 = tmp; }
	    if (pl_blox[h3].curdoc > pl_blox[h2].curdoc) { tmp = h3; h3 = h2; h2 = tmp; }
	    if (pl_blox[h4].curdoc > pl_blox[h3].curdoc) { tmp = h4; h4 = h3; h3 = tmp; }
	  }
	}
	candid8 = h4;
      }
      else {
	//sort_terms_by_curdoc(qwd_cnt, curdoc_ranking, pl_blox);
	//qsort_s(curdoc_ranking, qwd_cnt, sizeof(int), curdoc_parator, (void *)pl_blox);
	// qsort_s is quite a bit slower than my crummy function, but
	// Avoiding the function call by repeating the code inline here, makes some difference
	// to speed.  There seems to be some very slight advantage to avoiding the call for one-word queries.
	int tmp;
	for (k = 0; k < (qex->tl_saat_blocks_used - 1); k++) {
	  for (l = k + 1; l < qex->tl_saat_blocks_used; l++) {
	    if (pl_blox[curdoc_ranking[l]].curdoc < pl_blox[curdoc_ranking[k]].curdoc) {
	      tmp = curdoc_ranking[l];
	      curdoc_ranking[l] = curdoc_ranking[k];
	      curdoc_ranking[k] = tmp;
	    }
	  }
	}
	candid8 = curdoc_ranking[pivot];
      }
    }

    if (pl_blox[candid8].curdoc == CURDOC_EXHAUSTED) {
      if (qoenv->debug >= 1)
	fprintf(out, "Exhaustion: candidates considered: %d; skips = %d\n", candidates_considered, skips);
      return;  // No matches possible
    }

    if (0) fprintf(out, "Chose candidate %d(u - 1 = %d) docnum is %lld.  posting_num = %lld\n", candid8, u - 1, 
		   pl_blox[candid8].curdoc, pl_blox[candid8].posting_num);
    possibles++;

    // If in force, check both deterministic and elapsed time timeouts every tenth possible.
    if ((qoenv->timeout_kops > 0 || qoenv->timeout_msec > 0) && (possibles % 10) == 0) {
      if (qoenv->timeout_kops > 0) {
	if (qoenv->debug >= 1) fprintf(out, "Checking timeout %d v %d\n", op_cost(qex), qoenv->timeout_kops);
	if (op_cost(qex) > qoenv->timeout_kops) {
	  qex->timed_out = TRUE;
	  qoenv->query_timeout_count++;
	  if (qoenv->debug >= 1) {
	    fprintf(out, "Timed out!(%s). Total recorded = %d.  Timeout KOPS: %d\n", 
		    qex->query_as_processed, total_recorded, qoenv->timeout_kops);
	    fprintf(out, "candidates considered: %d; skips = %d\n", candidates_considered, skips);
	  }
	  return;  // TIMEOUT  ------------------------------>
	}
      }

      if (qoenv->timeout_msec > 0) {
	double elapsed = 1000.0 * (what_time_is_it() - qex->start_time);
	if (0) printf("           ----- Checking msec timeout %.0f v. %d -----\n",
		      elapsed, qoenv->timeout_msec);
	if (elapsed > (double)qoenv->timeout_msec) {
	  qex->timed_out = TRUE;
	  qoenv->query_timeout_count++;
	  if (qoenv->debug >= 1) {
	    fprintf(out, "Timed out!(%s). Total recorded = %d.  Timeout msec: %d\n", 
		    qex->query_as_processed, total_recorded, qoenv->timeout_msec);
	    fprintf(out, "candidates considered: %d; skips = %d\n", candidates_considered, skips);
	  }
	  return;  // TIMEOUT  ------------------------------>
	}

      }
    }

  }  // -----------------------  End of while (!finished) -- the big outer loop --------------------------------

  if (qoenv->debug >= 1) fprintf(out, " End: candidates considered: %d; skips = %d\n", candidates_considered, skips);
  return;
}






