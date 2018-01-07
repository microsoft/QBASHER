// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#define _POSIX_C_SOURCE 200809L  // To get access to fileno() in gcc while using std=c11

#ifdef WIN64
#include <windows.h>
#include <WinBase.h>
#include <strsafe.h>
#include <Psapi.h>  // Windows Process State API
#else
#include <errno.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h> 
#include <fcntl.h>
#include <time.h>
#include <math.h>

#include "../shared/unicode.h"
#include "../shared/QBASHER_common_definitions.h"
#include "../shared/utility_nodeps.h"
#include "../utils/dahash.h"
#include "Write_Inverted_File.h"
#include "arg_parser.h"
#include "input_buffer_management.h"
#include "../utils/latlong.h"
#include "QBASHI.h"
#include "../utils/linked_list.h"

static double earth_radius = 6371.0;  // Km

static int MAX_LINE = MAX_DOCBYTES_NORMAL;    // Will be increased by -x_bigger_trigger..
                                              // Only used in file order indexing
#define DFLT_MAX_DOCS 127000000000   // 100 billion +


// QBASHI Builds an inverted file index for searching and auto-suggesting.  It takes as input
// a .forward file in TSV format.  


// -------------------------- Forward index format ----------------------------
//
// The .forward file contains at least two columns.  The first contains the text to be
// indexed (the "trigger"), and the second contains a numeric static weight.   Other columns 
// may be present
//
// QBASHI accesses only the trigger and the weight fields. 
// QBASHQ also accesses the trigger (to extract textual features like 'phrase match'
// and the weights (for scoring).    It can also be told to display a value from a
// column other than the trigger.
//
// 
// -----------------------------------------------------------------------------


// Note the use of Windows-specific functions for writing to non-stream files:
// CreateFile(), CloseFile(), WriteFile().  
// e.g. Doco for WriteFile() is at http://msdn.microsoft.com/en-us/library/windows/desktop/aa365747(v=vs.85).aspx


CROSS_PLATFORM_FILE_HANDLE forward_handle, dt_handle;  // Make global so error handlers can close.
static dahash_table_t *word_table = NULL;

// Define the masks and shifts to enable extraction of the fields from a .doctable entry.
// the fields are:  word count, document offset in .forward, document static score, and
// document signature derived from the first letters of words.
// - Shifts and masks calculated from the DTE_*_BITS definitions in QBASHI.h
// - They are set up in calculate_dte_shifts_and_masks() below
// - there are two masks for each field, one extracts the bits where they are in the entry,
//   the other (MASK2) assumes that they have been shifted to the least significant end.

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

// Symbols defining whether synthetic terms are being generated or not.
#define TERM_GEN_OFF 0
#define TERM_GEN_ZIPF 1

// Variables settable from the command line
u_int SB_POSTINGS_PER_RUN = 0;   // How many postings per skip block run.  Can't exceed SB_MAX_COUNT.  Zero means set dynamically
u_int SB_TRIGGER = 500;            // If there are more than this number of postings and it's > 0, skip blocks will be inserted.
docnum_t x_max_docs = DFLT_MAX_DOCS;   // QBASHI can be configured to stop after x_max_docs records.  This 
double max_forward_GB;
docnum_t doccount = 0, ignored_docs = 0, truncated_docs = 0, incompletely_indexed_docs = 0, empty_docs = 0;
u_ll tot_postings = 0, vocab_size, chunks_allocated = 0,	*doc_length_histo = NULL;
double max_raw_score = UNDEFINED_DOUBLE, log_max_score = UNDEFINED_DOUBLE, score_threshold = 0.0,
  msec_elapsed_list_building, msec_elapsed_list_traversal, *head_term_cumprobs = NULL,
  hashtable_MB, linkedlists_MB;
u_int min_wds = 0, max_wds = 0;   // You can tell QBASHI to index only records with lengths in this range.
u_int max_line_prefix = 0, // This turns on and controls the line prefix indexing mechanism.
                           // (Autosuggest when there are no full words)
  max_line_prefix_postings = 100;  

// The following group of declarations correspond to options which are regarded as experimental.  I.e, the 
// non-experimental values are set as defaults and the corresponding x_<blah> option can be used to 
// set an experimental non-default value of <blah>.  Experimental options are disabled in arg_parser.cpp 
// when QBASHER_LITE is defined.
BOOL x_use_vbyte_in_chunks = TRUE, x_bigger_trigger = FALSE, x_doc_length_histo = FALSE, x_2postings_in_vocab = TRUE;
u_int x_min_payloads_per_chunk = 0;
//int x_sort_postings_instead = 0;
int x_hashbits = 0, x_hashprobe = 0, x_chunk_func = 102, x_cpu_affinity = -1;
double x_geo_tile_width = 0;
int x_geo_big_tile_factor = 1;
BOOL x_use_large_pages = FALSE, x_fileorder_use_mmap = FALSE, x_minimize_io = FALSE;


#ifdef WIN64
size_t large_page_minimum;
DWORD pfc_list_build_start, pfc_list_build_end, pfc_list_scan_start, pfc_list_scan_end;  // Page fault counts
#endif


int debug = 0, MAX_WDS_INDEXED_PER_DOC = MAX_WDPOS + 1;
u_char *index_dir = NULL, *fname_if = NULL, *fname_doctable = NULL, *fname_vocab = NULL, 
  *fname_forward = NULL, *fname_dlh = NULL, *language = NULL, *other_token_breakers = NULL,
  *token_break_set = NULL;
BOOL sort_records_by_weight = TRUE, unicode_case_fold = TRUE, conflate_accents = FALSE,
  expect_cp1252 = TRUE, this_trigger_was_truncated = FALSE;

// Next two are items for when we sort postings rather than build linked lists (not fully implemented)
byte *postings_accumulator_for_sort;
u_ll postings_accumulated;



doh_t ll_heap;     // Note that ll_heap is nothing to do with min_heaps or max_heaps.  It's a reference to 
// a program-managed memory heap in which linked_list blocks are allocated.   Replacing 
// millions of very small malloc()s with a small number of big ones saved huge amounts of 
// time and reduced memory overhead.  A pointer to ll_heap is passed to all the functions
// which need to access linked lists.
size_t num_doh_blocks;   // DOH = Dave's Own Heap


static void print_usage();


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions to pack and unpack the parts of the value part of a vocabulary hash table entry                           //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int ve_unpack4552(byte *vep, u_int *count, u_ll *p1, u_ll *p2, u_short *us) {
  // Extract one 4-byte, two 5-byte and a two-byte quantity from the 16-byte vocab entry value pointed to by vep
  u_ll *ullp = (u_ll *)vep, tmp1, tmp2;
  // *count = *((u_int *)vep);   // Doesn't work cos of little-endianness
  *count = *ullp >> 32;  // Get 4 left most bytes
  tmp1 = (*ullp & 0xFFFFFFFF) << 8;   // Get the remaining four bytes from the first u_ll and shift left one byte
  ullp++;
  *us = *ullp & 0xFFFF;   // Get the least significant two bytes 
  tmp2 = (*ullp & 0xFFFFFFFFFFFF0000) >> 16;  // Get rightmost six bytes out of the second u_ll and shift right two bytes.
  *p2 = tmp2 & 0xFFFFFFFFFF;   // Second pointer is the rightmost five bytes
  tmp2 >>= 40;  // Shift those bytes out of the road and we're left with the rightmost byte of p1
  *p1 = tmp1 | tmp2;  // Or it with the other four bytes previously shifted to make room.
  return 0;
}


int ve_unpack466(byte *vep, u_int *count, u_ll *p1, u_ll *p2) {
  // Extract one 4-byte and two 6-byte quantities from the 16-byte vocab entry value pointed to by vep
  u_ll *ullp = (u_ll *)vep, tmp1, tmp2;
  //*count = *((u_int *)vep); // Doesn't work cos of little-endianness
  *count = *ullp >> 32;  // Get 4 left most bytes
  tmp1 = (*ullp & 0xFFFFFFFF) << 16;   // Get the remaining four bytes from the first u_ll and shift left two bytes
  ullp++;
  tmp2 = *ullp;  
  *p2 = tmp2 & 0xFFFFFFFFFFFF;   // Second pointer is the rightmost six bytes
  tmp2 >>= 48;  // Shift those bytes out of the road and we're left with the rightmost 2 bytes of p1
  *p1 = tmp1 | tmp2;  // Or it with the other four bytes previously shifted to make room.
  return 0;
}


int ve_pack4552(byte *vep, u_int count, u_ll p1, u_ll p2, u_short us){
  // Store one 4-byte, two 5-byte and one 2-byte quantities into the 16-byte vocab entry value pointed to by vep
  u_ll t1, t2, t3, *ullp = (u_ll *)vep;
  t1 = (u_ll)count << 32;  // Put the count in the leftmost 4 bytes of t1
  t2 = (p1 & 0xFF) << 56;  // Get the right most byte from p1 and shift to the leftmost byte
  t3 = (p1 & 0xFFFFFFFFFF) >> 8;  // Get the leftmost 4 bytes out of the rightmost 5;
  *ullp = t1 | t3;
  ullp++;
  t1 = (p2 & 0xFFFFFFFFFF) << 16;  // Get the rightmost 5 bytes out of p2 and shift left leaving room for the remaining byte of p1
  *ullp = t1 | t2 | (u_ll)us;
  return 0;
}


int ve_pack455x(byte *vep, u_int count, u_ll p1, u_ll p2) {
  // Store one 4-byte, and two 5-byte into the 16-byte vocab entry value pointed to by vep, while leaving the two 
  // byte chunk count as it was.
  u_ll t1, t2, t3, *ullp = (u_ll *)vep;
  u_short us ;
  us = ve_get_chunk_count(vep);
  t1 = (u_ll)count << 32;  // Put the count in the leftmost 4 bytes of t1
  t2 = (p1 & 0xFF) << 56;  // Get the right most byte from p1 and shift to the leftmost byte
  t3 = (p1 & 0xFFFFFFFFFF) >> 8;  // Get the leftmost 4 bytes out of the rightmost 5;
  *ullp = t1 | t3;
  ullp++;
  t1 = (p2 & 0xFFFFFFFFFF) << 16;  // Get the rightmost 5 bytes out of p2 and shift left leaving room for the remaining byte of p1
  *ullp = t1 | t2 | (u_ll)us;
  return 0;
}


int ve_pack466(byte *vep, u_int count, u_ll p1, u_ll p2){
  // Store one 4-byte and two 5-byte quantities into the 16-byte vocab entry value pointed to by vep
  u_ll t1, t2, t3, *ullp = (u_ll *)vep;
  t1 = (u_ll)count << 32;  // Put the count in the leftmost 4 bytes of t1
  t2 = (p1 & 0xFFFF) << 48;  // Get the right most 2 bytes from p1 and shift as far as possible to the right
  t3 = (p1 & 0xFFFFFFFFFFFF) >> 16;  // Get the leftmost 4 bytes out of the rightmost 6;
  *ullp = t1 | t3;
  ullp++;
  t1 = (p2 & 0xFFFFFFFFFFFF) ;  // Get the rightmost 6 bytes out of p2, leaving room for the remaining 2 bytes of p1
  *ullp = t1 | t2;
  return 0;
}


