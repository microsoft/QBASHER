// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// QBASHQ_lib - A library for QBASHER query processing.  

// All of the indexes are mmapped into memory.
// 
// This version no longer handles multi-shard indexes

// Inputs are assumed to consist of a sequence of full words, separated
// by single spaces (multiple spaces may actually be allowed.)  Also supported
// are queries consisting of at least one full word and one or more 
// partial words indicated by a leading '/'.  E.g. {australian national /uni}  
// If the auto_partials argument is non-zero, then the last word in a multi-word 
// input will be automatically prefixed with a '/'.


// Currently, we rely on static ordering of docnums in generating the 
// candidate set and return the first k documents which match the
// full AND of the query term.  Candidates are then ranked
// by a score computed from a linear combination of features.

// Each query word is looked up in the .vocab indexfile using binary 
// search which should be plenty fast enough since the indexes are 
// assumed to be in RAM.load


// -------------------------- Software Engineering -----------------------------
//
// 1. THREAD SAFETY: All functions should be thread safe.  No use of static
//    or global storage.  All required information must be passed by parameters,
//    which sometimes results in very long argument lists, offset in many
//    cases by passing pointers to environments.
//
// 2. MEMORY DISCIPLINE: All memory allocated must be freed. Even a leak of 
//    100 bytes per query steals a gigabyte every 10 million queries.
//
// 3. EXITS: Because this code may be incorporated in a higher-level persistent
//    application, it cannot call exit() ever, once started.  The only exceptions
//    to this are when error codes are returned from the system thread library,
//    or from malloc().
//    At startup however, we're very fussy and error_exit for errors in initial 
//    options, inaccessible index files, version incompatibilities etc.
//    Exits may cause the high-level application to exit.
// -----------------------------------------------------------------------------

// -------------------------- Forward index format ----------------------------
//
// From Sep 2014 QBASHQ supports only one format for the .forward file.  It is the
// AutoSuggest (AS) production format (TSV).
// Neither QBASHI nor QBASHQ modify the .forward file.  
//
// QBASHQ requires only the trigger and the weight fields. However, it can make use of 
// a display column, if one is defined.  By default, displaycol is set to 4 corresponding
// to the spelling correction column in AS.  In other words, the input query is matched
// against column, but if there is a non-empty value in displaycol, then that value is
// displayed as the result -- rather than the value in column 1.   If display_col is zero,
// the whole record (multiple columns) is displayed.
// Potentially QBASHQ could do stuff with other fields
// but that is not yet implemented.
//
// The .forward data is in the form of lines each divided
// into a fixed number of fields.  The fields are separated by tabs
// 
// FORMAT (AS Production) - 7 columns
//   1 - suggestion - often contains ^G what does that mean  
//   2 - occurrence frequency
//   3 - unknown (to Istvan)
//	 4 - spell correction
//   5 - URL 1
//   6 - URL 2
//   7 - URL 1
//
// 
// ------------------------  Thread Safety 08 August 2014 ---------------------------------
//
// http://msdn.microsoft.com/en-us/library/windows/desktop/ms686774(v=vs.85).aspx says that
// each thread gets its own stack space.  I presume that that's true even when the creation
// of threads happens outside this code.   All non-local and local-static variables have been
// moved into dynamically allocated structs.  Mutexes have been put around some areas of the 
// code.  Most library calls are to threadsafe versions.  Need to check strtol and strtod but.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>

#ifdef WIN64
#include <windows.h>
#include <WinBase.h>
#include <tchar.h>
#include <strsafe.h>
#define WINPROTO _cdecl *
#else
#define WINPROTO 
#endif

#include "../shared/unicode.h"
#include "../shared/utility_nodeps.h"
#include "../shared/QBASHER_common_definitions.h"
#include "../utils/dahash.h"
#include "../utils/latlong.h"
#include "../utils/street_addresses.h"
#include "QBASHQ.h"
#include "saat.h"
#include "../shared/substitutions.h"
#include "arg_parser.h"
#include "classification.h"
#include "query_shortening.h"


// Shifts and masks calculated from the DTE_*_BITS definitions in QBASHI.h  (Set once from load_query_processing_environment()).
unsigned long long
DTE_WDCNT_SHIFT,
  DTE_WDCNT_MASK,
  DTE_WDCNT_MASK2,
  DTE_DOCOFF_SHIFT,
  DTE_DOCOFF_MASK,
  DTE_DOCOFF_MASK2,
  DTE_DOCSCORE_SHIFT,
  DTE_DOCSCORE_MASK,
  DTE_DOCSCORE_MASK2,
  DTE_DOCBLOOM_MASK,
  DTE_DOCBLOOM_MASK2,
  DTE_DOCBLOOM_SHIFT;




static inline int count_one_bits(unsigned long long x) {
  // count how many bits are set in x
  int cnt = 0;
  while (x) {
    if (x & 1) cnt++;
    x >>= 1;
  }
  return cnt;
}




static void calculate_dte_shifts_and_masks() {
  // Derive all the shifts and masks from the DTE _BITS definitions in QBASHI.h
  DTE_WDCNT_SHIFT = 0;
  DTE_WDCNT_MASK2 = (1ULL << DTE_WDCNT_BITS) - 1;
  DTE_WDCNT_MASK = DTE_WDCNT_MASK2 << DTE_WDCNT_SHIFT;
  DTE_DOCOFF_SHIFT = DTE_WDCNT_BITS;
  DTE_DOCOFF_MASK2 = (1ULL << DTE_DOCOFF_BITS) - 1;
  DTE_DOCOFF_MASK = DTE_DOCOFF_MASK2 << DTE_DOCOFF_SHIFT;
  DTE_DOCSCORE_SHIFT = DTE_WDCNT_BITS + DTE_DOCOFF_BITS;
  DTE_DOCSCORE_MASK2 = (1ULL << DTE_SCORE_BITS) - 1;
  DTE_DOCSCORE_MASK = DTE_DOCSCORE_MASK2 << DTE_DOCSCORE_SHIFT;
  DTE_DOCBLOOM_SHIFT = DTE_WDCNT_BITS + DTE_DOCOFF_BITS + DTE_SCORE_BITS;
  DTE_DOCBLOOM_MASK2 = (1ULL << DTE_BLOOM_BITS) - 1;
  DTE_DOCBLOOM_MASK = DTE_DOCBLOOM_MASK2 << DTE_DOCBLOOM_SHIFT;
  if (0) printf("Doctable field widths: wdcount = %d, doc offset = %d, score = %d, coarse Bloom = %d\n",
		DTE_WDCNT_BITS, DTE_DOCOFF_BITS, DTE_SCORE_BITS, DTE_BLOOM_BITS);
}



static int test_shifts_and_masks() {
  // Check that the DTE_ definitions are consistent.  It's quite hard to tell
  // the difference between 9 consecutive Fs and 10  :-)
  //
  // Return 0 for success and a negative error_code for a fail.

  int l1 = count_one_bits(DTE_WDCNT_MASK),
    l2 = count_one_bits(DTE_DOCOFF_MASK),
    l3 = count_one_bits(DTE_DOCSCORE_MASK),
    l4 = count_one_bits(DTE_DOCBLOOM_MASK),
    L1 = count_one_bits(DTE_WDCNT_MASK),
    L2 = count_one_bits(DTE_DOCOFF_MASK),
    L3 = count_one_bits(DTE_DOCSCORE_MASK),
    L4 = count_one_bits(DTE_DOCBLOOM_MASK),
    totbits = DTE_LENGTH * 8;

  if (totbits != 64)
    return(-200010);  // ------------------------------------->

  if (0) printf("l1 = %d, l2 = %d, l3 = %d, l4 = %d, sum = %d\n", l1, l2, l3, l4, l1 + l2 + l3 + l4);

  if (l1 != L1 || l2 != L2 || l3 != L3 || l4 != L4)
    return(-200011);  // ------------------------------------->
  if ((l1 + l2 + l3 + l4) != totbits)
    return(-200012);  // ------------------------------------->
  if (DTE_DOCOFF_SHIFT != l1)
    return(-200013);  // ------------------------------------->
  if (DTE_DOCSCORE_SHIFT != (l1 + l2))
    return(-200014);  // ------------------------------------->
  if (DTE_DOCBLOOM_SHIFT != (l1 + l2 + l3))
    return(-200015);  // ------------------------------------->
  return(0);
}




double get_score_from_dtent(unsigned long long dte) {
  // Return the static score as a fraction between 0 and 1
  unsigned long long t;
  double r;
  t = (dte & DTE_DOCSCORE_MASK) >> DTE_DOCSCORE_SHIFT;
  r = (double)t / (double)DTE_DOCSCORE_MASK2;
  //printf("   g_s_f_d: dte = %llx,  %llu / %llu  --> %.5f\n", dte, t, DTE_DOCSCORE_MASK2, r);
  return r;
}




static unsigned long long calculate_signature_from_first_letters_of_partials(u_char **partials, int partial_cnt, int bits,
									     int *error_code)  {
  // For the character at the start of each partial word in the query
  // non-space sequences) calculate a bit position in the range 0 - bits minus 1
  // by taking the value of the character modulo bits.
  // Note:  Should use unicode value derived from UTF-8.  For the moment just use bytes
  // Use modulo rather than bit mask so we can easily vary to any number of bits we want.
  // We probably should use something more sophistiated so that the one bits are uniformly
  // distributed.  
  // We only care about the partials because the inverted file matching
  // takes care of everything else.  The more we complicate the query syntax, e.g. 
  // disjunctions within phrases and v-v, the harder it would be to include the
  // matching part of the query.


  unsigned long long signature = 0, bitpat = 0;
  int bit, i;
  u_char *p;
  byte first_byte;

  *error_code = 0;
  if (bits > 8 * sizeof(unsigned long long)) {
    *error_code = -200051;
    return 0ULL;  // Error!  -------------->
  }

  for (i = 0; i < partial_cnt; i++) {
    // No operator complications here.
    p = partials[i];
    first_byte = *p;
    if (!(first_byte & 0x80)) first_byte = tolower(first_byte);  // If ASCII do ASCII lowering.
    bit = (int)(first_byte) % bits;
    bitpat = 1ULL << bit;
    signature |= bitpat;
    while (*p > ' ') p++;  // Skip word characters
  }
  return signature;
}





static BOOL normalise(double *coeffs, int nc) {
  // Normalise the entries in the reranking or classifications coefficients arrays.
  // Return FALSE iff only the first coefficient is non-zero (i.e scoring is not needed)
  double sum = 0;
  int i;
  for (i = 0; i < nc; i++) sum += coeffs[i];
  if (sum < EPSILON)  return FALSE;  // Avoid div by zero
  for (i = 0; i < nc; i++) coeffs[i] /= sum;
  for (i = 1; i < nc; i++) if (coeffs[i] > EPSILON) return TRUE;
  return FALSE;
}



// -----------------------------------------------------------------------------------
// Functions and structures relating to operation counts
// -----------------------------------------------------------------------------------


static void setup_for_op_counting(book_keeping_for_one_query_t *qex) {
  strcpy(qex->op_count[COUNT_DECO].label, "postings_decompressed");
  qex->op_count[COUNT_DECO].cost = 1;
  strcpy(qex->op_count[COUNT_SKIP].label, "postings_skips");
  qex->op_count[COUNT_SKIP].cost = 1;
  strcpy(qex->op_count[COUNT_CAND].label, "candidates_considered");
  qex->op_count[COUNT_CAND].cost = 1;
  strcpy(qex->op_count[COUNT_SCOR].label, "scores_calculated_from_text");
  qex->op_count[COUNT_SCOR].cost = 10;
  strcpy(qex->op_count[COUNT_PART].label, "partial_checks");
  qex->op_count[COUNT_PART].cost = 10;
  strcpy(qex->op_count[COUNT_ROLY].label, "rank_only_checks");
  qex->op_count[COUNT_ROLY].cost = 10;
  strcpy(qex->op_count[COUNT_TLKP].label, "term_lookup");
  qex->op_count[COUNT_TLKP].cost = 1;
  strcpy(qex->op_count[COUNT_BLOM].label, "Check_Bloom_filter");
  qex->op_count[COUNT_BLOM].cost = 1;
}


static void zero_op_counts(book_keeping_for_one_query_t *qex) {
  int c;
  for (c = 0; c < NUM_OPS; c++) {
    qex->op_count[c].count = 0;
  }
}


int op_cost(book_keeping_for_one_query_t *qex) {
  // Return is cost of ops performed so far divided by 1000 with rounding
  int c, rslt = 0;
  for (c = 0; c < NUM_OPS; c++) {
    rslt += qex->op_count[c].count * qex->op_count[c].cost;
  }
  rslt = (rslt + 500) / 1000;
  return rslt;
}

static void display_op_counts(query_processing_environment_t *qoenv, book_keeping_for_one_query_t *qex) {
  int c;
  long long total = 0, total_cost = 0;
  fprintf(qoenv->query_output, "\n------------ Counts for basic operations and total cost -------------\n");
  for (c = 0; c < NUM_OPS; c++) {
    fprintf(qoenv->query_output, "%s(cost = %d): ", qex->op_count[c].label, qex->op_count[c].cost);
    fprintf(qoenv->query_output, "%d\n", qex->op_count[c].count);
    total += qex->op_count[c].count;
    total_cost += qex->op_count[c].count * qex->op_count[c].cost;
  }
  fprintf(qoenv->query_output, "Total cost = %lld\n", total_cost);
  fprintf(qoenv->query_output, "-------------------------------------------------------------------------\n");
}


static void display_shard_stats(query_processing_environment_t *qoenv, book_keeping_for_one_query_t *qex, int timeout_kops,
				int tl_returned, u_char **tl_suggestions) {
  // Display op count and timeout info.  In format similar to that requested by Developer2
  // Shard<tab>1<tab>time<tab>25.2<tab>numberOfCandidatesVetted<tab>100<tab>numberOfCandidatesReturned<tab>8<tab>etc
  int c, total_cost = 0;
  u_char timed_out;
  // Calculate the total operation costs
  for (c = 0; c < NUM_OPS; c++) {
    total_cost += qex->op_count[c].count * qex->op_count[c].cost;
  }

  timed_out = 'N';
  if (timeout_kops > 0 && total_cost > 1000 * timeout_kops) { timed_out = 'Y'; }
  fprintf(qoenv->query_output, "\tShard\t0\ttimedOut\t%c\tCost\t%d\tpostingsExamined\t%d\tcandidatesVetted\t%d\tsuggestionsReturned\t%d\n",
	  timed_out, total_cost, qex->op_count[COUNT_DECO].count,
	  qex->op_count[COUNT_PART].count, tl_returned);
}

static int isduplicate(char *s1, char *s2, int debug)  {
  // Check whether s2 should be considered a duplicate of s1
  char terminator = '\t';

  // First skip leading spaces (may not be necessary)
  while (*s1 && *s1 == ' ') { s1++; }
  while (*s2 && *s2 == ' ') { s2++; }


  while (*s1 && *s1 != terminator && *s2 && *s2 != terminator) {
    if (*s1 != *s2) { return 0; }
    s1++;
    s2++;
  }

  if (*s1 && *s1 != terminator) { // s2 has finished but s1 has not 
    while (*s1 && *s1 == ' ') s1++;  // Skip trailing spaces in s1
    if (*s1 && *s1 != terminator) return 0;
  }
  else if (*s2 && *s2 != terminator) { // s1 has finished but s2 has not
    while (*s2 && *s2 == ' ') s2++;  // Skip trailing spaces in s2
    if (*s2 && *s2 != terminator) return 0;
  }
  return 1;
}



int test_isduplicate(int debug) {
  if (!isduplicate("a", "a", debug)) {
    if (debug) printf("TEST FAIL: isduplicate(%s,%s,%c) should have returned TRUE\n", "a", "a", 'A');
    return(-36);
  }
  if (!isduplicate(" a", "a", debug)) {
    if (debug) printf("TEST FAIL: isduplicate(%s,%s,%c) should have returned TRUE\n", " a", "a", 'A');
    return(-36);
  }
  if (!isduplicate(" a", "a  ", debug)) {
    if (debug) printf("TEST FAIL: isduplicate(%s,%s,%c) should have returned TRUE\n", " a", "a  ", 'A');
    return(-36);
  }
  return 0;
}



// -----------------------------------------------------------------------------------
// Functions to access document text
// -----------------------------------------------------------------------------------
byte *get_doc(unsigned long long *docent, byte *forward, int *doclen_inwords, size_t fsz) {
  // Return a pointer to the .forward text of the suggestion document referenced by docent.
  // In doclen_inwords return the word count stored in the doc table entry.
  // 
  // return error_code if an error is encountered.
  unsigned long long docoff;
  if (docent == NULL) {
    return NULL;
  }
  if (0) printf("Here we are. DTE_WDCNT_MASK = %llX, DTE_DOCOFF_MASK= %llX, DTE_DOCOFF_SHIFT = %llu\n",
		DTE_WDCNT_MASK, DTE_DOCOFF_MASK, DTE_DOCOFF_SHIFT);
  //fprintf(out, "    Raw docent value is %llx\n", *docent);
  // NOTE that in version 1.3 indexes, only 5 bits are used to store document length in words, although up
  // to 254 may be indexed.   If doclen_inwords is 31, that means >=31
  *doclen_inwords = (int)(*docent & DTE_WDCNT_MASK);
  docoff = (*docent & DTE_DOCOFF_MASK) >> DTE_DOCOFF_SHIFT;
  //fprintf(out, "      docoff = %llu\n", docoff);
  if (docoff > fsz) {
    // Error: get_doc() found offset beyond .forward
    return NULL;
  }
  if (0) printf("Here we aren't.\n");
  return forward + docoff;
}



void show_doc(byte *doctable, byte *forward, size_t fsz, saat_control_t *pl_blok) {
  // Print the trigger of the document referenced by the SAAT control block, enclosed in braces
  // Implemented for debugging purposes.
  byte *doc;
  u_char *p, terminator = '\t';
  int dc_len;
  doc = get_doc((unsigned long long *)(doctable + (pl_blok->curdoc * DTE_LENGTH)), forward, &dc_len, fsz);
  if (doc == NULL) {
    printf("{NULL.  (Error)}\n");
  }
  else {
    p = (u_char *)doc;
    putchar('{');
    show_string_upto_nator_nolf(p, terminator, 0);
    putchar('}');
    putchar('\n');
  }
}



void terse_show(query_processing_environment_t *qoenv, u_char **returned_strings,
		double *corresponding_scores, int how_many_results) {
  // Given an array of result strings, and a corresponding array of scores, print one result per line
  // comprising the suggestion and the score, with a tab between them.
  int r;
  u_char *p;
  for (r = 0; r < how_many_results; r++) {
    p = returned_strings[r];
    while (*p && *p != '\n' && *p != '\r') {
      fputc(*p, qoenv->query_output);
      p++;
    }
    fprintf(qoenv->query_output, "\t%.5f\n", corresponding_scores[r]);
  }
}


void experimental_show(query_processing_environment_t *qoenv, u_char *multiqstr,
		       u_char **returned_strings, double *corresponding_scores,
		       int how_many_results, u_char *lblstr) {
  int r;
  u_char *p;
  for (r = 0; r < how_many_results; r++) {
    fprintf(qoenv->query_output, "Query:\t%s\t%d\t", multiqstr, r + 1);
    p = returned_strings[r];
    while (*p && *p != '\n' && *p != '\r') {
      fputc(*p, qoenv->query_output);
      p++;
    }
    fprintf(qoenv->query_output, "\t%.5f", corresponding_scores[r]);
    if (lblstr != NULL) fprintf(qoenv->query_output, "\t%s\n", lblstr);
    else fputc('\n', qoenv->query_output);
  }
}


static void replace_controls_in_line(u_char *str) {
  // Replace TAB with '!', RS with '#', and other controls with '*'
  // Replace CR or NL with NUL
  while (*str) {
    if (*str == '\t') *str = '!';
    else if (*str == 0x1E) *str = '#';
    else if (*str == '\r' || *str == '\n') {
      *str = 0;
      return;   // ----------------------->
    } else if (*str < ' ') *str = '*';
    str++;
  }
}


void present_results(query_processing_environment_t *qoenv, u_char *multiqstr, u_char *lblstr,
		     u_char **returned_strings, double *corresponding_scores, int how_many_results,
		     double query_start_time) {

  // in this new version, the query string may correspond to a multi-query, with multiple variants and
  // multiple fields within each variant. If printed literally, this could cause considerable
  // processing difficulties downstream.   Accordingly, before printing, a function is called to
  // replace ASCII controls with printable punctuation.  
  // lblstr is a query-dependent label which will be appended (after a tab) to each result.  For example,
  // it might show the expected answer for a query.

  double elapsed_msec_d;
  int elapsed_msec, verbose = qoenv->debug;

  replace_controls_in_line(multiqstr);
  

  if (qoenv->report_match_counts_only) {
    fprintf(qoenv->query_output, "Match count for AND of\t%s\t%d\n", multiqstr, how_many_results);
  } else if (qoenv->x_batch_testing) {
    // Multiquery is shown on every result line, as is query label if there is one.
    // If there are no results, the query and label only are shown
    if (how_many_results > 0) {
      experimental_show(qoenv, multiqstr, returned_strings, corresponding_scores, how_many_results,
			lblstr);
    } else {
      if (lblstr != NULL)
		fprintf(qoenv->query_output, "Query:\t%s\t%s\n", multiqstr, lblstr);
      else  fprintf(qoenv->query_output, "Query: {%s}\n", multiqstr);
    }
  } else {
    if (lblstr != NULL)
      fprintf(qoenv->query_output, "Query: {%s}\tLabel: {%s}\n", multiqstr, lblstr);
    else  fprintf(qoenv->query_output, "Query: {%s}\n", multiqstr);
    if (how_many_results > 0) 
      terse_show(qoenv, returned_strings, corresponding_scores, how_many_results);
  }

  elapsed_msec_d = 1000.0 *(what_time_is_it() - query_start_time);
  if (verbose >= 1) fprintf(qoenv->query_output, "    Elapsed time %.0f msec for {%s}.\n\n",
			    elapsed_msec_d, multiqstr);
  qoenv->total_elapsed_msec_d += elapsed_msec_d;

  if (verbose >= 1) {
    if (elapsed_msec_d > 100) fprintf(qoenv->query_output, "Slow (%.0f msec) query {%s}\n",
				      elapsed_msec_d, multiqstr);
  }
  if (elapsed_msec_d >= qoenv->max_elapsed_msec_d) {
    if (verbose >= 1) fprintf(qoenv->query_output, "   New max: %.0f {%s}\n",
			      elapsed_msec_d, multiqstr);
    qoenv->max_elapsed_msec_d = elapsed_msec_d;
    strcpy((char *)qoenv->slowest_q, (char *)multiqstr);
  }
  elapsed_msec = (int)(floor(elapsed_msec_d + 0.5));
  if (elapsed_msec < 0) elapsed_msec = 0;
  if (elapsed_msec >= ELAPSED_MSEC_BUCKETS) elapsed_msec = ELAPSED_MSEC_BUCKETS - 1;
  qoenv->elapsed_msec_histo[elapsed_msec]++;
  qoenv->queries_run++;

}


static int test_doctable_n_forward(byte *doctable, byte *forward, size_t dsz, size_t fsz) {
  // New version of test, designed to test both doc formats:
  // Check every entry in the doctable to make sure that it references the character after a LF
  // Success - return 0
  // Error - return negative error code
  int doclen_inwords, prevdoclen_inwords = 0, j, verbose = 1;
  byte *doc, *prevdoc = NULL;
  long long i, num_docs = dsz / DTE_LENGTH;
  unsigned long long dte, docoff;

  if (DTE_LENGTH != 8) {
    if (verbose) printf("Can't run internal test of doc offsets because it assumes doctable entries are 8\n"
			"bytes.  Actually they are %d bytes.\n\n", DTE_LENGTH);
    return(-200016);   // ---------------------------------------------->
  }

  if (verbose) printf("-----------Internal Test: Check %lld doctable offsets -----------\n", num_docs);

  // Now test the rest
  for (i = 0; i < num_docs; i++) {
    if (i && i % 10000 == 0) printf("      Testing doc %lld\n", i);
    dte = *(unsigned long long *)(doctable + (i * DTE_LENGTH));
    docoff = (dte & DTE_DOCOFF_MASK) >> DTE_DOCOFF_SHIFT;
    doc = get_doc((unsigned long long *)(doctable + (i * DTE_LENGTH)), forward, &doclen_inwords, fsz);
    if (docoff != 0ULL) {
      if (doc == NULL || forward[docoff - 1] != '\n') {
	if (verbose) {
	  printf("Error: Record %lld doesn't immediately follow an LF.  Offset is %lld (%llX)\n", i, docoff, docoff);
	  printf("Previous doc started at %8lld and had %4d wds: ", i - 1, prevdoclen_inwords);
	  show_string_upto_nator(prevdoc, '\n', 0);
	  printf("Current doc started at %8lld and had %4d wds: ", i, doclen_inwords);
	  show_string_upto_nator(doc, '\n', 0);
	  printf("Context: ");
	  for (j = -5; j < 5; j++) printf("%x, ", doc[j]);
	  putchar('\n');
	}
	return(-200017);
      }
    }
    prevdoc = doc;
    prevdoclen_inwords = doclen_inwords;
  }
  if (verbose) printf("----------- Internal test of doctable offsets: PASSED ---------\n\n");
  return(0);
}





