// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

#include "../definitions.h"
#include "../u/arg_parser.h"
#include "../u/unicode.h"
#include "../u/utility_nodeps.h"
#include "q.h"
#include "qArgTable.h"

#define MAX_QTERMS 100
#define MAX_FGETS 2048

params_t params;

static void print_usage(char *progname, arg_t *args) {
  printf("\n\nUsage: %s You must specify an indexStem.", progname);
  print_args(stdout, TEXT, args);
  exit(1);
}

typedef struct {
  int highest_unprocessed_score;
  int current_run_len;
  int postings_remaining;
  byte *if_pointer;
} term_control_block_t;


static term_control_block_t term_control_block[MAX_QTERMS];

static int *accumulators = NULL, *fake_heap = NULL, items_in_fake_heap = 0;

static void insert_in_fake_heap(int docid, int score) {
  // The fake heap is just an array of up to params.k docids, sorted
  // in descending order of the partial scores associated with those docids
  int i, j, lowest;
  BOOL inserted = FALSE;
  if (params.debug) fprintf(stderr, "         Inserting docid %d (score %d) in fake_heap.\n",
		 docid, score);

  // This docid may already be in the heap with a partial score.  Is it?
  for (i = 0; i < items_in_fake_heap; i++) {
    if (fake_heap[i] == docid) {
      // Yes it is.  Remove it.
      for (j = i + 1; j < items_in_fake_heap; j++) fake_heap[j - 1] = fake_heap[j];
      items_in_fake_heap--;
      break;
    }
  }
 
  
  if (items_in_fake_heap == 0) {  // Empty fake heap
    fake_heap[items_in_fake_heap++] = docid;
    if (0) fprintf(stderr, "FH: Inserted doc %d as first item\n", docid);
    return;   // --------------------------------->
  }
  
  if (items_in_fake_heap == params.k) {  // It's full
    // Is there going to be a slot for this one?
    if (0) fprintf(stderr, "FH: Inserting %d into full fake heap\n", docid);
    if (score <= accumulators[fake_heap[params.k - 1]]) return; // ----------no ---->
    for (i = 0; i < items_in_fake_heap; i++) {
      if (score >= accumulators[fake_heap[i]]) {
	// push down and insert this new docid at position i, dropping off
	// the current lowest scoring item.
	lowest = params.k - 1;
	for (j = lowest; j > i; j--) fake_heap[j] = fake_heap[j - 1];
	fake_heap[i] = docid;
	return;   // --------------------------------->
      }
    }
    return;   // --------------------------------->
  }

  // The fake heap is only partly full, this one's going to go in somewhere
  if (0) fprintf(stderr, "FH: Inserting %d into fake heap with %d items\n",
		 docid, items_in_fake_heap);
  for (i = 0; i < items_in_fake_heap; i++) {
    if (score >= accumulators[fake_heap[i]]) {
      // push down and insert this new docid at position i.
      lowest = items_in_fake_heap;
      if (lowest >= params.k) lowest = params.k - 1;
      for (j = lowest; j > i; j--) fake_heap[j] = fake_heap[j - 1];
      fake_heap[i] = docid;
      inserted = TRUE;
      items_in_fake_heap++;
      return;   // --------------------------------->
    }
  }
  if (!inserted) {
    // Must insert it at the end.
    fake_heap[items_in_fake_heap++] = docid;
  }
}


static int vcmp(const void *ip, const void *jp) {
  // Comparison function for bsearch.  Numerically compare two termids
  // represented as BYTES_FOR_TERMID bytes in byte-order independent order
  byte *bip = (byte *)ip, *bjp = (byte *)jp;
  int i, j;
  i = (int) make_ull_from_n_bytes(bip, BYTES_FOR_TERMID);
  j = (int) make_ull_from_n_bytes(bjp, BYTES_FOR_TERMID);
  if (i > j) return 1;
  if (j > i) return -1;
  return 0;
}