void test_ve_pup() {  // Test of the pack and unpack functions for vocab entries
  u_ll p1, p2, *ulp1, *ulp2;
  u_int c1;
  u_short us;
  byte bytes[16];

  // Tests of the 4byte, 5byte, 5byte packing
  ve_pack4552(bytes, 0xFFFFFFFF, 0ULL, 0xFFFFFFFFFFULL, 0);
  ve_unpack4552(bytes, &c1, &p1, &p2, &us);
  ulp1 = (u_ll *)bytes;
  ulp2 = ulp1 + 1;
  if (c1 != 0xFFFFFFFF || p1 != 0ULL || p2 != 0xFFFFFFFFFFULL || us != 0) {
    printf("The two unsigned long longs are: %llx, %llx\n", *ulp1, *ulp2);
    printf("c1 = %x, p1 = %llx, p2 = %llx, us = %x\n", c1, p1, p2, us);
    error_exit("test_ve_pup(hex) failed\n");
  }

  c1 = ve_get_count(bytes);
  if (c1 != 0xFFFFFFFF) {
    printf("ve_get_count returned %x.  It should have returned %x\n", c1, 0xFFFFFFFF);
    printf("The two unsigned long longs are: %llx, %llx\n", *ulp1, *ulp2);
    printf("c1 = %x, p1 = %llx, p2 = %llx, us = %x\n", c1, p1, p2, us);
    error_exit("test_ve_pup(ve_get_count()) failed\n");
  }

  us = ve_get_chunk_count(bytes);
  if (us != 0) {
    printf("ve_get_chunk_count returned %x.  It should have returned %x\n", us, 0);
    printf("The two unsigned long longs are: %llx, %llx\n", *ulp1, *ulp2);
    error_exit("test_ve_pup(ve_get_chunk_count()) failed\n");
  }

  ve_store_count(bytes, 0x12407777)
    c1 = ve_get_count(bytes);
  if (c1 != 0x12407777) {
    printf("ve_get_count returned %x.  It should have returned %x\n", c1, 0x12407777);
    printf("The two unsigned long longs are: %llx, %llx\n", *ulp1, *ulp2);
    error_exit("test_ve_pup(ve_get_count(2)) failed\n");
  }
  ve_increment_count(bytes);
  c1 = ve_get_count(bytes);
  if (c1 != 0x12407778) {
    printf("ve_get_count returned %x.  It should have returned %x\n", c1, 0x12407778);
    printf("The two unsigned long longs are: %llx, %llx\n", *ulp1, *ulp2);
    error_exit("test_ve_pup(ve_get_count(2)) failed\n");
  }

  ve_store_chunk_count(bytes, 0x1477)
    us = ve_get_chunk_count(bytes);
  if (us != 0x1477) {
    printf("ve_get_chunk_count returned %x.  It should have returned %x\n", us, 0x1477);
    printf("The two unsigned long longs are: %llx, %llx\n", *ulp1, *ulp2);
    error_exit("test_ve_pup(ve_get_chunk_count(2)) failed\n");
  }

  ve_increment_chunk_count(bytes);
  us = ve_get_chunk_count(bytes);
  if (us != 0x1478) {
    printf("ve_get_chunk_count returned %x.  It should have returned %x\n", us, 0x1478);
    printf("The two unsigned long longs are: %llx, %llx\n", *ulp1, *ulp2);
    error_exit("test_ve_pup(ve_get_chunk_count(2)) failed\n");
  }

  ve_pack455x(bytes, 98, 99, 100);
  us = ve_get_chunk_count(bytes);
  if (us != 0x1478) {
    printf("ve_get_chunk_count has changed to %x after ve_pack455x.  It should have remained as %x\n", us, 0x1478);
    printf("The two unsigned long longs are: %llx, %llx\n", *ulp1, *ulp2);
    error_exit("test_ve_pup(ve_get_chunk_count(2)) failed\n");
  }

  ve_pack4552(bytes, 3, 1ULL, 4ULL,0XFFFF);
  ve_unpack4552(bytes, &c1, &p1, &p2, &us);
  ulp1 = (u_ll *)bytes;
  ulp2 = ulp1 + 1;
  if (c1 != 3 || p1 != 1ULL || p2 != 4ULL || us != 0xFFFF) {
    printf("The two unsigned long longs are: %llx, %llx\n", *ulp1, *ulp2);
    printf("c1 = %u, p1 = %llu, p2 = %llu\n", c1, p1, p2);
    error_exit("test_ve_pup(3) failed\n");
  }

  ve_pack4552(bytes, 390990999, 1234567890ULL, 444044404440ULL, 4990);
  ve_unpack4552(bytes, &c1, &p1, &p2, &us);
  ulp1 = (u_ll *)bytes;
  ulp2 = ulp1 + 1;
  if (c1 != 390990999 || p1 != 1234567890ULL || p2 != 444044404440ULL || us != 4990) {
    printf("The two unsigned long longs are: %llx, %llx\n", *ulp1, *ulp2);
    printf("c1 = %u, p1 = %llu, p2 = %llu\n", c1, p1, p2);
    error_exit("test_ve_pup(399999999) failed\n");
  }

  // Tests of the 4byte, 6byte, 6byte packing
  ve_pack466(bytes, 0xFFFFFFFF, 0ULL, 0xFFFFFFFFFFULL);
  ve_unpack466(bytes, &c1, &p1, &p2);
  ulp1 = (u_ll *)bytes;
  ulp2 = ulp1 + 1;
  if (c1 != 0xFFFFFFFF || p1 != 0ULL || p2 != 0xFFFFFFFFFFULL) {
    ulp1 = (u_ll *)bytes;
    ulp2 = ulp1 + 1;
    printf("c1 = %u, p1 = %llu, p2 = %llu\n", c1, p1, p2);
    printf("The two unsigned long longs are: %llx, %llx\n", *ulp1, *ulp2);
    error_exit("test_ve_pup(hex) failed\n");
  }

  ve_pack466(bytes, 3, 1ULL, 4ULL);
  ve_unpack466(bytes, &c1, &p1, &p2);
  ulp1 = (u_ll *)bytes;
  ulp2 = ulp1 + 1;
  if (c1 != 3 || p1 != 1ULL || p2 != 4ULL) {
    printf("c1 = %u, p1 = %llu, p2 = %llu\n", c1, p1, p2);
    printf("The two unsigned long longs are: %llx, %llx\n", *ulp1, *ulp2);
    error_exit("test_ve_pup(3) failed\n");
  }

  ve_pack466(bytes, 390990999, 1234567890ULL, 444044404440ULL);
  ve_unpack466(bytes, &c1, &p1, &p2);
  ulp1 = (u_ll *)bytes;
  ulp2 = ulp1 + 1;
  if (c1 != 390990999 || p1 != 1234567890ULL || p2 != 444044404440ULL) {
    printf("c1 = %u, p1 = %llu, p2 = %llu\n", c1, p1, p2);
    printf("The two unsigned long longs are: %llx, %llx\n", *ulp1, *ulp2);
    error_exit("test_ve_pup(399999999) failed\n");
  }

  printf("Test of vocabulary entry pack and unpack passed for both 4552 and 466.\n");
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions for quantizing scores for storage in a doctable entry                                                     //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This function allows the representation of an approximate version of a score
// in a limited number of bits.  The score is converted to a ratio of logs in order
// to maximize accuracy.
static u_int quantize_log_score_ratio(double score, double log_max_score) {
  // Take the log of freq, divide it by the log of the max frequency expected
  // Force the result into the range 0 - 1, then scale it up so that 1.0 corresponds
  // to all ones in the doctable score field.  Finally mask the result
  // so it can be shifted and  ORed into a doctable field.
  double lograt, dmv = (double)DTE_DOCSCORE_MASK2;
  u_int rslt;

  if (log_max_score == UNDEFINED_DOUBLE) {
    // Should only happen in the file_order indexing case
    if (max_raw_score == UNDEFINED_DOUBLE) {
      max_raw_score = score;
      if (max_raw_score <= 0.01) max_raw_score = 1.0;
    }
    if (max_raw_score <= 1) log_max_score = 1.0;
    else log_max_score = log(max_raw_score);
  }
  if (score <= 0 || log_max_score < 1.0) return 0;
  lograt = log(score + 1) / log_max_score;    
  if (lograt > 1.0) lograt = 1.0;
  lograt *= dmv;
  rslt = (u_int) lograt;
  if (rslt >(u_int)DTE_DOCSCORE_MASK2) rslt = (u_int)DTE_DOCSCORE_MASK2;
  rslt &= (u_int)DTE_DOCSCORE_MASK2;
  return rslt;
}

#if 0
// Similar to the above function, but without logs  --- no longer used
static u_int quantize_raw_score(double raw_score) {
  // raw_score is assumed to be in the range 0 - max_raw_score
  // Force the result into the range 0 - 1, then scale it up so that 1.0 corresponds
  // to all ones in the doctable score field.  Finally mask the r
  // so it can be shifted and  ORed into a doctable field.
  double dmv = (double)DTE_DOCSCORE_MASK2; 
  u_int rslt;

  if (max_raw_score == UNDEFINED_DOUBLE) {
    max_raw_score = raw_score;
    if (max_raw_score <= 0.01) max_raw_score = 1.0;
  }

  if (raw_score < 0 ) return 0;
  if (raw_score > max_raw_score) raw_score = max_raw_score;
  rslt = (u_int) (raw_score * dmv / max_raw_score);
  if (rslt > (u_int)DTE_DOCSCORE_MASK2) rslt = (u_int)DTE_DOCSCORE_MASK2;
  rslt &= (u_int)DTE_DOCSCORE_MASK2;
  return rslt;
}
#endif

// Function to extract a score from a doctable entry  (no logs)
double get_score_from_dtent(unsigned long long dte) {
  unsigned long long t;
  double r;
  t = (dte & DTE_DOCSCORE_MASK) >> DTE_DOCSCORE_SHIFT;
  r = (double)t / (double)DTE_DOCSCORE_MASK2;
  return r;
}


static void test_quantize_log_score_ratio() {
  u_int r;
  double d, shouldbe, logmax = log((double)1000000.0), ratio;
  int errors = 0;

  if ((r = quantize_log_score_ratio(0, 5)) != 0) {
    printf("   Answer should be 0 but is %x\n", r);
    errors++;
  }
  if ((r = quantize_log_score_ratio(5000000, 1.1)) != DTE_DOCSCORE_MASK2) {
    printf("   Answer should be %x but is %x\n", (u_int)DTE_DOCSCORE_MASK2, r);
    errors++;
  }
  // All one bits should be set if freq == max_score
  if ((r = quantize_log_score_ratio(500000, log((double)500000))) != DTE_DOCSCORE_MASK2) {
    printf("   Answer should be %x but is %x\n", (u_int)DTE_DOCSCORE_MASK2, r);
    errors++;
  }

  for (d = 1; d < 1000000; d += 5000) {
    shouldbe = log(d + 1) / logmax;
    ratio = get_score_from_dtent((u_ll)quantize_log_score_ratio(d, logmax) << DTE_DOCSCORE_SHIFT) / shouldbe;
    if (ratio > 1.05 || ratio < 0.95) {
      printf("   d=%.1f.  Ratio is %.4f, should be closer to 1.000\n", d, ratio);
      errors++;
    }

  }
  if (errors) {
    error_exit("Test_quantize_log_score_ratio() failed.");      // OK - testing on startup
  }
  printf("Test_quantize_log_score_ratio() passed.\n");
}



// Test Bloom filter signature calculation
static void test_signature_calculation() {  
  unsigned long long r;
  int verbose = 0;
  r = calculate_signature_from_first_letters((u_char *)"  simon wilson-townsend  ", 32);
  if (verbose) printf("32 bit signature of '%s' is %llX\n", "  simon wilson-townsend  ", r);
  if (r != 0X980000) error_exit("signature test 0 failed");     // - OK testing
  r = calculate_signature_from_first_letters((u_char *)"an @ ", 32);
  if (verbose) printf("32 bit signature of '%s' is %llX\n", "an @ ", r);
  if (r != 0X2) error_exit("signature test 1 failed");    // - OK testing
  r = calculate_signature_from_first_letters((u_char *)"...Simon Wilson-TOWNSEND", 32);
  if (verbose) printf("32 bit signature of '%s' is %llX\n", "...Simon Wilson-TOWNSEND", r);
  if (r != 0X980000) error_exit("signature test 2 failed");    // - OK testing
  r = calculate_signature_from_first_letters((u_char *)"okra water for diabetes", 16);
  if (verbose) printf("16 bit signature of '%s' is %llX\n", "okra water for diabetes", r);
  if (r != 0X80D0) error_exit("signature test 2 failed");    // - OK testing
  printf("Test of signature_calculation() passed.\n");
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions for calculating shifts and masks for accessing Doctable entries  (DTE)                                    //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void calculate_dte_shifts_and_masks() {
  // Derive all the Doctable Entry shifts and masks from the DTE _BITS definitions in QBASHI.h
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
  max_forward_GB = (double)(1ULL << DTE_DOCOFF_BITS) / 1024 / 1024 / 1024;
  printf("Doctable field widths: wdcount = %d, doc offset = %d, score = %d, coarse Bloom = %d\n",
	 DTE_WDCNT_BITS, DTE_DOCOFF_BITS, DTE_SCORE_BITS, DTE_BLOOM_BITS);
}

static void test_shifts_and_masks() {
  // Check that the DTE_ definitions are consistent.  It's quite hard to tell
  // the difference between 9 consecutive Fs and 10 in the #defines :-)
  int l1 = count_one_bits_ull(DTE_WDCNT_MASK),
    l2 = count_one_bits_ull(DTE_DOCOFF_MASK),
    l3 = count_one_bits_ull(DTE_DOCSCORE_MASK),
    l4 = count_one_bits_ull(DTE_DOCBLOOM_MASK),
    L1 = count_one_bits_ull(DTE_WDCNT_MASK),
    L2 = count_one_bits_ull(DTE_DOCOFF_MASK),
    L3 = count_one_bits_ull(DTE_DOCSCORE_MASK),
    L4 = count_one_bits_ull(DTE_DOCBLOOM_MASK),
    totbits = DTE_LENGTH * 8;
		
  if (totbits != 64)
    error_exit("Doctable width is not 8 bytes");   // OK - on startup

  printf("l1 = %d, l2 = %d, l3 = %d, l4 = %d, sum = %d\n", l1, l2, l3, l4, l1 + l2 + l3 + l4);

  if (l1 != L1 || l2 != L2 || l3 != L3 || l4 != L4)
    error_exit("Doctable MASK and MASK2 don't match.");   // OK - on startup
  if ((l1 + l2 + l3 + l4) != totbits)
    error_exit("Doctable fields don't add to totbits.");   // OK - on startup
  if (DTE_DOCOFF_SHIFT != l1)
    error_exit("DOCOFF_SHIFT");   // OK - on startup
  if (DTE_DOCSCORE_SHIFT != (l1 + l2))
    error_exit("DOCSCORE_SHIFT");   // OK - on startup
  if (DTE_DOCBLOOM_SHIFT != (l1 + l2 + l3))
    error_exit("DOCBLOOM_SHIFT");   // OK - on startup
}



//-----------------------------------------------------------------------------
// The next two show_xxx() functions are passed as parameters to 
// dahash_dump_alphabetic() in order to show the contents of the vocab/postings
// hash table for debugging purposes.
//-----------------------------------------------------------------------------


static void show_key(const void *keyptr) {
  u_char *key = (u_char *)keyptr;
  printf("%s - ", key);
};


static void show_count_n_postings(const void *valptr, doh_t ll_heap) {
  byte *vep = (byte *)valptr;
  posting_p letter = NULL, headptr = NULL, nextptr = NULL;
  docnum_t docnum;
  int wdnum;
  u_ll head, tail;
  u_int count;
  ve_unpack466(vep, &count, &head, &tail);
  letter = doh_get_pointer(ll_heap, head);
  printf("[%d] ", count);
  // Now show the postings
  letter = headptr;
  while (letter != NULL) {
    unpack_posting(ll_heap, letter, &docnum, &wdnum, &nextptr, 1);
    printf("(%lld,%d)", docnum, wdnum);
    letter = nextptr;
  }
  printf("\n");
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions for accessing records in QBASH.forward and indexing the trigger                                           //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void process_a_word_internal(u_char *wd, docnum_t doccount, u_int wdpos, u_ll *max_plist_len, 
		    doh_t ll_heap) {
  // Look up wd in the vocabulary hash, inserting if not already there. Update 
  // occurrence count, add a posting and boost max_plist_len if appropriate
  // All the information needed to build the .vocab and .if files is accumulated
  // in memory until the input is all consumed, then those files are written.
  // The in-memory representation is a hash table keyed by words in the vocabulary,
  // in which the values are postings lists represented by single-linked lists, 
  // referenced by head and tail pointers.  (The tail pointer is essential to 
  // fast appending to long lists.)  Relevant linked list definitions are in 
  // linked_list.cpp and linked_list.h
  //
  // Note about wdpos:  wdpos represents the position of this word in the record, numbered.
  // from zero.  It is incremented by the code which calls this function, and, depending upon
  // the setting of x_bigger_trigger, may reach values as high as 100,000.  HOWEVER,
  // since only one byte is allocated for wdpos in the postings lists, and the value
  // 255 is used to introduce a skip block, any value > 254 which is passed in is
  // locally treated as 254.

  vocab_entry_p vep;
  u_int count;

  if (wdpos > MAX_WDPOS) wdpos = MAX_WDPOS;  // Make sure the wdpos written into postings never exceeds 254

  // ASCII lower casing
  if (unicode_case_fold) utf8_lower_case(wd);  // This function returns length but we ignore it.

  vep = (vocab_entry_p)dahash_lookup(word_table, (byte *)wd, 1);  // Returns a pointer to the value part of the entry.
  if (vep != NULL) {
    // NOTE: Memory in the hash table is zeroed when created
    count = ve_get_count(vep);   // ve_get_count works whether the hash table entry is organized 4,6,6 or 4,5,5,2.  This relies on that
    if (wd[0] == '>' && count >= max_line_prefix_postings) return;
#if 0
    if (x_sort_postings_instead > 0) {
      u_int termid;
      byte *entryp;
      u_ll *entryullp, ull;
      u_short *entryshortp;
      vep -= word_table->key_size;  // In this mode, we want this to point to the key rather than the value

      if (postings_accumulator_for_sort == NULL) {
	// The value of x_sort_postings_instead gives the number of million entries in an array of termid, docnum, wordpos postings
	postings_accumulator_for_sort = (byte *)malloc((size_t)x_sort_postings_instead * 1000000 * SORTABLE_POSTING_SIZE);
	if (postings_accumulator_for_sort == NULL) error_exit("Malloc failed for postings_accumulator_for_sort");
      }
      else if (postings_accumulated > x_sort_postings_instead * 1000000) {
	error_exit("Array postings_accumulator_for_sort has overflowed.");
      }
      termid = (u_int)((vep - (vocab_entry_p)(word_table->table))/word_table->entry_size);   // Termid is just entry number in vocab

      if (0) printf("PAW(%s) - termid is %u.  HT contains (%s)\n", wd, termid, vep);
      entryp = postings_accumulator_for_sort + postings_accumulated * SORTABLE_POSTING_SIZE;
      // The entry is written as a u_ll containing 4byte u-int termid and leading 4 bytes of docnum, followed by
      // a u_short containing the last byte of the docnum and the one-byte wordpos
      ull = termid;
      ull <<= 32;
      ull |= (doccount>> 8);
      entryullp = (u_ll *)entryp;
      *entryullp = ull;
      entryshortp = (u_short *)(entryullp + 1);
      *entryshortp = ((u_short)(doccount & 0xFF) << 8) | (wdpos);
      postings_accumulated++;
    } else
#endif
      {
	if (x_2postings_in_vocab  && count < 3) {
	  // In this mode we store up to two postings in the vocabulary entry and copy them out again if we 
	  // get a third.
	  u_ll dnwp1 = 0, dnwp2 = 0;  // Pach the docno and wordpos into a single u_ll for packing into the vocab entry
	  if (count == 2) {
	    // Here we extract the two postings from the vocab entry, split them up into docno and wdpos,
	    // set VEP up as an empty list head, then append the first of the extracted postings, then 
	    // the second of the extracted postings, then finally the posting we just encountered.
	    docnum_t doccountt;
	    u_int wdpost;
	    if (0) printf("Count=2.  Moving postings from hash table entry to linked list.\n");
	    ve_unpack466(vep, &count, &dnwp1, &dnwp2);

	    // ---------------------------------- Changing the 466 entry to 4552 ----------------------------------
	    //
	    // (Different packings are used in the vocab entry and in the linked list.)

	    ve_pack4552(vep, 1, VEP_NULL, VEP_NULL, 0);  // Initialize the list pointers
	    doccountt = dnwp1 >> WDPOS_BITS;
	    wdpost = dnwp1 & WDPOS_MASK;
	    if (0) printf("Count=2.  About to append 1. doccountt = %llu, wdpost = %u, count = %u\n",
			  doccountt, wdpost, count);
	    append_posting(ll_heap, vep, doccountt, wdpost, wd);   // Transfer the first posting
	    doccountt = dnwp2 >> WDPOS_BITS;
	    wdpost = dnwp2 & WDPOS_MASK;
	    if (0) printf("Count=2.  About to append 2.\n");
	    ve_store_count(vep, 2);
	    append_posting(ll_heap, vep, doccountt, wdpost, wd);  // Transfer the second posting
	    // Finally, append the [third] posting we've just encountered
	    if (0) printf("Count=2.  About to append 3.\n");
	    ve_store_count(vep, 3);
	    append_posting(ll_heap, vep, doccount, wdpos, wd);  // Store the new (third) posting
	    if (0) printf("Count=2.  Done appending.\n");
	  }
	  else if (count == 1) {
	    // Pack the new (docno, wdpos) pair into a u_ll and store it in the second 6-byte 
	    // field of the vocab entry.
	    if (0) printf("Count=1.  Adding second posting.\n");
	    ve_unpack466(vep, &count, &dnwp1, &dnwp2);
	    dnwp2 = (doccount << WDPOS_BITS) | (wdpos & WDPOS_MASK);
	    ve_pack466(vep, 2, dnwp1, dnwp2);
	  }
	  else if (count == 0) {
	    // Pack the new (docno, wdpos) pair into a u_ll and store it in the first 6-byte 
	    // field of the vocab entry.
	    if (0) printf("Count=0.  Adding first posting.\n");
	    dnwp1 = (doccount << WDPOS_BITS) | (wdpos & WDPOS_MASK);
	    ve_pack466(vep, 1, dnwp1, 0);
	  }
	  return;    // ------------------------------------------------------->
	}


	// We get here, either (A) we're not ever packing postings into the vocab entry, or 
	// (B) we are doing that packing but this list is longer than 2 postings.

	if (count == 0) {   // Works because of zero initialisation
	  ve_pack4552(vep, count, VEP_NULL, VEP_NULL, 0);
	}

	count++;
	ve_store_count(vep, count);
	if (count > *max_plist_len) *max_plist_len = count;
	if (0) printf("  ------  About to append a posting for '%s', count = %d\n", wd, count);
	append_posting(ll_heap, vep, doccount, wdpos, wd);
      }
  }
  else {
    printf("dahash_lookup(%s) failed.\n", wd);
  }
}

void process_a_word(u_char *wd, docnum_t doccount, u_int wdpos, u_ll *max_plist_len, 
		    doh_t ll_heap) {
  // This fn is now a front-end to process_a_word_internal(), which allows us to
  // generate multiple variants of the same word and index them at the same word
  // position.  This structure is initially motivated by the desire to be able
  // to index accented and unaccented versions of a word.

  int accents_removed = 0, verbose = 0;
  process_a_word_internal(wd, doccount, wdpos, max_plist_len, ll_heap);  // First one first
  if (conflate_accents) {
    if (verbose) printf("Indexed '%s' at position %d\n", wd, wdpos);
    accents_removed = utf8_remove_accents(wd);
    if (accents_removed > 0) {
      process_a_word_internal(wd, doccount, wdpos, max_plist_len, ll_heap);
      if (verbose) printf("Also indexed '%s' at position %d\n", wd, wdpos);
    }
  }
}


static int process_trigger(u_char *str, docnum_t doccount, u_ll *max_plist_len,
			   doh_t ll_heap) {
  // str is assumed to be a null terminated string in which words are separated by 
  // non-token characters.   Break into words and (eventually) add to the term
  // hash.
  //
  // Return a count of words indexed
  // Code changed on 16 Mar 2017 to more closely match utf8_split_line_into_null_terminated_words()
  u_char *wdstart, *p = str, *bafter, savep;
  int wdcount = 0, non_token_bytes, verbose = 0;
  u_char line_prefix[MAX_WD_LEN + 2];   // related to max_line_prefix
  BOOL incompletely_indexed = FALSE;
  u_int unicode;

  if (verbose) printf("(%s)\n", str);

  while (*p >= ' ') { // Skip over leading non tokens
    if (*p & 0x80) {   // Using a loose defn allows conversion of CP-1252 punctuation
      unicode = utf8_getchar(p, &bafter, TRUE);
      // Assume (falsely) that all non-punctuation unicode is indexable
      if (!(unicode_ispunct(unicode) || unicode == 0xA0 || unicode == UTF8_INVALID_CHAR)) break;   // A0 is NBSP
      else p = bafter;  // Skip over all the bytes in this punk
    }
    else if (ascii_non_tokens[*p]) p++;
    else break;  // This is an indexable ASCII character.	
  }

  wdstart = p;

  if (verbose) printf("First word is '%s'\n", wdstart);

  if (max_line_prefix) {
    u_int l;
    // Indexing prefixes of the first word on a line.  Such "words" will start with a '>'
    if (max_line_prefix >= MAX_WD_LEN) max_line_prefix = MAX_WD_LEN - 1;  // Avoid nastiness resulting from bad arg value
    line_prefix[0] = '>';
    l = 1;
    while (l <= max_line_prefix) {
      if (*p & 0x80) {  // Use weak test to handle CP-1252
	unicode = utf8_getchar(p, &bafter, TRUE) & BMP_MASK;
	if (unicode_ispunct(unicode) || unicode == 0xA0 || unicode == UTF8_INVALID_CHAR) break;  // Assume (falsely) that all non-punctuation unicode is indexable
	while (p < bafter) {  // Copy multiple bytes of UTF-8 char across
	  if (l > max_line_prefix) break;   // Avoid trying to index an incomplete UTF-8 sequence
	  line_prefix[l++] = *p++;
	  if (verbose) printf("Indexing UTF-8 '%s'\n", line_prefix);
	}	  
	line_prefix[l] = 0;
	process_a_word(line_prefix, doccount, 0, max_plist_len, ll_heap);
	// wdcount++;  // Don't count invisible words!
      } else {
	if (ascii_non_tokens[*p] || *p == 0) break;  
	line_prefix[l++] = *p++;
	line_prefix[l] = 0;
	if (verbose) printf("Indexing ASCII'%s'\n", line_prefix);
	process_a_word(line_prefix, doccount, 0, max_plist_len, ll_heap);
	// wdcount++; // Don't count invisible words!
      }
    }
  }

  p = wdstart;  // Back to the first indexable character in the trigger
  
  while (*p) {
    // Outer loop over words
    non_token_bytes = 0;
    while (*p) {  // Skip over indexable characters
      if (*p & 0x80) {   // Using this test rather than a stricter one allows interception of CP-1252
	unicode = utf8_getchar(p, &bafter, TRUE);
	if (0) verbose = 1;
	// UTF8_INVALID_CHAR is usually a space
	if (unicode_ispunct(unicode) || unicode == 0xA0 || unicode == UTF8_INVALID_CHAR) {
	  non_token_bytes = (int)(bafter - p);
	  if (verbose) printf("Unicode punctuation found. %d bytes\n", non_token_bytes);
	  break;  // Position on first byte of UTF-8 punct sequence
	}
	else p = bafter;
      }
      else if (!ascii_non_tokens[*p]) p++;		
      else {
	non_token_bytes = 1;
	break;  // This is a non-indexable ASCII character.
      }
    }

    if (verbose) printf("Word found, followed by %s.  NTB = %d\n", p, non_token_bytes);

    if (non_token_bytes) {
      savep = *p;
      *p = 0;
      if (wdstart[0]) {
	process_a_word(wdstart, doccount, wdcount, max_plist_len, ll_heap);
	wdcount++;
	if (verbose) printf("INdexing '%s'\n", wdstart);
      }
      *p = savep;  // Put things back as they were
      p += non_token_bytes;

      while (*p) {
	// Skip over successive non tokens	
	if (*p & 0x80) {
	  unicode = utf8_getchar(p, &bafter, TRUE);
	  if (!(unicode_ispunct(unicode) || unicode == 0xA0 || unicode == UTF8_INVALID_CHAR)) break;  // Assume (falsely) that all non-punctuation unicode is indexable
	  else p = bafter;
	}
	else if (ascii_non_tokens[*p]) p++;
	else break;  // This is an indexable ASCII character.	
      }
      wdstart = p;
    }
    else {
      // Processing the last word in the trigger
      if (verbose) printf("   Processing last word in trigger\n");
      if (!ascii_non_tokens[wdstart[0]]) {
	process_a_word(wdstart, doccount, wdcount, max_plist_len, ll_heap);
	wdcount++;
	if (verbose) printf("INDexing '%s'\n", wdstart);
      }
      break;
    }


    if (wdcount >= MAX_WDS_INDEXED_PER_DOC) {
      // Check whether there are remaining words
      while (*p) {
	// Skip over successive non tokens	
	if (*p & 0x80) {
	  unicode = utf8_getchar(p, &bafter, TRUE);
	  if (!(unicode_ispunct(unicode) || unicode == 0xA0 || unicode == UTF8_INVALID_CHAR)) break;  // Assume (falsely) that all non-punctuation unicode is indexable
	  else p = bafter;
	}
	else if (ascii_non_tokens[*p]) p++;
	else break;  // This is an indexable ASCII character.	
      }
      if (*p) incompletely_indexed = TRUE;
      break;
    }
  }  // End of outer loop over words.

  if (this_trigger_was_truncated) incompletely_indexed = TRUE;  // this_trigger_was_truncated means length exceeded buffer
  if (incompletely_indexed) incompletely_indexed_docs++;

  tot_postings += wdcount;
  if (verbose) printf("Wdcnt = %d\n", wdcount);

  //if (0 && wdcount == 1) printf("Short rec: %d wds: '%s'\n", wdcount, str);
  if (x_doc_length_histo && index_dir != NULL  && doc_length_histo != NULL) {
    if (incompletely_indexed) doc_length_histo[MAX_WDS_INDEXED_PER_DOC + 1]++;  // Array malloced with MAX_w... + 2
    else if (wdcount > 0) doc_length_histo[wdcount]++;
  }
  return  wdcount;
} 

#define CPYBUF_SIZE MAX_DOCBYTES_BIGGER 
static u_char cpybuf[CPYBUF_SIZE + 1];  // Used in split_and_index_record() below

static double split_and_index_record(u_char *buf, docnum_t doccount, u_ll *max_plist_len, doh_t ll_heap, 
				     unsigned long long *d_signature, u_int *wds_indexed,
				     size_t *actual_trigger_length) {
  // Each input record consists of at least two tab separated fields.
  // We just process the first (trigger) and second (frequency) fields.  
  // Return the raw score as a double
  // Skip indexing if the raw score in column 2 is below the score_threshold.
  // Also calculate and return a signature based on word first letters.
  u_char *start = buf, *end = start, *p, *q;
  double score;
  int l = 0;

  if (0) {
    printf("Record to index: ");
    show_string_upto_nator(start, '\n', 0);
  }

  this_trigger_was_truncated = FALSE;

  // Scan the trigger (first column) and copy into cpybuf 
  while (*end && *end != '\t' && l < CPYBUF_SIZE) {
    cpybuf[l++] = *end;
    end++;
  }
  cpybuf[l] = 0;
  if (l >= CPYBUF_SIZE) {
    this_trigger_was_truncated = TRUE;
    // The trigger field has been truncated.  Avoid the chance of indexing a truncated word.
    l--;
    while (l >= 0 && ((cpybuf[l] & 0x80) || (!ascii_non_tokens[cpybuf[l]]))) {
      //First clause makes sure we don't go back beyond start of buffer
      //Second clause zaps out all UTF-8 bytes and all indexable bytes
      cpybuf[l] = 0;  // Null out characters from the last, presumably truncated, word
      l--;
    }
    // Now skip end forward to the tab so we can get the frequency.
    while (*end && *end != '\t') end++;
    truncated_docs++;
  }
  *actual_trigger_length = end - buf;

  // Now extract the frequency from second column
  p = end + 1;
  score = strtod((char *)p, (char **)&q);   
  if (0) {
    printf("split_and_index_record: %.3f v. %.3f: ", score, score_threshold);
    while (*p >= ' ') {
      putchar(*p);
      p++;
    }
    putchar('\n');
    p = q;
  }
  if (score < score_threshold) return score;  // Frequency too low, signal no_index
  *d_signature = calculate_signature_from_first_letters(cpybuf, (int)DTE_BLOOM_BITS);
  *wds_indexed = process_trigger(cpybuf, doccount, max_plist_len, ll_heap);
  if (*wds_indexed <= 0)
    empty_docs++;

  // Generation and indexing of special words indicating geospatial tiles
  if (x_geo_tile_width > 0) {
    double lat, lon;
    u_char *tail;
    if (0) printf("We're doing geo-tiles with tile width = %.3f!\n", x_geo_tile_width);
    // Skip to column 4 to find the latitude
    while (*p >= ' ') p++;
    if (*p == '\t') { // beginning of column 3
      p++;
      while (*p >= ' ') p++;
      if (*p == '\t') { // beginning of column 4
	p++; 
	if (0) {
	  printf("Column 4: ");
	  show_string_upto_nator(p, '\n', 0);
	}
	errno = 0;
	lat = strtod((char *)p, (char **)&q);
	if (!errno) {  // Silently ignore errors
	  p = q;
	  lon = strtod((char *)p, (char **)&q);
	  tail = q;
	  if (!errno) {
	    // Hurrah we've got lat, long, generate special words.
	    char special_words[6 * (MAX_WD_LEN + 1)], big_words[MAX_WD_LEN + 4];
	    int g, generated, wdpos = 250, nonSpaces = 0;
	    
	    if (0) printf(" -- (lat, long) = (%.3f, %.3f)\n", lat, lon);

	    // ---------------------  Standard Tiles -----------------------------
	    generated = generate_latlong_words(lat, lon, x_geo_tile_width, special_words,
					       MAX_WD_LEN, 0);
	    for (g = 0; g < generated; g++) {
	      process_a_word((u_char *)special_words + g * (MAX_WD_LEN + 1), doccount,
			     wdpos, max_plist_len, ll_heap);
	      if (0) printf("   Special word '%s' indexed at wdpos %d\n",
			    special_words + g * (MAX_WD_LEN + 1), wdpos);
	      if (g == 2) wdpos++;  // First three are lat words, 2nd three are long words	      
	    }

	    if (x_geo_big_tile_factor > 1) {

	      // ---------------------  Big Tiles -----------------------------
	      //
	      // If factor is e.g. 12 then we index e.g. 12$x88 and 12y$23 
	      generated = generate_latlong_words(lat, lon, x_geo_tile_width * x_geo_big_tile_factor,
						 special_words, MAX_WD_LEN, 0);
	      sprintf(big_words,"%03d", x_geo_big_tile_factor); // Value is limited to 100.
	      for (g = 0; g < generated; g++) {
		strcpy(big_words + 3, special_words + g * (MAX_WD_LEN + 1));  // Buffer has room
		process_a_word((u_char *)big_words, doccount,
			       wdpos, max_plist_len, ll_heap);
		if (0) printf("   Special word '%s' indexed at wdpos %d\n",
			      special_words + g * (MAX_WD_LEN + 1), wdpos);
		if (g == 2) wdpos++;  // First three are lat words, 2nd three are long words	      
	      }
	    }

	    //
	    // Special words shouldn't add to the doc length or contribute to
	    // the Bloom signature.
	    //

	    // -- Extension in 1.5.87 -- index language or other special words,
	    // -- if they follow the longitude value in column 4.  q now points
	    // -- byte after the last byte of the longitude

	    q = tail;
	    while (*q >= ' ') {
	      if (*q != ' ')  nonSpaces++;
	      q++;
	    }
	    if (nonSpaces) {
	      int w, numWords;		
	      byte *funny_word_starts[50], *tailcopy;
		
	      tailcopy = make_a_copy_of_len_bytes(tail, q - tail);
	      if (0) {
		printf("Indexing special words in '");
		show_string_upto_nator(tail, '\n', 0);
	      }
	      numWords = utf8_split_line_into_null_terminated_words(tailcopy, funny_word_starts,
								    50, MAX_WD_LEN,
								    TRUE, FALSE, FALSE, FALSE);
	      for (w = 0; w < numWords; w++) {
		process_a_word(funny_word_starts[w], doccount,
			       ++wdpos, max_plist_len, ll_heap);
		if (0) printf("   Special word '%s' indexed at wdpos %d\n",
			      funny_word_starts[w], wdpos);
	      }
	      free(tailcopy);
	    }

	      
	  }
	}
      } else {
	if (0) {
	  printf("LatLong -- Column 4 not found: ");
	  show_string_upto_nator(p, '\n', 0);
	}
      }
    } else {
      if (0) {
	printf("LatLong -- Column 3 not found: ");
	show_string_upto_nator(p, '\n', 0);
      }
    }

  }
  
  return score;
}



static int count_wds_in_trigger(u_char *str) {
  // str is assumed to be an input line in QBASHI format (with no leading spaces)
  //
  // Returns a count of words in the trigger (up to first TAB)
  // Note: an empty trigger will return a count of one.
  // Sequences of blanks are treated as a single blank
  // 
  // Changes on 08 Feb 2014:
  //  - Return 0 rather than 1 if bracketed part contains only spaces
  //  - Return -1 if an error is detected, rather than taking an error exit.
  u_char *p = str, terminator = '\t', *bafter;
  int wds = 0, state = 1;   // state 0 - scanning word, state 1 - scanning delimiters between words 
  u_int unicode;

  while (*p && *p != terminator) {
    if (state == 0) {  //scanning a word
      if (*p & 0x80) {
	unicode = utf8_getchar(p, &bafter, TRUE);
	if (unicode_ispunct(unicode)) {
	  state = 1;
	  p = bafter - 1;  // Will be incremented at end of loop
	}
	else if (ascii_non_tokens[*p]) state = 1;
      }
    }
    else {
      if (*p & 0x80) {  // scanning delimeters between words
	unicode = utf8_getchar(p, &bafter, TRUE);
	if (!unicode_ispunct(unicode)) {
	  state = 0;
	  p = bafter - 1;  // Will be incremented at end of loop
	  wds++;
	}
      }
      else if (!ascii_non_tokens[*p]) {
	state = 0;
	wds++;
      }
    }
    p++;
  }
  return wds;

}



#define DFLT_DOH_BLOCKSIZE 67108864   // that ensures allocations are 64MB which is large multiple of the 1 or 2MB Large Page size [Note: bytes not entries]

void allocate_hashtable_and_heap(docnum_t doccount_estimate) {
  // The hashtable is for storing the vocabulary and the heap provides memory storage for the linked 
  // lists representing postings lists internally.
  int hashbits;

  if (x_hashbits) hashbits = x_hashbits;   // Explicitly set
  else {
    // Set automatically
    hashbits = 20;  // Default value should be large enough for about 5 million docs
    // The next are rules of thumb to reduce the need to double hash tables.
    if (doccount_estimate > 250000000) hashbits = 25;
    if (doccount_estimate > 100000000) hashbits = 24;
    else if (doccount_estimate > 50000000) hashbits = 23;
    else if (doccount_estimate > 15000000)hashbits = 22;
    else if (doccount_estimate > 5000000) hashbits = 21;
  }

  word_table = dahash_create((u_char *)"words", hashbits, MAX_WD_LEN, VOCAB_ENTRY_SIZE, (double)0.9);
#ifdef WIN64
  report_memory_usage(stdout, (u_char *)"after creating hash table", NULL);
#endif
  num_doh_blocks = (doccount_estimate * 5000) / DFLT_DOH_BLOCKSIZE;  // A generous estimate. (Note that blocks are not allocated if not needed.)
	     
  // Allow 5000 bytes of postings per doc at maybe 13 bytes per posting (Note that 
  // space is wasted in partly allocated chunks.
  if (x_bigger_trigger) num_doh_blocks *= 20;    // Have to substantially increase the allowance when indexing whole doc.s
  if (num_doh_blocks < 1) num_doh_blocks = 1;
  ll_heap = doh_create_heap(num_doh_blocks, DFLT_DOH_BLOCKSIZE); // Each block can hold millions of  postings.
#ifdef WIN64
  report_memory_usage(stdout, (u_char *)"after initial doh creation", NULL);
#endif

}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions for processing/indexing a whole file, either in score order or in file order                              //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void process_records_in_score_order(u_char *fname_forward, CROSS_PLATFORM_FILE_HANDLE dt_handle, dahash_table_t *word_table, 
					   u_int min_wds, u_int max_wds, docnum_t *ignored_docs, docnum_t *gdoccount,
					   u_ll *gmax_plist_len, size_t *infile_size) {
  //  --- Called when processing tab separated, non-score-ordered TSV  files ---
  // 1. Memory map fname_forward
  // 2. Scan it and make an array of all the line starts and a parallel array of the scores.  (Keep track of max score.)
  // 3. Sort the two arrays so that they corrspond to descending score order.
  // 4. Then re-scan the records in that order and index them.
  //
  // Note that the sort method is a "counting sort".  See https://en.wikipedia.org/wiki/Counting_sort
  docnum_t doccount = 0;
  long long docoff;
  double score, raw_score, max_score = 0;
  u_char *forward, *last, *p, *ep, **recstarts = NULL;
  byte *dt_buf = NULL;
  size_t sighs, scanned = 0, trigger_len, dt_buf_used = 0;
  HANDLE FMH;
  CROSS_PLATFORM_FILE_HANDLE FH;
  int  error_code = 0, s;
  u_int *scores = NULL, docscore, wds = 0;   // wds in the current record
  u_ll max_plist_len = 0, igdocs = 0, *score_histo, *permute = NULL, sum = 0,
    count, r, r_wi_maxscore = 0, recs = 0, dt_ent, qwt, d_signature = 0, pr;
  double start, verystart;

  start = what_time_is_it();
  verystart = start;
	
  score_histo = (u_ll *)malloc((DTE_DOCSCORE_MASK2 + 1) * sizeof(u_ll));  // MAL0707   // Needs to be long longs cos all docs might be the same score
  if (score_histo == NULL) error_exit("malloc of score_histo failed\n");   // OK - on startup

  memset(score_histo, 0, (DTE_DOCSCORE_MASK2 + 1) * sizeof(u_ll));

  forward = (u_char *)mmap_all_of(fname_forward, &sighs, FALSE, &FH, &FMH, &error_code);
  if (error_code) {
    printf("Error: mmap_all_of(): code = %d\n", error_code);
    exit(1);
  }
  printf("Forward file mapped:  %.1fMB\n", (double)sighs / MEGA);
  *infile_size = sighs;
  last = forward + sighs;
  if (sighs > DTE_DOCOFF_MASK2) 
    printf("\n\nWarning: .forward file is > %.1fGB. Records beyond %.1fGB will not be indexed.\n\n",
	   max_forward_GB, max_forward_GB);

  p = forward;
  // First loop: Count the number of records and find the maximum score
  do {
    if (0) printf("Record %llu.  %zu out of %zu\n", recs + 1, scanned, sighs);

    while (p < last && *p != '\t') p++;  // Skip the trigger
    p++;
    score = strtod((char *)p, (char **)&ep);
    if (score > max_score) { 
      max_score = score; 
      r_wi_maxscore = recs;
    }
    p = ep;
    while (p < last && *p != '\n') p++;  // Skip to end of record.
    p++;
    scanned = p - forward;
    recs++;
  } while (p < last);

  printf("Sorted-scan first pass elapsed time %.1f sec.\n", what_time_is_it() - start);
  printf("Records scanned: %lld\nMax score: %.3f\n", recs, max_score);
  log_max_score = log(max_score);

  fflush(stdout);  // Next stage might take ages.  Make sure to show where we're up to

  // Allocate storage for scores and recstarts;
  recstarts = (u_char **)malloc((recs + 1) * sizeof(u_char *));  //  MAL100
  if (recstarts == NULL) { error_exit("Malloc of recstarts failed"); }
  recstarts[recs] = forward + sighs;   // So that record len can always be calculated by differences in recstarts

  scores = (u_int *)malloc(recs * sizeof(u_int));  // MAL101
  if (scores == NULL) { error_exit("Malloc of scores failed"); }

  // Second loop: Record the startpoints and make a histogram of quantized log_score ratios
  printf("Starting second loop.\n");
  start = what_time_is_it();
  p = forward;
  recs = 0;
  do {
    // Record start point
    recstarts[recs] = p;
    while (p < last && *p != '\t') p++;  // Skip the trigger
    p++;
    score = strtod((char *)p, (char **)&ep);
    docscore = quantize_log_score_ratio(score, log_max_score);
    score_histo[docscore]++;   // For counting sort.
    scores[recs] = (u_int)docscore;
    p = ep;
    while (p < last && *p != '\n') p++;  // Skip to end of record.
    p++;
    scanned = p - forward;
    recs++;
  } while (p < last);

#ifdef WIN64
  printf("Sorted-scan second pass elapsed time %.1f sec.\n", what_time_is_it() - start);
#endif

  printf("The record with max score is number %llu: ", r_wi_maxscore);
  show_string_upto_nator(recstarts[r_wi_maxscore], '\t', 0);

  if (max_score < 1) { 
    printf("Warning: Max value in column 2 less than 1.  Taking action to avoid negative log."); 
    log_max_score = 1;
  }
  else log_max_score = log((double)max_score);

  fflush(stdout);
  // Turn the histogram into a sort of cumulative one, where the value in
  // score_histo[r] records the sum of all the values greater than r, and 
  // indicates the position of the first occurrence of r in the final scan.
  // NOTE: we want descending order

  sum = 0;
  for (s = (int) DTE_DOCSCORE_MASK2; s >= 0;  s--) {
    count = score_histo[s];
    score_histo[s] = sum;
    sum += count;
  }

  printf("Sum = %llu, Recs = %llu\n", sum, recs);

  permute = (u_ll *)malloc(recs * sizeof(u_ll));  // MAL102
  if (permute == NULL) { error_exit("Malloc of permute failed"); }
  for (r = 0; r < recs; r++) {
    permute[r] = -1;  // To enable detection of errors.   I hope this is OK because permute[r] is unsigned
  } 
  // Third loop: Permute the recstarts array.
  start = what_time_is_it();
  for (r = 0; r < recs; r++) {
    u_int val;
    u_ll pos;
    val = scores[r];
    pos = score_histo[val];
    permute[pos] = r;
    score_histo[val]++;   // A new spot for the next occurrence of val
  }


  printf("Sorted-scan third pass elapsed time %.1f sec.\n", what_time_is_it() - start);

  fflush(stdout);
  // Just a check of accuracy:
  if (debug) {
    for (r = 0; r < 10; r++) {
      if (r >= recs) break;
      printf("%3llu, %9llu, %9u: ", r, permute[r], scores[permute[r]]);
      show_string_upto_nator(recstarts[permute[r]], '\t', 0);
    }
  }

  free((void *)scores);    // FRE101
  scores = NULL;

  // Allocate large in-memory structures based on the actual document count.
  allocate_hashtable_and_heap((docnum_t)recs);

  // Fourth loop: Do the business in permuted order
  start = what_time_is_it();
  for (r = 0; r < recs; r++) {
    if (debug >= 2) printf("indexing record %lld\n", r);
    pr = permute[r];
    p = recstarts[pr];
    if (*p < ' ') continue;   // This line is empty, will be ignored.
    if (min_wds > 0 || max_wds > 0) {
      // Only do this counting  if limits are being imposed.
      wds = count_wds_in_trigger(p);  // Returns -1 in case of error
      if (wds < min_wds || wds > max_wds) {
	igdocs++;
	continue;
      }
    } 
    docoff = recstarts[pr] - forward;
    raw_score = split_and_index_record(recstarts[pr], doccount, &max_plist_len, ll_heap, &d_signature, 
				       &wds, &trigger_len);
    if (wds > 0 && raw_score >= score_threshold) {
      // We ignore records with scores below the frequency threshold and those which have no
      // indexable words.

      if ((u_ll)docoff > DTE_DOCOFF_MASK2) {
	igdocs++;
	continue;   // ----------------------------------------------->
      }


      qwt = (u_ll)quantize_log_score_ratio((double)raw_score, (double)log_max_score);
      dt_ent = docoff << DTE_DOCOFF_SHIFT;
      if (wds > DTE_WDCNT_MASK) wds = (int)DTE_WDCNT_MASK;
      dt_ent |= (wds & DTE_WDCNT_MASK);
      dt_ent |= ((qwt & DTE_DOCSCORE_MASK2) << DTE_DOCSCORE_SHIFT);
      dt_ent |= ((d_signature & DTE_DOCBLOOM_MASK2) << DTE_DOCBLOOM_SHIFT);
      if (!x_minimize_io) buffered_write(dt_handle, &dt_buf, HUGEBUFSIZE, &dt_buf_used, (byte *)&dt_ent, sizeof(dt_ent), (char *)"doctable entry");
      doccount++;

      if (doccount % 10000 == 0) {
	printf("%11lld\n", doccount);
#ifdef WIN64
	report_memory_usage(stdout, (u_char *)"permuted scanning", NULL);
#endif
      }
    }
    else igdocs++;
  }

  printf("Sorted-scan fourth pass elapsed time %.1f sec.\n", what_time_is_it() - start);

  free((void *)permute);    // FRE102
  free((void *)recstarts);  // FRE100
  free((void *)score_histo); // FRE0707
  if (!x_minimize_io) buffered_flush(dt_handle, &dt_buf, &dt_buf_used, ".doctable", TRUE); // Frees the buffer and closes the handle
  unmmap_all_of(forward, FH, FMH, sighs);
  *gdoccount = doccount;
  *gmax_plist_len = max_plist_len;
  *ignored_docs = igdocs;

  msec_elapsed_list_building = (what_time_is_it() - verystart) * 1000.0;
  printf("Sorted-scan overall elapsed time %.1f sec.\n", msec_elapsed_list_building / 1000.0);
}