// ------------------------------------------------------------------------------------
// Functions for accessing postings lists
// ------------------------------------------------------------------------------------


static int show_postings(byte *doctable, byte *index, byte *forward,
			 u_char *word, byte *dicent, size_t fsz, int max_to_show) {
  // Show the postings for the word referenced by dicent.  Stop if max_to_show
  // postings have been shown.  This function is provided for use in internal 
  // testing.

  // Success: return 0
  // Failure: return negative error_code
  u_ll docnum = 0ULL, docgap, occs, payload;
  int wpos;
  byte *doc, bight, last;
  int doclen_inwords, verbose = 0;
  byte qidf;

  if (doctable == NULL || index == NULL || forward == NULL
      || dicent == NULL) return(-18);  // ---------------------------------------->

  vocabfile_entry_unpacker(dicent, MAX_WD_LEN + 1, &occs, &qidf, &payload);

  if (verbose) printf("  Postings payload = %llu. Freq = %llu\n", payload, occs);
  if (occs == 1) {
    docnum = payload;
    wpos = (int)(docnum & 0xFFULL);
    docnum >>= DTE_WDCNT_BITS;
    doc = get_doc((unsigned long long *)(doctable + (docnum * DTE_LENGTH)), forward, &doclen_inwords, fsz);
    if (doc == NULL) {
      return(-19);  // ---------------------------------------->
    }
    if (verbose) printf("  *** Single posting references word %d of doc %llu, of length %d wds: ", wpos, docnum, doclen_inwords);
    printf("%s[%lld, %d] - ", word, docnum, wpos);
    show_string_upto_nator(doc, '\n', 4);
  }
  else {
    // payload references a chunk of the index file
    byte *ixptr = index + payload;
    int p, sb_count = 0;
    BOOL zero_length_skip_found = FALSE;

    // Now loop over the postings starting at ixptr.
    for (p = 0; p < occs; p++) {
      if (p >= max_to_show) break;
      if (*ixptr == SB_MARKER) {
	u_ll *sb = (u_ll *)(ixptr + 1);
	u_int sb_length = sb_get_length(*sb);
	printf(" --- Skip block marker %d found. [%lld, %llu, %u]---\n",
	       sb_count++, sb_get_lastdocnum(*sb), sb_get_count(*sb), sb_length);
	ixptr += (SB_BYTES + 1);
	if (zero_length_skip_found) return -78;  // We found a skip block with zero length which wasn't the last one.

	if (sb_length == 0) {
	  printf("Warning: Length shown in skip block is zero.\n");
	  zero_length_skip_found = TRUE;
	}
      }
      wpos = *ixptr;
      if (verbose) printf("   -- wpos = %d\n", wpos);
      // The docnum is in succeeding bytes.  Assemble them
      docgap = 0;
      ixptr++;
      do {
	bight = *ixptr++;
	if (verbose) printf("   -- byte: %X - %d @ %zd\n",
			    bight, p, (ixptr - (index + payload) - 1));
	last = bight & 1;
	bight >>= 1;
	docgap <<= 7;
	docgap |= bight;
      } while (!last);
      if (verbose) printf(" ~~~~ Docgap = %lld [%llX]\n", docgap, docgap);
      docnum += docgap;
      doc = get_doc((unsigned long long *)(doctable + (docnum * DTE_LENGTH)), forward, &doclen_inwords, fsz);
      if (doc == NULL) {
	return(-19);  // ---------------------------------------->
      }
      //printf("  *** Normal posting references word %d of doc %llu, of length %lld wds: ", wpos, docnum, doclen_inwords);
      printf("%s[%lld, %d] - ", word, docnum, wpos);
      show_string_upto_nator(doc, '\n', 0);
      printf("\n");
    }
  }
  return(0);
}



// ------------------------------------------------------------------------------------
// Functions for vocabulary look up
// ------------------------------------------------------------------------------------




byte *lookup_word(u_char *wd, byte *vocab, size_t vsz, int debug) {
  // Search for wd in vocab using binary search.
  // Return a pointer to the vocab entry, or NULL if not found.
  byte key[VOCABFILE_REC_LEN], *found_item;  // MAL0004 
  u_ll occs, payload;
  byte qidf;
  strncpy((char *)key, (char *)wd, MAX_WD_LEN + 1);
  if (debug >= 1) printf("Looking up %s among %lld vocab objects of size %d.\n", key,
			 (long long)(vsz / VOCABFILE_REC_LEN), VOCABFILE_REC_LEN);
  found_item = (byte *)bsearch(key, vocab, vsz / VOCABFILE_REC_LEN, VOCABFILE_REC_LEN,
			       (int(*)(const void *, const void *))
			       strcmp);
  if (debug >= 1) {
    if (found_item == NULL) {
      printf("   NOT FOUND: '%s'\n", wd);
    }
    else {
      printf("FOUND: %s\n", found_item);
      vocabfile_entry_unpacker(found_item, MAX_WD_LEN + 1, &occs, &qidf, &payload);
      printf("   FOUND: '%s' - %llu occurrences.\n", wd, occs);
    }
  }
  return found_item;
}



// ----------------------------------------------------------------------------------
// Functions used in scoring and reranking candidates
// ----------------------------------------------------------------------------------




static void extract_text_features(u_char *doc_content, size_t dc_len, int dwd_cnt, u_char **qwds, int qwd_cnt,
				  int *feat_phrase, int *feat_wds_in_seq, int *feat_primacy, BOOL remove_accents,
				  int debug) {
  // doc_content is the content of a document matching the query represented by qwds (an array of 
  // query words) and qwd_cnt (how many words there are in the query.)
  // This function first breaks up the document content into words and then calculates features which 
  // can be used to calculate a score for the document.

  u_char dc_copy[MAX_RESULT_LEN + 1], **dwds;
  int d = 0, q, failed;

  if (dwd_cnt <= 0 || qwd_cnt <= 0) {
    return;  // These conditions probably arise from an earlier error
  }

  if (dc_len > MAX_RESULT_LEN) return;

  dwds = (u_char **)malloc(dwd_cnt * sizeof(u_char **));  // MAL0007
  if (dwds == NULL) {
    return;   // Malloc failed is a very serious error, but what can we do?
  }

  utf8_lowering_ncopy(dc_copy, doc_content, dc_len);  // This function avoids a potential problem
                                                      // when dc_copy ends with an incomplete UTF-8
                                                      // sequence.
  dc_copy[dc_len] = 0;  // We don't rely on dc_len.
  if (debug >= 2)
    printf("extract_text_features(%s): dc_len = %zu, dwd_cnt = %d, MAX_RESULT_LEN=%d\n",
	   dc_copy, dc_len, dwd_cnt, MAX_RESULT_LEN);

  utf8_split_line_into_null_terminated_words(dc_copy, dwds, dwd_cnt, MAX_WD_LEN,
					     FALSE, remove_accents, FALSE, FALSE);
  

  if (0) {
    printf("  Words from candidate doc: \n");
    for (d = 0; d < dwd_cnt; d++) printf("     %s\n", dwds[d]);
    printf("  Words from query: \n");
    for (q = 0; q < qwd_cnt; q++) printf("     %s\n", qwds[q]);	  


  }
  

  // ----------- B. Match the document words against the query words and set the features -------------
  *feat_phrase = 0;
  *feat_wds_in_seq = 0;
  *feat_primacy = 0;
  // 1. Set the primacy feature if the first doc word is a query word
  for (q = 0; q < qwd_cnt; q++) {
    if (!strcmp((char *)dwds[0], (char *)qwds[q])) {
      *feat_primacy = 1;
    }
  }

  if (qwd_cnt < 2) {
    // Single word query gets phrase and wds_in_sequence credit
    *feat_wds_in_seq = 1;
    *feat_phrase = 1;
  }
  else {
    // B. Set the wds_in_seq feature, checking all possible starting points in dwds
    d = 0;
    if (debug >= 1) {
      printf("Checking words_in_seq feature for candidate '");
      show_string_upto_nator_nolf(doc_content, '\n', 0);
      printf("'\n");
    }
    while (d < dwd_cnt) {
      if (!strcmp((char *)dwds[d], (char *)qwds[0])) {
	// OK, an occurrence of the first query word has been found at pos. d in
	// the doc.  Check the other words
	if (debug >= 1) printf("  .. found '%s' at position %d\n", dwds[d], d);
	d++;
	failed = 0;
	for (q = 1; q < qwd_cnt; q++) {
	  failed = 1;
	  while (d < dwd_cnt) {
	    if (!strcmp((char *)dwds[d], (char *)qwds[q])) {
	      if (debug >= 1) printf("  .. found '%s' at position %d\n", dwds[d], d);
	      failed = 0;
	      break;
	      d++;
	    }
	    d++;
	  }
	  if (failed) break;
	}
	if (!failed){
	  if (debug >= 1) printf("  .. Success.  words_in_sequence feature set.\n");
	  *feat_wds_in_seq = 1;
	  break;
	}
      }
      d++;
    }

    // C. Set the words_in_phrase feature, checking all possible starting points in dwds
    if (debug >= 1) {
      printf("Checking phrase feature for candidate '");
      show_string_upto_nator_nolf(doc_content, '\n', 0);
      printf("', dwd_cnt = %d, qwd_cnt = %d\n", dwd_cnt, qwd_cnt);
    }


    for (d = 0; d <= (dwd_cnt - qwd_cnt); d++) {
      if (debug >= 1) printf("  .. Comparing '%s' (%d/%d) with '%s'\n", dwds[d], d, dwd_cnt, qwds[0]);
      if (!strcmp((char *)dwds[d], (char *)qwds[0])) {
	// OK, an occurrence of the first query word has been found at pos. d in
	// the doc.  Check the rest.
	if (debug >= 1) printf("  .. found '%s' at position %d\n", dwds[d], d);
	failed = 0;
	for (q = 1; q < qwd_cnt; q++) {
	  if (strcmp((char *)dwds[d + q], (char *)qwds[q])) {
	    if (debug >= 1) printf("  .. failed to find '%s' at position %d\n", qwds[q], d + q);
	    failed = 1;
	    break;
	  }
	}
	if (!failed) {
	  if (debug >= 1) printf("  .. Success. Phrase feature set.\n");
	  *feat_phrase = 1;
	  break;
	}
      }
    }
  }

  free(dwds);    // FRE0007
}


static double score(byte *doctxt, int dwd_cnt, u_char **qwds, int qwd_cnt,
		    double *rr_coeffs, double wt_from_doctable, double bm25score,
		    double location_lat, double location_long,
		    BOOL remove_accents, byte intervening_words, int debug) {
  // Assign a score to the candidate whose .forward string is passed as
  // doctxt.  Score is currently a linear combination of:
  //   alpha.   the applicable wt of the document
  //   beta.    a phrase feature
  //   gamma.   a words-in-sequence feature
  //   delta.   primacy feature
  //   epsilon. an excess length feature.
  //   zeta.    BM25 score
  //   eta.     Score derived from geographical distance from location_lat, location_long
  //   theta.   Score derived from intervening words (partials only)
  // In the case of error, return 0.0

  u_char *doc_content = NULL, *p, *end_of_doc_content = NULL,
    terminator = '\t';
  double applicable_wt = wt_from_doctable, rslt = 0.0, length_score = 0.0,
    geo_score = 0.0, doclat = 0.0, doclong = 0.0, span_score = 0.0;
  size_t dc_len, col4len;
  int feat_phrase = 0, feat_wds_in_seq = 0, feat_primacy = 0;

  if (debug >= 1) printf("   Score() called.  wt_from_doctable = %.3f\n", wt_from_doctable);

  // A. Isolate the actual document content
  p = (u_char *)doctxt;

  doc_content = p;
  while (*p && *p != terminator) p++;
  end_of_doc_content = p;
  dc_len = end_of_doc_content - doc_content;

  extract_text_features(doc_content, dc_len, dwd_cnt, qwds, qwd_cnt,
			&feat_phrase, &feat_wds_in_seq, &feat_primacy, remove_accents, debug);

  p++;

  // B. Get the length score.  Favour shorter suggestions.  Partha Parthasarathy pointed out that, with
  // relaxation_level > 0 it is possible for the document to be shorter than the query, hence the abs.  Without
  // abs, a length difference of -1 results in division by zero.
  // With abs, the length score increases with the length difference, regardless of its sign.  This seems
  // appropriate for both negative and positive differences.

  length_score = 1.0 / (double)(abs((int)(dwd_cnt - qwd_cnt)) + 1);

  // C. Calculate geo distance score.  
  
  if (rr_coeffs[6] > 0.0) {
    // Get doclat and doclong from the document. If present, they will be stored, space-separated
    // in column four.
    u_char *col4, *q;
    if (debug) {
      printf("Attempting to extract lat and long from column 3 of: \n");
      show_string_upto_nator(end_of_doc_content + 1, '\n', 4);
    }
    col4 = extract_field_from_record(end_of_doc_content + 1, 3, &col4len);
    errno = 0;
    doclat = strtod((char *)col4, (char **)&q);
    if (!errno) {
      doclong = strtod((char *)q, NULL);
      if (!errno) {
	if (debug) printf("Found doclat, doclong = %.3f, %.3f\n", doclat, doclong);
	geo_score = geoScore(location_lat, location_long, doclat, doclong);
	if (debug) printf("distance score derived from origin %.3f, %.3f was %.5f\n",
		      location_lat, location_long, geo_score);
      }
    }  // Silently ignore errors and leave geo_score at 0.0
  }

  // D. Span score
  if (rr_coeffs[7] > 0.0) {
    span_score = 1.0 / ((double)intervening_words  + 1.0);
  }

  // E. Finally, linearly combine all the features.
  rslt = rr_coeffs[0] * applicable_wt + rr_coeffs[1] * feat_phrase + rr_coeffs[2] * feat_wds_in_seq
    + rr_coeffs[3] * feat_primacy + rr_coeffs[4] * length_score + rr_coeffs[5] * bm25score
    + rr_coeffs[6] * geo_score + rr_coeffs[7] * span_score;
  if (debug >= 1) {
    printf("score(): %.2f X %.3f + %.2f X %d + %.2f X %d + %.2f X %d + %.2f X %.3f + %.2f X %.3f + %.2f X %.3f + %.2f X %.3f = %.3f, for ",
	   rr_coeffs[0], applicable_wt, rr_coeffs[1], feat_phrase, rr_coeffs[2], feat_wds_in_seq,
	   rr_coeffs[3], feat_primacy, rr_coeffs[4], length_score, rr_coeffs[5], bm25score,
	   rr_coeffs[6], geo_score, rr_coeffs[7], span_score, rslt);
    show_string_upto_nator(doc_content, terminator, 0);
  }

  return rslt;
}



static int score_cmp(const void *ip, const void *jp) {
  candidate_t *cip = (candidate_t *)ip, *cjp = (candidate_t *)jp;
  if (cjp->score < cip->score) return -1;
  if (cjp->score > cip->score) return 1;
  return 0;   // Not sure how to avoid returning zero.
}


u_char *what_to_show(long long docoff, byte *doc, int *showlen, int displaycol, u_char *extra_fields) {
  // If displaycol is zero, we return a copy of the whole record.  If 1 we return a
  // copy of the trigger, if -1 we show the document byte offset in QBASH.forward.
  // Otherwise, check whether there is a non-empty display column in the TSV line.  If so, return 
  // it instead of the trigger.  In AutoSuggest, column four is a spelling correction,
  // and displaycol should be 4.  In cases such as song lyrics, we might include a bunch
  // of HTML in that column or another.  If displaycol is less than three or greater 
  // than the number of columns actually present, we just return a pointer to the 
  // start of the record.
  // This function now mallocs the storage and makes a copy.  If terms_matched_bits NE zero,
  // then an additional column will be added to output, including a Hex representation of
  // the bit pattern.
  // If displaycol != 0, we squeeze out leading, trailing and multiple spaces.

  byte *p = doc, *what2show, *terminating_null;
  byte *rp, *wp = NULL, last;
  size_t tomalloc = 0,  field_lens[3];
  int l = 0, lbml = 0, f = 0, dcol = displaycol;
  byte *fields[3] = {NULL, NULL, NULL};

  if (0) printf("what_to_show(%d '%s')\n", displaycol, extra_fields);

  if (displaycol == -1) {
      what2show = (byte *)malloc(30);  // MAL2006
      sprintf((char *)what2show, "Off%lld", docoff);
      return what2show;  // ---------------------------------------------->
  }

  
  if (displaycol == 0)  {
    // Show the whole record.
    fields[f] = doc;
    while (*p && *p != '\n') p++;
    l = p - doc;
    field_lens[f] = l;
  }
  else if (displaycol >= 1) {
    // Can now display up to 3 columns
    int this_field = 0;

    
    while (dcol > 0) {
      this_field = dcol % 100;  // Get a field to display.
      dcol /= 100;
      // Get a copy of this field in fields[f] and its length in field_lens[f]
      fields[f] = extract_field_from_record(doc, this_field, field_lens + f);
      if (displaycol < 100 && field_lens[f] == 0) {
	// Only one field to be displayed and it's empty -- fall back to column 1
	if (fields[f] != NULL) free(fields[f]);
	fields[f] = extract_field_from_record(doc, 1, field_lens + f);
      }	  
      if (fields[f] == NULL){
	printf("Warning: Malloc MAL2006A failed.\n");
	return NULL;
      }
      l += (field_lens[f]);
      if (f != 0) l += 5;  // Allowing " +++ " separator
      if (0) printf("Field %d extracted. '%s', %zd. L = %d\n",
		    this_field, fields[f], field_lens[f], l);
      f++;
    }
  }
      
#if 0    
  cols2skip = displaycol - 2;
  while (*p && *p != '\t' && *p != '\n' && *p != '\r') p++;  // skip trigger
  // Assume there are at least 2 columns  -- it must be a tab

  for (s = 0; s < cols2skip; s++) {
    if (*p == '\t') {
      p++;
      while (*p && *p != '\t' && *p != '\n' && *p != '\r') p++;  // skip content of a column
    }
    else {
      p = doc;  // There are too few columns
      break;
    }
  }

  if (p != doc) {
    if (*p != '\t') p = doc; // There's no displaycol column
    else {
      p++;
      while (*p == ' ') p++;   // Skip leading whitespace
      if (*p < ' ') p = doc;  // There is a displaycol column but it's empty
    }
  }
  what2copy = p;
  //}  // end of if (displaycol ...

  // Now get the length of what we have to show unless displaycol == 0;
  if (displaycol > 0) while (*what2copy == ' ') what2copy++;  // Skip leading spaces
  p = what2copy;
  l = 0;
  while (*p && *p != terminator && *p != '\n' && *p != '\r' && l < MAX_RESULT_LEN) {
    p++;  l++;
  }
  if (displaycol > 0) while (p > what2copy && *(p - 1) == ' ') {
      p--;  l--; // Suppress trailing spaces and reduce the length.
    }

#endif


// Now work out whether there are bit maps to display and the characters needed for such display.
  lbml = 0;
  if (extra_fields != NULL && extra_fields[0]) {
    lbml = (int)strlen((char *)extra_fields);
  }

  tomalloc = l + lbml + 2;
  what2show = (byte *)malloc(tomalloc);  // MAL2006
  if (what2show == NULL) {
    printf("Warning: Malloc MAL2006 failed for %zd bytes (lbml was %d).\n", tomalloc, lbml);
    return NULL;
  }


  // Copy in the fields with a separator.
  p = what2show;
  f--;
  while (f >= 0) {
    if (p > what2show) {
      strcpy((char *)p, " +++ "); // separator if not the first field
      p += 5;
    }
		  
    strcpy((char *)p, (char *)fields[f]);
    p += field_lens[f];
    free(fields[f]);
    f--;
  }
  *p = 0;  // NULL terminate

  rp = what2show;  wp = rp;  last = 0;
  // Squeeze out superfluous spaces
  last = 0;  // Non-space
  terminating_null = what2show + l;
  while (*rp == ' ') rp++;  // Skip leading spaces
  while (*rp && rp < (terminating_null)) {
    if (*rp != ' ' || last != ' ') {
      *wp++ = *rp;
    }
    else {
      l--;
    }
    last = *rp;
    rp++;
  }
  *wp = 0;

  if (0) printf("After condensing multiple spaces: tomalloc = %zd l = %d [%s]\n", tomalloc, l, what2show);

  if (lbml) {
    *wp++ = '\t';
    strcpy((char *)wp, (char *)extra_fields);
    lbml++;
  }

  what2show[l + lbml] = 0;
  *showlen = l + lbml;
  if (0) printf("what2show = '%s', showlen = %d\n", what2show, *showlen);
  return what2show;
}


#define BITMAP_LIST_LEN 10000

#define okapi_k1 2.0
#define okapi_b 0.75


