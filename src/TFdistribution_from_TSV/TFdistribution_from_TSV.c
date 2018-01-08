// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#ifdef WIN64
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include <Psapi.h>
#else
#include <errno.h>
#endif

#include "TFdistribution_from_TSV.h"
#include "../shared/QBASHER_common_definitions.h"
#include "../shared/utility_nodeps.h"
#include "../shared/unicode.h"
#include "../utils/dahash.h"


// Reads every line in an input TSV file (e.g. QBASH.forward) and calls process_repetitions()
// on column one of each line.
// process_repetitions() makes an array of words and sorts them case-insensitively into
//lexicographic order.  A scan of this array is enough to find repeated terms.  


#define BUFSIZE (50 * 1048576)   // X * MB

static dahash_table_t *term_rep_table = NULL;
static CROSS_PLATFORM_FILE_HANDLE term_rep_handle;
static double start_time, very_start;
static byte *writebuf;
static size_t bytes_in_writebuf = 0;
static int min_occurrences = 2;   //  Only record terms which occur at least this often in a record.
static u_char token_break_set[] = "%\"[]~/ &'( ),-.:;<=>?@\\^_`{|}!";  // These are the token breakers used by default in QBASHI
static long long total_word_occs = 0;
static int highest_tf_overall = 0;


static int uint2string(u_char *str, u_int number) {
  // Write a NUL-terminated decimal representation of number to str and return the number of digits written
  int digit, left, written = 0;
  digit = number % 10;
  left = number / 10;
  if (left > 0) written = uint2string(str, left);  // Recurse
  str[written] = digit + '0';
  return written + 1;
}


static void show_token(u_char *str) {
  // Print str up to the first occurrence of non-token-char (which include NUL and controls)
  // Like fputs(str, stdio) but
  while (!ascii_non_tokens[*str]) putchar(*str++);
  putchar('\n');
}


#define MAX_DWDS 100000
static u_char *word_starts[MAX_DWDS + 1];
static int permute[MAX_DWDS + 1];


int wdcmp(const void *vi, const void *vj) {
  // Comparison function called by qsort to alphabetically compare
  // two strings referenced by elements of the permute array. Strings
  // can now be assumed to have been lower-cased and null_terminated.
  int *i = (int *)vi, *j = (int *)vj;
  u_char *ci = word_starts[*i], *cj = word_starts[*j];
  return strcmp((char *)ci, (char *)cj);
}




static BOOL string_contains_space_or_control(u_char *s) {
  // Just for debugging a problem with president bush.
  while (*s) {
    if (*s <= ' ') {
      printf("string_contains_space_or_control: %d\n", *s);
      return TRUE;
    }
    s++;
  }
  return FALSE;
}



static u_char doc_copy[MAX_DOCBYTES_BIGGER + 1];   // Assume single-threaded operation