#ifdef WIN64
static u_char *get_line(buffer_queue_t *bq, u_char **linebuf, size_t linebufsize, size_t *bytes_scanned) {
  // Emulate the functionality of fgets in a way which allows the .forward 
  // file to be opened in SEQUENTIAL SCAN mode and the i/o to be done in
  // large chunks.  Note that fgets() on windows removes CRs but this 
  // function does not.
  //
  // Note also that this function relies on the presence of a buffer filling thread. See the code
  // in input_buffer_management.c
  //
  // get_line() isn't used when sorting records by weight (default behaviour).  That requires mmapping the whole file.
  // Used if x_fileorder_use_mmap=FALSE, i.e. we're not sorting the input records and not mmapping 
  // the .forward file.


  u_char *lastspot = NULL, *linebufp = NULL, *last_in_buffer = NULL;
  int b;
  size_t b_s = 0;

  if (debug) printf("get_line(%llu)\n", linebufsize);
  // Check for errors in arguments
  if (linebufsize < 3) error_exit("get_line() - linebuf is too small");   // Allow for CRLF, NUL // OK - should only happen on startup
  if (*linebuf == NULL) {
    *linebuf = (u_char *)malloc(linebufsize + 1);
    if (*linebuf == NULL) error_exit("Can't malloc linebuf"); // OK - should only happen on startup
  }

  linebufp = *linebuf;
  lastspot = *linebuf + linebufsize - 1;
  *lastspot = 0;


  // Transfer bytes from hugebuf to linebuf until we come to LF or EOF,
  // or linebuf is full.

  // Wait here until there's a full buffer for us to empty or we hit EOF

  b = bq->buf2empty;  // This is the buffer we need to look at.
  if (bq->emptyingptr == NULL || bq->emptyingptr == bq->buffers[b]) {
    // We don't need to do this on every call.
    if (0) printf("Waiting for FULL buffer\n");
    while (1) {
      acquire_lock_on_state_of(bq, (byte *)"getline A");
      //Check state of the buffer-to-empty
      if (bq->buffer_state[b] == BUF_EOF) return NULL; // EOF and linebuf is empty ---------------------->
      if (bq->buffer_state[b] == BUF_FULL) {
	if (bq->emptyingptr == NULL) bq->emptyingptr = bq->buffers[b];  // Otherwise we've already read part of this buffer
	release_lock_on_state_of(bq, (byte *)"getline A");
	break;
      }
      release_lock_on_state_of(bq, (byte *)"getline A");
      Sleep(0);  // Make sure the filler has a chance to get in
    }
    if (0) {
      printf("Got a full one:  %d: ", b);
      for (int i = 0; i < 72; i++) {
	if (bq->emptyingptr[i] == '\n') break;
	putchar(bq->emptyingptr[i]);
      }
      putchar('\n');
    }
  }

  // We will only get here if the state of the buffer to empty is BUF_FULL  (which can mean partly read.)
  // b will be set to the right buffer and bq->emptyingptr will be set correctly.
  last_in_buffer = bq->buffers[b] + bq->bytes_in_buffer[b] - 1;

  while (linebufp < lastspot) {
    // Read one byte per iteration.  Make sure b_s is incremented each time, otherwise offsets in doctable will be wrong
    if (bq->emptyingptr > last_in_buffer) {
      if (debug) printf("get_line(): Switching buffers from %d\n", b);

      acquire_lock_on_state_of(bq, (byte *)"getline B");
      bq->buffer_state[b] = BUF_EMPTY;  // Make this buffer available for filling
      bq->buf2empty = (b + 1) % bq->queue_depth;  // Move onto next buffer in the ring.
      bq->emptyingptr = bq->buffers[bq->buf2empty];

      // Wait here until there's a full buffer for us to empty or EOF
      while (1) {
	// We've always acquired the lock at the top of the loop
	// Check state of the buffer-to-empty
	b = bq->buf2empty;
	if (bq->buffer_state[b] == BUF_EOF) {
	  *linebufp = 0;
	  *bytes_scanned = b_s;
	  release_lock_on_state_of(bq, (byte *)"getline C");
	  if (linebufp > *linebuf) {
	    *linebufp = 0;  // NULL terminate the buffer.
	    if (debug) printf("get_line(): Returning '%s'\n", *linebuf);
	    *bytes_scanned = b_s;
	    return *linebuf; // EOF but we have a line ---------------------->
	  }
	  else return NULL; // EOF and linebuf is empty ---------------------->
	}
	if (bq->buffer_state[b] == BUF_FULL) {
	  if (debug) printf("State of buffer %d is FULL\n", b);
	  last_in_buffer = bq->buffers[b] + bq->bytes_in_buffer[b] - 1;
	  release_lock_on_state_of(bq, (byte *)"getline B/C");
	  // emptyingptr has already been set.
	  break;
	}

	// Nothing for us to do yet
	release_lock_on_state_of(bq, (byte *)"getline B/C");
	Sleep(0);
	acquire_lock_on_state_of(bq, (byte *)"getline C");
      }

    }

    *linebufp = *(bq->emptyingptr);
    bq->emptyingptr++;
    linebufp++;
    b_s++;
    if (*(linebufp - 1) == '\n') break;
  }
  *linebufp = 0;  // NULL terminate the buffer.
  if (debug) printf("get_line(): Returning '%s'.  Bytes scanned = %lld\n", *linebuf, b_s);
  *bytes_scanned = b_s;
  return *linebuf;     // ---------------------------->
}