static void rerank_and_record(query_processing_environment_t *qoenv, book_keeping_for_one_query_t *qex,
			      byte *forward, byte *doctable, size_t fsz, double score_multiplier,
			      double penalty_multiplier_for_partial_matches) {
  // *** This function isn't used in classifier_modes ***
  // Record up to max_last_rank candidates in the 
  // tl_suggestions and tl_scores arrays after some sort of reranking
  //
  // Note that in the case of multi-query usage, we may arrive here with tl_suggestions slots
  // already filled and qex-tl_returned > 0.   In that case, we fill in results at the
  // in the unused slots.
  // After each query variant is run, we zero qex->candidates_recorded[rb] for all the rbs.
  // For each variant we create a new contiguous array of candidates.
  byte *doc, *bmlp;
  u_char terminator = '\t';
  int doclen_inwords, r, rb, dwd_cnt, start_slot, slot, candidates_recorded_this_variant = 0,
    t, terms_missing, rbu = 0,  // rbu - result blocks used
    s;
  docnum_t d;
  double score_from_doctable, bm25score = 0.0, penalty_multiplier = score_multiplier;
  unsigned long long *dtent;  // Excluding the signature part
  candidate_t *candidates, *contiguous_array_of_candidates;
  byte *rank_only_counts = NULL;
  BOOL zapadupe;
  
  if (0) printf("\nArriving in r_and_r() with tl_returned = %d\n\n",
		qex->tl_returned);

  for (rb = 0; rb <= MAX_RELAX; rb++) {
    if (qex->candidates_recorded[rb] > 0) {
      candidates_recorded_this_variant += qex->candidates_recorded[rb];
      rbu = rb + 1;
    }
  }
  if (qoenv->debug >= 1) fprintf(qoenv->query_output, "Candidates recorded this variant = %d\n",
				      candidates_recorded_this_variant);

  if (candidates_recorded_this_variant <= 0) {
    if (qoenv->debug >= 1)
      fprintf(qoenv->query_output,
	      "  rerank_and_record(): No reranking or recording to be done: zero candidates\n");
    return;
  }

  contiguous_array_of_candidates = (candidate_t *)malloc(candidates_recorded_this_variant * sizeof(candidate_t));  // MAL1110
  if (contiguous_array_of_candidates == NULL) {
    fprintf(qoenv->query_output, "Warning: Malloc of contiguous_array_of_candidates failed.  No results will be displayed.\n");
    return;
  }
  if (qoenv->debug >= 1) fprintf(qoenv->query_output, "  rerank_and_record(): Reranking %d candidates from %d result blocks\n",
				 candidates_recorded_this_variant, rbu);

  if (qoenv->scoring_needed && qoenv->debug >= 1)
    fprintf(qoenv->query_output, "  rerank_and_record(): Complex scoring is needed.\n");

  candidates_recorded_this_variant = 0;

  for (rb = 0; rb < rbu; rb++) {    // ----------- Loop through all the result blocks, assigning scores
    // ----------- and copying into a contiguous array for sorting.
      terms_missing = rb;
    penalty_multiplier = score_multiplier;
    // Calculate the penalty multiplier to be applied for this number of terms missing
    for (t = 0; t < terms_missing; t++)
      penalty_multiplier *= penalty_multiplier_for_partial_matches;
    if (qoenv->debug >= 1) fprintf(qoenv->query_output, "Result block %d: terms missing %d:  penalty_multiplier %f\n",
				   rb, terms_missing, penalty_multiplier);
    candidates = qex->candidatesa[rb];
    if (qex->rank_only_cnt) rank_only_counts = qex->rank_only_countsa[rb];
    if (qoenv->debug >= 1) {
      fprintf(qoenv->query_output, "Result block %d: %d candidates recorded; terms_missing = %d\n",
	      rb, qex->candidates_recorded[rb], terms_missing);
    }
    // Assign scores to all the candidates at this level of relaxation
    for (r = 0; r < qex->candidates_recorded[rb]; r++) {
      d = candidates[r].doc;
      dtent = (unsigned long long *)(doctable + (d * DTE_LENGTH));
      dwd_cnt = (int)(*dtent & DTE_WDCNT_MASK);
      if (0) printf("dwd_cnt = %d\n", dwd_cnt);
      // NOTE that in version 1.3+ indexes, only 5 bits are used to store document length in words, although up
      // to 254 may be indexed.   If doclen_inwords is 31, that means >=31
      if (dwd_cnt == 0) {
	if (qoenv->debug >= 2) fprintf(qoenv->query_output, "Setting score to zeroq for doc %lld because dwd_cnt is zero.\n", d);
	// Could be because suggestion is actually too long to represent in 8 bits
	candidates[r].score = 0;
      }
      else {
	score_from_doctable = get_score_from_dtent(*dtent);
	if (qoenv->debug >= 1)
	  fprintf(qoenv->query_output, "  rerank_and_record(): candidate %d, doc %lld, score_from_dt= %.4f, plier = %.4f\n",
		  r, d, score_from_doctable, penalty_multiplier);
	candidates[r].score = score_from_doctable * penalty_multiplier;  // Default, will be used if no complex scoring
	doc = get_doc(dtent, forward, &doclen_inwords, fsz);
	if (doc == NULL) {
	  // This is an error condition which shouldn't occur but which must be handled
	  // Set score to negative which may push this result output of the top-k
	  candidates[r].score = -1.0;
	  continue;
	}

	if (qoenv->debug >= 3) {
	  fprintf(qoenv->query_output, "  rerank_and_record(): score_from_doctable candidate %d, doc %lld: \n", r, d);
	  show_string_upto_nator(doc, '\n', 0);
	}

	if (qoenv->scoring_needed) {
	  if (qoenv->debug >= 3) {
	    fprintf(qoenv->query_output, "  rerank_and_record(): about to call score().  Fwd Offset = %lld\n",
		    (long long)(doc - forward));
	    fprintf(qoenv->query_output, "  rerank_and_record(): dwds = %d, qwd_cnt = %d\n",
		    (int)(*dtent & DTE_WDCNT_MASK), qex->qwd_cnt);
	  }
	  qex->op_count[COUNT_SCOR].count++;

	  if (qoenv->rr_coeffs[5] > 0.0) {
	    int k;
	    double tf, idf, doclen, lenratio;
	    bm25score = 0.0;
	    if (dwd_cnt == 31) doclen = (double) utf8_count_words_in_string(doc, FALSE, FALSE, FALSE, FALSE);
	    else doclen = (double) dwd_cnt;

	    lenratio = doclen / qoenv->avdoclen;
	    for (k = 0; k < qex->qwd_cnt; k++) {
	      tf = (double)candidates[r].tf[k];
	      idf = get_idf_from_quantized(qoenv->N, 0xFF, candidates[r].qidf[k]);
	      
	      bm25score +=  (tf * idf) / (tf + okapi_k1 *(1.0 - okapi_b + okapi_b * lenratio));
	      if (qoenv->debug) printf("BM25(doc %lld): tf = %.0f, idf = %.4f, len = %.0f lenratio = %.3f, dwd_cnt= %d: Cumul.Score = %.4f\n",
			    d, tf, idf, doclen, lenratio, dwd_cnt, bm25score);
	    }

	  }

	  candidates[r].score = score(doc, dwd_cnt, qex->qterms, qex->qwd_cnt, qoenv->rr_coeffs,
				      score_from_doctable, bm25score, qoenv->location_lat, qoenv->location_long,
				      qoenv->conflate_accents, candidates[r].intervening_words, qoenv->debug)
	    * penalty_multiplier;
	}
      }

      // Adjustment for rank_only terms  -- This formula is just to test the mechanism.  Need to come up
      // with a more sensible formula
      // The test for NULL here should only succeed if a malloc() failed in process_query()
      if (qex->rank_only_cnt && rank_only_counts != NULL && rank_only_counts[r]) {
	double plier = 3.0;
	if (qoenv->debug >= 1) fprintf(qoenv->query_output, "Rank_only_terms(r = %d): Multiplying score of doc %lld by %.3f\n",
				       r, d, plier);
	candidates[r].score *= plier;
      }

      memcpy(contiguous_array_of_candidates + candidates_recorded_this_variant, candidates + r, sizeof(candidate_t));
      candidates_recorded_this_variant++;
      if (qoenv->debug >= 1) fprintf(qoenv->query_output,
				     "R-and-R(result block %d): %d - doc %llu [%.3f]. Total now recorded = %d\n",
				     rb, r, candidates[r].doc, candidates[r].score, candidates_recorded_this_variant);
    }
  }


  
  // ------------- sorting by score ----  all other modes
  if (qoenv->debug >= 2) fprintf(qoenv->query_output, "  rerank_and_record(): qsorting by score\n");
  qsort(contiguous_array_of_candidates, candidates_recorded_this_variant, sizeof(candidate_t), score_cmp);
  if (qoenv->debug >= 2) fprintf(qoenv->query_output, "  rerank_and_record(): displaying\n");


  // Now loop through the contiguous array of candidates (which reference documents as numbers)
  // and work out what text to put in the result slot.  

  r = 0; bmlp = NULL;
  start_slot = qex->tl_returned;  // May be non-zero in the case of multiqueries
  slot = start_slot;
  if (0) printf("R&R: start_slot = %d, totcanrec = %d\n", start_slot, candidates_recorded_this_variant);
  while (r < candidates_recorded_this_variant && slot < qoenv->max_to_show) {
    d = contiguous_array_of_candidates[r].doc;
    zapadupe = FALSE;
    
    // First thing to do is to make sure that this candidate isn't the same
    // document as one already placed by a previous query variant.
    for (s = start_slot - 1; s >= 0; s--) { // Check those items
      if (0) printf("     Checking CAC[%d] against docids[%d]\n", r, s);
      if (d == qex->tl_docids[s]) zapadupe = TRUE;
    }
    if (zapadupe) break; 
    
    // -------------- Working out what to show  ----------------
    dtent = (unsigned long long *)(doctable + (d * DTE_LENGTH));
    if (qoenv->debug >= 2) fprintf(qoenv->query_output, "  rerank_and_record(): %d, %lld\n", r, d);
    doc = get_doc(dtent, forward, &doclen_inwords, fsz);
    if (0) printf("doclen_inwords = %d\n", doclen_inwords);
    if (doc != NULL) {
      int showlen = 0;
      u_char *what2show = what_to_show((long long)(doc - forward), doc, &showlen, qoenv->displaycol, bmlp);
      if (what2show != NULL)  {  // Could be NULL in case of memory failure in what_to_show()

	if (qoenv->debug >= 2) fprintf(qoenv->query_output, "Recording candidate %d (doc %lld, with score %.3f) in slot %d.\n",
				       r, d, contiguous_array_of_candidates[r].score, slot);

	//  =============  Check for equal-score duplicates here  =======================
	// Duplicates have the same first column of output and the same score

	//Query:  x$9     4       Beehive, Molesworth St, Pipitea, Wellington 6011, New Zealand   0.38552
	//Query:  x$9     5       Grawking Towers, 260 Creighton Siding Rd, Euroa 0.38552
	//Query:  x$9     6       Beehive, Molesworth St, Pipitea, Wellington 6011, New Zealand   0.38552

	if ((qoenv->duplicate_handling > 0) && (slot > 0)) {
	  for (s = slot -1; s >= 0; s--) { // Check all the already placed items with equal score
	    if (qex->tl_scores[s] > contiguous_array_of_candidates[r].score) break;  // --->
	    zapadupe = isduplicate((char *)(qex->tl_suggestions[s]),(char *)what2show, FALSE);
	    if (zapadupe) break;
	  }
	}
	  
	if (!zapadupe) {
	  //  Doesn't duplicate the previous answer
	  qex->tl_docids[slot] = d;
	  qex->tl_suggestions[slot] = what2show;  // That's in malloced storage (MAL2006)
	  if (qoenv->debug >= 2) {
	    fprintf(qoenv->query_output, "R_and_R: Slot %d: copied '%s' from: ", slot, qex->tl_suggestions[slot]);
	    show_string_upto_nator(what2show, terminator, 0);
	  }
	  qex->tl_scores[slot] = contiguous_array_of_candidates[r].score;
	  slot++;
	}
      }
    }  // Just ignore any erroneous doc
    r++;
  }  // end of while (r < candidates_recorded_this_variant && slot < qoenv->max_to_show)
  free(contiguous_array_of_candidates);  // FRE1110
  memset(qex->candidates_recorded, 0, (MAX_RELAX + 1) * sizeof(int));  // Zero all the result block
                                                                       // counts in case there's another variant.

  qex->tl_returned = slot;

  if (qoenv->debug >= 1) fprintf(qoenv->query_output, "End of R & R\n");
}



// ---------------------------------------------------------------------------------------
// Functions for filtering and recording candidate suggestions.
// ---------------------------------------------------------------------------------------


static BOOL isprefixmatch(u_char *qstring, u_char *dstring)  {
  // Returns true if qstring is a prefix match of dstring, taking into account the 
  // use of slashes in qstring to indicate word prefixes.
  BOOL prefix_matching = FALSE;

  // Ignore leading space in both strings.  Possibly not needed
  while (*qstring && *qstring == ' ') qstring++;
  while (*dstring && *dstring == ' ') dstring++;

  while (*qstring && *dstring) {
    if (prefix_matching) {
      if (*qstring == ' ') {
	// Just skip dstring forward until space or null
	while (*dstring && *dstring != ' ') { dstring++; }
	prefix_matching = FALSE;
      }
      else {
	if (*qstring != *dstring) { return FALSE; }  // ------------------->
	qstring++;
	dstring++;
      }
    }
    else {
      if (*qstring == '/') {
	prefix_matching = TRUE;
	qstring++;  // Advance only the qstring pointer.
      }
      else {
	if (*qstring != *dstring) { return FALSE; }  // ------------------->
	qstring++;
	dstring++;
      }
    }
  }


  if (*qstring && !*dstring) {
    // Normally this should never happen because qstring couldn't be a full match if it's "longer" than dstring.
    return FALSE;  // ------------------->
  }

  return TRUE;    // ------------------->
}



static int possibly_store_in_order(double *cf_coeffs, long long candid8, double degree_of_match, 
				   candidate_t *candidates, int max_to_show, int *recorded,
				   u_int terms_matched_bits, byte match_flags, double *FV) {
  // This function is used only in classifier modes.
  // candid8 is a document which has passed the classifier_threshold test.  It achieved a
  // lexical degree_of_match which is passed in, along with its record-type and static scores.
  // The three are linearly combined.  If the combined_score exceeds the minimum so far recorded,
  // then it is inserted in the appropriate spot in the candidates array so as to preserve
  // descending score order.
  //
  //  - candidates is an array with max_to_show elements, numbered 0 - (max_to_show - 1).  
  //  - *recorded says how many elements have been already inserted into candidates.
  //  - element zero is always the best (highest-scoring) item

  // Return 1 if it was inserted, zero otherwise

  // It's like a heap but more simple-minded.  For very small lists which I assumed would be the norm,
  // it should be faster than a heap.
  //

  double combined_score = 0.0;
  BOOL ldebug = FALSE;
  if (cf_coeffs[1] > EPSILON || cf_coeffs[2] > EPSILON)
    combined_score = cf_coeffs[0] * degree_of_match + cf_coeffs[1] * FV[5] + cf_coeffs[2] * FV[6];
  else
    combined_score = degree_of_match;
  if (ldebug) printf("chi = %.5f, psi = %.5f, omega = %.5f,  dolm = %.5f, rt_score=%.5f, ss = %.5f\n", 
		     cf_coeffs[0], cf_coeffs[0], cf_coeffs[0], degree_of_match, FV[5], FV[6]);
  if (ldebug) printf("PSIR term bits = %X\n", terms_matched_bits);

  if (*recorded == 0) {
    // It's the first and only entry
    if (ldebug) printf("Storing doc %lld with score %.4f as first entry.\n",
		       candid8, combined_score);
    if (0) printf("Writing to candidates[%d]\n", *recorded);
    memcpy(candidates[*recorded].FV, FV, FV_ELTS * sizeof(double));
    candidates[*recorded].score = combined_score;
    candidates[*recorded].terms_matched_bits = terms_matched_bits;
    candidates[*recorded].match_flags = match_flags;
    candidates[(*recorded)++].doc = candid8;
    return 1;
  }
  else if (combined_score <= candidates[*recorded - 1].score) {
    // It goes below any of the results we've recorded,   Is there an unused slot?
    if (*recorded >= max_to_show) {
      // No, there's not.
      if (ldebug) printf("Ignoring doc %lld with score %.4f at tail position %d / %d.\n",
			 candid8, combined_score, *recorded - 1, max_to_show - 1);
      return 0;  // We've already got max_to_show results better than this.
    } 
    else {
      // Yes there is.  Just append this one to the bottom of the list
      if (ldebug) printf("Storing doc %lld with score %.4f at tail position %d / %d.\n",
			 candid8, combined_score, *recorded, max_to_show - 1);
      if (0) printf("Writing to candidates[%d]\n", *recorded);
      memcpy(candidates[*recorded].FV, FV, FV_ELTS * sizeof(double));
      candidates[*recorded].score = combined_score;
      candidates[*recorded].terms_matched_bits = terms_matched_bits;
      candidates[*recorded].match_flags = match_flags;
      candidates[(*recorded)++].doc = candid8;
      return 1;
    }
  } else {
    // It fits somewhere in the list.  We'll need to do some shuffling
    int i, j, bottom, slot;

    // If *recorded == 1 and the new score is higher than candidates[0] then i will be set to -1 and this loop
    // won't execute

    // Starting from the second lowest candidate scan up the list until we find an item >= than the one we're trying 
    // to insert.
    for (i = (*recorded - 2); i >= 0; i--) {
      if (combined_score <= candidates[i].score) {
	// We need to put this item at slot i + 1, after shuffling down stuff below 
	slot = i + 1;
	// shuffle everything down, being careful not to go beyond max_to_show items.
	bottom = *recorded;
	if (*recorded == max_to_show) bottom--;
	if (ldebug) printf("Shiffling down items from %d to %d\n", bottom, slot);
	for (j = bottom; j > slot; j--) {
	  if (ldebug) printf("    Copying position %d to position %d\n", j - 1, j);
	  if (0) printf("Writing to candidates[%d]\n", j);
	  memcpy(candidates + j, candidates + (j - 1), sizeof(candidate_t));
	}
	if (ldebug) printf("Storing doc %lld with score %.4f at middle position %d / %d.\n",
			   candid8, combined_score, slot, max_to_show - 1);
	if (0) printf("Writing to candidates[%d]\n", slot);
	memcpy(candidates[slot].FV, FV, FV_ELTS * sizeof(double));
	candidates[slot].score = combined_score;
	candidates[slot].terms_matched_bits = terms_matched_bits;
	candidates[slot].match_flags = match_flags;
	candidates[slot].doc = candid8;
	if (*recorded != max_to_show) (*recorded)++;
	if (ldebug) {
	  printf("Sorted list:");
	  for (i = 0; i < *recorded; i++) printf(" %.4f", candidates[i].score);
	  printf("\n");
	}
	return 1;  // --------------------------------------------->
      }
    }
    // If we get here, it means that this one is the new top scorer
    slot = 0;
    bottom = *recorded;
    if (*recorded == max_to_show) bottom--;
    if (ldebug) printf("Shuiffling down items from %d to %d\n", bottom, slot);
    for (j = bottom; j > slot; j--) {
      if (ldebug) printf("    Copying position %d to position %d\n", j - 1, j);
      if (0) printf("Writing to candidates[%d]\n", j);
      memcpy(candidates + j, candidates + (j - 1), sizeof(candidate_t));
    }
    if (ldebug) printf("Storing doc %lld with score %.4f at middle position %d / %d.\n",
		       candid8, combined_score, slot, max_to_show - 1);
    if (0) printf("Writing to candidates[%d]\n", slot);
    memcpy(candidates[slot].FV, FV, FV_ELTS * sizeof(double));
    candidates[slot].score = combined_score;
    candidates[slot].terms_matched_bits = terms_matched_bits;
    candidates[slot].match_flags = match_flags;
    candidates[slot].doc = candid8;
    if (*recorded != max_to_show) (*recorded)++;
    if (ldebug) {
      printf("Sorted list:");
      for (i = 0; i < *recorded; i++) printf(" %.4f", candidates[i].score);
      printf("\n");
    }
    return 1;
  }
}