static int term_lookup(int termid, byte *vocab_in_mem, size_t vocab_terms) {
  byte *ve, key[BYTES_IN_VOCAB_ENTRY] = {0};
  store_least_sig_n_bytes((u_ll)termid, key, BYTES_FOR_TERMID);
  ve = (byte *)bsearch(key, vocab_in_mem, vocab_terms, BYTES_IN_VOCAB_ENTRY, vcmp);
  if (ve == NULL) {
    return -1;
  } else return (ve - vocab_in_mem) / BYTES_IN_VOCAB_ENTRY;
}



static void process_query(int queryid, int *query_array, int q_len, byte *vocab_in_mem, byte *if_in_mem,
			  size_t vocab_size, size_t if_size, int *accumulators) {
  int q, t = 0, terms_still_going = q_len, docid;
  byte *vocab_entry;
  u_ll tmp, if_offset, postings_processed = 0;

  if (params.debug) fprintf(stderr, "Q: Processing query %d.  %d accumulators.\n",
			    queryid, params.numDocs);

  memset(accumulators, 0, params.numDocs * sizeof(int));
  memset(fake_heap, 0, params.k * sizeof(int));
  memset(term_control_block, 0,  q_len * sizeof(term_control_block_t));
  
  items_in_fake_heap = 0;
  // for each query term we need to keep track of:
  //   1. The highest unprocessed score
  //   2. The length of the run of thos scores, 
  //   3. The number of postings remaining, and
  //   4. The next spot to read in the in-memory IF

  // ------------- Set up the control blocks --------------
  for (q = 0; q < q_len; q++) {
    if (query_array[q] == 0) {
      // A check for a regression in the indexer
      int t = make_ull_from_n_bytes(vocab_in_mem, BYTES_FOR_TERMID);
      if (t != 0) {
	printf("Error: vocab_in_mem doesn't start with term 0.  t = %d\n", t);
	exit(1);
      }
    }
    
    t = term_lookup(query_array[q], vocab_in_mem, vocab_size / BYTES_IN_VOCAB_ENTRY);
    if (t < 0) {
      printf("Error: Lookup failed for term %d\n", query_array[q]);
      exit(1);
    }
    
    vocab_entry = vocab_in_mem + t * BYTES_IN_VOCAB_ENTRY;
    tmp = make_ull_from_n_bytes(vocab_entry + BYTES_FOR_TERMID, BYTES_FOR_POSTINGS_COUNT);
    term_control_block[q].postings_remaining = (int)tmp;
    if (params.debug > 0) fprintf(stderr, "  setting up for term %d (termid %d, postings remaining %llu): \n",
				  q, t, tmp);
    if (tmp > 0) {
      if_offset = make_ull_from_n_bytes(vocab_entry + BYTES_FOR_TERMID + BYTES_FOR_POSTINGS_COUNT,
					BYTES_FOR_INDEX_OFFSET);
      term_control_block[q].if_pointer = if_in_mem + if_offset;
      // Read the qscore from the run header
      term_control_block[q].highest_unprocessed_score =
	(int) make_ull_from_n_bytes(term_control_block[q].if_pointer, BYTES_FOR_QSCORE);
      term_control_block[q].if_pointer += BYTES_FOR_QSCORE;
      // Read the run length from the run header
      term_control_block[q].current_run_len =
	(int) make_ull_from_n_bytes(term_control_block[q].if_pointer, BYTES_FOR_RUN_LEN);
      term_control_block[q].if_pointer += BYTES_FOR_RUN_LEN;
      if (params.debug > 0) fprintf(stderr,
				    "     postings remaining: %d\n"
				    "     index offset: %llu\n"
				    "     highest qscore: %d\n"
				    "     length of run: %d\n",
				    term_control_block[q].postings_remaining,
				    if_offset,
				    term_control_block[q].highest_unprocessed_score,
				    term_control_block[q].current_run_len);

    } else {
      fprintf(stderr, "Error: the number of postings for term %d is zero. That can't be!\n", 
	      t);
      exit(1);
    }
  }
  
  if (params.debug) fprintf(stderr, "Q: Control blocks set up.\n");

  // ---------- Now process the query in SAAT fashion -----------
  while (terms_still_going > 0) {
    // find the highest current score.
    int max_qscore = -1, chosen = -1, p;
    for (q = 0; q < q_len; q++) {
      if (term_control_block[q].postings_remaining > 0) {
	if (term_control_block[q].highest_unprocessed_score > max_qscore) {
	  max_qscore = term_control_block[q].highest_unprocessed_score;
	  chosen = q;
	}
      }
    }

    if (chosen == -1) {
      fprintf(stderr, "Error: Unable to find a best.  Huh???\n");
      exit(1);
    }

    // Process the run from the chosen one unless we've hit a cutoff
    if (params.debug) fprintf(stderr, "         Processing a run of %d for term %d (termid %d).\n",
		   term_control_block[chosen].current_run_len, chosen, query_array[chosen]);
    if (max_qscore >= params.lowScoreCutoff) {
    
      for (p = 0; p < term_control_block[chosen].current_run_len; p++) {
	docid = (int) make_ull_from_n_bytes(term_control_block[chosen].if_pointer, BYTES_FOR_DOCID);
	if (params.debug) fprintf(stderr, "   .. adding %d to %d to make new score for doc %d\n",
		       max_qscore, accumulators[docid], docid);
	accumulators[docid] += max_qscore;
	insert_in_fake_heap(docid, accumulators[docid]);	
	term_control_block[chosen].if_pointer += BYTES_FOR_RUN_LEN;
      }

      term_control_block[chosen].postings_remaining -= term_control_block[chosen].current_run_len;
      postings_processed += term_control_block[chosen].current_run_len;
     
      if (params.postingsCountCutoff > 0 && postings_processed > params.postingsCountCutoff) {
	if (params.debug) fprintf(stderr, "Early termination due to postings count: > %d\n", params.postingsCountCutoff); 
	break;  // Early termination --------------------------------------------->
      }

      
      if (term_control_block[chosen].postings_remaining > 0) {
	// Read the qscore from the run header
	term_control_block[chosen].highest_unprocessed_score =
	  (int) make_ull_from_n_bytes(term_control_block[chosen].if_pointer, BYTES_FOR_QSCORE);
	term_control_block[chosen].if_pointer += BYTES_FOR_QSCORE;
	// Read the run length from the run header
	term_control_block[chosen].current_run_len =
	  (int) make_ull_from_n_bytes(term_control_block[chosen].if_pointer, BYTES_FOR_RUN_LEN);
	term_control_block[chosen].if_pointer += BYTES_FOR_RUN_LEN;
      } else {
	terms_still_going --;
	if (params.debug) fprintf(stderr, "Terms still going: %d\n", terms_still_going);		      
      }
    } else {
      if (params.debug) fprintf(stderr, "Early termination due to low score cutoff: < %d\n", params.lowScoreCutoff); 
      break;  // Early termination: Reached low score cutoff ------->
    }

  }

  // ------ now produce the ranking ---------
  if (params.debug) fprintf(stderr, "Q: Producing a ranking.\n");

  // Commented-out statements produce format used prior to changing over
  // to TREC-style submission format.
  //printf("Query %d:", queryid);
  //for (q = 0; q < q_len; q++) {
  //  printf(" %d", query_array[q]);
  //}
  //printf(" ...  k = %d\n", params.k);

  for (t = 0; t < items_in_fake_heap; t++) {
    //    printf("   %5d %7d %7d   # rank, docid, score\n",
    //	   t + 1, fake_heap[t], accumulators[fake_heap[t]]);
    printf("%3d Q0 %7d %5d %7d SATIRE\n", queryid, fake_heap[t],
	   t + 1, accumulators[fake_heap[t]]);
  }
  // printf("\n"); 
}