#endif


static void process_records_in_file_order(u_char *fname_forward, CROSS_PLATFORM_FILE_HANDLE dt_handle,
					  dahash_table_t *word_table, docnum_t max_docs, u_int min_wds,
					  u_int max_wds, docnum_t *ignored_docs, docnum_t *gdoccount,
					  u_ll *gmax_plist_len, size_t *infile_size) {
  //  --- Called when processing tab separated, frequency-ordered or frequency-lacking TSV  files ---
  // 1. Memory map fname_forward  OR open for reading using get_line() (above).....  
  // 2. Scan the records in file order and index them.
  docnum_t doccount = 0;
  long long docoff = 0;
  double raw_score;
  u_char *forward = NULL, *last = NULL, *p = NULL;
  byte *dt_buf = NULL;
  size_t sighs, trigger_len, dt_buf_used = 0, bytes_read = 0;
  CROSS_PLATFORM_FILE_HANDLE FH;
  HANDLE FMH = NULL;
  double start;
  int error_code;
  u_ll max_plist_len = 0, igdocs = 0, estimated_doccount;
  u_int wds = 0, qwt;
  unsigned long long dt_ent, d_signature;
#ifdef WIN64
  u_char *fwdbuf = NULL, *linebuf = NULL;
  size_t linebufsize = MAX_LINE;
  buffer_queue_t *buffer_queue = NULL;
  HANDLE buffer_filler_thread = NULL;
  FH = NULL;
#else
  char *fgetsbuf;
  FILE *FORWARD = NULL;
  FH = -1;
  fgetsbuf = (char *)malloc(MAX_LINE + 1);
  if (fgetsbuf == NULL) {
    printf("Error: malloc failed in process_records_in_file_order()\n");
    exit(1);
  }  
#endif

  if (x_fileorder_use_mmap) {
    forward = (u_char *)mmap_all_of(fname_forward, &sighs, FALSE, &FH, &FMH, &error_code);
    if (error_code) {
      printf("Error: mmap_all_of(): Code = %d\n", error_code);
      exit(1);
    }
    printf("File mapped: %zu bytes mapped\n", sighs);
    *infile_size = sighs;
    last = forward + sighs - 1;
    if (sighs > DTE_DOCOFF_MASK2)
      printf("\n\nWarning: .forward file is > %.1fGB. Records beyond %.1fGB will not be indexed.\n\n",
	     max_forward_GB, max_forward_GB);

    estimated_doccount = estimate_lines_in_mmapped_textfile(forward, sighs, 5);
    printf("\nEstimated number of records in .forward file: %llu\n\n", estimated_doccount);

    p = forward;
  }
  else {
    if (0) printf(" .. about to get_filesize(%s)\n", fname_forward);
    *infile_size = get_filesize(fname_forward, TRUE, &error_code);
    if (error_code) {
      printf("Error: get_filesize(%s): Code = %d\n", fname_forward, error_code);
      exit(1);
    }

#ifdef WIN64		
    buffer_queue = create_a_buffer_queue(IBM_BUFFERS_IN_RING, IBM_IOBUFSIZE);  // function will exit if it can't create the queue
    buffer_queue->infile = CreateFile((LPCSTR)fname_forward,
				      GENERIC_READ,
				      FILE_SHARE_READ,
				      NULL,
				      OPEN_EXISTING,
				      FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN,
				      NULL);

    if (buffer_queue->infile == INVALID_HANDLE_VALUE) {
      error_code = -210006;
      printf("Error: opening .forward file: Code = %d\n", error_code);
      exit(1);
    }

    estimated_doccount = estimate_lines_in_textfile(buffer_queue->infile, *infile_size, 5);
    printf("\nEstimated number of records in .forward file: %llu\n\n", estimated_doccount);

    buffer_filler_thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)fill_buffers, (LPVOID)buffer_queue, 0, NULL);
    printf("Buffer-queue of %d x %u created, and buffer-filler thread started.\n\n", IBM_BUFFERS_IN_RING, IBM_IOBUFSIZE);
    Sleep(10);  // Give the filler a chance to start up.