int possibly_record_candidate(query_processing_environment_t *qoenv,
			      book_keeping_for_one_query_t *qex,
			      saat_control_t *pl_blox,
			      byte *forward, byte *index, byte *doctable, size_t fsz,
			      long long candid8, int result_block_to_use,
			      u_int terms_matched_bits) {
  // Take a candidate document supplied by saat_relaxed_and() and decide if we want to 
  // record it in candidate.
  // Return 1 if we do, zero if we don't, and -ve if we encounter an error.
  // candid8  is the index of a document in the doctable which matches the query.
  //
  // Reasons for rejection:
  //  1. ...
  //  2. The candidate is longer than the input by more than max_length_diff words
  //  3. The input contains a repeated word (eventually, or overlapping term) and
  //     the candidate has only one such occurrence.
  //  4. The input contains one or more partial words and they don't match any 
  //     doc words (which haven't already been matched by a qwd.
  //  5. Lat-longs of both query and document are known, andocument is too far from the query location.
  //  6. Query contained a street number which was invalid for this doc (assumed an address) 
  // An accepted candidate is stored within an element of a result block in the qex->candidatesa
  // array.   The result block is specified by result_block_to_use and the element by 
  // *recorded.  The value in the latter is updated.

  // Note:  If indexed with x_bigger_trigger=TRUE, candidates may have more than 255 words
  // indexed (MAX_WDS_INDEXED_PER_DOC).  However, here we only consider up to WDPOS_MASK (255)
	
  // qwds[i] represents the i-th word in the query.  However, depending upon the mode it may represent
  // a more complex term.  E.g. 
  //	  - "a simple phrase"
  //	  - "a [complex phrase]"
  //	  - [a simple disjunction]
  //	  - [a "complex disjunction"]

  int candid8_length, dc_len = 0, dwd_cnt = 0,
    *recorded = qex->candidates_recorded + result_block_to_use,
    intervening_words = 0;   // Only used in partials thus far
  byte *doc = NULL, rank_only_count = 0;
  unsigned long long *dtent = NULL, d_signature = 0;
  candidate_t *candidates = qex->candidatesa[result_block_to_use];
  byte *rank_only_counts = NULL;
  u_char dc_copy[MAX_RESULT_LEN + 1], *dwds[WDPOS_MASK + 1];
  double score = 0.0;
  BOOL apply_geo_filtering = FALSE, explain_rejection = qoenv->debug;

  if (qoenv->debug >= 1) {
    printf("P_R_C.  recorded = %d.  cg_qwd_cnt = %dqwd_cnt = %d.  terms_matched_bits = %X\n",
	   *recorded, qex->cg_qwd_cnt, qex->qwd_cnt, terms_matched_bits);
    if (qex->qwd_cnt > MAX_WDS_IN_QUERY) return(-100050);
  }

  // Unfortunately, the following bit setting doesn't work if the query has been shortened
  candidates[*recorded].terms_matched_bits = 0;

  if (qex->rank_only_cnt) rank_only_counts = qex->rank_only_countsa[result_block_to_use];
  dtent = (unsigned long long *)(doctable + candid8 * DTE_LENGTH);
  candid8_length = (int)(*dtent & DTE_WDCNT_MASK);
  if (0) printf("candid8_length = %d\n", candid8_length);
  d_signature = (*dtent >> DTE_DOCBLOOM_SHIFT); // No need for masking cos Bloom is Most Sig, and zeroes are shifted in from left.

  qex->op_count[COUNT_CAND].count++;

  if (qoenv->relaxation_level == 0) {
    // Some rejection tests don't make sense when we're relaxing.
    qex->op_count[COUNT_BLOM].count++;
    if ((d_signature & qex->q_signature) != qex->q_signature) { // Are all the one bits in q_signature set?
      if (explain_rejection)
	fprintf(qoenv->query_output,
		"possibly_record_candidate(): Rejection reason 'signature mismatch' %llX v. %llX\n",
		d_signature, qex->q_signature);
      return 0; // 1 -------------------------------------------->
    }
  }

  // NOTE that in version 1.3+ indexes, only 5 bits are used to store document length in words, although positions up
  // to 254 may be indexed.   If doclen_inwords is 31, that means >=31.  This could lead to very long candidates not
  // being rejected (in relatively uncommon circumstances).
  if (0) printf("Length check():  %d - %d > %d?\n", candid8_length, qex->qwd_cnt, qex->max_length_diff);
  if (candid8_length - qex->q_max_mat_len > qex->max_length_diff) {
    if (explain_rejection) 
      fprintf(qoenv->query_output, "possibly_record_candidate(): Rejection reason 'too long' (%d - %d > %d)\n",
	      candid8_length, qex->q_max_mat_len, qex->max_length_diff);
    return 0; // 2 ------------------------------------------------->
  }

  // ------------------  Handling repeated query words --------------------
  //
  // Note: As of 1.5.18-OS, this is only needed when a repeated word appears within
  // a phrase or disjunction.  E.g  {galaxies clusters [galaxies universe]}.
  //
  // *** We really should properly handle the case where type is SAAT_WORD
  // *** and repetition_count > 1.
  //
  // Extra checking for queries with repeated words
  // All docnums must be the same, make sure that all the wpos es are different
  //
  // The wpos entries are all zero to start with.  As we examine each term we set the wpos entry to 
  // 1.  If we find a wpos entry which is already 1, we try to advance the postings
  // list for the affected term, within this document.  If we can't, we reject this candidate, on the 
  // grounds that a term is repeated in the query but not in the document.   This test may not
  // correctly handle queries with phrases or disjunctions.
  //
  // There has to be a special case for collections indexed with -x_bigger_trigger=TRUE, since the word
  // positions in such an index max out at 254.  If we encounter a word pos of 254, we abandon the
  // checking
  if (qoenv->relaxation_level == 0 && qex->cg_qwd_cnt == qex->qwd_cnt) {
    // Can maybe think through how to do this while relaxing, but haven't done so yet.
    // Also skip this section if the query has been shortened.
    BOOL abandon_repcheck = FALSE;
    int rslt, w, wpos[WDPOS_MASK] = { 0 };
    for (w = 0; w < qex->tl_saat_blocks_used; w++) {
      if (qoenv->debug >= 2)
	fprintf(qoenv->query_output,
		"possibly_record_candidate(): Repcheck: qwd %d/%d, wpos[%d] = %d\n",
		w, qex->qwd_cnt, pl_blox[w].curwpos, wpos[pl_blox[w].curwpos] );
      if (pl_blox[w].curwpos >= 254) {
	if (0) printf("Abandon repeated query words ship 1!\n");
	abandon_repcheck = TRUE;  // No need to set this really, since this is the outer loop
	break;  //-------->
      }
     // Need a while loop here for cases where term is multiply repeated, e.g. {damn damn damn}
      while (wpos[pl_blox[w].curwpos]) {
	if (0) {
	  doc = get_doc(dtent, forward, &dc_len, fsz);   
	  printf("wpos[%d] is non-zero, calling saat_advance(): Content of doc %lld is:\n",
		 pl_blox[w].curwpos, candid8);
	  show_string_upto_nator(doc, '\n', 0);
	  printf("--------------------------\n");
	}
	rslt = saat_advance_within_doc(qoenv->query_output, pl_blox + w, index, qex->op_count, qoenv->debug);
	if (rslt == 0) {
	  if (explain_rejection)
	    fprintf(qoenv->query_output,
		    "possibly_record_candidate(): Rejection reason 'repeated term not repeated'\n");
	  return 0;        // 3 --------------------------------------------------->
	}
	else if (rslt < 0) return rslt;	   // An error signalled by saat_advance_within_doc ---------------------->
	if (pl_blox[w].curwpos >= 254) {
	  if (0) printf("Abandon repeated query words ship!\n");
	  abandon_repcheck = TRUE;
	  break;  //-------->
	}
      }
      if (abandon_repcheck) break;   //-------->
      if (qoenv->debug >= 2)
	fprintf(qoenv->query_output,
		"possibly_record_candidate(): Setting wpos[%d] to 1\n", pl_blox[w].curwpos);
      wpos[pl_blox[w].curwpos] = 1;
    }
  }
  // ------------------  End of handling repeated query words section --------------------


  // ----------------- Preparations for modes which require document text: classifier; partial words; rank-only; geo-filtering ----------------
  apply_geo_filtering = (qoenv->geo_filter_radius > 0.0)
    && (qoenv->location_lat != UNDEFINED_DOUBLE)
    && (qoenv->location_long != UNDEFINED_DOUBLE);
  
  if (qoenv->classifier_mode || qex->partial_cnt || qex->rank_only_cnt
      || apply_geo_filtering || qoenv->street_address_processing > 1) {
    u_char *p = NULL;
    if (0) printf("Partials, classifier or rank_only, *dtent = %llx\n", *dtent);

    doc = get_doc(dtent, forward, &dc_len, fsz);   
    if (doc == NULL) {
      return 0;
    }

    if (apply_geo_filtering) {
      double km;
      km = distance_between((char *)doc, qoenv->location_lat, qoenv->location_long);
      if (qoenv->debug >= 1) printf("Document/Query distance = %.3fkm\n", km);
      if (km > qoenv->geo_filter_radius) {
	if (explain_rejection)
	  printf("   Rejecting document due to excessive geo-distance from query origin.\n");
	return 0;  // 4 --------------------------------------------------------->
      }
    }

    // 1. Make a copy of the doc in malloced memory  (Actually it's on the stack at the moment)
    p = (u_char *)doc;
    while (*p &&  *p >= ' ') p++;  // Skip to the tab
    dc_len = (int)(p - (u_char *)doc);
    if (qoenv->debug >= 3)
      fprintf(qoenv->query_output, "possibly_record_candidate(): dc_len is %d c.f. %d\n",
	      dc_len, MAX_RESULT_LEN);
    if (dc_len > MAX_RESULT_LEN)  {
      if (explain_rejection)
	printf("   Rejecting document due to excessive document length %d. Huh?\n", dc_len);
      return 0;   // 5 ---------------------------------------------------------->
    }

    // Copy trigger to dc_copy, converting to lower case

     
    utf8_lowering_ncopy(dc_copy, doc, dc_len);  // This function avoids a potential problem
                                                        // when dc_copy ends with an incomplete UTF-8
                                                        // sequence.  
    if (qoenv->conflate_accents) utf8_remove_accents(dc_copy);
    dc_copy[dc_len] = 0;

    if (qoenv->debug >= 3) fprintf(qoenv->query_output, "possibly_record_candidate(): dc_copy is '%s'\n", dc_copy);

    if (qoenv->use_substitutions) {
      // Make substitutions in the candidate document, but not if query operators are present or would be introduced.
      // Note: apply_substitutions_rules_to_string() applies limits to the input length, and to the output
      // length.  If input > 256 no substitutions will occur.  Output is limited to 
      apply_substitutions_rules_to_string(qoenv->substitutions_hash, qoenv->language, dc_copy,
					  TRUE, TRUE, qoenv->debug);

      if (qoenv->debug >= 2)
	fprintf(qoenv->query_output,
		"possibly_record_candidate(): after substitution, dc_copy is '%s'\n", dc_copy);
    }


    if (qoenv->debug >= 3)
      fprintf(qoenv->query_output, "possibly_record_candidate(): qex->qwd_cnt, partial_cnt, dwd_cnt: %d, %d, %d\n",
	      qex->qwd_cnt, qex->partial_cnt, dwd_cnt);
  }
  // ----------------- End of preparations for modes which require document text ----------------


  // ------------------  Classifier Mode Section --------------------------------
  // The classification score will be zero if the degree of lexical match doesn't reach the threshold
  // and a combination of lexical match and static score (range 0 - 1) otherwise

  if (qoenv->classifier_mode) {
    int stored = 0;
    byte match_flags = 0;
    double FV[FV_ELTS] = { 0.0 };   // Classifier feature vector:  Q, D, I, M, S, rectype, static, Jaccard DOLM
    dwd_cnt = (int)((*dtent & DTE_WDCNT_MASK) >> DTE_WDCNT_SHIFT);
    if (dwd_cnt == DTE_WDCNT_MAX) {
      // The value 31 in the WDCNT field of the doctable means >= 31.  Need to find exact length.
      dwd_cnt = utf8_count_words_in_string(dc_copy, FALSE, FALSE, FALSE, FALSE);
    }
    if (0) printf(" -- dc_copy = '%s'\n", dc_copy);
    score = classification_score(qoenv, qex, dtent, dc_copy, dc_len, dwd_cnt, &match_flags, FV, &terms_matched_bits);
    qex->op_count[COUNT_SCOR].count++;

    // A segment_intent_multiplier may be set if the original query contained intent words such as 'lyrics of'
    // Originally, we multiplied the score by the multiplier, now we multiply the threshold
    if (0) printf("... ran the classifier:  score was %.5f.  s_i_m = %.4f\n", score, qex->segment_intent_multiplier);
    if (score < qoenv->classifier_threshold * qex->segment_intent_multiplier) {
      if (explain_rejection)
	fprintf(qoenv->query_output, "Candidate rejected due to insufficient classifier score %.5f\n", score);
      return 0;  // 6 -------------------------------------------------------------------->
    }

    stored = possibly_store_in_order(qoenv->cf_coeffs, candid8, score, candidates, qoenv->max_to_show, recorded,
				     terms_matched_bits, match_flags, FV);
    if (0) printf("  CCC %d\n", qex->qwd_cnt);

    if (qoenv->debug >= 1)
      fprintf(qoenv->query_output,
	      "possibly_record_candidate(classify): recording %lld in candidates[%d].  RB to use = %d.  Score = %.5f\n",
	      candid8, *recorded, result_block_to_use, score);
    if (stored == 0) {
      if (explain_rejection)
	fprintf(qoenv->query_output, "Candidate rejected by possibly_store_in_order()\n");
      return 0;  // 7 ---------------------------------------------------------------------->
    }
    return 1;  // Possibly worthy but we may find something better later.--------------------------------------->

  }

  // ------------------  End of classifier Mode Section --------------------------------



  //  ---------------- Handling the matching of partial words. ----------------------------------
  if (qex->partial_cnt) {
    // Split the document into words and zap the ones which match a query word.
    // Then make sure that each partial word is the prefix of a separate unzapped word
    BOOL all_partials_matched = TRUE;
    int q = 0, d = 0, min_matched_index = dwd_cnt, max_matched_index = -1, words_matched = 0;

    if (qoenv->debug >= 2)
      fprintf(qoenv->query_output, "possibly_record_candidate(): Checking partial words\n");
    qex->op_count[COUNT_PART].count++;

    // 2. Split the doc copy into words.

    dwd_cnt = utf8_split_line_into_null_terminated_words(dc_copy, dwds, WDPOS_MASK, MAX_WD_LEN,
							 FALSE, FALSE, FALSE, FALSE); 

    // 3. If the doc is long enough to match, zap out the words corresponding to full word matches.

    if (0) printf("qex->qwd_cnt = %d, partial_cnt = %d, dwd_cnt = %d\n", qex->qwd_cnt, qex->partial_cnt, dwd_cnt);

    // The following conditional caused failures in the local search demo because the query words
    // inserted behind the scenes e.g. '[x$14 x$15] [y$20 y$21] l$fr' inflated the qex->qwd_cnt and caused
    // matching short documents to be rejected.
    //if (qex->qwd_cnt + qex->partial_cnt <= dwd_cnt)  {  // Can't match the partials if this doesn't hold

    // Zap out the query words from dwds.
    for (q = 0; q < qex->qwd_cnt; q++) {
      for (d = 0; d < dwd_cnt; d++) {
	if (!strcmp((char *)qex->qterms[q], (char *)dwds[d])) {
	  dwds[d][0] = 0;  // Zap out this query word
	  words_matched++;
	  if (d < min_matched_index) min_matched_index = d;
	  if (d > max_matched_index) max_matched_index = d;
	}
      }
    }


    // Now look for the partials
    for (q = 0; q < qex->partial_cnt; q++) {
      BOOL matched = FALSE;
      if (qoenv->debug >= 2)
	fprintf(qoenv->query_output, "possibly_record_candidate(): Trying to match partial '%s' against: \n",
		qex->partials[q]);
      for (d = 0; d < dwd_cnt; d++) {
	if (qoenv->debug >= 2 && dwds[d][0]) fprintf(qoenv->query_output, "              '%s'\n", dwds[d]);
	if (!strncmp((char *)qex->partials[q], (char *)dwds[d], strlen((char *)qex->partials[q]))) {
	  matched = TRUE;
	  if (qoenv->debug >= 2) fprintf(qoenv->query_output, "possibly_record_candidate(): Partial '%s' matched wd '%s'\n",
					 qex->partials[q], dwds[d]);
	  dwds[d][0] = 0;  // Zap out this query word
	  words_matched++;
	  if (d < min_matched_index) min_matched_index = d;
	  if (d > max_matched_index) max_matched_index = d;	  
	}
      }
      if (!matched) {
	all_partials_matched = FALSE;
	break;
      }
    }
    if (!all_partials_matched) {
      if (explain_rejection) {
	fprintf(qoenv->query_output, "possibly_record_candidate(): Rejection due to partial constraints: ");
	show_string_upto_nator(doc, '\n', 0);
      }
      return 0;  // 8 ---------------------------------------------------->
    }


    if (qoenv->debug >= 1) {
      printf("Checking span length: limit = %d, min=%d, max=%d, matched=%d, intervening=%d\n",
	     qoenv->x_max_span_length,  min_matched_index, max_matched_index, words_matched,
	     (max_matched_index - min_matched_index - words_matched + 1));
    }
    intervening_words = max_matched_index - min_matched_index - words_matched + 1;
    
    if (intervening_words > qoenv->x_max_span_length) {
      if (explain_rejection) {
	fprintf(qoenv->query_output, "possibly_record_candidate(): Rejection due to x_max_span_length: ");
	show_string_upto_nator(doc, '\n', 0);
      }
      return 0;  // 9 ----------------------->
    }
  }  // end of if (qex->partial_cnt)
  //  -------------- End of handling the matching of partial words section ----------


  if (qoenv->street_address_processing > 1 && qex->street_number > 0) {
    if (!check_street_number(doc, qoenv->street_specs_col, qex->street_number)) {
      if (explain_rejection)
	fprintf(qoenv->query_output,
		"possibly_record_candidate(): Rejection due to invaldi street number %d\n",
		qex->street_number);
      return 0; // 10 ------------------------>
    }
  }


  //  ---------------- Handling the matching and scoring of rank-only terms ---------------------------------
  // Finally, count the number of rank_only_terms which match this suggestion
  // Test for NULL protects against malloc() failure in process_query()
  if (qex->rank_only_cnt  && rank_only_counts != NULL) {
    int rs = 0;
    u_char *match = NULL;
    rank_only_count = 0;
    for (rs = 0; rs < qex->rank_only_cnt; rs++) {
      qex->op_count[COUNT_ROLY].count++;
      if ((match = (u_char *)strstr((char *)dc_copy, (char *)qex->rank_only[rs])) != NULL) {
	if (qoenv->debug >= 1) fprintf(qoenv->query_output, "Rank_only '%s' matched doc '%s'.  Slot %d\n",
				       qex->rank_only[rs], dc_copy, *recorded);
	//Check that the match starts on a word boundary (to avoid too many false matches.)
	if (match == dc_copy || match[-1] == ' ')
	  rank_only_count++;
      }
    }
    rank_only_counts[*recorded] = (byte)rank_only_count;
  }
  //  ---------------- End of handling the matching and scoring of rank-only terms section -------------------------



  if (qoenv->debug >= 1)
    fprintf(qoenv->query_output, "possibly_record_candidate(): recording %lld in candidates[%d].  RB to use = %d.\n",
	    candid8, *recorded, result_block_to_use);
  if (qoenv->rr_coeffs[5] > 0) {
    // Passing over information for BM25 scoring
    int k;
    u_char tfb = 0;
    for (k  = 0; k < qex->qwd_cnt; k++) {
      if (pl_blox[k].tf > 256) tfb = (u_char)256;
      else tfb = (u_char)pl_blox[k].tf;
      candidates[(*recorded)].tf[k] = tfb;
      candidates[(*recorded)].qidf[k] = pl_blox[k].qidf;    
    }
  }
  if (intervening_words > 255) intervening_words = 255;
  candidates[(*recorded)].intervening_words = (byte)intervening_words;  // zero in most cases
  candidates[(*recorded)++].doc = candid8;
  return 1;  // --------------------------------------->
}



// ---------------------------------------------------------------------------------------
// Functions for normalising, modifying, relaxing  a query
// ---------------------------------------------------------------------------------------

#if 0  // Not currently used but could be in future
static int strip_qbash_operators(u_char *str) {
  // In-place replacement of all QBASHQ operators with spaces.  Call before normalize_qtext()
  // Return the number of characters replaced
  u_char *r = str;
  int c = 0;
  if (str == NULL) return 0;
  while (*r) {
    if (*r == '"' || *r == '[' || *r == ']' || *r == '~' || *r == '/') {
      *r = ' ';
      c++;
    }
    r++;
  }
  return c;
}
#endif  // Not currently used but could be in future



static size_t trim_and_strip_all_ascii_punctuation_and_controls(u_char *str,
								int *query_words,
								int *max_wd_len_in_bytes) {
  // In-place replacement of all ascii punctuation and control chars with spaces, and removal
  // of excess spaces.  Also impose limits on the total number of bytes and the 
  // total number of words.
  // Return the length of the resulting string
  u_char *r = str, *w = str, last_written = 0, *word_start = str;
  size_t byte_limit = 240, wdlen, max_wdlen = 0; 
  int word_count = 0;
 
  *query_words = 0;
  if (str == NULL) return 0;
  if (0) printf("Stripping(%s) --> ", str);
  while (*r == ' ') r++;  // Skip leading spaces
  while (*r) {
    if (ispunct(*r) || *r <= ' ') {
      if (w > str && last_written != ' ') {  // Don't write spaces at start or after a space
	*w++ = ' ';
	wdlen = w - word_start;
	if (0) printf(" --- wdlen %zd\n", wdlen);
	if (wdlen > max_wdlen) max_wdlen = wdlen;
	word_start = w + 1;
	last_written = ' ';
	if (++word_count >= MAX_WDS_IN_QUERY) break;
      }
    }
    else {
      last_written = *r;
      *w++ = *r;
      if ((size_t)(w - str) >= byte_limit) {
	//Retreat to the previous space
	while (w > str && *w != ' ') w--;
	*w = 0;  // Either null out the last space or make an empty string
	break;
      }
    }
    r++;
  }
  *w = 0;
  if (last_written == ' ') {
    w--;  // Last char might be a space.  Trim it.
    *w = 0;
  }
  if (w > word_start) {
    wdlen = w - word_start;
    if (0) printf(" --- wdlen %zd\n", wdlen);
    if (wdlen > max_wdlen) max_wdlen = wdlen;
  }

  if (0) printf(" --> (%s)\n", str);
  *query_words = word_count;
  *max_wd_len_in_bytes = (int)max_wdlen;
  return (w - str);
}


#if 0  // Not currently used but could be in future

static void normalize_qtext(u_char *qstring, BOOL remove_trailing_spaces,
			    BOOL remove_possessives, int debug) {
  // Remove punctuation from inputs.First [optionally] remove possessive apostrophes, then
  // replace each punc u_character with a space.  Finally replace all multiple spaces
  // with a single one and drop leading or trailing spaces.
  //
  // Assumptions:
  //   - qstring is assumed to be null-terminated and lower cased.
  //
  // Copying is in place
  //
  // Complications: Partial words begin with a slash and [ ] " and ~ are used as query
  // operators.
  // 
  // Unfortunately, this makes a lot of ASCII assumptions.

  u_char *r, *w, *unbalanced_quote = NULL;
  int state = 0;   // 0 - normal scanning, initial state
	 
	
  BOOL in_brackets = FALSE, in_quotes = FALSE;

  if (debug >= 2) printf("  normalize_qtext(%s)\n", qstring);
  if (remove_possessives) {
    // 1. Remove possessives
    r = qstring + 1;
    w = r;
    while (*r) {
      if ((*r == '\'' || *r == 0x92) && isalpha(*(r - 1)) && *(r + 1) == 's') {
	r++;  // Skip this apostrophe
      }
      *w++ = *r++;
    }
    *w = 0;
    if (debug >= 3) printf("  normalize_qtext(%s) - possessives removed\n", qstring);
  }

  // 2. Replace all  punctuation characters with a space being careful not to
  //   muck up %, [, ], ~, and " operators
  r = qstring;
  while (*r) {
    if (ispunct(*r)) {
      if (*r == '[') {
	// Zap '[' if it's not at the start of string and not preceded by a non-word character or if already in disunction
	// or if it's the last character in the string or if the next character is ]
	if (in_brackets || ((r != qstring) && *(r - 1) != ' ' && *(r - 1) != '"') || *(r + 1) == ' '
	    || *(r + 1) == ']') *r = ' ';
	else in_brackets = TRUE;
      }
      else if (*r == ']') {
	// Only zap ']' if we're not in brackets
	if (in_brackets) {
	  in_brackets = FALSE;
	}
	else {
	  *r = ' ';
	}
      }
      else if (*r == '"') {
	if (in_quotes) {
	  in_quotes = FALSE;
	  unbalanced_quote = NULL;
	}
	else {
	  // Zap '"' if it's not at the start of string and not preceded by a space or a square bracket or a rank-only character
	  if ((r != qstring) && *(r - 1) != ' ' && *(r - 1) != '[' && *(r - 1) != RANK_ONLY_CHAR) *r = ' ';
	  else {
	    in_quotes = TRUE;
	    unbalanced_quote = r;
	  }
	}
      }
      else if (*r == PARTIAL_CHAR) {
	// partial char must be at the beginning of a word and not in a phrase or disjunction
	// and must be followed by a word character
	if (in_brackets || in_quotes || ((r != qstring) && *(r - 1) != ' ')
	    || !isalnum(*(r + 1))) *r = ' ';

      }
      else if (*r == RANK_ONLY_CHAR) {
	// rank_only char must be at the beginning of a word (or phrase) and not in a phrase or disjunction
	// and must be followed by a word character
	if (in_brackets || in_quotes || ((r != qstring) && *(r - 1) != ' ')
	    || (!isalnum(*(r + 1)) && *(r + 1) != '"')) *r = ' ';

      }
      else *r = ' ';
    }
    r++;
  }    // end of while loop

  if (unbalanced_quote != NULL) *unbalanced_quote = ' ';
  if (debug >= 3) printf("  normalize_qtext(%s) - token breakers spaced out\n", qstring);

  // 3. Remove all surplus spaces.  Except if 
  r = qstring;
  w = qstring;
  while (*r && *r == ' ') r++;
  state = 0;   // 0 - scanning non-spaces
  while (*r) {
    if (state == 0) {
      if (*r == ' ') state = 1;   // 1 - scanning spaces
      *w++ = *r++;
    }
    else {
      if (*r != ' ') {
	state = 0;
	*w++ = *r++;
      }
      else r++;  // Skip subsequent spaces.
    }
  }
  *w = 0;
  if (remove_trailing_spaces) {
    w--;
    if (*w == ' ') *w = 0;
  }
  if (debug >= 2) printf("  normalize_qtext(%s) - final\n", qstring);
}
#endif  // Not currently used but could be in future




static void normalize_delimiters(u_char *qstring, BOOL remove_trailing_spaces,
				 u_char *ascii_non_tokens, int debug) {
  // Reeduce all token delimiter sequences to a single space. First replace each non-space delimiter
  // with a space.  Then replace all multiple spaces
  // with a single one and drop leading or trailing spaces.
  //
  // Assumptions:
  //   - qstring is assumed to be null-terminated and lower cased.
  //
  // Copying is in place
  //
  // Complications: Partial words begin with a slash and [ ] " and ~ are used as query
  // operators.
  // 
  // Unfortunately, this makes a lot of ASCII assumptions.

  u_char *r, *w, *rp, *unbalanced_quote = NULL, *unclosed_bracket = NULL, *bafter;
  int state = 0;   // 0 - Initial state and normal scanning state
  // 1 - Within double quotes
  // 2 - Within square brackets
  // 3 - Within square brackets within double quotes
  // 4 - Within double quotes within square brackets.
  BOOL in_quotes;
	
  if (debug >= 2) printf("  normalize_delimiters(%s)\n", qstring);

  // Replace all  punctuation characters with a space being careful not to
  //   muck up %, [, ], ~, and " operators
  // Note that if line_prefixes are active '>' will not be in the token_break set.
  r = qstring;
  while (*r) {
    if (*r & 0x80) {
      if (utf8_ispunct(r, &bafter)) {
	// Space out the bytes of a UTF-8 punctuation character
	while (r < bafter) *r++ = ' ';
      }
      r = bafter - 1;  // Because of increment at foot of loop
    } else if (ascii_non_tokens[*r]) {
      if (*r == '[') {
	if (state == 0) {  // -- state 0 ---
	  if (((r != qstring) && *(r - 1) != ' ' && *(r - 1) != '"')
	      || *(r + 1) == 0
	      || *(r + 1) == ']') {
	    // Zap '[' if it's not at the start of string and not preceded by a space or ",
	    // OR if it's the last character in the string,
	    // OR if the next character is ].
	    *r = ' ';
	  } else {
	    state = 2;
	    unclosed_bracket = r;
	  }
	} else if (state == 1) {  // -- state 1 ---
	  state = 3;
	  unclosed_bracket = r;
	} else if (state > 1) {    // -- states 2, 3 ---
	  *r = ' ';  // Nesting of square brackets not allowed
	}
      } else if (*r == ']') {
	if (state == 0) {
	  *r = ' ';
	} else if (state == 1) {		
	  *r = ' ';
	} else if (state == 2) {
	  state = 0;
	  unclosed_bracket = NULL;
	} else if (state == 3) {
	  state = 1;
	  unclosed_bracket = NULL;
	} else if (state == 4) {
	  *r = ' ';
	}
      } else if (*r == '"') {
	if (state == 0) {
	  if (((r != qstring) && *(r - 1) != ' ' && *(r - 1) != '[' && *(r - 1) != '~')
	      || *(r + 1) == '"') {
	    // Zap '"' if it's not at the start of string and not preceded by a space or [ or rank-only,
	    // OR if the next character is ".
	    *r = ' ';
	  } else {
	    state =1;
	    unbalanced_quote = r;
	  }
	} else if (state == 1) {		
	  state = 0;
	  unbalanced_quote = NULL;
	} else if (state == 2) {
	  state = 4;
	  unbalanced_quote = r;
	} else if (state == 3) {
	  *r = ' ';
	} else if (state == 4) {
	  state = 2;
	  unbalanced_quote = NULL;
	}
      } else if (*r == PARTIAL_CHAR) {
	// partial char must be at the beginning of a word and not in a phrase or disjunction
	// and must be followed by a word character
	if (state > 0 || ((r != qstring) && *(r - 1) != ' ')
	    || ascii_non_tokens[*(r + 1)]) *r = ' ';
      } else if (*r == RANK_ONLY_CHAR) {
	// rank_only char must be at the beginning of a word (or phrase) and not in a phrase or disjunction
	// and must be followed by a word character
	if (state > 0 || ((r != qstring) && *(r - 1) != ' ')
	    || (ascii_non_tokens[*(r + 1)] && *(r + 1) != '"')) {
	  *r = ' ';
	}
      } else {  
	*r = ' ';
      }
    }
    r++;
  }    // end of while loop

  if (unbalanced_quote != NULL) *unbalanced_quote = ' ';
  if (unclosed_bracket != NULL) *unclosed_bracket = ' ';
  if (debug >= 3) printf("  normalize_delimiters(%s) - token breakers spaced out\n", qstring);

  // Remove empty [] or ""
  in_quotes = FALSE;
  r = qstring;
  w = qstring;
  while (*r) {
    if (*r == '"') {
      if (in_quotes) {
	*w++ = *r++;
	in_quotes = FALSE;
      }
      else {
	rp = r + 1;
	while (*rp == ' ') rp++;
	if (*rp == '"') r = rp + 1;  // Skip over both quotes and all the spaces in between them.
	else {
	  in_quotes = TRUE;
	  *w++ = *r++;
	}
      }
    }
    else if (*r == '[') {
      rp = r + 1;
      while (*rp == ' ') rp++;
      if (*rp == ']') r = rp + 1;  // Skip over both brackets and all the spaces in between them.
      else {
	*w++ = *r++;
      }
    }
    else *w++ = *r++;
  }
  *w = 0;


  // Remove all surplus spaces. 
  r = qstring;
  w = qstring;
  while (*r && *r == ' ') r++;
  state = 0;   // 0 - scanning non-spaces
  while (*r) {
    if (state == 0) {
      if (*r == ' ') state = 1;   // 1 - scanning spaces
      *w++ = *r++;
    }
    else {
      if (*r != ' ') {
	state = 0;
	*w++ = *r++;
      }
      else r++;  // Skip subsequent spaces.
    }
  }
  *w = 0;
  if (remove_trailing_spaces) {
    w--;
    if (*w == ' ') *w = 0;
  }


  if (debug >= 2) printf("  normalize_delimiters(%s) - final\n", qstring);
}