int main(int argc, char **argv) {
  byte *vocab_in_mem = NULL, *if_in_mem = NULL;
  size_t vocab_size, if_size;
  char *ignore, *fgets_buf, *p, *q, *fname_buf;
  CROSS_PLATFORM_FILE_HANDLE vocabh, ifh;
  HANDLE vocabmh, ifmh;
  int a, error_code, t, query_array[MAX_QTERMS], q_count = 0, stemlen, queryid, termid;
  double start_time = what_time_is_it();
    

  setvbuf(stderr, NULL, _IONBF, 0);
  setvbuf(stdout, NULL, _IONBF, 0);
  
  initialiseParams(&params);
  fprintf(stderr, "Q: Params initialised\n");

  for (a = 1; a < argc; a++) {
    assign_one_arg(argv[a], (arg_t *)(&args), &ignore);
  }
  fprintf(stderr, "Q: Args assigned\n");

  if (params.indexStem == NULL  || params.numDocs <= 0) {
    print_usage(argv[0], (arg_t *)(&args));
  }

  fprintf(stderr, "Q: Opening the query input steam, assigning buffers etc.\n");
  
  fgets_buf = (char *)cmalloc(MAX_FGETS, (u_char *)"buffer for fgets()", FALSE);

  stemlen = (int) strlen(params.indexStem);
  fname_buf = cmalloc(stemlen + 50, (u_char *)"fname_buf", FALSE);
  strcpy(fname_buf, params.indexStem);
  if (params.debug) fprintf(stderr, "Q: Memory map the .vocab and .if files\n");
  strcpy(fname_buf + stemlen, ".vocab");
  vocab_in_mem = mmap_all_of((byte *)fname_buf, &vocab_size, FALSE, &vocabh, &vocabmh, &error_code);
  strcpy(fname_buf + stemlen, ".if");
  if_in_mem = mmap_all_of((byte *)fname_buf, &if_size, FALSE, &ifh, &ifmh, &error_code);

  accumulators = cmalloc(params.numDocs * sizeof(int), (u_char *)"accumulators", FALSE);
  
  fake_heap = cmalloc(params.k * sizeof(int), (u_char *)"fake_heap", FALSE);
 
  free(fname_buf);
  fname_buf = NULL;

  if (params.debug) fprintf(stderr, "Q: About to start reading queries from stdin ...\n"
			    "Queries consist of a numeric query-id, a tab, then a list of\n"
			    "space separated (integer) termids.\n");
  
  while (fgets(fgets_buf, MAX_FGETS, stdin) != NULL) {
    if (params.debug) fprintf(stderr, "\n\nQ: Read and process a line.\n%s\n", fgets_buf);
    q_count++;
    p = fgets_buf;
    queryid = strtol(p, &q, 10);
    if (p == q) break;  // no integer found
    p = q;
    while (*p == ' ') p++;
    if (*p != '\t') {
      fprintf(stderr, "Error:  A query must consist of a query id followed by a tab followed by termids\n");
      exit(1);
    }
    p++;
    t = 0;
    while (*p >= ' ') {
      termid = strtol(p, &q, 10);
      if (p == q) break;  // no integer found
      if (t >= MAX_QTERMS) {
	printf("Warning: Query too long.  Only first %d terms considered.\n",
	       MAX_QTERMS);
	break;
      }
      query_array[t] = termid;
      t++;
      p = q;
    }

    if (params.debug) fprintf(stderr, "    terms in this query: %d\n", t);
    process_query(queryid, query_array, t, vocab_in_mem, if_in_mem, vocab_size, if_size,
		  accumulators);
    if (q_count % 10 == 0) fprintf(stderr, "%8d\n", q_count);
  }
    

  unmmap_all_of(vocab_in_mem, vocabh, vocabmh, vocab_size);
  unmmap_all_of(if_in_mem, ifh, ifmh, if_size);
  free(accumulators);
  free(fake_heap);

  fprintf(stderr, "Q: %d queries processed in %.3f sec.\n",
	  q_count, what_time_is_it() - start_time);
  

}