#else
    FORWARD = fopen((char *)fname_forward, "rb");
    if (FORWARD == NULL) {
      printf("Error: failed to fopen %s\n", fname_forward);
      exit(1);
    }
    if (0) printf("File %s opened for indexing.\n", fname_forward);
    estimated_doccount = estimate_lines_in_textfile(fileno(FORWARD), *infile_size, 5);
    printf("\nEstimated number of records in .forward file (length %zu): %llu\n\n",
	   *infile_size, estimated_doccount);
#endif
  }

	
  allocate_hashtable_and_heap(estimated_doccount);


  start = what_time_is_it();

  // Loop over all the records in sequence and index the triggers.
  do {
    if (x_fileorder_use_mmap) {
      docoff = p - forward;
    } 
    else {
#ifdef WIN64
      p = get_line(buffer_queue, &linebuf, linebufsize, &bytes_read);
      if (p == NULL) break;  // -----------------------------------------------------  BREAK >
      if (debug) printf("%lld bytes read by get_line()\n", bytes_read);
#else
      if (fgets(fgetsbuf, MAX_LINE + 1, FORWARD) == NULL) break; // ----------------------  BREAK >
      bytes_read = strlen(fgetsbuf);
      p = (u_char *)fgetsbuf;
#endif
    }
    if (*p < ' ') {
      // This line is empty, will be ignored.  Could start with null or tab or just newline
      igdocs++;
      docoff += bytes_read;
      continue;   // ------------------------------------------------------>
    }
    if (min_wds > 0 || max_wds > 0) {
      // Only do this counting  if limits are being imposed.
      wds = count_wds_in_trigger(p);  // Returns -1 in case of error
      if (wds < min_wds || wds > max_wds) {
	igdocs++;
	docoff += bytes_read;
	continue;    // ------------------------------------------------------>
      }
    }
    raw_score = split_and_index_record(p, doccount, &max_plist_len, ll_heap, &d_signature, 
				       &wds, &trigger_len);
    if (wds > 0 && raw_score >= score_threshold) {
      // We ignore suggestions with scores below the frequency threshold.

      if ((unsigned long long)docoff > DTE_DOCOFF_MASK2) {
	igdocs++;
	docoff += bytes_read;
	continue;   // ----------------------------------------------->
      }

      qwt = quantize_log_score_ratio(raw_score, log_max_score);
      dt_ent = docoff << DTE_DOCOFF_SHIFT;
      if (wds > DTE_WDCNT_MAX) wds = DTE_WDCNT_MAX;  // To cope with reduced DTE_WDCNT_BITS in version 1.3 indexes
      dt_ent |= (wds & DTE_WDCNT_MASK);
      dt_ent |= ((qwt & DTE_DOCSCORE_MASK2) << DTE_DOCSCORE_SHIFT);
      dt_ent |= ((d_signature & DTE_DOCBLOOM_MASK2) << DTE_DOCBLOOM_SHIFT);
      if (!x_minimize_io) buffered_write(dt_handle, &dt_buf, HUGEBUFSIZE, &dt_buf_used, (byte *)&dt_ent, sizeof(dt_ent), "doctable entry");

      doccount++;

      if (doccount % 10000 == 0) {
	printf("%11lld\n", doccount);
#ifdef WIN64
	report_memory_usage(stdout, (u_char *)"scanning in file order", NULL);
#endif
      }
      if (doccount >= max_docs) break;
    }
    else {
      igdocs++;
      docoff += bytes_read;
      continue;   // ----------------------------------------------->
    }
    if (x_fileorder_use_mmap) {
      // skip forward to the end of this record
      p += trigger_len;
      while (p <= last && *p != '\n') p++;
      if (*p == '\n') p++;
      if (p >= last) break;
    }
    else docoff += bytes_read;

  } while (1);

  if (!x_fileorder_use_mmap) {
#ifdef WIN64
    // Make sure we don't close the file while the filler thread is working.  That can happen
    // with a low value of x_max_docs
    if (0) printf("Wrapping up!\n");
    acquire_lock_on_state_of(buffer_queue, (byte *)"wrapping up");
    buffer_queue->buffer_state[buffer_queue->buf2fill] = BUF_ABORT_READING;
    release_lock_on_state_of(buffer_queue, (byte *)"wrapping up");
    Sleep(15);  // Give the buffer-filler a chance to wind up.  Delay is in milliseconds
    if (0) printf("Wrapped up!\n");
#else
    fclose(FORWARD);
    free(fgetsbuf);
#endif
  }

  if (!x_minimize_io) buffered_flush(dt_handle, &dt_buf, &dt_buf_used, ".doctable", TRUE); // Frees the buffer and closes the handle
  msec_elapsed_list_building = (what_time_is_it() - start) * 1000.0;
  printf("In-file-order scan elapsed time %.1f sec.\n", msec_elapsed_list_building / 1000.0);
  if (x_fileorder_use_mmap) {
    unmmap_all_of(forward, FH, FMH, *infile_size);
  }
  else {
#ifdef WIN64
    destroy_buffer_queue(&buffer_queue);
#endif
		
  }
  *gdoccount = doccount;
  *gmax_plist_len = max_plist_len;
  *ignored_docs = igdocs;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Usage message and functions to print option settings etc.                                                                                                    //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void print_usage() {
  printf("Usage: QBASHI.exe (-index_dir=<directory|-input_forward=<file> -output_if=<file> -output_vocab=<file> -output_doctable=<file>) [<option> ...]\n\n");
  print_args(TEXT);
  printf("\nIf index_dir is specified, QBASHI expects to find a file called QBASH.forward in the specified index\n"
	 "directory.  It indexes it and creates index files called QBASH.vocab, QBASH.if, and QBASH.doctable,\n");
  printf("If on the other hand, individual files are specified, then all four files must be specified, and there is no\n"
	 "restriction on what the files are called or where they are located.  However, please note that the\n"
	 "forward file is both the input file and the per document index (PDI).  If its contents change after\n"
	 "indexing, things are likely to break. If index files are located on remote storage, QBASHI performance\n"
	 "may be slow.\n\n"
	 "The forward file (however specified) must be a TSV file in which column 1 is the suggestion, and \n"
	 "column 2 is a frequency/weight. Other columns may be present but are ignored by QBASHI.\n"
	 "By default records are sorted internally by weight, but there is an option to turn off sorting.\n\n");
  printf("QBASHER version: %s%s\n", INDEX_FORMAT, QBASHER_VERSION);
  exit(1);    // OK - on startup
}