static void test_normalize_delimiters(u_char *ascii_non_tokens) {
  char test_buf[1000];
  strcpy(test_buf, "\"[dog cat \"rat [vole \"\"\"[[[[]]]\"]]]\"");
  normalize_delimiters((u_char *)test_buf, TRUE, ascii_non_tokens, FALSE);
  if (strcmp(test_buf, "\"[dog cat rat vole ] \"")
      && strcmp(test_buf, "\"[dog cat rat vole]\"")) {
    printf("Error: test of normalize_delimiters(\"[dog cat rat vole]\") failed.  '%s'\n", test_buf);
    exit(1);  // This function only called in heavy debug mode.
  }
  strcpy(test_buf, "[\"dog cat \" rat [vole \"\"\"[[[[]]]\"]]]\"");
  normalize_delimiters((u_char *)test_buf, TRUE, ascii_non_tokens, FALSE);
  if (strcmp(test_buf, "[\"dog cat \" rat vole ]")) {
    printf("Error: test of normalize_delimiters([\"dog cat \" rat vole]) failed.  '%s'\n", test_buf);
    exit(1);  // This function only called in heavy debug mode.
  }

}



static void prefix_last_word_with_slash(u_char *qstring, int debug) {
  // If null-terminated qstring contains more than one word, prefix the 
  // last word with a slash to indicate partial.  Note this is done in place.  No malloc()
  u_char *p = qstring, *last_space = NULL;
  size_t l;
  while (*p) {
    if (*p == ' ') last_space = p;
    p++;
  }
  l = p - qstring;
  if (l > (MAX_QLINE - 2)) return;  // Can't do it or buffer would overflow
  if (last_space == NULL) return;  // Only one word

  // Now that we're aiming to support dijunctions and phrases, two additional tests
  // on the last character are needed:
  if (*(p - 1) == ']' || *(p - 1) == '"') {
    return;    // ------------------------------------------->
  }

  // What if qstring ends with a space?  We don't want to do the prefixing
  if (last_space == (p - 1)) {
    // Rub out the trailing spaces and return;
    p--;
    while (p > qstring && *p == ' ') {
      *p = 0;
      p--;
    }
    if (debug >= 2)printf("  prefix_last_word_with_slash(%s) - there was originally a trailing space.\n", qstring);
    return;   // ------------------------------------------->
  }
  *(p + 1) = 0;   // String is now one char longer
  p--;
  // Shift the last word one place right
  while (p > last_space) {
    *(p + 1) = *p;
    p--;
  }
  *(p + 1) = '/';
  if (debug >= 2)printf("  prefix_last_word_with_slash(%s)\n", qstring);
}



static void perhaps_prefix_line_with_rab(u_char *qstring, int debug) {
  // If null-terminated qstring contains no space, prefix qstring with '>'.
  // Note that this is done in place.  No malloc()
  u_char *p = qstring;
  size_t l;

  if (debug) printf("perhaps_prefix_line_with_rab(%s)\n", qstring);
  while (*p) {
    if (*p == ' '  || *p == '['  || *p == ']'
	|| *p == '"'  || *p == '/') return;  // Do nothing if query contains any of these.
    p++;
  }
  l = p - qstring;
  if (l > (MAX_QLINE - 2)) return;  // Can't do it or buffer would overflow

  // Move every byte one space to the right
  while (p >= qstring) {
    *(p + 1) = *p;  // This includes the trailing null
    p--;
  }
  *qstring = '>';
  if (debug) printf("perhaps_prefix(%s)\n", qstring);
  return;
}



static int process_query_text(query_processing_environment_t *qoenv, book_keeping_for_one_query_t *qex) {
  // We only need to process the query text (e.g. splitting it into words) once per query
  //
  // Returns the number of query words or a negative error code.
  u_char *q, *term_start, saveq;
  size_t len;
  BOOL explain = (qoenv->debug >= 1);

  // Make a lower cased copy of the query text.
  if (0) printf("Before lowering: '%s'\n", qex->query);
  len = utf8_lowering_ncopy(qex->qcopy, qex->query, MAX_QLINE);  // This function avoids a potential problem
                                                            // when dc_copy ends with an incomplete UTF-8
                                                            // sequence.  
  if (qoenv->conflate_accents) utf8_remove_accents(qex->qcopy);
  qex->qcopy[MAX_QLINE] = 0;
  if (0) printf("After lowering: '%s'\n", qex->qcopy);
  q = qex->qcopy;
  qex->qwd_cnt = 0;
  qex->q_max_mat_len = 0;

  if (qoenv->debug >= 1) fprintf(qoenv->query_output, "process_query_text(%s)\n", qex->query);

  while (*q == ' ') q++;   // Skip leading spaces

  if (qoenv->street_address_processing >= 1) {
    // Do this before the substitution rules:  Remove stuff like 'unit 10' or 'suite 4', then
    // remove and record the street number
    qex->street_number = process_street_address(q, TRUE);  // Do it in-place - query can't grow.
    if (qoenv->display_parsed_query)
      printf("Query after street address processing is {%s}; Original query was {%s}\n",
	     q, qex->query);
  }


  if (qoenv->auto_line_prefix) {
    // Must do this before slash prefixing because prefix_last_word_with_slash()
    // may remove trailing spaces.
    perhaps_prefix_line_with_rab(q, qoenv->debug);
  }

  if (qoenv->auto_partials) {
    prefix_last_word_with_slash(q, qoenv->debug);  // Prefix the last word with a slash if
                                                   // there are more than one.
  }

  if (qoenv->classifier_mode) {
    int yes = 0;

    // Remember, the query referenced by q has been lower-cased, has had non-operator punctuation removed,
    // has had leading and trailing blanks removed, and is null-terminated.

    if (qoenv->segment_rules_hash != NULL) {
      if (explain) printf("Applying segment rules\n");
      yes = apply_substitutions_rules_to_string(qoenv->segment_rules_hash, qoenv->language,
						q, TRUE, TRUE, qoenv->debug);
    }
    
    if (yes) {
      qex->vertical_intent_signaled = TRUE;
      qex->segment_intent_multiplier = qoenv->segment_intent_multiplier;
      if (qoenv->debug >= 1)
	printf("Segment intent signaled.  Modified query is {%s}; Original query was {%s}. s_i_m = %.4f\n",
	       q, qex->query, qex->segment_intent_multiplier);
    }
    else if (qoenv->debug >= 1) printf("No explicit vertical intent signal.\n");

    if (qoenv->display_parsed_query) {
      // [Developer2] What I really wanted is to get the final query with punctuation removed -
      // but I guess you don't keep that around?!
      // (Instead the parsed query is placed directly into a structured query.)
      // Also this version isn't useful unless only a single thread is run in parallel (-query_streams=1)
      printf("Query after application of classifier rules is {%s}; Original query was {%s}\n", q, qex->query);
    }
  }

    if (qoenv->use_substitutions) {
    // Applying substitutions to the query but not if it already contains operators.  Stop if
    // a substitution rule has introduced an operator.
       if (explain) printf("Applying general substitution rules\n");
       apply_substitutions_rules_to_string(qoenv->substitutions_hash, qoenv->language,
					   q, TRUE, FALSE, qoenv->debug);
       if (qoenv->display_parsed_query)
	 printf("Query after application of %s substitutions is {%s}; Original query was {%s}\n",
		qoenv->language, q, qex->query);
  }

  normalize_delimiters(q,  // query string
		       (qoenv->classifier_mode || (!qoenv->auto_partials && !qoenv->auto_line_prefix)),
		       ascii_non_tokens, qoenv->debug);
  if (qoenv->display_parsed_query) printf("Query after normalize_delimiters is {%s}; Original query was {%s}.\n",
					  q, qex->query);


  strncpy((char *)qex->query_as_processed, (char *)q, MAX_QLINE);
  qex->query_as_processed[MAX_QLINE] = 0;
  if (qoenv->debug >= 2)
    fprintf(qoenv->query_output, "Process_query_text(): query_as_processed = '%s'\n",
	    qex->query_as_processed);

  // A: Break up qtext into an array of query terms, an array of query partial words, and an
  //    array of rank-only words (which might one day be regexes)
  //    Partial words start with a '/'  
  //	  Rank_only words start with a '~'
  //    Leading spaces have already been stripped.
  term_start = q;
  int max_wds_matching_this_term = 0, wip = 0;

  
  while (*q) {  // --- Loop over all the words in this query.
    
    if (*q == '[') {          // --- We have a term which is a top level disjunction ----
      // The maximum number of words matched by this disjunction is either one or the length
      // of the longest internal phrase.
      qex->query_contains_operators = TRUE;
      max_wds_matching_this_term = 1;
      while (*q && *q != ']') {
	if (*q == '"') {
	  while (*q && *q != '"') {  // Phrase within disjunction
	    if (*q == ' ') wip++;
	    q++;
	  }
	  if (wip > max_wds_matching_this_term) max_wds_matching_this_term = wip;
	}
	q++;
      }
      if (*q) q++;
    }
    else if (*q == '"') {    // --- We have a term which is a top level phrase ----
      // The maximum number of words matched by this phrase is the number of words plus the number of disjunctions.
      qex->query_contains_operators = TRUE;
      max_wds_matching_this_term = 1;
      q++;
      while (*q && *q != '"') {
	if (*q == '[') {
	  while (*q && *q != ']') q++;  // Disjunction within phrase
	  max_wds_matching_this_term++;
	}
	else if (*q == ' ') max_wds_matching_this_term++;
	q++;
      }
      if (*q) q++;
    }
    else if (*q == RANK_ONLY_CHAR) {  // --- We have a term which is a rank-only word or phrase ----
      // A rank-only expression can include a simple phrase
      // Don't include rank-only terms in  max_wds_matching_this_term 
      max_wds_matching_this_term = 0;

      q++;
      if (*q == '"') {
	q++;
	while (*q && *q != '"') q++;
      }
      else {
	while (*q && *q != ' ') q++;
      }
    }
    else {// --- We have a term which is a simple word or possibly a partial ----
      max_wds_matching_this_term = 1;
      while (*q && *q != ' ') q++;
    }
    // Found the end of the term starting at term_start

    if (0) printf("Found term starting at %s\n", term_start);
    saveq = *q;
    *q = 0;  // null terminate
    len = q - term_start;
    if (len > 0) {
      if (*term_start == PARTIAL_CHAR) {         // --- partial ----
	qex->partials[qex->partial_cnt++] = term_start + 1;  // Ignore the PARTIAL_CHAR
	if (qoenv->debug >= 1) fprintf(qoenv->query_output, "Found prefix term '%s' in query\n", term_start + 1);
	if (qex->partial_cnt == MAX_WDS_IN_QUERY) break;
      }
      else if (*term_start == RANK_ONLY_CHAR) {  // --- rank-only ----
	if (term_start[1] == '"') {
	  u_char *q;
	  qex->rank_only[qex->rank_only_cnt++] = term_start + 2;  // Ignore the RANK_ONLY_CHAR
	  // and the quote
	  q = term_start + 2;
	  //while (*q && *q != '"') q++;
	  //if (*q) *q = 0;   // Closing quote is already zapped.
	  if (qoenv->debug >= 1) fprintf(qoenv->query_output, "Rank-only phrase is '%s'\n", q);
	}
	else {
	  qex->rank_only[qex->rank_only_cnt++] = term_start + 1;  // Ignore the RANK_ONLY_CHAR
	}
	if (qoenv->debug >= 1) fprintf(qoenv->query_output, "Found rank-only term '%s' in query\n", qex->rank_only[qex->rank_only_cnt - 1]);
	if (qex->rank_only_cnt == MAX_WDS_IN_QUERY) break;
      } else {   // --- other term ----
	if (qoenv->debug >= 2) printf("qex->qterms[%d] = %s\n", qex->qwd_cnt, term_start);
	qex->qterms[qex->qwd_cnt++] = term_start;
	qex->q_max_mat_len += max_wds_matching_this_term;
	if (qex->qwd_cnt == MAX_WDS_IN_QUERY) break;
      }
    }
    if (saveq) q++;
    term_start = q;
  }  // End of while (*q).


  if (qoenv->debug >= 1)
    fprintf(qoenv->query_output, "     Query terms: %d, Partials: %d\n",
	    qex->qwd_cnt, qex->partial_cnt);

  
  // B. We now have an array of qwd_cnt query words.
  if (qex->qwd_cnt == 0) {
    if (qoenv->debug >= 1)  printf("  ***** Empty query\n");
    return(0);  // ------------------------------------------------>
  }
  return(qex->qwd_cnt);
}



static int process_query(query_processing_environment_t *qoenv, book_keeping_for_one_query_t *qex,
			 byte *doctable, byte *vocab, byte *index, byte *forward, size_t vsz,
			 size_t fsz, double score_multiplier) {
  //  -------- This is called once per query variant (in a multi_query_string) --------------------
  // Processes the query so far typed by a user (represented by qtext)
  //  - breaks the query into an array of words (assuming whitespace separation)
  //  - calls saat_setup() to setup the data structures to control saat 
  //    (Query-document At A Time) processing.
  //  - calls saat_relaxed_and to generate a filtered list of up to 
  //    max_candidates_to_consider candidates
  //  - calls rerank_and_record to record up to max_to_show ranked suggestions.
  //  
  //  Returns zero on success and a negative error cqde (see error_explanations.cpp) otherwise.

  int terms_not_present = 0, error_code = 0;
  double penalty_multiplier_for_partial_matches = 0.1;
  saat_control_t *plists;

  if (qex->qwd_cnt == 0) return(-41);   // ----------------------------------------------->

  qex->max_length_diff = qoenv->max_length_diff;  // For each query set it back to what the user specified
  if (qex->max_length_diff >= 100 && qex->max_length_diff < 1000) {
    int length_cutoff = qex->max_length_diff / 100;   // No length limit applies to queries longer than this.
    int addon = qex->max_length_diff % 100;
    // Dynamic setting of maximum length difference based on the length of query.  The basic formula is l**2 / (l + 2)
    //	 Length					1   2   3   4   5   6   7   8   9   10
    //	 Max_length_diff	    0   1   1   2   3   4   5   6   7   8 
    //  
    // To this is added:  the relaxation level and the addon value.
    if (qex->qwd_cnt > length_cutoff) qex->max_length_diff = 1000;
    else qex->max_length_diff = (qex->qwd_cnt * qex->qwd_cnt) / (qex->qwd_cnt + 2) + qoenv->relaxation_level + addon;
    if (qoenv->debug >= 1) printf("Max_length_diff set to %d.  It was %d\n", qex->max_length_diff, qoenv->max_length_diff);
  }

  if (0) printf("Max_length_diff = %d\n", qex->max_length_diff);


  // Possibly reduce the number of terms used in candidate generation

  create_candidate_generation_query(qoenv, qex);
  if (qoenv->display_parsed_query) {
    u_char shortening_code[10] = {0};
    int pos = 0;
    if ((qex->shortening_codes & SHORTEN_NOEXIST)) shortening_code[pos++] = 'X';
    if ((qex->shortening_codes & SHORTEN_REPEATED)) shortening_code[pos++] = 'R';
    if ((qex->shortening_codes & SHORTEN_ALL_DIGITS)) shortening_code[pos++] = '9';
    if ((qex->shortening_codes & SHORTEN_HIGH_FREQ)) shortening_code[pos++] = 'H';

    printf("Query used for candidate generation is {%s}; Original query was {%s}. Shortening code: {%s}\n",
	   qex->candidate_generation_query, qex->query, shortening_code);
  }
  

  
  plists = saat_setup(qoenv, qex, &terms_not_present, &error_code);

  if (error_code < 0) {
    // An error return from saat_setup()
    return(error_code);   // ------------------------------------------------>
  }
  if (qoenv->debug >= 1 && terms_not_present) {
    fprintf(qoenv->query_output, "%d word(s) in this %d-word query never occurred.\n",
				   terms_not_present, qex->qwd_cnt);
  }

  
  if (terms_not_present <= qoenv->relaxation_level) {
    qex->q_signature = calculate_signature_from_first_letters_of_partials(qex->partials,
									  qex->partial_cnt,
									  DTE_BLOOM_BITS,
									  &error_code);
    if (error_code < -200000) return(error_code);   // -------------------------------------------------->
    if (qoenv->debug >= 2)
      fprintf(qoenv->query_output, "Query signature = %llx. (bits = %d)\n",
	      qex->q_signature, DTE_BLOOM_BITS);

    // NOTE: The following calls saat_relaxed_and() in all cases.  This makes sense for code simplicity
    //       and because the old saat_and() achieved only half the throughput because its algorithms
    //       for choosing candidates and advancing had not been optimized in the way the relaxed
    //       version have been.
    saat_relaxed_and(qoenv->query_output, qoenv, qex, plists, forward,
					     index, doctable, fsz, &error_code);
    if (error_code < -200000) return(error_code);

    if (qoenv->report_match_counts_only) {
      // Special behaviour triggered by max_to_show == 0
      free_querytree_memory(&plists, qex->tl_saat_blocks_allocated);
      return 0;   // ---------------------------------------------------------->
    }

    if (qoenv->classifier_mode > 0) {
      // ---- we're classifying ----
      classifier(qoenv, qex, forward, doctable, fsz, score_multiplier);
      // The results of classifier() are returned in the following elements of qex:
      //  docnum_t *tl_docids;    - The docid of each result
      //  u_char **tl_suggestions; - Copies of the relevant document text in malloced memory
      //  double *tl_scores;    - Scores associated with the each result
      //  int tl_returned;  - A count of the number or results returned.

    } else {
      // ---- normal operations ----
	
      rerank_and_record(qoenv, qex, forward, doctable, fsz, score_multiplier,
			penalty_multiplier_for_partial_matches);
      // The results of rerank_and_record() are returned in the following elements of qex:
      //  docnum_t *tl_docids;    - The docid of each result
      //  u_char **tl_suggestions; - Copies of the relevant document text in malloced memory in descending score order
      //  double *tl_scores;  - Scores associated with the each result
      //  int tl_returned;    - A count of the number of results returned.

    }

    if (qoenv->debug >= 1) printf("process_query() --> tl_returned = %d\n", qex->tl_returned);

  }


  free_querytree_memory(&plists, qex->tl_saat_blocks_allocated);
  if (qoenv->debug >= 1) {
    fprintf(qoenv->query_output, "  Everything is beautiful, in it's own way ...\n");
  }
  return(error_code);   // ------------------------------------------------>
}


static void analyze_response_times(query_processing_environment_t *qoenv) {
  // Analyse the response times accumulated in histo and report
  // median, 90, 95 and 99 th percentiles
  int h, median = -1, rt90 = -1, rt95 = -1, rt99 = -1, rt999 = -1;
  double total = (double)qoenv->queries_run, cumulator = 0.0;
  for (h = 0; h < ELAPSED_MSEC_BUCKETS; h++) {
    cumulator += qoenv->elapsed_msec_histo[h];
    if (cumulator >= 0.5 * total) {
      if (median < 0) median = h;  // -1 means undefined
      if (cumulator >= 0.9 * total) {
	if (rt90 < 0) rt90 = h;  // -1 means undefined
	if (cumulator >= 0.95 * total) {
	  if (rt95 < 0) rt95 = h;  // -1 means undefined
	  if (cumulator >= 0.99 * total) {
	    if (rt99 < 0) rt99 = h;  // -1 means undefined
	    if (cumulator >= 0.999 * total) {
	      if (rt999 < 0) rt999 = h;  // -1 means undefined
	    }
	  }
	}
      }
    }
  }
  fprintf(qoenv->query_output, "\nElapsed time percentiles:\n   50th - %3d\n   90th - %3d\n   95th - %3d\n   99th - %3d\n"
	  " 99.9th - %3d\n", median, rt90, rt95, rt99, rt999);

  if (rt999 >= (ELAPSED_MSEC_BUCKETS - 1)) printf("Note: %d implies %d or greater.\n\n", ELAPSED_MSEC_BUCKETS - 1, ELAPSED_MSEC_BUCKETS - 1);
}


QBASHQ_API int test_sb_macros() {
  // Internal test of the macros used in handling Skip Blocks
  unsigned long long a, b, c, x, a2, b2, c2;
  a = 0x1FFFFFFFFF;  // docnum
  b = 0xFFF;		 // no. postings in run
  c = 0x7FFF;      // length of run in bytes
  x = sb_assemble(a, b, c);
  a2 = sb_get_lastdocnum(x);
  b2 = sb_get_count(x);
  c2 = sb_get_length(x);

  if (0) printf("sb_test_macros:  %llX, %llX, %llX\n", a2, b2, c2);

  if (a != a2) return(-200020);  // -------------------------------------->
  if (b != b2) return(-200021);  // -------------------------------------->
  if (c != c2) return(-200022);  // -------------------------------------->
  return(0);
}





static int one_test_of_isprefixmatch(char *q, char *d, BOOL expected) {
  int verbose = 0;
  BOOL r = isprefixmatch((u_char *)q, (u_char *)d);
  if ((r && !expected) || (!r && expected)) {
    if (expected) {
      if (verbose) printf("TEST FAILED: isprefixmatch(%s,%s) should have returned TRUE\n", q, d);
    }
    else {
      if (verbose) printf("TEST FAILED: isprefixmatch(%s,%s) should have returned FALSE\n", q, d);
    }
    return 1;
  }
  return 0;
}