static u_char *process_repetitions(u_ll line_count, u_char *line,
				   u_char *end_of_file, FILE *ratios) {
  // Count terms which are repeated multiple times in documents.  The
  // following hash key is used to 
  // count the number of documents in which 'the' is repeated 3 times: "the@3"
  //
  // The end of the mmapped file is passed so that we can avoid
  // segfaults when the last line of the file has no TAB or LF.
  u_char *p = line, term_rep[MAX_REP_LEN + 1], *terminator = line;
  int w, wds_found = 0, first, occurrences, ox, distinct_wds = 0, max_tf,
    digits_written;  
  size_t doclen_orig, doclen, lenw1;
  long long *bvp, checksum = 0;

  // State 0 - scanning non-word
  // State 1 - scanning word

  while (p <= end_of_file && *p >= ' ') p++;  // terminate scanning on any ASCII
                                              // control char (should be TAB), or
                                              // end of file
  terminator = p;
  doclen_orig = p - line;
  if (doclen_orig > MAX_DOCBYTES_BIGGER) doclen_orig = MAX_DOCBYTES_BIGGER;
  
  doclen = utf8_lowering_ncopy(doc_copy, line, doclen_orig);
  doc_copy[doclen] = 0;  // Guarantee null-termination
  wds_found = utf8_split_line_into_null_terminated_words(doc_copy, word_starts,
							 MAX_DWDS, MAX_WD_LEN,
							 FALSE, FALSE, FALSE, FALSE); 
  if (0) printf("Word count for line %llu:  %d\n", line_count, wds_found);
  total_word_occs += wds_found;

  // Now sort the words into order using qsort and the permute array
  for (w = 0; w < wds_found; w++) permute[w] = w;
#if defined(WIN64) || defined(__APPLE__)
  qsort(permute, (size_t)wds_found, sizeof(int), wdcmp);
#else
  qsort(permute, (size_t)wds_found, sizeof(int), (__compar_fn_t)wdcmp);
#endif

  if (0){
    for (w = 0; w < wds_found; w++) {
      u_char *p = word_starts[permute[w]];
      printf("Sorted: ");
      show_token(p);
    }
  }
  // Now look for repeated words
  distinct_wds = 0;
  max_tf = 0;
  first = 0;
  w = 1;
  while (w <= wds_found) {   // <=  in order to handle the case where the last word 
    occurrences = 1;   // Occurrences of a particular word.
    while (w < wds_found && !strcmp((char *)word_starts[permute[first]], (char *)word_starts[permute[w]])) {
      occurrences++;
      w++;
    }

    if (0 && w <= wds_found) {
      printf("Inner loop ended after %d occurrences: Lengths(%zd, %zd)\n",
	     occurrences, strlen((char *)word_starts[permute[first]]),
	     strlen((char *)word_starts[permute[w - 1]]));
      show_token(word_starts[permute[first]]);
      show_token(word_starts[permute[w - 1]]);
    }

    distinct_wds++;
    if (occurrences > max_tf) max_tf = occurrences;
    checksum += occurrences;

    if (occurrences >= min_occurrences) {
      // Store the word with rep count
      strcpy((char *)term_rep, (char *)word_starts[permute[first]]);  // Now we can assume it's
                                                                      // truncated and case folded.
      lenw1 = strlen((char *)term_rep);
      term_rep[lenw1++] = '@';
      ox = occurrences;
      if (ox > 10000) ox = 9999;   // To avoid buffer overflow
      digits_written = uint2string(term_rep + lenw1, ox);
      lenw1 += digits_written;
      term_rep[lenw1] = 0;
      if (string_contains_space_or_control(term_rep)) {
	printf("Aargh!  About to add term '%s' with space or control\n", term_rep);
	show_string_upto_nator(line, '\n', 0);
	exit(1);
      }
      // Now plonk it in the hash and increment the count
      if (0) printf("Inserting term_rep '%s' \n", term_rep);
      bvp = (long long *)dahash_lookup(term_rep_table, (byte *)term_rep, 1);
      (*bvp)++;
    }
    first = w++;
  }   // End of while (w <= wds_found)

  if (max_tf > highest_tf_overall) highest_tf_overall = max_tf;

  if (checksum != wds_found) {
    printf("Error: checksum %lld != word count %d\n", checksum, wds_found);
    exit(1);
  }

  fprintf(ratios, "%llu\t%d\t%d\t%d\t%.5f\t%.5f\n",
	  line_count, wds_found, distinct_wds, max_tf,
	  (double)distinct_wds / (double)wds_found,
	  (double)max_tf / (double)wds_found);
  if (0) printf("%llu\t%d\t%d\t%d\t%.5f\t%.5f\n",
		line_count, wds_found, distinct_wds, max_tf,
		(double)distinct_wds / (double)wds_found,
		(double)max_tf / (double)wds_found);

  return(terminator);
}



static int compare_keys_alphabetic(const void *i, const void*j) {
  // Alphabetically compare two null-terminated strings 
  char *ia = *(char **)i, *ja = *(char **)j;
  return strcmp(ia, ja);
}


void dahash_dump_alphabetic_simple(dahash_table_t *ht) {
  // Sort the keys stored in ht into alphabetic order, then for each entry
  // in the sorted list 
  // 


  int e, p;
  byte **permute;
  long long idx_off, divisor = 1000000;

  if (ht == NULL) {
    printf("Error: dahash_dump_alphabetic(): attempt to dump NULL table.\n");
    exit(1);
  }
  start_time = what_time_is_it();
  permute = (byte **)malloc(ht->entries_used * sizeof(byte *));
  if (permute == NULL) error_exit("Malloc of permute failed");
  // permute is freed at the end of this function.
  idx_off = 0;
  p = 0;
  for (e = 0; e < ht->capacity; e++) {
    if (((byte *)(ht->table))[idx_off]) {
      permute[p++] = ((byte *)(ht->table)) + idx_off;
    }
    idx_off += ht->entry_size;
  }

  qsort(permute, p, sizeof(byte *), compare_keys_alphabetic);
  // NOTE: Could save a bit of memory using qsort_r() and defining permute
  // as an array of integers rather than an array of pointers.
  printf("   Data sorted.  Time taken: %.2f sec\n", what_time_is_it() - start_time);

  start_time = what_time_is_it();
  for (e = 0; e < p; e++) {
    //valp = (long long *)(permute[e] + MAX_REP_LEN + 1);
    //printf("%s\t%I64d\n", permute[e], *valp);
    buffered_write(term_rep_handle, &writebuf, BUFSIZE, &bytes_in_writebuf,
		   permute[e], (size_t)(MAX_REP_LEN + 1 + sizeof(long long)), "repetitions");
    if (e > 0 && e % divisor == 0)  {
      printf("   --- %d ---\n", e);
      if (e % (divisor * 10) == 0) divisor *= 10;
    }
  }
  free(permute);
  if (bytes_in_writebuf > 0) buffered_flush(term_rep_handle, &writebuf, &bytes_in_writebuf, "repetitions", TRUE);  // TRUE will clean up

  printf("File QBASH.repetitions written.  Time taken: %.2f sec\n", what_time_is_it() - start_time);
}