static void print_version_and_option_settings() {
  u_char *arg_list = NULL;

  arg_list = (u_char *)malloc(IF_HEADER_LEN - 250);  // Same as used when writing to .if header
  if (arg_list == NULL) {
    printf("Malloc failed for arg_list.\n");
    exit(1);
  }
  printf("QBASHER version:%s%s\n", INDEX_FORMAT, QBASHER_VERSION);

  printf("----------------------------------- Option Settings -------------------------------\n");
  printf("Forward: %s\nDoctable: %s\nIF: %s\nVocab: %s\n", fname_forward, fname_doctable, fname_if, fname_vocab);
  printf("Token break set: %s\n", token_break_set);
  if (sort_records_by_weight) printf("Records will be sorted in weight order before indexing.\n");
  else {
    if (x_fileorder_use_mmap)
      printf("File will be mapped.  Records will be processed in file order and raw scores will be divided by score of first record.\n");
    else
      printf("Records will be read and processed in file order and raw scores will be divided by score of first record.\n");
  }
  printf("Filtering parameters:\n    x_max_docs=%lld\n    min_wds=%d\n    max_wds=%d\n    score_threshold=%.3f\n\n",
	 x_max_docs, min_wds, max_wds, score_threshold);

  if (SB_TRIGGER > 0) {
    printf("Skip blocks will be written in runs of %d when there are more than %d postings.\n"
	   " - a run length of zero means that run length is dynamically set.\n",
	   SB_POSTINGS_PER_RUN, SB_TRIGGER);
  }
  else {
    printf("Skip blocks will not be written.\n");
  }

  if (x_hashbits) printf("Initial hashbits explicitly set to %d.\n", x_hashbits);
  if (x_hashprobe) printf("Hashtable collisions handled by linear probing.\n");
  else printf("Hashtable collisions handled by relatively prime rehash.\n");
  if (x_minimize_io) printf("Ths run is useful for timing purposes only.  Index files will not be written\n");
  if (x_use_large_pages) printf("An attempt will be made to make use of VM Large Pages\n");
  else printf("Program will use standard VM pagesize - presumably 4k\n");
  if (x_2postings_in_vocab) printf("Postings lists of up to 2 postings will be stored in the hash table.\n");
  else printf("Hash table entries will not be used for storing postings.\n");

  printf("Chunking function used for linked lists is %u\n", x_chunk_func);
  printf("\nComplete list of option settings ...\n");
  store_arg_values(arg_list, IF_HEADER_LEN - 250, TRUE);
  puts((char *)arg_list);
  free(arg_list);
  printf("-----------------------------------------------------------------------------------\n\n");
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions to set up the the tables for managing posting list chunk sizes                                            //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u_int validateK(u_int inK) {
  // Check that the value of K computed in calculate_k_table doesn't cause the 19-bit bytes_used field 
  // to overflow, or isn't ridiculously small.  Return the modified (or unmodified value.)
  if (inK > MAX_PAYLOADS) return MAX_PAYLOADS;
  if (x_min_payloads_per_chunk && inK < x_min_payloads_per_chunk) return x_min_payloads_per_chunk;
  return inK;
}