QBASHQ_API int test_isprefixmatch() {
  int errs = 0, verbose = 0;
  errs += one_test_of_isprefixmatch("australian", "australian government attorney-general's department", TRUE);
  errs += one_test_of_isprefixmatch("australian government", "australian government attorney-general's department", TRUE);
  errs += one_test_of_isprefixmatch(" australian", "australian government attorney-general's department", TRUE);
  errs += one_test_of_isprefixmatch("australian /g", "australian government attorney-general's department", TRUE);
  errs += one_test_of_isprefixmatch("/a government /a", "australian government attorney-general's department", TRUE);
  errs += one_test_of_isprefixmatch("  /a government /a", "australian government attorney-general's department", TRUE);
  errs += one_test_of_isprefixmatch("/aust government /attorney-g", "australian government attorney-general's department", TRUE);
  errs += one_test_of_isprefixmatch("australian department", "australian government attorney-general's department", FALSE);
  errs += one_test_of_isprefixmatch("government", "australian government attorney-general's department", FALSE);
  errs += one_test_of_isprefixmatch("australian /depart", "australian government attorney-general's department", FALSE);
  errs += one_test_of_isprefixmatch("australian /department", "australian government attorney-general's department", FALSE);
  errs += one_test_of_isprefixmatch("australian /gov /department", "australian government attorney-general's department", FALSE);

  if (errs) {
    // The test is silent unless errors are encountered.
    if (verbose) printf("test_isprefixmatch() - %d errors encountered.\n", errs);
    return(-200024);  // ---------------------------------------->
  }
  return(0);
}

static u_char *get_value_from_header_line(u_char *linestart, u_char *attribute_sought, u_char **next_line){
  // linestart is a string null-terminated string which is expected to start with a substring represented by attribute_sought,
  // such as "format=" or "age:".   If it doesn't start with that, we return NULL.
  // otherwise we start at the character immediately following attribute_sought and scan forward to the
  // first NULL or control-char (START).  Then we scan backward to the first non-blank character (END)
  // and make a copy of the substring (START, END) in malloced memory. This is returned as the 
  // result of the function and must be freed by the caller.
  size_t l;
  u_char *vstart, *p, *rslt;
  if (0) printf("GVFHL(%s)\n", linestart);
  if (0) printf(" -- checking for '%s'\n", attribute_sought);
  l = strlen((char *)attribute_sought);
  if (strncmp((char *)linestart, (char *)attribute_sought, l)) return NULL;
  vstart = linestart + l;
  while (*vstart == ' ') vstart++; // skip spaces
  p = vstart;
  while (*p >= ' ') p++;  // Stop at the first NULL or control
  l = p - vstart;
  rslt = (u_char *)malloc(l + 1);
  if (rslt == NULL) return NULL;
  strncpy((char *)rslt, (char *)vstart, l);
  rslt[l] = 0;
  *next_line = p + 1;
  return rslt;
}



static u_char *check_if_header(index_environment_t *ixenv, query_processing_environment_t *qoenv,
			       u_char **other_token_breakers, u_char *index_label, int *error_code) {
  // Check the header of the .if file and return the QBASHER version string (or NULL).  Caller's 
  // responsibility to free.
  u_char *if_in_memory, *line, *value, *version;
  size_t sighs;
  long long *llp;
  double index_format_d, tot_postings = UNDEFINED_DOUBLE;
  int verbose = 0;

  if_in_memory = ixenv->index;
  line = if_in_memory;
  // First the index_format line
  value = get_value_from_header_line(line, (u_char *)"Index_format:", &line);

  if (value == NULL) {
    if (verbose) printf("\nWarning: Header line format error - line 1, in index %s\n", index_label);
    *error_code = -200025;
    // Caller will need to free the mapping(s)
    return NULL;  // ---------------------------------------------->
  }
  else {
    // The value is something like "QBASHER x.y"
    // skip forward to a space
    u_char *p;
    p = value;
    while (*p && *p != ' ') p++;
    index_format_d = strtod((char *)p, NULL);

    if (strcmp((char *)value, INDEX_FORMAT)) {
      if (verbose) printf("\nWarning: %s indexes are not in current (%s) format; They are in (%s).\n", index_label, INDEX_FORMAT, value);
      *error_code = -26;

      //if (index_format_d < 1.1) {   // No backward compatibility anymore unfortunately
      *error_code = -200027;
      free(value);
      return NULL;  // ---------------------------------------->
      //}
    }

    ixenv->index_format_d = index_format_d;
    free(value);

    // version will be returned as the result of this function.
    version = get_value_from_header_line(line, (u_char *)"QBASHER version:", &line);
    if (version == NULL) {
      if (verbose) printf("\nWarning: Header line format error - line 2, in index %s\n", index_label);
      *error_code = -200025;
      // Caller will need to free the mapping(s)
      return NULL;  // ---------------------------------------------->
    }
    // Then the Query_meta_chars line
    value = get_value_from_header_line(line, (u_char *)"Query_meta_chars:", &line);
    if (value == NULL) {
      if (verbose) printf("\nWarning: Header line format error - line 2, in index %s\n", index_label);
      *error_code = -200028;
      return NULL;  // ---------------------------------------->
    }
    else {
      if (strcmp((char *)value, QBASH_META_CHARS)) {
	if (verbose) printf("\nWarning: %s indexes were built assuming a different set of QBASH metachars. [%s] v. [%s]\n",
			    index_label, value, QBASH_META_CHARS);
	free(value);
	*error_code = -200029;
	return NULL;  // ---------------------------------------->
      }
      free(value);
    }

    // Then the Other_token_breakers: line
    value = get_value_from_header_line(line, (u_char *)"Other_token_breakers:", &line);
    if (value == NULL) {
      if (verbose) printf("\nWarning: Header line format error - line 3, in index %s\n", index_label);
      *error_code = -200030;
      return NULL;  // ---------------------------------------->
    }
    else {
      *other_token_breakers = value;
    }

    // Now look at the file sizes.  This info only started being recorded from format 1.2 on.
    // Skip the checks for earlier index versions.
    if (index_format_d >= 1.2) {
      value = get_value_from_header_line(line, (u_char *)"Size of .forward:", &line);
      if (value != NULL) sighs = (size_t)strtoll((char *)value, NULL, 10);
      else sighs = -1;
      if (sighs != ixenv->fsz) {
	if (verbose) printf("\nError: Length of .forward file (%zu) is not what it was during indexing (%zu), in index %s\n",
				 sighs, ixenv->fsz, index_label);
	*error_code = -200031;
	return NULL;  // ---------------------------------------------->
      }
      free(value);
      value = get_value_from_header_line(line, (u_char *)"Size of .dt:", &line);
      if (value != NULL) sighs = (size_t)strtoll((char *)value, NULL, 10);
      else sighs = -1;
      if (sighs != ixenv->dsz) {
	if (verbose) printf("\nError: Length of .dt file (%zu) is not what it was during indexing (%zu), in index %s\n",
			    sighs, ixenv->dsz, index_label);
	*error_code = -200032;
	return NULL;  // ---------------------------------------------->
      }
      free(value);
      value = get_value_from_header_line(line, (u_char *)"Size of .vocab:", &line);
      if (value != NULL) sighs = (size_t)strtoll((char *)value, NULL, 10);
      else sighs = -1;
      if (sighs != ixenv->vsz) {
	if (0) fprintf(stderr, "\nError: Length of .vocab file (%zu) is not what it was during indexing (%zu), in index %s\n",
		       sighs, ixenv->vsz, index_label);
	*error_code = -200033;
	return NULL;  // ---------------------------------------------->
      }
      free(value);

      // Extract "Total postings" and "Number of documents" fields if present (added in 1.5.75)
      // and from them set N and avdoclen fields in qoenv.
      value = get_value_from_header_line(line, (u_char *)"Total postings:", &line);
      if (value != NULL) tot_postings = (double)strtoll((char *)value, NULL, 10);
      free(value);
      value = get_value_from_header_line(line, (u_char *)"Number of documents:", &line);
      if (value != NULL) {
	qoenv->N = (double)strtoll((char *)value, NULL, 10);
	qoenv->avdoclen = tot_postings / qoenv->N;
      }
      free(value);

      line = (u_char *)strstr((char *)line, "expect_cp1252=");
      if (line != NULL) {
	value = line + 14;
	if (verbose) printf("expect_cp1252: value = %s\n", value);
	if (strncmp((char *)value, "TRUE", 4)) {
	  ixenv->expect_cp1252 = FALSE;
	  if (verbose) printf("Set expect_cp1252 to FALSE\n");
	}
	else if (verbose) printf("Left expect_cp1252 at TRUE\n");
      }




      // Now check the length of this .if file.  The size is in binary at the end of the file
      llp = (long long *)(if_in_memory + ixenv->isz - sizeof(long long));
      if (*llp != ixenv->isz) {
	if (verbose) printf("\nError: Length of .if file (%zu) is not what it was during indexing (%zu), in index %s\n",
			    sighs, ixenv->isz, index_label);
	*error_code = -200034;
	return NULL;  // ---------------------------------------------->
      }
    }
  }
  return(version);
}

static u_char *open_and_check_index_set(query_processing_environment_t *qoenv,
					index_environment_t *ixenv,
					u_char *index_stem, size_t stemlen,
					BOOL verbose, BOOL run_tests,
					int *error_code) {
  // This version of the function is used in Case 1, where an index_dir is specified. index_stem
  // comprises <index_dir>/QBASH, and has room to append up to 29 characters.
  // Open all four QBASH index files and read them into memory.  Return pointers to the
  // memory blocks and the sizes.
  // Stem is usually "QBASH".
  u_char *fname = index_stem, *suffix, *other_token_breakers = NULL, *version, unknown[] = "<unknown>";

  suffix = index_stem + stemlen;

  strcpy((char *)suffix, ".forward");
  ixenv->forward = (byte *)mmap_all_of(fname, &(ixenv->fsz), verbose, &(ixenv->forward_H),
				       &(ixenv->forward_MH), error_code);
  if (*error_code < 0) return NULL;  // -------------------------------->
  strcpy((char *)suffix, ".if");
  ixenv->index = (byte *)mmap_all_of(fname, &(ixenv->isz), verbose, &(ixenv->index_H),
				     &(ixenv->index_MH), error_code);
  if (*error_code < 0) return NULL;  // -------------------------------->
  strcpy((char *)suffix, ".vocab");
  ixenv->vocab = (byte *)mmap_all_of(fname, &(ixenv->vsz), verbose, &(ixenv->vocab_H),
				     &(ixenv->vocab_MH), error_code);
  if (*error_code < 0) return NULL;  // -------------------------------->
  strcpy((char *)suffix, ".doctable");
  ixenv->doctable = (byte *)mmap_all_of(fname, &ixenv->dsz, verbose, &ixenv->doctable_H,
					&(ixenv->doctable_MH), error_code);
  if (*error_code < 0) return NULL;  // -------------------------------->

  if (qoenv->use_substitutions) {
    strcpy((char *)suffix, ".substitution_rules");
    load_substitution_rules(fname, &qoenv->substitutions_hash, qoenv->debug);
  }

  if (qoenv->classifier_mode != 0) {
    strcpy((char *)suffix, ".segment_rules");
    if (0) printf("Attempting to load segment rules from index_dir\n");
    load_substitution_rules(fname, &(qoenv->segment_rules_hash), qoenv->debug);
  }

  version = check_if_header(ixenv, qoenv, &other_token_breakers, index_stem, error_code);
 
  // Now setup ascii_non_tokens (defined and zeroed in unicode.c)
  initialize_ascii_non_tokens((u_char *)QBASH_META_CHARS, FALSE);
  initialize_ascii_non_tokens((u_char *)other_token_breakers, TRUE);  // Always additive

  if (version == NULL) version = unknown;
  if (*error_code < 0) return NULL;  // -------------------------------->

  if (verbose  || qoenv->debug >= 1) {
    display_ascii_non_tokens();
    test_normalize_delimiters(ascii_non_tokens);
    fprintf(qoenv->query_output, "Case 1: indexes loaded from %s.  Index written by %s being read by %s%s\n",
	    index_stem, version, INDEX_FORMAT, QBASHER_VERSION);
  }
  if (run_tests) {
    *error_code = test_doctable_n_forward(ixenv->doctable, ixenv->forward,
					  ixenv->dsz, ixenv->fsz);
    if (*error_code < 0) return NULL;  // -------------------------------->
    *error_code = test_postings_list((u_char *)"goteborgsposten", ixenv->doctable, ixenv->index, ixenv->forward, ixenv->fsz,
				     ixenv->vocab, ixenv->vsz, 10000);
    *error_code = test_postings_list((u_char *)"se", ixenv->doctable, ixenv->index, ixenv->forward, ixenv->fsz,
				     ixenv->vocab, ixenv->vsz, 10000);
    return other_token_breakers;  // --------------------------------------------------------------------->

    *error_code = test_postings_list((u_char *)"protein", ixenv->doctable, ixenv->index, ixenv->forward, ixenv->fsz,
				     ixenv->vocab, ixenv->vsz, 10000);
    if (*error_code < 0) return NULL;  // -------------------------------->
    *error_code = test_postings_list((u_char *)"to", ixenv->doctable, ixenv->index, ixenv->forward, ixenv->fsz,
				     ixenv->vocab, ixenv->vsz, 100);
    if (*error_code < 0) return NULL;  // -------------------------------->
    *error_code = test_postings_list((u_char *)"be", ixenv->doctable, ixenv->index, ixenv->forward, ixenv->fsz,
				     ixenv->vocab, ixenv->vsz, 100);
    if (*error_code < 0) return NULL;  // -------------------------------->
    *error_code = test_postings_list((u_char *)"or", ixenv->doctable, ixenv->index, ixenv->forward, ixenv->fsz,
				     ixenv->vocab, ixenv->vsz, 100);
    if (*error_code < 0) return NULL;  // -------------------------------->
    *error_code = test_postings_list((u_char *)"not", ixenv->doctable, ixenv->index, ixenv->forward, ixenv->fsz,
				     ixenv->vocab, ixenv->vsz, 100);
    if (*error_code < 0) return NULL;  // -------------------------------->
    *error_code = test_postings_list((u_char *)"the", ixenv->doctable, ixenv->index, ixenv->forward, ixenv->fsz,
				     ixenv->vocab, ixenv->vsz, 10000);
    if (*error_code < 0) return NULL;  // -------------------------------->
  }
  return other_token_breakers;
}


static u_char *open_and_check_index_set_aether(query_processing_environment_t *qoenv, index_environment_t *ixenv,
					       BOOL verbose, BOOL run_tests, int *error_code) {
  // ********* NOTE: This version is used in ObjectStore as well as via Aether ***********
  // Open allQBASH index files and read them into memory.  Return pointers to the memory blocks and the sizes.
  // Stem is either "QBASH" or "x/QBASH" where x is a single digit character.
  u_char *other_token_breakers = NULL;

		 
  ixenv->forward = (byte *)mmap_all_of(qoenv->fname_forward, &(ixenv->fsz), verbose, &(ixenv->forward_H),
				       &(ixenv->forward_MH), error_code);
  if (*error_code < 0) return NULL;  // -------------------------------->
  ixenv->index = (byte *)mmap_all_of(qoenv->fname_if, &(ixenv->isz), verbose, &(ixenv->index_H),
				     &(ixenv->index_MH), error_code);
  if (*error_code < 0) return NULL;  // -------------------------------->
  ixenv->vocab = (byte *)mmap_all_of(qoenv->fname_vocab, &(ixenv->vsz), verbose, &(ixenv->vocab_H),
				     &(ixenv->vocab_MH), error_code);
  if (*error_code < 0) return NULL;  // -------------------------------->
  ixenv->doctable = (byte *)mmap_all_of(qoenv->fname_doctable, &(ixenv->dsz), verbose, &(ixenv->doctable_H),
					&(ixenv->doctable_MH), error_code);
  if (*error_code < 0) return NULL;  // -------------------------------->

  check_if_header(ixenv, qoenv, &other_token_breakers, qoenv->fname_forward, error_code);
  if (*error_code < 0) return NULL;  // -------------------------------->


  if (qoenv->use_substitutions && qoenv->fname_substitution_rules != NULL) 
    load_substitution_rules(qoenv->fname_substitution_rules, 
			    &(qoenv->substitutions_hash), qoenv->debug);

  if (qoenv->classifier_mode != 0 && qoenv->fname_segment_rules != NULL) {
    if (0) printf("Attempting to load segment rules from explicit filename\n");
    load_substitution_rules(qoenv->fname_segment_rules,
			    &(qoenv->segment_rules_hash), qoenv->debug);
  }



  // Now setup ascii_non_tokens (defined and zeroed in unicode.c)
  initialize_ascii_non_tokens((u_char *)QBASH_META_CHARS, FALSE);
  initialize_ascii_non_tokens((u_char *)other_token_breakers, TRUE);  // Always additive
 

  if (verbose) fprintf(qoenv->query_output, "Case 2: indexes loaded: %s %s %s %s\n", qoenv->fname_forward, qoenv->fname_if,
		       qoenv->fname_vocab, qoenv->fname_doctable);
  if (run_tests) {
    *error_code = test_doctable_n_forward(ixenv->doctable, ixenv->forward,
					  ixenv->dsz, ixenv->fsz);
    if (*error_code < 0) return NULL;  // -------------------------------->
    *error_code = test_postings_list((u_char *)"to", ixenv->doctable, ixenv->index, ixenv->forward, ixenv->fsz,
				     ixenv->vocab, ixenv->vsz, 100);
    if (*error_code < 0) return NULL;  // -------------------------------->
    *error_code = test_postings_list((u_char *)"be", ixenv->doctable, ixenv->index, ixenv->forward, ixenv->fsz,
				     ixenv->vocab, ixenv->vsz, 100);
    if (*error_code < 0) return NULL;  // -------------------------------->
    *error_code = test_postings_list((u_char *)"or", ixenv->doctable, ixenv->index, ixenv->forward, ixenv->fsz,
				     ixenv->vocab, ixenv->vsz, 100);
    if (*error_code < 0) return NULL;  // -------------------------------->
    *error_code = test_postings_list((u_char *)"not", ixenv->doctable, ixenv->index, ixenv->forward, ixenv->fsz,
				     ixenv->vocab, ixenv->vsz, 100);
    if (*error_code < 0) return NULL;  // -------------------------------->
  }

  return other_token_breakers;
}




static byte touch_all_pages(byte *mem, size_t memsz) {
  byte xor = 0;
  size_t o = 0;
  long long counter = 0;
  int verbose = 0;

  while (o < memsz) {
    xor ^= mem[o];
    o += PAGESIZE;
    counter++;
  }
  if (verbose) printf("        %lld touches.\n", counter);
  return xor;
}


int warmup_indexes(query_processing_environment_t *qoenv, index_environment_t *ixenv) {
  byte b;

  if (qoenv->debug >= 1) fprintf(qoenv->query_output, "\nWarming up ...\n");
  // Do the .forwards first. 
  b = touch_all_pages(ixenv->forward, ixenv->fsz);
  if (qoenv->debug >= 1) fprintf(qoenv->query_output, "   .forward: %X\n", b);

  b = touch_all_pages((byte *)ixenv->doctable, ixenv->dsz);
  if (qoenv->debug >= 1) fprintf(qoenv->query_output, "   .doctable: %X\n", b);
  b = touch_all_pages(ixenv->vocab, ixenv->vsz);
  if (qoenv->debug >= 1) fprintf(qoenv->query_output, "   .vocab: %X\n", b);
  b = touch_all_pages(ixenv->index, ixenv->isz);
  if (qoenv->debug >= 1) fprintf(qoenv->query_output, "   .if: %X\n", b);


  return 0;
}


static book_keeping_for_one_query_t *load_book_keeping_for_one_query(query_processing_environment_t *qoenv,
								     int *error_code) {
  book_keeping_for_one_query_t *qex;
  int t, rl, rbn = MAX_RELAX + 1;
  
  // Called once per multi-query

  *error_code = 0;
  qex = (book_keeping_for_one_query_t *)malloc(sizeof(book_keeping_for_one_query_t));
  if (qex == NULL) {
    if (qoenv->debug >= 1)
      fprintf(qoenv->query_output, "Warning: malloc of book_keeping structure failed.  This query will be ignored.\n");
    *error_code = -220037;
    return NULL;
  }

  // Embarrassingly the +1 below is because of a memory overwriting problem observed
  // in classifier mode with some query sets and some indexes. ("Random" SEGFAULTs,
  // loops etc.)  I've been unable to track down the cause so far.
  // In classifier mode, max_candidates must be at least max_to_show,
  // and there's no point in it being much bigger.
  if (qoenv->classifier_mode || qoenv->max_candidates_to_consider == IUNDEF) 
    qoenv->max_candidates_to_consider = qoenv->max_to_show + 1;


  qex->query = NULL;
  for (t = 0; t < MAX_WDS_IN_QUERY; t++) {
    qex->qterms[t] = NULL;
    qex->partials[t] = NULL;
  }
  qex->qwd_cnt = 0;
  qex->partial_cnt = 0;
  qex->rank_only_cnt = 0;
  qex->tl_suggestions = NULL;
  qex->tl_docids = NULL;
  qex->tl_scores = NULL;
  qex->tl_returned = 0;
  qex->timed_out = FALSE;
  qex->vertical_intent_signaled = FALSE;
  qex->segment_intent_multiplier = 1.0;
  qex->query_contains_operators = FALSE;
  qex->full_match_count = 0;
  qex->street_number = -1;
  if (qoenv->timeout_msec > 0) qex->start_time = what_time_is_it();

  memset(qex->candidates_recorded, 0, (MAX_RELAX + 1) * sizeof(int));
  
  qex->candidatesa = NULL;
  if (!qoenv->report_match_counts_only) {
    // Only allocate memory if we're going to use it
    qex->candidatesa = (candidate_t **)malloc(sizeof(candidate_t *) * rbn);  // MAL0009
    if (qex->candidatesa == NULL) {
      if (qoenv->debug >= 1)
	fprintf(qoenv->query_output, "Warning: Malloc failure (qex->candidatesa) in load_book_keeping...()\n");
      *error_code = -220042;
      return NULL;  // ----------------------------------------------------------->
    }

    memset(qex->candidatesa, 0, sizeof(candidate_t *) * rbn);  // Zero all the result blocks

    qex->rank_only_countsa = (byte **)malloc(sizeof(byte *) * rbn);   // MAL0012

    if (qex->rank_only_countsa == NULL) {
      free(qex->candidatesa);    // FRE0009
      qex->candidatesa = NULL;
      if (qoenv->debug >= 1)
	fprintf(qoenv->query_output, "Warning: Malloc failure (rank_only_countsa) in load_book_keeping...()\n");
      *error_code = -220043;
      return NULL;  // ----------------------------------------------------------->
    }

    for (rl = 0; rl < rbn; rl++) {
      if (0) printf("Mallocing for result block %d (%d elements)\n", rl, qoenv->max_candidates_to_consider);
      qex->candidatesa[rl] = (candidate_t *)malloc(sizeof(candidate_t) * qoenv->max_candidates_to_consider);  // MAL0010
      if (qex->candidatesa[rl] == NULL) {
	int fi;
	for (fi = 0; fi < rl; fi++) free(qex->candidatesa[fi]);
	free(qex->candidatesa);    // FRE0009
	qex->candidatesa = NULL;
	free(qex->rank_only_countsa);
	qex->rank_only_countsa = NULL;
	if (qoenv->debug >= 1)
	  fprintf(qoenv->query_output,
		  "Warning: Malloc failure (candidatesa[%d]) in load_book_keeping...()\n", rl);
	*error_code = -220044;
	return NULL;  // ----------------------------------------------------------->
      }
      memset(qex->candidatesa[rl], 0, sizeof(candidate_t) * qoenv->max_candidates_to_consider);

      qex->rank_only_countsa[rl] = (byte *)malloc(sizeof(byte) * qoenv->max_candidates_to_consider);  // MAL0011
      if (qex->rank_only_countsa[rl] == NULL) {
	fprintf(qoenv->query_output, "Warning: Malloc failure (rank_only_countsa[%d]) in load_book_keeping...()\n", rl);
	int fi;
	for (fi = 0; fi < rbn; fi++) free(qex->candidatesa[fi]);  // All of them were allocated.
	for (fi = 0; fi < rl; fi++) free(qex->rank_only_countsa[fi]);
	free(qex->candidatesa);    // FRE0009
	qex->candidatesa = NULL;
	free(qex->rank_only_countsa);
	qex->rank_only_countsa = NULL;
	if (qoenv->debug >= 1)
	  fprintf(qoenv->query_output, "Warning: Malloc failure (rank_only_countsa[%d]) in load_book_keeping...()\n", rl);
	*error_code = -220045;
	return NULL;  // ----------------------------------------------------------->
      }
      memset(qex->rank_only_countsa[rl], 0, sizeof(byte) * qoenv->max_candidates_to_consider);
    }
  }
  return qex;
}



