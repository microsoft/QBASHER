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
#define MAX_FGETS (TWOMEG * 100)  // 200 MB
params_t params;

static void print_usage(char *progname, arg_t *args) {
  printf("\n\nUsage: %s All of the following options in option=value format.", progname);
  print_args(stdout, TEXT, args);
  exit(1);
}

static void store_least_sig_n_bytes(u_ll data, byte *buf, int n) {
  // Write the n least significant bytes of data into buf
  // in big-endian format, i.e. least-significant last
  // buf must exist and have at least n bytes of storage.
  int i;
  for (i = n - 1; i >= 0; i--) {
    buf[i] = (byte)(data & 0xFF);
    data >>= 8;
  }
}


int main(int argc, char **argv) {
  int a, docid, termid, qscore, r, runlen, postings_count, error_code, stemlen;
  char *ignore, *fgets_buf, *p, *q, *fname_buf;
  size_t vbuf_size = TWOMEG, ibuf_size = TWOMEG, bytes_in_vbuf, bytes_in_ibuf;
  u_ll if_offset = 0, lines_read = 0, if_bytes_written;
  FILE *inf, *config;
  CROSS_PLATFORM_FILE_HANDLE vocabh, ifh;
  byte bytebuf[sizeof(u_ll)], *vbuf = NULL, *ibuf = NULL;
  
  setvbuf(stdout, NULL, _IONBF, 0);
  
  initialiseParams(&params);
  printf("Params initialised\n");

  for (a = 1; a < argc; a++) {
    assign_one_arg(argv[a], (arg_t *)(&args), &ignore);
  }
  printf("Args assigned\n");

  if (params.inputFileName == NULL || params.outputStem == NULL
      || params.numDocs <= 0 || params.numTerms <= 0) {
    print_usage(argv[0], (arg_t *)(&args));
  }

  printf("Opening the input file, assigning buffers etc.\n");
  
  fgets_buf = (char *)cmalloc(MAX_FGETS, (u_char *)"buffer for fgets()", FALSE);
  inf = fopen(params.inputFileName, "rb");
  if (inf == NULL) {
    printf("Error: failed to read %s\n", params.inputFileName);
    exit(1);
  }
  setvbuf(inf, NULL, _IOFBF, TWOMEG);

  printf("Opening output files: .cfg, .vocab, and .if\n");
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
    if (0) printf("Read and process a line.\n");
    lines_read++;
    p = fgets_buf;
    termid = strtol(p, &q, 10);
    if (0) printf("Wrote termid %d and offset %llu to .vocab\n", termid, if_offset);
    if (*q == '\t') q++;
    postings_count = 0;
    if_bytes_written = 0;
    while (*q >= ' ') {  // Loop over the runs.  Stop on NUL, CR, or LF
      p = q;
      qscore = strtol(p, &q, 10);
      if (q == p) break;  // No data
      p = q;
      runlen = strtol(p, &q, 10);
      if (q == p) break;  // No data
      if (0) printf("Score %d, runlength %d\n", qscore, runlen);
      store_least_sig_n_bytes((u_ll)qscore, bytebuf, BYTES_FOR_QSCORE);
      buffered_write(ifh, &ibuf, ibuf_size, &bytes_in_ibuf, bytebuf, BYTES_FOR_QSCORE, "qscore");

      store_least_sig_n_bytes((u_ll)runlen, bytebuf, BYTES_FOR_RUN_LEN);
      buffered_write(ifh, &ibuf, ibuf_size, &bytes_in_ibuf, bytebuf, BYTES_FOR_RUN_LEN, "runlen");
      if_bytes_written += (BYTES_FOR_QSCORE + BYTES_FOR_RUN_LEN);
    
      if (*q == '*') q++;  // Asterisks may have been inserted for legibility
      for (r = 0; r < runlen; r++) {
	p = q;
	docid = strtol(p, &q, 10);
	if (p == q) {
	  printf("Error: missing docid.\n");
	  exit(1);
	}
	store_least_sig_n_bytes((u_ll)docid, bytebuf, BYTES_FOR_RUN_LEN);
	buffered_write(ifh, &ibuf, ibuf_size, &bytes_in_ibuf, bytebuf, BYTES_FOR_DOCID, "docid");	
	if_bytes_written += BYTES_FOR_DOCID;
      }
      if (*q == '#') q++;  // Hash may have been inserted for legibility
      p = q;
      postings_count += runlen;
    }  // End of postings for one term
    // Write term-id, postings_count and index offset in byte-order independent fashion into .vocab
    if (0) printf("Writing vocab entry:  %d, %d, %llu\n", termid, postings_count, if_offset);
    store_least_sig_n_bytes((u_ll)termid, bytebuf, BYTES_FOR_TERMID);
    buffered_write(vocabh, &vbuf, vbuf_size, &bytes_in_vbuf, bytebuf, BYTES_FOR_TERMID, "termid");
    store_least_sig_n_bytes((u_ll)postings_count, bytebuf, BYTES_FOR_POSTINGS_COUNT);
    buffered_write(vocabh, &vbuf, vbuf_size, &bytes_in_vbuf, bytebuf, BYTES_FOR_POSTINGS_COUNT, "postings_count");
    store_least_sig_n_bytes((u_ll)if_offset, bytebuf, BYTES_FOR_INDEX_OFFSET);
    buffered_write(vocabh, &vbuf, vbuf_size, &bytes_in_vbuf, bytebuf, BYTES_FOR_INDEX_OFFSET, "ixoff");
    if_offset += if_bytes_written;
  }
  fclose(inf); 

  buffered_flush(vocabh, &vbuf, &bytes_in_vbuf, ".vocab", TRUE);
  buffered_flush(ifh, &ibuf, &bytes_in_ibuf, ".if", TRUE);


  close_file(vocabh);
  close_file(ifh);
  free(fgets_buf);
  printf("Hallelujah! %llu lines read, %llu bytes written to .if file\n", lines_read, if_offset);
  

}