static void calculate_k_table(int funkno) {
  // Set up the tables which support dynamic chunking of linked lists.
  // Each run in a chunking sequence corresponds to an entry in the two chunk tables
  // The i-th run (starting at i = 1) corresponds to chunk_length_table[i] and 
  // chunk_K_table[i].  The former records the length of postings list which 
  // can be represented by runs 1,..., r.  and K records the biggest chunk
  // to be used in that sequence.
  // See also the comments around the definition of chunk_length_table in linked_list.cpp
  u_int i, r = 0, K, fiba = 1, fibb = 1, fibsum, maxi = 1, k = 2, kpower = 1;
  int funkno_used = funkno, funkdiv10,
    orig_funkno = funkno;
  u_ll total = 0;
  chunk_length_table[0] = 0;
  chunk_length_table[1] = 100000000000;  // Default to avoid multiple settings.
  chunk_K_table[0] = 1;

  // ------------------------------------------------------------------------------
  // This code section handles funkno values such as 2007 where the 2000 says that
  // we're using a sub-power function and 7 gives the value of k
  funkdiv10 = funkno / 10;
  if (funkno < 1)  funkno_used = 1;  // Default unchunked
  else if (funkno > 100 && funkno != 101 && funkno != 102 
	   && funkdiv10 != 200 && funkdiv10 != 300 && funkdiv10 != 400) funkno_used = 1;

  if (funkno_used != funkno && funkno != 0) 
    printf("Warning.  Unimplemented chunking function changed from %d to %d\n", funkno, funkno_used);

  if (funkno_used > 2000) {
    funkno = funkdiv10;
    k = funkno_used % 10;
    if (k < 2) k = 2;
  }
  else funkno = funkno_used;
  // ------------------------------------------------------------------------------

  printf("Chunking:  (now looked up by chunk number rather than posting number). Minimum chunk size = %u\n", x_min_payloads_per_chunk);
  if (funkno == 1) printf("None\n");
  else if (funkno <= 100) printf("Fixed chunks of size %d\n", funkno);
  else if (funkno == 101) printf("Chunks of Fibonacci(k), runs of 1\n");
  else if (funkno == 102) printf("Chunks of Fibonacci(k), runs of Fibonacci(k)\n");
  else if (funkno == 200) printf("Chunks of %d^i, runs of %d^(i - 1) - sub-power\n", k, k);
  else if (funkno == 300) printf("Chunks of %d^i, runs of %d^i - power\n", k, k);
  else if (funkno == 400) printf("Chunks of %d^i, runs of %d^(i + 1) - super-power\n", k, k);

  if (funkno <= 100) {
    chunk_K_table[1] = validateK(funkno);
    if (0) printf("K-table[%d] r=infinity, K=%d; Limit = %llu\n", 1, chunk_K_table[1], chunk_length_table[1]);
  }
  else {
    K = 1;
    kpower = 1;
    for (i = 1; total <= 100000000000; i++) {
      // K is the size of chunks in the current run
      // r is the number of chunks in the run
      if (i >= MAX_K_TABLE_ENTS) {
	printf("Chunking table full.  Total accommodated = %llu.  Will use chunksize %d from then on.\n",
	       total, K);
	// Go on using the same K up to 100 billion occurrences (it must already have been validated.)
	total = 100000000000;
	if (0) printf("K-table[%d] r=%d, K=%d; Limit = %llu\n", i, r, K, total);
	chunk_length_table[i] = total;
	chunk_K_table[i] = K;
	break;
      }
      maxi = i;
      if (funkno == 101 || funkno == 102) {
	// Fibonacci
	K = validateK(fibb);
	if (funkno == 101) r = 1;
	else r = K;
	total += (u_ll)r;
	if (0) printf("K-table[%d] r=%d, K=%d; Limit = %llu\n", i, r, K, total);
	chunk_length_table[i] = total;
	chunk_K_table[i] = K;
	fibsum = fiba + fibb;
	fiba = fibb;
	fibb = fibsum;
      }
      else if (funkno >= 200 && funkno <= 400) {
	kpower = validateK(kpower);
	K = kpower;
	if (funkno == 200) r = kpower / k;   // sub-power
	else if (funkno == 300) r = kpower;   // power
	else if (funkno == 400) r = kpower * k;   // super-power
	if (r < 1) r = 1;
	total += (u_ll)r ;
	if (0) printf("K-table[%d] r=%d, K=%d; Limit = %llu\n", i, r, K, total);
	chunk_length_table[i] = total;
	chunk_K_table[i] = K;
	kpower *= k;
      }
    }
  }

  printf("calculate_k_table called with function %d:  %d table entries used\n", orig_funkno, maxi);
}


static void write_doc_length_histo_to_file(double *mean, double *stdev) {
  // If fname_dlh is NULL, just calculate mean and stdev and write nothing to file.
  int l, highest;
  double sum = 0.0, sumsqdevs = 0.0, n = 0.0, dl, df, dlmean, var, tot_postings = 0;
  FILE *DLH = NULL;
  if (fname_dlh != NULL) {
    DLH = fopen((char *)fname_dlh, "w");
    if (DLH == NULL) {
      printf("Error: Unable to write to doclenhist file %s\n", fname_dlh);
    }
    fprintf(DLH, "#Term frequency histogram with %lld docs and MAX_WDS_INDEXED_PER_DOC = %d.\n"
	    "#Format is lengthTABcount\n", doccount, MAX_WDS_INDEXED_PER_DOC);
  }

  for (highest = (MAX_WDS_INDEXED_PER_DOC + 1); highest > 1 && doc_length_histo[highest] == 0; highest--);

  // Calculate the mean length
  for (l = 1; l <= highest; l++) {
    df = (double)doc_length_histo[l];
    dl = (double)l;
    n += df;
    sum += dl * df;
  }

  dlmean = sum / n;

  // Repeat the loop to calculate the stdev and output the histo
  for (l = 1; l <= highest; l++) {
    if (DLH != NULL) fprintf(DLH, "%5d\t%lld\n", l, doc_length_histo[l]);
    df = (double)doc_length_histo[l];
    dl = (double)l;
    sumsqdevs += df * (dl - *mean) *(dl - *mean);
    tot_postings += df * dl;
  }
  if (DLH != NULL) {
    fclose(DLH);
    printf("  --  Document length histogram written to QBASH.doclenhist\n");
  }

  free(doc_length_histo);
  doc_length_histo = NULL;

  *mean = dlmean;
  var = sumsqdevs / n;
  *stdev = sqrt(var);
  printf("Document lengths: max = %d, mean = %.3f, stdev = %.3f. Tot Postings: %.0f\n",
	 highest, *mean, *stdev, tot_postings);
}