static void unload_book_keeping_for_one_query(book_keeping_for_one_query_t **qexp) {
  book_keeping_for_one_query_t *qex = *qexp;
  int rb;
  // Called once per multi-query.
  if (qex == NULL) return;
  // Don't free qterms or partials because they are pointers to storage within qex, not
  // to malloced memory.
  if (qex->tl_docids != NULL) free(qex->tl_docids);            // FRE2005
  if (qex->tl_scores != NULL) free(qex->tl_scores);            // FRE2004
  if (qex->tl_suggestions != NULL) free(qex->tl_suggestions);       // FRE2003

  if (qex->candidatesa != NULL) {
    for (rb = 0; rb <= MAX_RELAX; rb++) {
      if (qex->candidatesa[rb] != NULL) free(qex->candidatesa[rb]);
    }
    free(qex->candidatesa);
  }

  if (qex->candidatesa != NULL) {
    for (rb = 0; rb <= MAX_RELAX; rb++) {
      if (qex->rank_only_countsa[rb] != NULL) free(qex->rank_only_countsa[rb]);
    }
    
    free(qex->rank_only_countsa);
  }
  
  free(qex);
  *qexp = NULL;
}


int test_postings_list(u_char *word, byte *doctable, byte *index, byte *forward, size_t fsz,
		       byte *vocab, size_t vsz, int max_to_show) {
  byte *dicent;
  int ec = 0, verbose = 1;
  dicent = lookup_word(word, vocab, vsz, 0);
  if (dicent == NULL) {
    if (verbose) printf("Test_postings_list: Word '%s' not found in vocab\n", word);
    return(0);  // Not fatal because the test words are in English but the index may not be. --------------->
  }

  if (verbose) printf("\n\nTest_postings_list(%s)\n", word);
  ec = show_postings(doctable, index, forward, word, dicent, fsz, max_to_show);
  return(ec);  // -------------------------------------------->
}




// *******************************************************************************************************************************//
//                                                                                                                                //
// Begin API Definitions                                                                                                          //
//                                                                                                                                //
// *******************************************************************************************************************************//

void print_qbasher_version(FILE *f) {
  fprintf(f, "QBASHER version: %s%s\n", INDEX_FORMAT, QBASHER_VERSION);
}



static int handle_one_query(index_environment_t *ixenv, query_processing_environment_t *qoenv,
			    book_keeping_for_one_query_t *qex, u_char *query_string, u_char *options_string,
			    double score_multiplier, u_char **returned_results, double *corresponding_scores,
			    double vweight, BOOL *timed_out) {

  //  --- this is called once per query variant  -- NO LONGER SUPPORTS SHARDS ----
  //  --- No longer called directly, only through handle_multi_query()
  //
  // Returns the number of results found, or a negative error code.
  //
  // The steps in this function:
  //  1. Optional option overriding
  //  2. If applicable, set up for result-count-only mode
  //  3. Set number of candidates to consider and, in classifier mode, thoroughly clean the query string
  //  4. Special settings for relaxation and special matching modes
  //  5. Disable line_prefixing if index doesn't include line prefix words
  //  6. Query pre-processing
  //  7. Loading query book-keeping
  //  8. Setting up for deterministic time-out counting
  //  ----------------------------------------------------------------------
  //  9. Check whether we need to score candidates and normalise coefficients
  //  10. Process query text and exit on empty query or error
  //  11. In classifier modes, validate settings and bail out if answer can't be yes.
  //  --> HMQ 12. Allocate memory and deal with failures unless we want result_counts only
  //  13. Call process_query() and check for errors
  //  -----------------------------------------------------------------------
  //  --> HMQ 14. Sort results, eliminate adjacent duplicates and set up result and score arrays
  //  --> HMQ 15. If required, display query processing statistics
  //  --> HMQ 16. Clean up and return results and scores

  //     **** VITAL:  It is the callers responsibility to call free_results_memory()  !!!!
  //     **** VITAL:  to avoid memory leaks.                                          !!!!

  int error_code = 0, words_in_query = 0;
  query_processing_environment_t *local_qenv = NULL;

  if (re_match((u_char *)EASTER_EGG_PATTERN, query_string,
	       PCRE2_CASELESS, qoenv->debug)) { 
    if (0) printf("Happy Easter!!\n");
    // To display an easter egg we need to set up the following elements of qex
    //  docnum_t *tl_docids;    - The docid of each result
    //  u_char **tl_suggestions; - Copies of the relevant document text in malloced memory
    //  double *tl_scores;    - Scores associated with the each result
    //  int tl_returned;  - A count of the number or results returned.
    qex->tl_docids[0] = 1;
    qex->tl_suggestions[0] = malloc(1000);
    sprintf((char *)qex->tl_suggestions[0], "Easter-Egg: %s%s - %.0f documents",
	    INDEX_FORMAT, QBASHER_VERSION, qoenv->N);
    qex->tl_scores[0] = 0.00001;  // Very low so downstream processors can flick it
    qex->tl_returned = 1;    
    return qex->tl_returned;  

  }
  
  qex->query = query_string;

  if (options_string == NULL || *options_string == 0) {
    // There are no options specific to this query, and we don't need to fiddle with options during classifying,
    //  -- just use the global QP environment
    local_qenv = qoenv;
  }
  else {
    // Global query processing options are being overridden for this query.  Make a copy of the global environment
    // and overwrite as per.
    if (0) {
      printf("Options string received in handle_one_query: %s\n", options_string);
      fflush(stdout);
    }
    local_qenv = (query_processing_environment_t *)malloc(sizeof(query_processing_environment_t));  // MAL1953
    if (local_qenv == NULL) {
      if (qoenv->debug >= 1) fprintf(qoenv->query_output, "Warning: Malloc failed in handle_one_query().  Ignore local options and go global\n");
      local_qenv = qoenv;
      error_code = -20038;  // Not fatal
    }
    else {
      u_char *p, *q, saveq;
      memcpy(local_qenv, qoenv, sizeof(query_processing_environment_t));
      error_code = initialize_qoenv_mappings(local_qenv);  // Must set up the option mappings vector.
      if (error_code < -200000) {
	if (local_qenv != qoenv) unload_query_processing_environment(&local_qenv, FALSE, FALSE);  // FRE1953
	return(error_code);  // Fatal ------------------------------------>
      }
      p = options_string;
      while (*p == '-') p++;  // skip leading hyphens
      while (*p) {
	// Get the next arg=value token.  It starts with p.
	q = p;
	while (TRUE) {
	  if (*q == 0 || isspace(*q)) {
	    // q indicates the end of a token (any white space)
	    saveq = *q;
	    *q = 0;
	    if (0) printf("Assigning '%s'\n", p);
	    error_code = assign_one_arg(local_qenv, p, FALSE, TRUE, FALSE);  // Not initialising, Enforce limits, don't explain
	    if (error_code < -200000) {
	      if (local_qenv != qoenv) unload_query_processing_environment(&local_qenv, FALSE, FALSE);  // FRE1953
	      return(error_code);   // Fatal ------------------------------------>
	    }
	    *q = saveq;  // Put back what was disturbed
	    break;
	  }
	  q++;
	}

	// At the end of the inner loop q points to the character after the arg=value token.
	p = q;
	while (*p && isspace(*p)) p++;
	while (*p == '-') p++;  // skip leading hyphens
      }
    }  //  end of else branch in which local_qenv is created
  }

  // The following all happens whether options have been overwridden or not.

  if (local_qenv->ixenv == NULL) local_qenv->ixenv = ixenv;  // Just make sure we can access indexes through qoenv

  if (local_qenv->max_to_show == 0) {
    // Special mode to report match counts without returning any actual results
    local_qenv->report_match_counts_only = TRUE;
    local_qenv->max_candidates_to_consider = A_BILLION_AND_ONE;
    if (0) printf("# Entering special result counting mode with max_candidates = %d\n",
	   local_qenv->max_candidates_to_consider);
  }

  
  if (local_qenv->classifier_mode) {
    // Introduced on 26 Aug 2016 (in response to further production crashes)
    size_t l;
    int original_query_words = 0, max_word_length_in_bytes = 0;
    l = trim_and_strip_all_ascii_punctuation_and_controls(qex->query,
							  &original_query_words,
							  &max_word_length_in_bytes);
    if (0) printf("  --- orig q words %d, max_wdlen_in_bytes %d\n",
		  original_query_words, max_word_length_in_bytes);
    if (local_qenv->display_parsed_query)
      printf("Query after stripping punctuation and controls is {%s}\n",
	     qex->query);
    if (l <= 0) return(0);  // -----------------------------------------------> Empty Query
    if (local_qenv->classifier_min_words > 0
	&& original_query_words < local_qenv->classifier_min_words) return(0);  //-----------> Query too short
        // It's not an error, we're just saving ourselves a lot of work
    if (local_qenv->classifier_longest_wdlen_min > 0
	&& max_word_length_in_bytes < local_qenv->classifier_longest_wdlen_min)
      return(0);  //-----------> All query words are too short
        // It's not an error, we're just avoiding embarrassing false positives.
  } 


  if (local_qenv->relaxation_level != 0) {
    // In these special modes, deactivate features only useful in AutoSuggest experiments
    // These are no longer important because of the development of RevIdx.
    local_qenv->auto_partials = FALSE;
    local_qenv->auto_line_prefix = FALSE;
  }


  // Can only do line_prefixing with an appropriate index.
  if (strstr((char *)ixenv->other_token_breakers, "<=??") == NULL) local_qenv->auto_line_prefix = FALSE;
  
  // Check whether we need to score candidates
  local_qenv->scoring_needed = normalise(local_qenv->rr_coeffs, NUM_COEFFS);
  normalise(local_qenv->cf_coeffs, NUM_CF_COEFFS);

  words_in_query = process_query_text(local_qenv, qex);
  if (0) printf("Query text processed.  words_in_query = %d\n", words_in_query);
  if (words_in_query == 0) {
    // unload_book_keeping_for_one_query(&qex);  Don't do this in multi-query environment
    if (local_qenv != qoenv) unload_query_processing_environment(&local_qenv, FALSE, FALSE);  // FRE1953
    return(0);  // -----------------------------------------------> Empty Query
  }
  if (words_in_query < -200000) {  // Negative signals an error
    // unload_book_keeping_for_one_query(&qex);  Don't do this in multi-query environment
    if (local_qenv != qoenv) unload_query_processing_environment(&local_qenv, FALSE, FALSE);  // FRE1953
    return(error_code);  // ----------------------------------------------->  Error
  }
  if (local_qenv->classifier_mode > 0) {
    classifier_validate_settings(local_qenv, qex);
    if (qex->qwd_cnt > local_qenv->classifier_max_words) {
       return 0;   // ------------------------------------------------------> No results
                   // It's not an error, we're just saving ourselves a lot of work
    }
  }

  // 2. Call process_query()
  if (0) printf("calling process_query()\n");
  error_code = process_query(local_qenv, qex, ixenv->doctable, ixenv->vocab, ixenv->index,
			     ixenv->forward, ixenv->vsz, ixenv->fsz, score_multiplier);

  if (error_code < -200000) {
    if (local_qenv != qoenv) unload_query_processing_environment(&local_qenv, FALSE, FALSE);  // FRE1953
    return(error_code);  // -------------------------------------------->
  }

  // When we return to handle_multi_query() qex will be all set up with result_count,
  // results array and corresponding scores.
  if (local_qenv != qoenv) unload_query_processing_environment(&local_qenv, FALSE, FALSE);  // FRE1953
  return qex->tl_returned;  
}


// For lyrics and other query-classification projects, we modified
// the QBASHER query processor DLL to support multi-queries.  
// A multi-query is expected to comprise a small set of queries,
// each with different lexical forms and/or different processing
// options, but all with the same intent.  In an envisaged scenario there
// might be a raw, user-typed query, a spelling-corrected version, and
// two query rewrites.  In some scenarios all of the queries may be
// run.  In others, the first query is always run but whether
// subsequent queries are run depends upon the outcome of post-query
// tests.  Associated with each query is a weight used to scale scores
// obtained when running it.
//
// If a deterministic timeout is in force, it will be applied across
// the whole MQS, not separately for each component query.
//
// A query string (QS) within a multi-query string (MQS) consists of
// between 1 and 4 tab-separated fields.  In approximate BNF:
//
// <MQS> ::= <QS>…NUL

// <QS> ::=
// <query>[\t<options>[\t<weight>[\t<post-query-test>]]]<qTermChar>
//
// <qTermChar> ::= \n|RS
//
// <query> is a UTF-8 string containing no ASCII controls <options>
//   and <weight> may be empty strings.
// <options> are QBASHER per-query options -- Note that these will
//   be ignored unless allow_per_query_options=TRUE.
// <weight> is a decimal fraction between 0 and 1 which is used
//   to scale scores of documents retrieved by this variant.
// <post-query-test> can be either a test of how many results (N)
//   have been found so far, e.g. "N<5" or to the highest score (H)
//   so far achieved, e.g. "H<0.95".  The only relational operator to
//   be implemented in the initial version is < (less than) and the
//   only quantities able to be tested are H and N.  If there is no
//   post-query-test, the next variant will always be run.
// <qTermChar> - RS is the ASCII record separator character ctrl-^
//   (0x1E).  Use of the alternative \n (ASCII linefeed) 
//   improves readability, but can't be used when runnng batches
//   of queries through QBASHQ.
//  
//
// Some MQS examples:
//
// Simplest possible:
//   Lucie in the sky with dimends\n
//   NUL # a single query with no weight (1.0 assumed), no query-specific
//   options and no post-query-test.  (The interpreter will tolerate
//   the lack of a final \n).
//
// All variants are run:
//   Lucie in the sky with dimends\n
//   Lucy in the sky with diamonds\n
//   NUL # Both query variants are always run, producing a single result list.
//
//
// Query fallback. Running of subsequent queries depends upon
// post-query conditions:
//   Lucie in the sky with dimends\t\t1.0\N<1\n
//   Lucie in the sky with dimends\t-relaxation_level=1\t0.9\tH<0.85\n
//   Lucy in the sky with diamonds\t\t0.8\n
//   NUL # This MQS represents a set of three query variants.  The first one
//   is the raw user query, run with normal options and with full weight.  If
//   it fails to find any results (N<1), execution will continue with the
//   second variant, which is the same query run with relaxation.  If any
//   results are found in this process, their scores are multiplied by
//   0.9.  If this query fails to find a result with a score of at least
//   0.85, then execution proceeds to the final variant, run with a
//   scoring weight of 0.8



int handle_multi_query(index_environment_t *ixenv, query_processing_environment_t *qoenv,
		       u_char *multi_query_string, u_char ***returned_results,
		       double **corresponding_scores, BOOL *timed_out) {

  // This is the one-and-only interface to QBASHER query processing.  What is sent in
  // is a multi-query string (MQS) as described in the comment immediately above.  As
  // noted in that comment, the MQS may in fact be just a single query.
  //
  // This function:
  //   1. Allocates storage for returned_results and corresponding_scores.
  //   2. Splits multi_query_strings into individual query strings, and for each:
  //      2.1 Splits the query string into query, options, weight, and post-test strings
  //      2.2 Calls handle_one_query with query and options
  //      2.3 Applies the post-test to the results returned and breaks if not true
  //   3. Sort results, eliminate adjacent duplicates and set up result and score arrays
  //   4. If required, display query processing statistics
  //   5. Clean up and return results and scores

  //     **** VITAL:  It is the callers responsibility to call free_results_memory()  !!!!
  //     **** VITAL:  to avoid memory leaks.                                          !!!!

  BOOL isadupe, explain = (qoenv->debug >= 1);
  book_keeping_for_one_query_t *qex = NULL;
  // local variables corresponding to the last two parameters
  u_char **lrr = NULL, *p, *q, *query, *options, *weight, *post_test;
  double *lcs = NULL, qweight = 1.0;
  int rslt_count = 0, shown = 0, i, j, error_code;
  size_t clen;

    // Make sure these are null if not otherwise assigned.
  *returned_results = NULL;
  *corresponding_scores = NULL;

  // Terminate at first CR or LF
  p = multi_query_string;
  while (*p && *p != '\r' && *p != '\n') p++;
  *p = 0;
  
  if (explain) printf("Handle_multi_query(%s).\n", multi_query_string);
  qex = load_book_keeping_for_one_query(qoenv, &error_code);
  if (error_code < -200000) {
    return error_code;  //  ------------------------------------------------------>
  }

  setup_for_op_counting(qex);
  
  if (!qoenv->report_match_counts_only) {
    // Don't allocate memory if we're in the max_to_show == 0 special case

    // 1.  Allocate memory and deal with failures
    qex->tl_suggestions = (u_char **)malloc(qoenv->max_to_show * sizeof(u_char *));  // MAL2003
    qex->tl_scores = (double *)malloc(qoenv->max_to_show * sizeof(double));          // MAL2004
    qex->tl_docids = (docnum_t *)malloc(qoenv->max_to_show * sizeof(docnum_t));      // MAL2005
    lrr = (u_char **)malloc(qoenv->max_to_show * sizeof(u_char *));   // MAL701
    lcs = (double *)malloc(qoenv->max_to_show * sizeof(double)); // MAL702
    if (0) printf("Mallocs done -- max_to_show = %d\n", qoenv->max_to_show);

    if (qex->tl_suggestions == NULL || qex->tl_scores == NULL || lrr == NULL || lcs == NULL) {
      if (explain)
	fprintf(qoenv->query_output, "Warning: Malloc failed in handle_multi_query(). Unable to proceed with this query.\n");
      if (lrr != NULL) free(lrr);									 // FRE701
      if (lcs != NULL) free(lcs);									 // FRE702
      lrr = NULL;
      lcs = NULL;
      unload_book_keeping_for_one_query(&qex);
      error_code = -220040;
      return(error_code);   // -------------------------------------------->
    }
    if (explain)
      fprintf(qoenv->query_output,
	      "handle_multi_query: malloced qex->tl_suggestions, and qex->tl_scores arrays plus lrr and lcs\n");

    zero_op_counts(qex);

    for (i = 0; i < qoenv->max_to_show; i++) {
      qex->tl_suggestions[i] = NULL;
      qex->tl_scores[i] = 0.0;
    }
  }

  // ------------ This is where we split up the multi-query string -----------------------------

  p = multi_query_string;
  while (*p) {   // Loop over query variants
    if (explain) printf(" -------------- looping over q variants '%s' ------\n", p);
    query = p;
    options = NULL;
    weight = NULL;
    post_test = NULL;
    while (*p && *p != '\t' && *p != ASCII_RS && *p != '\r' && *p != '\n') {
      p++;
    }
    if (*p == '\t') {
      *p = 0;  // Terminate the query part.
      options = ++p;
      while (*p && *p != '\t' && *p != ASCII_RS && *p != '\r' && *p != '\n') p++;
      if (*p == '\t') {
	*p = 0;  // Terminate the options part.
	weight = ++p;
	while (*p && *p != '\t' && *p != ASCII_RS && *p != '\r' && *p != '\n') p++;
	if (*p == '\t') {
	  *p = 0;  // Terminate the weight part.
	  post_test = ++p;
	  while (*p && *p != '\t' && *p != ASCII_RS && *p != '\r' && *p != '\n') p++;
	}
      }
    }

    // We should now be at end-of-variant or end-of-mqs but maybe there's extra fields or
    // other rubbish
    q = p;  // Remember where the end of the last field was so we can terminate it after testing
    while (*p && *p != ASCII_RS && *p != '\r' && *p != '\n') p++;  // Skip to EOV or EOMSQS
    while (*p && (*p == ASCII_RS || *p == '\r' || *p == '\n')) p++;  // skip to end of sequence of terminators
    *q = 0;  // Terminate the last field actually present.

    if (weight != NULL) {
      if (0) printf("   --- weight string --- %s\n", weight);
      // Note: default weight for first query variant is 1.0
      //       default weight for subsequent query variants is the weight of the preceding query
      if (isdigit(*weight)) qweight = strtod((char *)weight, NULL);
      if (qweight < 0.0 || qweight > 1.0) qweight = 1.0;
    }


    if (qoenv->allow_per_query_options) {
      rslt_count = handle_one_query(ixenv, qoenv, qex, query, options, qweight,
				    *returned_results, *corresponding_scores, qweight, timed_out);
    } else {
      rslt_count = handle_one_query(ixenv, qoenv, qex, query, (u_char *)"", qweight,
				    *returned_results, *corresponding_scores, qweight, timed_out);   
    }

    if (explain) {
      printf("  '%s, ", query);
      if (options != NULL) printf("%s", options);
      printf(", %.4f,", qweight);
      if (post_test != NULL) printf("%s ", post_test);
      printf("' -> %d results\n", rslt_count);
    }
    
    // Note post_tests are ignored if they don't make sense
    if (post_test != NULL) {
    if (explain) printf("Testing post_test (%s).  Tl_returned = %d\n",
				      post_test, qex->tl_returned);
      if (*post_test == 'N') {
	if (post_test[1] == '<') {
	  int criterion;
	  criterion = strtol((char *)post_test + 2, NULL, 10);
	  if (rslt_count >= criterion) {
	    if (explain) printf("Post-test N: No need to run fallback queries\n"); 
	    break;    //  --------------------->
	  }
	  if (explain) printf("Post-test N: going to run next fallback query.  Criterion was %d\n", criterion);
	}
      } else if (*post_test == 'H') {
	double criterion;
	criterion = strtod((char *)post_test + 2, NULL);
	if (rslt_count >= criterion) {  ///  !!!!!!!!!!!!!! Not correct !!!!!!!!!!!!!   FIXME
	  if (explain) printf("Post-test H: No need to run fall back queries\n"); 
	  break;    //  --------------------->
	}
	if (explain) printf("Post-test H: going to run next fallback query.  Criterion was %.4f\n", criterion);
      } else {
	if (explain)
	  printf("Invalid post test: '%s'.  Will run the next query in the MQS\n",
		 post_test);
      }
    }
  }  // End of while loop over query variants
 

  // -----------   Clean up and present results -------------------------------------------->



  

  if (qoenv->report_match_counts_only) {
    //  ---------------- Special max_to_show == 0 behaviour ---------------------
    shown = qex->full_match_count;
    //  -------------------------------------------------------------------------
  } else {
    // 3.  Show up to max_to_show suggestions, eliminating duplicates
    if (0) printf("3.  Show %d (up to %d) suggestions, eliminating duplicates\n",
		  qex->tl_returned, qoenv->max_to_show);
    // 
    for (j = 0; j < qoenv->max_to_show; j++) {
      lrr[j] = NULL;
      lcs[j] = 0.0;
    }

    if (explain)
      fprintf(qoenv->query_output, "Initialised the lrr & lcs memory\n");
    shown = 0;
    i = 0;

    // i is the index into qex->tl_returned,
    // shown is the index into lrr and lcs -- the results which will actually be shown
    while (shown < qoenv->max_to_show && i < qex->tl_returned) {
      if (qex->tl_suggestions[i] == NULL) {
	break;
      }
      isadupe = FALSE;
      if (qoenv->duplicate_handling > 1) {
	// We don't want to eliminate duplicates when told not too
	// Check this hasn't already been shown.
	// qbash_timing_check.pl with and without this duplicate detection shows little difference
	for (j = i - 1; j >= 0; j--) {
	  if (0) printf("Dup checking: j = %d\n", j);
	  isadupe = isduplicate((char *)qex->tl_suggestions[i], (char *)qex->tl_suggestions[j],
				explain);
	  if (isadupe) {
	    if (explain) {
	      fprintf(qoenv->query_output, "Duplicate suppressed.  '%s'(%d) v. '%s'(%d)\n", qex->tl_suggestions[i],
		      i, (char *)qex->tl_suggestions[j], j);
	    }
	    break;
	  }
	}
      }
      if (isadupe) {
	i++;  // Have to advance this in all cases
	continue;
      }

      if (0) printf("copying to lrr[%d / %d]\n", shown, qoenv->max_to_show);
      clen = strlen((char *)(qex->tl_suggestions[i]));
      lrr[shown] = (u_char *)malloc(clen + 1);  // MAL703
      if (lrr[shown] == NULL) {
	fprintf(qoenv->query_output, "Warning: Malloc failed for returned_results element.  This result won't be shown.\n");
      }
      else {
	strcpy((char *)(lrr[shown]), (char *)(qex->tl_suggestions[i]));
	lcs[shown] = qex->tl_scores[i];
	shown++;
      }

      i++;
    }

    qex->tl_returned = shown;
  }

  
  // 8. Clean up.

  if (qoenv->x_show_qtimes ||  explain) {
    display_op_counts(qoenv, qex);  // Note: the op_counts are zeroed in handle_one_query()
    display_shard_stats(qoenv, qex, qoenv->timeout_kops, qex->tl_returned, qex->tl_suggestions);
  }
  
  if (!qoenv->report_match_counts_only) {
    for (i = 0; i < qoenv->max_to_show; i++) {
      if (qex->tl_suggestions[i] != NULL) {
	free((void *)qex->tl_suggestions[i]);   // FRE2006
	qex->tl_suggestions[i] = NULL;
      }
    }
  }

  if (qex->timed_out) {
    if (explain) printf("TIMED OUT: %s\n", qex->query_as_processed);
    *timed_out = TRUE;
  }
  unload_book_keeping_for_one_query(&qex);
  *returned_results = lrr;
  *corresponding_scores = lcs;
  if (explain)
    fprintf(qoenv->query_output,
	    "Reached the end of handle_multi_query() with %d\n", shown);
  if (shown == 0) qoenv->queries_without_answer++;
  return shown;
}