int main(int argc, char* argv[]) {
  byte *fileinmem, *p, *linestart, *ratbuf = NULL;
  size_t scanned = 0, filesize;
  CROSS_PLATFORM_FILE_HANDLE fh;
  HANDLE h;
  int error_code;
  long long line_count = 0, interval = 10000;
  u_char *filename = NULL, *end_of_file, *lastslash;
  FILE *ratios = NULL;

  setvbuf(stdout, NULL, _IONBF, 0);
  if (argc < 2) {
    printf("Usage: %s <Input file in TSV format, e.g. QBASH.forward> [-singletons_too]\n"
	   "        Only looks at text in column one of the TSV\n"
	   "        Output will be in QBASH.repetitions in same directory as input TSV\n"
	   "		 Ratios of unique terms to length e.g. will be put out to term_ratios.tsv\n\n"
	   "        If -singletons_too is given, all term occurrences will be recorded.\n\n",
	   argv[0]);
    exit(1);
  }

  if (argc > 2) {
    if (!strcmp(argv[2], "-singletons_too")) {
      min_occurrences = 1;
    }
  }

  start_time = what_time_is_it();
  very_start = start_time;

  initialize_unicode_conversion_arrays(TRUE);

  initialize_ascii_non_tokens(token_break_set, TRUE);

  term_rep_table = dahash_create((u_char *)"repetitions", 24, MAX_REP_LEN,
				 sizeof(long long), (double)0.9, TRUE);

  filename = (u_char *)malloc(strlen(argv[1]) + 50);
  if (filename == NULL) error_exit("Malloc failed for filename\n");
  strcpy((char *)filename, argv[1]);

  fileinmem = (byte *)mmap_all_of((u_char *)filename, &filesize, FALSE, &fh, &h, &error_code);
  if (fileinmem == NULL) {
    printf("Error:  Failed to mmap %s, error_code was %d\n", filename, error_code);
    exit(1);
  }
  //printf("File %s mmapped:  Time taken: %.2f sec\n", filename, what_time_is_it() - start_time);

  lastslash = (u_char *)strrchr((char *)filename, '/');
  if (lastslash == NULL) strcpy((char *)filename, "QBASH.repetitions");
  else strcpy((char *)lastslash + 1, "QBASH.repetitions");
  printf("Output will be written to %s\n", filename);
  printf("Min_occurrences: %d\n", min_occurrences);

  term_rep_handle = open_w((char *)filename, &error_code);
  if (error_code) {
    error_exit("Unable to open .repetitions file for writing.");
  }

  if (lastslash == NULL) strcpy((char *)filename, "term_ratios.tsv");
  else strcpy((char *)lastslash + 1, "term_ratios.tsv");
  printf("Distinct terms and max_tf for each record will be written to %s\n\n", filename);
  ratios = fopen((char *)filename, "wb");
  if (ratios == NULL) error_exit("Unable to write to ratios file.\n");
  ratbuf = (byte *)malloc(BUFSIZE);
  if (ratbuf == NULL) error_exit("Malloc of ratbuf failed.\n");
  setvbuf(ratios, (char *)ratbuf, _IOFBF, BUFSIZE);

  p = fileinmem;
  end_of_file = fileinmem + filesize - 1;
  linestart = p;
  start_time = what_time_is_it();
  fprintf(ratios, "#line_count\twords\tdistinct_words\tmax_tf\tdistinct ratio\tmax_tf ratio\n");
  do {
    line_count++;
    if (line_count % interval == 0) {
      printf("   --- %s line %10lld scanned ---\n", argv[1], line_count);
      if (line_count % (interval * 10) == 0) interval *= 10;
    }
    p = process_repetitions(line_count, linestart,
			    end_of_file, ratios);

    // Now skip to the next linefeed
    while (p < end_of_file && *p != '\n') p++;
    p++;
    linestart = p;
    scanned = p - fileinmem;
    //printf("Scanned, Filesize = %I64d, %I64d\n", scanned, filesize);
  } while (scanned < filesize);

  unmmap_all_of(fileinmem, fh, h, filesize);
  printf("File %s scanned.  Time taken: %.2f sec\n", argv[1], what_time_is_it() - start_time);
  fclose(ratios);
  free(ratbuf);
  printf("File %s written.\n", filename);
  printf("term_rep_table entries used: %zu\nOutput will be in .vocab format in QBASH.repetitions\n",
	 term_rep_table->entries_used);
  dahash_dump_alphabetic_simple(term_rep_table);
  dahash_destroy(&term_rep_table);
  close_file(term_rep_handle);
  printf("\nAll done.  Total postings: %lld.  Highest TF: %d.  Total time taken: %.2f sec\n", 
	 total_word_occs, highest_tf_overall, what_time_is_it() - very_start);

  return 0;
}