int main(int argc, char **argv) {

  double total_index_size = 0.0, doclen_mean = 0.0, doclen_stdev = 0.0, total_elapsed_time;
  int a;
  size_t infile_size = 0, l1, l2;
  u_char *ap, *p;
  u_ll max_plist_len = 0, word_table_collisions = 0;
  double start = 0, wifstart = 0;


#ifdef WIN64
  forward_handle = NULL;
  dt_handle = NULL;
#else
  forward_handle = -1;
  dt_handle = -1;
#endif

  testGCD();
	  
  calculate_dte_shifts_and_masks();
  test_shifts_and_masks();

  test_quantize_log_score_ratio();
  test_ve_pup();

  vocabfile_test_pack_unpack(MAX_WD_LEN + 1); 
  test_quantized_idf();
  initialize_unicode_conversion_arrays(TRUE);
  test_count_ones_b();
  test_utf8_functions();
  start = what_time_is_it();
  //test_rand_gamma();
  //printf("Sec elapsed for test_rand_gamma(): %.3f\n", what_time_is_it() - start);
  //test_rand_cumdist();
  //printf("Sec elapsed for test_rand_cumdist(): %.3f\n", what_time_is_it() - start);

  if (argc <= 1) {
    // Follow the convention that running an exe with no args should print a usage message.
    print_usage();
  }

  printf("sizeof(posting_p) = %zu\n\n", sizeof(posting_p));
  if (sizeof(u_char *) != 8) {
    error_exit("This program should be built for a 64-bit architecture, but isn't.");
  }

  start = what_time_is_it();
#ifdef WIN64
  report_memory_usage(stdout, (u_char *)"at the very start", NULL);
#endif

	
  language = make_a_copy_of((u_char *)"EN"); 
  other_token_breakers = make_a_copy_of((u_char *)OTHER_TOKEN_BREAKERS_DFLT);

  // Read and process the command line options.
  for (a = 1; a < argc; a++) {
    u_char *ignore;
    ap = (u_char *)argv[a];
    while (*ap == '-') ap++;
    assign_one_arg(ap, &ignore);
  }

  if (debug == 10) {
    setvbuf(stdout, NULL, _IONBF, 0);
    debug = 0;  // Same convention as QBASHQ
  }

  // Resolve incompatibilities
  if (sort_records_by_weight && x_max_docs != DFLT_MAX_DOCS) {
    printf("Warning:  x_max_docs is incompatible with sort_records_by_weight.  Restoring default value\n");
    x_max_docs = DFLT_MAX_DOCS;
  }

  if (SB_POSTINGS_PER_RUN && SB_POSTINGS_PER_RUN < 2) SB_POSTINGS_PER_RUN = 2;  //  SB_RUN_LENGTH = 0 is OK
  if (SB_TRIGGER && SB_TRIGGER < 3) SB_TRIGGER = 3;  // To avoid problems when postings lists of length 2 are stored in hash table

  if (max_line_prefix > 0) {
    if (max_line_prefix > (MAX_WD_LEN - 1)) {
      max_line_prefix = (MAX_WD_LEN - 1);
      printf("Warning:  Too large a value for max_line_prefix. Setting to %d\n",
	     max_line_prefix);
    }
    if (max_line_prefix_postings < 10) {
      max_line_prefix_postings = 10;
      printf("Warning:  Too small a value for max_line_prefix_postings. Setting to %d\n",
	     max_line_prefix_postings);
    }
  }

  if (index_dir == NULL) {
    if (fname_forward == NULL || fname_if == NULL || fname_vocab == NULL || fname_doctable == NULL) {
      printf("Error: If index_dir is not given, all four index files must be individually specified.\n");
      print_usage();
    }
  }
  else {
    if (fname_forward != NULL || fname_if != NULL || fname_vocab != NULL || fname_doctable != NULL) {
      printf("Error: It is not permitted to specify both index_dir and individual input/output files.\n");
      print_usage();
      exit(1);   
    }
		
    // Set up the four filenames based on index_dir
    size_t l = strlen((char *)index_dir), max_fname_len;
    max_fname_len = l + 40;  // 40 to allow for /QBASH.doctable etc.
    fname_forward = (u_char *)malloc(max_fname_len);
    fname_if = (u_char *)malloc(max_fname_len);
    fname_vocab = (u_char *)malloc(max_fname_len);
    fname_doctable = (u_char *)malloc(max_fname_len);
    if (fname_forward == NULL || fname_if == NULL || fname_vocab == NULL || fname_doctable == NULL) {
      printf("Error: Malloc failed for filename allocation.\n");
      exit(1);
    }
    strcpy((char *)fname_forward, (char *)index_dir);
    strcpy((char *)fname_forward + l, "/QBASH.");
    strcpy((char *)fname_forward + l + 7, "forward");
    strcpy((char *)fname_if, (char *)index_dir);
    strcpy((char *)fname_if + l, "/QBASH.");
    strcpy((char *)fname_if + l + 7, "if");
    strcpy((char *)fname_vocab, (char *)index_dir);
    strcpy((char *)fname_vocab + l, "/QBASH.");
    strcpy((char *)fname_vocab + l + 7, "vocab");
    strcpy((char *)fname_doctable, (char *)index_dir);
    strcpy((char *)fname_doctable + l, "/QBASH.");
    strcpy((char *)fname_doctable + l + 7, "doctable");

  }

#ifdef WIN64
  if (x_use_large_pages) {
    // Set the Privilege needed in order to be able to use LARGE PAGES
    printf("Attempting to set LOCK_MEMORY privilege.  (Caused by use of x_use_large_pages option.)\n");
    Privilege(TEXT("SeLockMemoryPrivilege"), TRUE, &x_use_large_pages, &large_page_minimum);
  }

  if (x_cpu_affinity >= 0) set_cpu_affinity(x_cpu_affinity);
#endif

  if (x_bigger_trigger) {
    MAX_LINE = MAX_DOCBYTES_BIGGER;  // Less than the new IBM_IOBUFSIZE - not sure if that's important
    MAX_WDS_INDEXED_PER_DOC = 200000;
    printf("Words after position 254 will all be given position 255.\n");
  }

  printf("Maximum text indexed per input record: %d bytes or %d words (whichever\n"
	 "  limit is hit first). Text after that will be ignored. \n\n",
	 MAX_LINE, MAX_WDS_INDEXED_PER_DOC);

  if (x_doc_length_histo) {
    // Set up a histogram array for recording document lengths
    // ... and a filename for the document length histo, if we need it.
    if (index_dir != NULL) {
      // Note:  If index_dir was given as ZIPF-GENERATOR it will be "." now.
      size_t l = strlen((char *)index_dir), max_fname_len;
      printf("... setting up to record a document length histogram.\n");
      max_fname_len = l + 40;  // 40 to allow for /QBASH.doclenhist etc.
      fname_dlh = (u_char *)malloc(max_fname_len);
      if (fname_dlh != NULL) {
	strcpy((char *)fname_dlh, (char *)index_dir);
	strcpy((char *)fname_dlh + l, "/QBASH.");
	strcpy((char *)fname_dlh + l + 7, "doclenhist");
	printf("... doc. length histogram will be in %s.  max_fname_len = %zu MAX_WDS_INDEXED_PER_DOC = %u\n", 
	       fname_dlh, max_fname_len, MAX_WDS_INDEXED_PER_DOC);
      }
    }

    doc_length_histo = (u_ll *)malloc((MAX_WDS_INDEXED_PER_DOC + 2) * sizeof(u_ll));  // Last element records overlength docs
    if (doc_length_histo == NULL) {
      printf("Error: Malloc failed for doc_length_histo.\n");
      exit(1);
    }
    memset(doc_length_histo, 0, (MAX_WDS_INDEXED_PER_DOC + 2) * sizeof(u_ll));
  }

  if (x_geo_big_tile_factor < 0) {
    printf("Warning: x_geo_big_tile_factor cannot be negative, setting to one\n");
    x_geo_big_tile_factor = 1;
  } else if (x_geo_big_tile_factor > 100) {
    printf("Error: x_geo_big_tile_factor cannot exceed 100, aborting ...\n");
    exit(1);  // It wouldn't be useful to index with a factor different to what the user specified.
  } else if (x_geo_tile_width * x_geo_big_tile_factor > earth_radius) {
    printf("Error: Product of x_geo_big_tile_factor and x_geo_tile_width cannot exceed earth radius, aborting ...\n");
    exit(1);  // Tiles which are too big don't achieve the purpose of tiling, i.e. to reduce latency.
  }
  
  
  // Set up the token breaking character sets.
  l1 = strlen(QBASH_META_CHARS);
  l2 = strlen((char *)other_token_breakers);
  if (max_line_prefix) {
    // Have to remove '>' from the token break set
    u_char *p = (u_char *)strchr((char *)other_token_breakers, '>');
    if (p != NULL) *p = *(p + 1);   // Zap '>' by repeating the next token breaker or by NULLing
  }


  token_break_set = (u_char *)malloc(l1 + l2 + 1);
  if (token_break_set == NULL) error_exit("malloc of token_break_set failed.\n");
  strcpy((char *)token_break_set, QBASH_META_CHARS);

  strcpy((char *)(token_break_set + l1), (char *)other_token_breakers);
  // Now set up the ascii_non_tokens map
  for (a = 0; a <= 32; a++) ascii_non_tokens[a] = 1;
  p = token_break_set;
  while (*p) {
    ascii_non_tokens[*p] = 1;
    p++;
  }

  if (expect_cp1252) {
    for (a = 128; a < 160; a++) ascii_non_tokens[a] = 1;
  }

  if (debug) 	setvbuf(stdout, (char *)NULL, _IONBF, 0);  //Only in debug mode for performance reasons.

  test_signature_calculation();  // Must come after we define ascii_non_tokens

  if (x_hashprobe) dahash_set_probing_method(1);


  print_version_and_option_settings();

  calculate_k_table(x_chunk_func);

  fflush(stdout);  // Otherwise it may be ages before any output appears.


  if (SB_POSTINGS_PER_RUN > SB_MAX_COUNT)
    error_exit("Error in skip block parameters: SB_POSTINGS_PER_RUN must be >= 0 and <= SB_MAX_COUNT");

  if (!x_minimize_io)  {
    int error_code;
    dt_handle = open_w((char *)fname_doctable, &error_code);
    if (error_code)	error_exit("Unable to open QBASH.doctable for writing.");
  }

#ifdef WIN64
  report_memory_usage(stdout, (u_char *)"Start of List Building phase", &pfc_list_build_start);
#endif

    if (sort_records_by_weight) {
      // In this case, we don't need to allocate hash tables until we've scanned the entire input
      // So process_records_in_score_order() calls 	allocate_hashtable_and_heap() with the actual
      // number of documents.
      printf("About to do score-order scan ...\n");
      process_records_in_score_order(fname_forward, dt_handle, word_table, min_wds, max_wds,
				     &ignored_docs, &doccount, &max_plist_len, &infile_size);
      printf("Returned from process_records_in_score_order()\n");
    } else {
      printf("About to do file-order scan ...\n");
      process_records_in_file_order(fname_forward, dt_handle, word_table, x_max_docs, min_wds, max_wds,
				    &ignored_docs, &doccount, &max_plist_len, &infile_size);
      printf("Returned from process_records_in_file_order()\n");
    }

#ifdef WIN64
  report_memory_usage(stdout, (u_char *)"End of List Building phase", &pfc_list_build_end);
#endif
  // CloseHandle(dt_handle);  Already closed by buffered_flush()

  // Show some more stats immediately after the scan.
  printf("Scan finished: Number of documents scanned: %lld\n", doccount);
  printf("Scan finished: Vocabulary size: %zu\n", word_table->entries_used);

  if (x_doc_length_histo) {
    // File won't be written if fname_dlh == NULL
    write_doc_length_histo_to_file(&doclen_mean, &doclen_stdev);
  }



  if (debug) {
    printf("Alphabetic word list\n====================\n");
    dahash_dump_alphabetic(word_table, ll_heap, show_key, show_count_n_postings);
    printf("====================\nAlphabetic word list\n");
  }

  wifstart = what_time_is_it();
  printf("Vocab filename is %s\n", fname_vocab);

  // ===============  This is where the inverted file is written ========================
  total_index_size = write_inverted_file(word_table, fname_vocab, fname_if, ll_heap,
					 SB_POSTINGS_PER_RUN, SB_TRIGGER, doccount, infile_size, max_plist_len);
  msec_elapsed_list_traversal = (what_time_is_it() - wifstart) * 1000.0;
  printf("Write-inverted-file elapsed time %.1f sec.\n", msec_elapsed_list_traversal / 1000.0);
#ifdef WIN64
  report_memory_usage(stdout, (u_char *)"End of List Building phase", &pfc_list_scan_end);
#endif
  printf("QBASH.doctable file: %8.1fMB\n", (double)(doccount * DTE_LENGTH) / 1048576.0);
  total_index_size += ((double)(doccount * DTE_LENGTH + (double)infile_size)) / 1048576.0;
  printf("Total index size:    %8.1fMB\n", total_index_size);
  printf("=================================\n\n");


  printf("Input file of was kosher: %.1fMB\n", (double)infile_size / MEGA);
  vocab_size = word_table->entries_used;  // Save for reporting at the end


  word_table_collisions = word_table->collisions;


  if (CLEAN_UP_BEFORE_EXIT) {
    double perc;
    // We may not want to bother doing this since it will all be cleaned up on exit
#ifdef WIN64
    report_memory_usage(stdout, (u_char *)"after writing the inverted file, before cleaning up memory", NULL);
#endif
    doh_free(&ll_heap);

#ifdef WIN64 
    report_memory_usage(stdout, (u_char *)"before destroying the hash table", NULL);
#endif
    perc = 100.0 * (double)word_table->entries_used / (double)word_table->capacity;
    printf("The 'word' hash table was doubled %d times.  %zu / %zu entries were used.  I.e. it was %.1f%% full.\n\n",
	   word_table->times_doubled, word_table->entries_used, word_table->capacity, perc);
    dahash_destroy(&word_table);
  }
#ifdef WIN64
  report_memory_usage(stdout, (u_char *)"at the very end", NULL);
#endif



  // WARNING: Be careful about changing the following print statements.  The format of their output
  // WARNING: relied on by various analysis scripts such as synthetic_index_timings.pl

  printf("\nRecords (excluding ignoreds): %lld\n"
	 "Records ignored because #wds outside range(%u,%u), or freq < %.3f or record invalid: %llu\n"
	 "Records whose column 1 was truncated because of %d-byte copy buffer: %llu\n"
	 "Records whose column 1 was incompletely indexed due to limit of %d words per record: %llu\n"
	 "Records whose column 1 had no indexable words: %llu\n",
	 doccount, min_wds, max_wds, score_threshold, ignored_docs,
	 CPYBUF_SIZE, truncated_docs, MAX_WDS_INDEXED_PER_DOC,
	 incompletely_indexed_docs, empty_docs);

  if (x_doc_length_histo)
    printf("Record lengths: Mean: %.4f; St. Dev: %.4f words. (Indexed text only.)\n",
	   doclen_mean, doclen_stdev);

#ifdef WIN64
  printf("\nList building: %.3f sec elapsed;  %u page faults (soft + hard)\n", 
	 msec_elapsed_list_building / 1000.0, pfc_list_build_end - pfc_list_build_start);
  printf("List traversal: %.3f sec elapsed;  %u page faults (soft + hard)\n", 
	 msec_elapsed_list_traversal / 1000.0, pfc_list_scan_end - pfc_list_scan_start);
#else
  printf("\nList building: %.3f sec elapsed\n", msec_elapsed_list_building / 1000.0);
  printf("List traversal: %.3f sec elapsed\n", msec_elapsed_list_traversal / 1000.0);
#endif
  printf("Hash table: %.1fMB; Collisions per posting: %.5f; Linked lists: %.1fMB; List chunks allocated: %lld\n", 
	 hashtable_MB, (double)word_table_collisions/(double)tot_postings, linkedlists_MB, chunks_allocated);

  printf("Distinct words: %lld; Total postings: %lld; Longest postings list: %lld (It includes %.3f%% of all postings.)\n",
	 vocab_size, tot_postings, max_plist_len, 100.0 * (double)max_plist_len / (double)tot_postings);
  printf("Indexer version: %s%s\n", INDEX_FORMAT, QBASHER_VERSION);
  total_elapsed_time = what_time_is_it() - start;
  printf("Total elapsed time %.1f sec. to index %lld docs (.forward is %.1fMB).  Indexing rate: %.3fM postings/sec.\n",
	 total_elapsed_time, doccount, (double)infile_size / 1048576.0,
	 (double)tot_postings / (1000000.0 * total_elapsed_time));
}