void free_results_memory(u_char ***result_strings, double **corresponding_scores, int num_results) {
  // Frees the memory returned by handle_multi_query. 
  //  - First arg is a pointer to an array of pointers to strings - must free the strings and then the array
  //  - Second is an array of doubles
  //  - Third gives the dimension of the above arrays.
  u_char **lrr = *result_strings;
  double *lcs = *corresponding_scores;

  if (lrr != NULL) {
    int r;
    for (r = 0; r < num_results; r++) {
      if (lrr[r] != NULL) free(lrr[r]);  // FRE703
    }
    free(lrr);  // FRE701
    *result_strings = NULL;
  }
  if (lcs != NULL) {
    free(lcs);  // FRE702
    *corresponding_scores = NULL;
  }
}


static int split_filelist_arg(query_processing_environment_t *qoenv, u_char *pFilenames) {
  u_char *r, *fnstart, *name;
  size_t len;
  // Split the list on the commas and assign the paths to the appropriate file_xxxx args
  r = pFilenames;
  while (*r) {
    while (*r && isspace(*r)) r++; // skip whitespace if any
    fnstart = r;
    if (*r == 0) break;
    while (*r && *r != ',') r++; // Find the end of this filename.
    len = r - fnstart;
    if (len > 0) {  // Ignore extra commas
      name = make_a_copy_of_len_bytes(fnstart, len);
      if (name == NULL) {
	return -220070;
      }
      if (!strcmp((char *)name + (len - 8), ".forward")) qoenv->fname_forward = name;
      else if (!strcmp((char *)name + (len - 9), ".doctable")) qoenv->fname_doctable = name;
      else if (!strcmp((char *)name + (len - 6), ".vocab")) qoenv->fname_vocab = name;
      else if (!strcmp((char *)name + (len - 3), ".if")) qoenv->fname_if = name;
      else if (!strcmp((char *)name + (len - 7), ".config")) qoenv->fname_config = name;
      else if (!strcmp((char *)name + (len - 19), ".substitution_rules")) qoenv->fname_substitution_rules = name;
      else if (!strcmp((char *)name + (len - 19), ".segment_rules")) qoenv->fname_segment_rules = name;
      else if (!strcmp((char *)name + (len - 12), ".query_batch")) qoenv->fname_query_batch = name;
      else if (!strcmp((char *)name + (len - 7), ".output")) qoenv->fname_output = name;
      else {
	printf("Erroneous name: %s\n", name);
	return -200071;
      }
    }

    if (*r) r++;  // Skip the comma
  }
  return 0;
}



int finalize_query_processing_environment(query_processing_environment_t *qoenv, BOOL verbose,
					  BOOL explain_errors) {
  // set up all the derived variables, open files etc.
  // Return -ve error code;  1 - success
  int error_code = 0;


  // But but first first see if we've been given a list of files in an object_store_files arg

  if (qoenv->object_store_files != NULL) {
    if (0) printf("Processing object store files: %s\n", qoenv->object_store_files);
    error_code = split_filelist_arg(qoenv, qoenv->object_store_files);
    if (error_code) return error_code;
  }

  // But first see if we need to read a config file with more arguments.
  if (qoenv->fname_config != NULL  && exists((char *)qoenv->fname_config, "")) {
    error_code = assign_args_from_config_file(qoenv, qoenv->fname_config, TRUE, explain_errors);  // Initialising.  
    if (error_code) return error_code;
  }


  if (qoenv->index_dir == NULL) {
    if (qoenv->fname_forward == NULL || qoenv->fname_if == NULL || qoenv->fname_vocab == NULL || qoenv->fname_doctable == NULL) {
      return -200064;
    }
  }
  else {
    if (qoenv->fname_forward != NULL || qoenv->fname_if != NULL || qoenv->fname_vocab != NULL || qoenv->fname_doctable != NULL) {
      return -200065;
    }
  }


  if (qoenv->fname_output != NULL) {
    qoenv->query_output= fopen((char *)qoenv->fname_output, "w");
    if (qoenv->query_output == NULL) {
      // can't write to query_stream if we failed to open it.
      return -200066;
    }
  }

  if (qoenv->max_length_diff == IUNDEF) {
    // In classifier_modes 2 and 4, the classification score is based on sums of IDF values.  When the queries are short
    // and the records are long, QPS rates and latencies deteriorate, sometimes very dramatically (factor of 100 in QPS)
    // due to the need to look up the IDF for every term in every candidate.  But most of this computation is wasted.
    // Setting max_length_diff to 401 means that the actual length diff maximum is set dynamically for queries up to 4
    // words in length.  Note that this change will not occur if an explicit max_length_diff value has been specified.
    // 402 means:
    //	   4 - no length difference limit will be applied for queries longer than 4 words
    //    02 - the length difference limit will be the result of the formula above plus 02.
    if (qoenv->classifier_mode == 2 || qoenv->classifier_mode == 4) qoenv->max_length_diff = 402; 
    else qoenv->max_length_diff = 1000;

    if (0) printf("Autoset max_length_diff = %d\n", qoenv->max_length_diff);
  }

  if (qoenv->debug >= 1) {
    setvbuf(qoenv->query_output, NULL, _IONBF, 0);    // Normally turned off because of major time penalty.  HUGE!!!
    setvbuf(stderr, NULL, _IONBF, 0);    // Normally turned off because of major time penalty.  HUGE!!!
    if (qoenv->debug == 10) qoenv->debug = 0;  // Value 10 has meaning: unbuffer query_output but don't print debugging
  }


  if (qoenv->x_show_qtimes) {
    // Force single-stream running to get sensible times per query
    qoenv->query_streams = 1;
  }


  if (verbose) {
    fprintf(qoenv->query_output, "Feature weighting coefficients: %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n",
	    qoenv->rr_coeffs[0], qoenv->rr_coeffs[1], qoenv->rr_coeffs[2], qoenv->rr_coeffs[3],
	    qoenv->rr_coeffs[4], qoenv->rr_coeffs[5], qoenv->rr_coeffs[6], qoenv->rr_coeffs[7]);
#ifdef WIN64		
    report_memory_usage(qoenv->query_output, "near the very beginning", NULL);
#endif
  }


  return 1;  // success
}


#ifdef WIN64
static double report_pagefile_usage_in_kb() {
  // Return current pagefile usage in kilobytes
  PROCESS_MEMORY_COUNTERS m;
  BOOL ok;
  ok = GetProcessMemoryInfo(GetCurrentProcess(), &m, (DWORD)sizeof(m));
  return (double)m.PagefileUsage / 1024.0;
}


void report_milestone(query_processing_environment_t *qoenv) {
  fprintf(qoenv->query_output, "Milestone: %lld queries run; Total elapsed time %.0f sec.  Pagefile usage %.0fKB\n",
	  qoenv->queries_run, what_time_is_it() - qoenv->inthebeginning,
	  report_pagefile_usage_in_kb());
}
#else
void report_milestone(query_processing_environment_t *qoenv) {
  fprintf(qoenv->query_output, "Milestone: %lld queries run; Total elapsed time %.0f sec.\n",
	  qoenv->queries_run, what_time_is_it() - qoenv->inthebeginning);
}
#endif


void report_query_response_times(query_processing_environment_t *qoenv) {
  double macro_total_time = what_time_is_it() - qoenv->inthebeginning;
#ifdef WIN64
  fprintf(qoenv->query_output, "Milestone: %lld queries run; Total elapsed time: Macro %.1f sec; Micro %.1f sec. Pagefile usage %.0fKB -- %.1f QPS\n",
	  qoenv->queries_run, macro_total_time, qoenv->total_elapsed_msec_d / 1000.0, report_pagefile_usage_in_kb(),
	  qoenv->queries_run / macro_total_time);
#else
  fprintf(qoenv->query_output, "Milestone: %lld queries run; Total elapsed time: Macro %.1f sec; Micro %.1f sec. -- %.1f QPS\n",
	  qoenv->queries_run, macro_total_time, qoenv->total_elapsed_msec_d / 1000.0, 
	  qoenv->queries_run / macro_total_time);
#endif


  fprintf(qoenv->query_output, "\n\nInputs processed: %lld.  Inputs with zero results: %lld\n",
	  qoenv->queries_run, qoenv->queries_without_answer);
  fprintf(qoenv->query_output, "Deterministic timeout was set at: %d kilo-cost-units\n", qoenv->timeout_kops);
  fprintf(qoenv->query_output, "Elapsed time timeout was set at: %d msec\n", qoenv->timeout_msec);
  fprintf(qoenv->query_output, "  Query timeout count (from either cause): %lld\n", qoenv->query_timeout_count);
  fprintf(qoenv->query_output, "  Global_IDF Lookups: %lld\n", qoenv->global_idf_lookups);


  fprintf(qoenv->query_output, "Average elapsed msec per query: %.3f\n", qoenv->total_elapsed_msec_d / qoenv->queries_run);
  fprintf(qoenv->query_output, "Maximum elapsed msec per query: %.0f  (%s)\n", qoenv->max_elapsed_msec_d, qoenv->slowest_q);

  analyze_response_times(qoenv);
}


void show_mode_settings(query_processing_environment_t *qoenv) {
  fprintf(qoenv->query_output, "\n------- Mode Settings -----------\n");
  if (qoenv->auto_partials) fprintf(qoenv->query_output, "Auto partials active\n");
  fprintf(qoenv->query_output, "Relaxation level: %d\n", qoenv->relaxation_level);
  fprintf(qoenv->query_output, "Column to display: %d\n", qoenv->displaycol);
  if (!qoenv->scoring_needed) fprintf(qoenv->query_output, "Scoring is NOT needed\n");
  fprintf(qoenv->query_output, "Degree of parallelism: %d\n", qoenv->query_streams);
  fprintf(qoenv->query_output, "----------------------------------\n\n");
#ifdef WIN64
  fprintf(qoenv->query_output, "Milestone: %lld queries run; Total elapsed time %.0f sec.  Pagefile usage %.0fKB\n",
	  qoenv->queries_run, what_time_is_it() - qoenv->inthebeginning, report_pagefile_usage_in_kb());
#else
  fprintf(qoenv->query_output, "Milestone: %lld queries run; Total elapsed time %.0f sec.\n",
	  qoenv->queries_run, what_time_is_it() - qoenv->inthebeginning);
#endif
}



index_environment_t *load_indexes(query_processing_environment_t *qoenv, BOOL verbose, BOOL run_tests,
				  int *error_code) {
  // No longer Chdir to the index directory  -  it's not threadsafe
  //
  // There are two usage cases: 
  // Case 1 - qoenv->index_dir is not NULL.   This is the old mode but it no longer supports multiple shards on a single server.
  //				Memorymap the index files in the specified directory path
  // Case 2 - qoenv-index_dir is NULL.  This is the Aether mode
  //		Just open and memorymap the files specified in qoenv->fname_*
  // 
  index_environment_t *ixenv;
  u_char *other_token_breakers;


  *error_code = 0;
  if (qoenv == NULL) {
    *error_code = -200062;
    return NULL;
  }
  // - - - - - - - - - - - - - - - - - - - - - - - Common to both cases - - - - - - - - - - - - - - - - - - - - - - - 
  ixenv = (index_environment_t *)malloc(sizeof(index_environment_t));   // MAL801
  if (ixenv == NULL) {
    *error_code = -220063;
    return NULL;
  }
  ixenv->doctable = NULL;
  ixenv->vocab = NULL;
  ixenv->index = NULL;
  ixenv->forward = NULL;
  ixenv->other_token_breakers = NULL;
  ixenv->expect_cp1252 = TRUE;


  if (qoenv->index_dir != NULL) {
    // - - - - - - - - - - - - - - - - - - - - - - - - - - Case 1 - - - - - - - - - - - - - - - - - - - - - - - - - -
    u_char  *index_stem;
    size_t idplen, stemlen;
    idplen = strlen((char *)qoenv->index_dir);

    if (verbose) fprintf(qoenv->query_output, " -- loading indexes from index_dir %s --\n", qoenv->index_dir);

    // Malloc space for longest file path =
    //  strlen(index_directory_path) + strlen("/0/") + strlen("QBASH.doctable");   // 17 extra chars
    index_stem = (u_char *)malloc(idplen + 30);    // MAL800
    if (index_stem == NULL) {
      free(ixenv);
      *error_code = -220063;
      return NULL;
    }
    strcpy((char *)index_stem, (char *)qoenv->index_dir);

    strcpy((char *)index_stem + idplen + 3, "QBASH");
    stemlen = idplen + 3 + 5;  // 3 for \.\  and 5 for QBASH
    index_stem[idplen] = '/';

    if (verbose) fprintf(qoenv->query_output, "Falling back to single QBASH.* index\n");
    strcpy((char *)index_stem + idplen, "/QBASH");
    stemlen = idplen + 6;
    other_token_breakers = open_and_check_index_set(qoenv, ixenv, index_stem, stemlen, verbose, run_tests, error_code);
    free(index_stem);  // FRE800
    if (other_token_breakers != NULL) {
      if (ixenv->other_token_breakers == NULL) ixenv->other_token_breakers = other_token_breakers;
    }
  }
  else {
    // - - - - - - - - - - - - - - - - - - - - - - - - - - Case 2 - - - - - - - - - - - - - - - - - - - - - - - - - -
    ixenv->other_token_breakers = open_and_check_index_set_aether(qoenv, ixenv, verbose, run_tests, error_code);
  }
  if (*error_code < -200000) {
    free(ixenv);
    return NULL;
  }


  // - - - - - - - - - - - - - - - - - - - - - - - Common to both cases - - - - - - - - - - - - - - - - - - - - - - -

  
  if (verbose) {
    fprintf(qoenv->query_output, "Indexes loaded.\n");
    if (ascii_non_tokens[0x92]) printf("CP-1252 punctuation causes breaks\n");
#ifdef WIN64
    report_memory_usage(qoenv->query_output, "After mapping all the files but before running anything", NULL);
#endif
  }
  return ixenv;
}


void unload_indexes(index_environment_t **ixenvp) {
  index_environment_t *ixenv = *ixenvp;
  if (ixenv == NULL) return;
  if (ixenv->doctable != NULL) {
    unmmap_all_of(ixenv->doctable, ixenv->doctable_H, ixenv->doctable_MH, ixenv->dsz);
  }
  if (ixenv->forward != NULL) {
    unmmap_all_of(ixenv->forward, ixenv->forward_H, ixenv->forward_MH, ixenv->fsz);
  }

  if (ixenv->index != NULL) {
    unmmap_all_of(ixenv->index, ixenv->index_H, ixenv->index_MH, ixenv->isz);
  }
  if (ixenv->vocab != NULL) {
    unmmap_all_of(ixenv->vocab, ixenv->vocab_H, ixenv->vocab_MH, ixenv->vsz);
  }
  free(ixenv);   // FRE801
  *ixenvp = NULL;
}


query_processing_environment_t *load_query_processing_environment() {
  query_processing_environment_t *qoenv;
  qoenv = (query_processing_environment_t *)malloc(sizeof(query_processing_environment_t));
  if (qoenv == NULL) {
    fprintf(qoenv->query_output, "Warning: malloc of query_processing environment failed.  Unable to process queries.\n");
    return NULL;
  }
  memset(qoenv, 0, sizeof(query_processing_environment_t));
  calculate_dte_shifts_and_masks();
  test_shifts_and_masks();
  initialize_unicode_conversion_arrays(FALSE);
  initialize_qoenv_mappings(qoenv);
  set_qoenv_defaults(qoenv);
  return qoenv;
}


void unload_query_processing_environment(query_processing_environment_t **qoenvp, BOOL report_final_memory_usage,
					 BOOL full_clean) {
  query_processing_environment_t *qoenv = *qoenvp;
  if (qoenv == NULL) return;

  if (full_clean) {
    if (qoenv->substitutions_hash != NULL) {
      unload_substitution_rules(&qoenv->substitutions_hash, qoenv->debug);
    }
    if (qoenv->segment_rules_hash != NULL) {
      unload_substitution_rules(&qoenv->segment_rules_hash, qoenv->debug);
    }
    
  }
#ifdef WIN64
  if (report_final_memory_usage) report_memory_usage(qoenv->query_output, "at the very end", NULL);
#endif
  if (full_clean && qoenv->query_output != stdout) {
    //close qoenv-> query output file, unless it's stdout
    fclose(qoenv->query_output);
    qoenv->query_output = NULL;
  }

  if (full_clean) free_options_memory(qoenv);
  if (qoenv->vptra != NULL) free(qoenv->vptra);

  free(qoenv);
  *qoenvp = NULL;
}


/////////////////////////////////////////////////////////////////////////////
// The following definitions are only used when running in ObjectStore.  
/////////////////////////////////////////////////////////////////////////////

#ifdef WIN64
QBASHQ_API int NativeInitializeSharedFiles(u_char *pFilenames, query_processing_environment_t **qoenv) {

  // Called once in the life of a particular QBASHER ObjectStore service.  
  // IN:  pFilenames is a comma separated list.  It should contain paths to each of the QBASHER index files:
  //  .forward, .if, .doctable and .vocab, plus optionally the path to a config file .config
  // OUT: return error code and, via params, pointer to query processing environment including index

  // 0. Run some internal tests
  // 1. Create a query processing environment
  // 2. Assign all the file values according to what has been passed in
  // 3. Call finalize_query_processing_environment()
  // 4. Load indexes
  // 5. Warm-up indexes
  // 6. Return a pointer to the QPE and IXE

  // DO we need to do an equivalent of FreeSharedFiles();

  int error_code = 0;

  *qoenv = NULL;  // Presumably these will be NULL already or at least unassigned.

  // Run a bunch of internal tests to make sure all is well with the code.
  error_code = test_sb_macros();
  if (error_code) return 1000 + error_code;
  error_code = test_isprefixmatch();
  if (error_code) return 2000 + error_code;
  error_code = test_isduplicate(0);
  if (error_code) return 3000 + error_code;
  error_code = test_substitute();
  if (error_code) return 4000 + error_code;
  error_code = run_bagsim_tests();
  if (error_code) return 5000 + error_code;


  if (sizeof(size_t) != 8) {
    printf("sizeof(size_t) = %lld\n", sizeof(size_t));
    return -200068;  // A necessary start up condition
    return error_code;
  }

  *qoenv = load_query_processing_environment();
  if (*qoenv == NULL) {
    return -220069;
  }

  error_code = split_filelist_arg(*qoenv, pFilenames);
  if (error_code) return error_code;

  // Check that all files have been supplied.  .config, and .substitutions are not mandatory
  if ((*qoenv)->fname_forward == NULL || (*qoenv)->fname_doctable == NULL || (*qoenv)->fname_vocab == NULL
      || (*qoenv)->fname_if == NULL) {
    return -200072;
  }


  error_code = finalize_query_processing_environment((query_processing_environment_t *)(*qoenv), FALSE, FALSE);
  if (error_code < 0) return error_code;  // finalize..() returns 1 on success

  // ..., FALSE, TRUE, ... in the following line means NOT Verbose, BUT run index tests (time consuming)
  //(*qoenv)->ixenv = load_indexes(*qoenv, FALSE, TRUE, &error_code);
  (*qoenv)->ixenv = load_indexes(*qoenv, FALSE, FALSE, &error_code);
  if (error_code) return error_code;

  warmup_indexes(*qoenv, (*qoenv)->ixenv);

  return 0;   // 0 is success
}


QBASHQ_API void NativeInitialize() {
  // Does nothing at this stage

}



QBASHQ_API void NativeDeInitialize(query_processing_environment_t **qoenv) {
  // To be filled in

}

#define MAX_QUERY_LEN 1024
#define MAX_OS_RESULT_LEN 1024

#define RESPONSE_VERSION_STRING     L"QbasherVersion:1"
#define RESPONSE_LINEFORMAT_SII     L"%s\t%d\t%d\n" // Version\tErrorStatus\thowmanyresults
#define RESPONSE_LINEFORMAT_SF      L"%s\t%f\n"     // String\tConfidence

QBASHQ_API int NativeExecuteQueryAsync(LPWSTR pQuery, query_processing_environment_t *qoenv, PFN_ISSUERESPONSE issueResponse) {
  //extern "C" QBASHQ_API int NativeExecuteQueryAsync(query_processing_environment_t *qoenv, query_processing_environment_t *qoenv2, PFN_ISSUERESPONSE issueResponse) {
  // Assume pQuery is null-terminated  (but is it?)
  int how_many_results = 0, r;
  u_char qstring[MAX_QUERY_LEN + 1];   
  double *corresponding_scores = NULL;
  u_char **returned_results = NULL;
  wchar_t *wresult;
  int wresult_buflen;
  int wresult_index = 0;
  BOOL error = FALSE, timed_out;

  r = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, pQuery, -1, (char *)qstring, MAX_QUERY_LEN, NULL, NULL);
  if (r == 0) {
    return(-230080);
  }


  if (0) {
    printf("In NativeExecuteQueryAsync\n");
    fflush(stdout);
    //wprintf(L"Input query is %s\n", pQuery);
    fflush(stdout);
    printf("Qstring is %s\n", qstring);
    fflush(stdout);
  }


  how_many_results = handle_multi_query(qoenv->ixenv, qoenv, qstring, &returned_results, &corresponding_scores, &timed_out);

  if (how_many_results > 0) {
    wresult_buflen = (how_many_results + 1)*(MAX_RESULT_LEN + 1);
    wresult = (wchar_t*)malloc(wresult_buflen);

    if (wresult != NULL) {
      int written = swprintf(wresult + wresult_index, wresult_buflen - wresult_index, RESPONSE_LINEFORMAT_SII, RESPONSE_VERSION_STRING, 0, how_many_results);

      if (written >= 0) {
	wresult_index += written;
      }
      for (r = 0; r < how_many_results; r++) {
	wchar_t result[MAX_OS_RESULT_LEN];

	MultiByteToWideChar(CP_UTF8, 0, (char *)returned_results[r], -1, result, MAX_OS_RESULT_LEN);

	written = swprintf(wresult + wresult_index, wresult_buflen - wresult_index, RESPONSE_LINEFORMAT_SF, result, corresponding_scores[r]);

	if (written >= 0) {
	  wresult_index += written;
	}

      }
      issueResponse(wresult);
      free(wresult);
    }
    else {
      error = TRUE;
    }
  }

  if (error || how_many_results < 0) {
    wchar_t result[MAX_OS_RESULT_LEN];

    swprintf(result, MAX_OS_RESULT_LEN, RESPONSE_LINEFORMAT_SII, RESPONSE_VERSION_STRING, how_many_results, 0);

    issueResponse(result);
  }
  else if (how_many_results == 0) {
    wchar_t result[MAX_OS_RESULT_LEN];

    swprintf(result, MAX_OS_RESULT_LEN, RESPONSE_LINEFORMAT_SII, RESPONSE_VERSION_STRING, 0, 0);

    issueResponse(result);
  }
  for (r = 0; r < how_many_results; r++)  if (returned_results[r] != NULL) free(returned_results[r]);

  if (returned_results != NULL) free(returned_results);
  if (corresponding_scores != NULL) free(corresponding_scores);

  return 0;
}


#endif





// *********************************************************************************************************************//
//                                                                                                                      //
// End   API Definitions                                                                                                //
//                                                                                                                      //
// *********************************************************************************************************************//







