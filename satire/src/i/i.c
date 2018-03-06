// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h> 
#include <fcntl.h>
#include <time.h>
#include <math.h>

#include "../definitions.h"
#include "i.h"
#include "../u/utility_nodeps.h"
#include "../u/arg_parser.h"
#include "iArgTable.h"

#define TWOMEG 2097152
#define MAX_FGETS 1024  // It only has to be big enough for three numbers.
#define SCORE_MULTIPLIER 10000
params_t params;

static void print_usage(char *progname, arg_t *args) {
  printf("\n\nUsage: %s All of the below options in option=value format."
	 "\nNote that the input file must contain one line for each term-document score,\n"
	 "in the format <term-id>TAB<doc-id>TAB<score>, where term-id and doc-id are\n"
	 "positive integers and score is a floating point number in the range 0 - 1.\n"
	 "The file must be sorted first by ascending term-id, then by descending score,\n"
	 "then by ascending docid.  Don't say you weren't warned!\n\n"
	 "\nThe floating point scores are converted to integers by multiplying by 10000\n"
	 "and using floor().\n", progname);
  print_args(stdout, TEXT, args);
  exit(1);
}

int main(int argc, char **argv) {
  int a, docid, termid, qscore, cur_qscore = 0, runlen = 0, postings_count = 0, error_code, stemlen,
    *run_buf, cur_term = -1, r, num_distinct_terms = 0;
  char *ignore, *fgets_buf, *p, *q, *fname_buf;
  size_t vbuf_size = TWOMEG, ibuf_size = TWOMEG, bytes_in_vbuf, bytes_in_ibuf;
  u_ll if_offset = 0, lines_read = 0, if_bytes_written = 0, total_postings_count = 0, postings_ignored_count = 0;
  FILE *inf, *config;
  CROSS_PLATFORM_FILE_HANDLE vocabh, ifh;
  byte bytebuf[sizeof(u_ll)], *vbuf = NULL, *ibuf = NULL;
  double dscore, start_time = what_time_is_it();
  
  setvbuf(stdout, NULL, _IONBF, 0);
  
  initialiseParams(&params);
  printf("I: Params initialised\n");

  for (a = 1; a < argc; a++) {
    assign_one_arg(argv[a], (arg_t *)(&args), &ignore);
  }
  printf("I: Args assigned\n");

  if (params.inputFileName == NULL || params.outputStem == NULL
      || params.numDocs <= 0) {
    print_usage(argv[0], (arg_t *)(&args));
  }

  printf("I: Opening the input file, assigning buffers etc.\n");
  
  fgets_buf = (char *)cmalloc(MAX_FGETS, (u_char *)"buffer for fgets()", FALSE);
  inf = fopen(params.inputFileName, "rb");
  if (inf == NULL) {
    printf("Error: failed to read %s\n", params.inputFileName);
    exit(1);
  }
  setvbuf(inf, NULL, _IOFBF, TWOMEG);

  run_buf = (int *)cmalloc(params.numDocs * sizeof(int), (u_char *)"run_buf", FALSE);  //Allow for the worst possible case.
  

  printf("I: Opening output files: %s.cfg, %s.vocab, and %s.if\n",
	 params.outputStem, params.outputStem, params.outputStem);
  stemlen = (int) strlen(params.outputStem);
  fname_buf = cmalloc(stemlen + 50, (u_char *)"fname_buf", FALSE);
  strcpy(fname_buf, params.outputStem);
  strcpy(fname_buf + stemlen, ".cfg");
  config = fopen(fname_buf, "wb");
  if (config == NULL) {
    printf("Error: failed to write %s\n", fname_buf);
    exit(1);
  }
  print_args(config, TEXT, args);
  fclose(config);
  
  strcpy(fname_buf + stemlen, ".vocab");
  vocabh = open_w(fname_buf, &error_code);
  if (error_code !=0) {
    printf("Error: Can't write to %s.  Error code %d\n", fname_buf, error_code);
    exit(1);
  }
  strcpy(fname_buf + stemlen, ".if");
  ifh = open_w(fname_buf, &error_code);
  if (error_code !=0) {
    printf("Error: Can't write to %s.  Error code %d\n", fname_buf, error_code);
    exit(1);
  }

  free(fname_buf);
  fname_buf = NULL;

  
  while (fgets(fgets_buf, MAX_FGETS, inf) != NULL) {
    if (0) printf("I: Read and process a line.\n");
    lines_read++;
    p = fgets_buf;
    termid = strtol(p, &q, 10);
    if (*q != '\t') {
      printf("Error: Missing first tab in input line: %s\n", fgets_buf);
      exit(1);
    } else if (p == q) {
      printf("Error: Missing termid in input line: %s\n", fgets_buf);
      exit(1);
    }     
    p = q;
    docid  = strtol(p, &q, 10);
    if (*q != '\t') {
      printf("Error: Missing second tab in input line: %s\n", fgets_buf);
      exit(1);
    } else if (p == q) {
      printf("Error: Missing docid in input line: %s\n", fgets_buf);
      exit(1);
    } else if (docid < 0 || docid >= params.numDocs) {
      printf("Error: docid %d in line %s is not in range 0 - %d\n", docid, fgets_buf, params.numDocs - 1);
      exit(1);
    }


    p = q;
    dscore = strtod(p, &q);
    
    if (p == q) {
      printf("Error: Missing score in input line: %s\n", fgets_buf);
      exit(1);
    } else if (dscore > 1.0 || dscore < 0.0) {
      printf("Error: Score %10g in line %s is not in range 0 - 1\n", dscore, fgets_buf);
      exit(1);
    }
    qscore = (int) floor(dscore * SCORE_MULTIPLIER);
    // Great!  We've read a valid line.  We have termid, docid and qscore

    if (qscore < params.lowScoreCutoff) {
      // Score is too low.  Treat this posting as though it didn't exist.
      postings_ignored_count++;
      continue;
    }
    if (termid != cur_term) {
      // We've encountered the end of the scores for one term
      num_distinct_terms++;
      if (num_distinct_terms % 10 == 0)
	printf("I: Distinct terms encountered: %d\n", num_distinct_terms);
      if (cur_term >= 0) {
	if (runlen > 0) {
	  // We need to store the last run for the previous term.
	  if (0) printf("I: Score %d, runlength %d\n", cur_qscore, runlen);
	  store_least_sig_n_bytes((u_ll)cur_qscore, bytebuf, BYTES_FOR_QSCORE);
	  buffered_write(ifh, &ibuf, ibuf_size, &bytes_in_ibuf, bytebuf, BYTES_FOR_QSCORE, "qscore");
	  store_least_sig_n_bytes((u_ll)runlen, bytebuf, BYTES_FOR_RUN_LEN);
	  buffered_write(ifh, &ibuf, ibuf_size, &bytes_in_ibuf, bytebuf, BYTES_FOR_RUN_LEN, "runlen");
	  if_bytes_written += (BYTES_FOR_QSCORE + BYTES_FOR_RUN_LEN);
	  postings_count += runlen;
	  for (r = 0; r < runlen; r++) {
	    store_least_sig_n_bytes((u_ll)run_buf[r], bytebuf, BYTES_FOR_DOCID);
	    buffered_write(ifh, &ibuf, ibuf_size, &bytes_in_ibuf, bytebuf, BYTES_FOR_DOCID, "docid");
	    if_bytes_written += BYTES_FOR_DOCID;
	  }
	}

	// And to write term-id, postings_count and index offset in byte-order independent fashion into .vocab
	if (0) printf("I: Writing vocab entry:  %d, %d, %llu\n", termid, postings_count, if_offset);
	store_least_sig_n_bytes((u_ll)cur_term, bytebuf, BYTES_FOR_TERMID);
	buffered_write(vocabh, &vbuf, vbuf_size, &bytes_in_vbuf, bytebuf, BYTES_FOR_TERMID, "termid");
	store_least_sig_n_bytes((u_ll)postings_count, bytebuf, BYTES_FOR_POSTINGS_COUNT);
	buffered_write(vocabh, &vbuf, vbuf_size, &bytes_in_vbuf, bytebuf, BYTES_FOR_POSTINGS_COUNT, "postings_count");
	store_least_sig_n_bytes((u_ll)if_offset, bytebuf, BYTES_FOR_INDEX_OFFSET);
	buffered_write(vocabh, &vbuf, vbuf_size, &bytes_in_vbuf, bytebuf, BYTES_FOR_INDEX_OFFSET, "ixoff");
	total_postings_count += postings_count;
      }
      runlen = 0;
      cur_qscore = qscore;
      cur_term = termid;
      postings_count = 0;  // For the new term.
      if_offset += if_bytes_written;
      if_bytes_written = 0;
    } else if (qscore != cur_qscore) {
      // We've encountered the end of a run but not the end of a term
      if (0) printf("I: Score2 %d, runlength %d\n", cur_qscore, runlen);
      store_least_sig_n_bytes((u_ll)cur_qscore, bytebuf, BYTES_FOR_QSCORE);
      buffered_write(ifh, &ibuf, ibuf_size, &bytes_in_ibuf, bytebuf, BYTES_FOR_QSCORE, "qscore");
      store_least_sig_n_bytes((u_ll)runlen, bytebuf, BYTES_FOR_RUN_LEN);
      buffered_write(ifh, &ibuf, ibuf_size, &bytes_in_ibuf, bytebuf, BYTES_FOR_RUN_LEN, "runlen");
      if_bytes_written += (BYTES_FOR_QSCORE + BYTES_FOR_RUN_LEN);
	  for (r = 0; r < runlen; r++) {
	    store_least_sig_n_bytes((u_ll)run_buf[r], bytebuf, BYTES_FOR_DOCID);
	    buffered_write(ifh, &ibuf, ibuf_size, &bytes_in_ibuf, bytebuf, BYTES_FOR_DOCID, "docid");
	    if_bytes_written += BYTES_FOR_DOCID;
	  }
      postings_count += runlen;
      runlen = 0;
      cur_qscore = qscore;
    }
 
    // Add the current docid to the run_buf
    run_buf[runlen++] = docid;
  }  // End of main while loop

  // Have to write the final run and the vocab entry for the final term.
  if (cur_term >= 0) {
    if (runlen > 0) {
      // We need to store the last run for the previous term.
      if (0) printf("I:Final score %d, runlength %d\n", cur_qscore, runlen);
      store_least_sig_n_bytes((u_ll)cur_qscore, bytebuf, BYTES_FOR_QSCORE);
      buffered_write(ifh, &ibuf, ibuf_size, &bytes_in_ibuf, bytebuf, BYTES_FOR_QSCORE, "qscore");
      store_least_sig_n_bytes((u_ll)runlen, bytebuf, BYTES_FOR_RUN_LEN);
      buffered_write(ifh, &ibuf, ibuf_size, &bytes_in_ibuf, bytebuf, BYTES_FOR_RUN_LEN, "runlen");
      if_bytes_written += (BYTES_FOR_QSCORE + BYTES_FOR_RUN_LEN);
      postings_count += runlen;
      for (r = 0; r < runlen; r++) {
	store_least_sig_n_bytes((u_ll)run_buf[r], bytebuf, BYTES_FOR_DOCID);
	buffered_write(ifh, &ibuf, ibuf_size, &bytes_in_ibuf, bytebuf, BYTES_FOR_DOCID, "docid");
	if_bytes_written += BYTES_FOR_DOCID;
      }
    }

    // And to write term-id, postings_count and index offset in byte-order independent fashion into .vocab
    if (0) printf("I: Writing final vocab entry:  %d, %d, %llu\n", termid, postings_count, if_offset);
    store_least_sig_n_bytes((u_ll)cur_term, bytebuf, BYTES_FOR_TERMID);
    buffered_write(vocabh, &vbuf, vbuf_size, &bytes_in_vbuf, bytebuf, BYTES_FOR_TERMID, "termid");
    store_least_sig_n_bytes((u_ll)postings_count, bytebuf, BYTES_FOR_POSTINGS_COUNT);
    buffered_write(vocabh, &vbuf, vbuf_size, &bytes_in_vbuf, bytebuf, BYTES_FOR_POSTINGS_COUNT, "postings_count");
    store_least_sig_n_bytes((u_ll)if_offset, bytebuf, BYTES_FOR_INDEX_OFFSET);
    buffered_write(vocabh, &vbuf, vbuf_size, &bytes_in_vbuf, bytebuf, BYTES_FOR_INDEX_OFFSET, "ixoff");
    total_postings_count += postings_count;
    if_offset += if_bytes_written;
    if_bytes_written = 0;
  }

 
  fclose(inf); 

  buffered_flush(vocabh, &vbuf, &bytes_in_vbuf, ".vocab", TRUE);
  buffered_flush(ifh, &ibuf, &bytes_in_ibuf, ".if", TRUE);


  close_file(vocabh);
  close_file(ifh);
  free(fgets_buf);
  printf("I: %llu lines read. %llu postings indexed + %llu postings ignored, %llu bytes written to .if file\n",
	 lines_read, total_postings_count, postings_ignored_count, if_offset);
  printf("I: Time taken: %.3f sec.\n", what_time_is_it() - start_time);
  

}
