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
#define ACC_BLOCK_SIZE 1024  // The accumulator array is divided into blocks containing ACC_BLOCK_SIZE accumulators 

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

#define NUM_COUNTERS 10

typedef enum {
  POSTINGS_PROCESSED,
  ALREADY_IN_HEAP_COMPARISONS,
  OTHER_HEAP_COMPARISONS,
  HEAP_ITEMS_MOVED,
  INSERT_INTO_EMPTY_HEAP,
  INSERT_INTO_FULL_HEAP,
  INSERT_INTO_PARTIAL_HEAP,
  ACC_BLOCKS_USED,
  ACC_BLOCKS,
  ACCUMULATORS_USED
} counter_t;
  
  
static u_ll per_query_counter[NUM_COUNTERS],  global_counter[NUM_COUNTERS] = {0};


static term_control_block_t term_control_block[MAX_QTERMS];

static int *accumulators = NULL, *fake_heap = NULL, items_in_fake_heap = 0, num_acc_blocks = 0;

static byte *acc_block_dirty_flags = NULL;


static void explain_counters() {
  fprintf(stderr, "Output lines starting with 'COUNTERS-' include a counter type code which is either PQ<qnum> (Per Query)\n"
	  "or Global) and the values of %d counters:\n"
	  " 2 - Number of postings processed.\n"
	  " 3 - Number of comparisons to check whether new item is already in heap.\n"
	  " 4 - Number of other comparisons with heap items.\n"
	  " 5 - Number of times an item is moved one slot up or down the heap.\n"
	  " 6 - Number of times an item was attempted to be inserted into an empty heap.\n"
	  " 7 - Number of times an item was attempted to be inserted into a full heap.\n"	 
	  " 8 - Number of times an item was attempted to be inserted into a partially occupied heap.\n"
	  " 9 - Number of accumulator blocks touched.\n"
	  "10 - Number of accumulator blocks defined.\n"
	  "11 - Number of accumulators used.\n\n",
	 NUM_COUNTERS);
}


static void add_to_global_counters() {
  int i;
  for (i = 0; i < NUM_COUNTERS; i++)
    global_counter[i] += per_query_counter[i];
}


static void zero_counter_array(u_ll *counter_array) {
  int i;
  for (i = 0; i < NUM_COUNTERS; i++) counter_array[i] = 0;
}


static void print_per_query_counters(int qnum) {
  int i;
  fprintf(stderr, "COUNTERS-PQ%03d  ", qnum);
  for (i = 0; i < NUM_COUNTERS; i++)  fprintf(stderr, "% 11lld", per_query_counter[i]);
  fprintf(stderr, "\n");
}


static void print_global_counters() {
  int i;
  fprintf(stderr, "COUNTERS-GB      ");
  for (i = 0; i < NUM_COUNTERS; i++)  fprintf(stderr, "% 11lld", global_counter[i]);
  fprintf(stderr, "\n");
}


static void zero_accumulators() {
  // The array of accumulators is divided into blocks of ACC_BLOCK_SIZE accumulators
  // Each of these is guarded by a dirty flag.   if the dirty flag is not set, it may
  // be safely assumed that all the accumulators in the block are already zero.
  // Note that the last block is always a full one, with some accumulators at the
  // end never being used.

  int b, start;
  for (b = 0; b < num_acc_blocks; b++) {
    if (acc_block_dirty_flags[b]) {
      // dirty flag is set. We need to zero the accumulators in the block
      start = b * ACC_BLOCK_SIZE;
      memset((void *)(accumulators + start), 0, ACC_BLOCK_SIZE * sizeof(int));
      acc_block_dirty_flags[b] = 0;
    }
  }
}

static void insert_in_fake_heap(int docid, int score) {
  // The fake heap is just an array of up to params.k docids, sorted
  // in descending order of the partial scores associated with those docids
  int i, j, lowest;
  BOOL inserted = FALSE;
  if (params.debug) fprintf(stderr, "         Inserting docid %d (score %d) in fake_heap.\n",
		 docid, score);


  if (items_in_fake_heap == params.k
      && score <= accumulators[fake_heap[items_in_fake_heap - 1]]) return; //Skip a whole lot of unnecessary work

  // This docid may already be in the heap with a partial score.  Is it?
  for (i = 0; i < items_in_fake_heap; i++) {
    per_query_counter[ALREADY_IN_HEAP_COMPARISONS]++;
    if (fake_heap[i] == docid) {
      // Yes it is.  Remove it.
      for (j = i + 1; j < items_in_fake_heap; j++) {
	per_query_counter[HEAP_ITEMS_MOVED]++;
	fake_heap[j - 1] = fake_heap[j];
      }
      items_in_fake_heap--;
      break;
    }
  }
 
  
  if (items_in_fake_heap == 0) {  // Empty fake heap
    per_query_counter[INSERT_INTO_EMPTY_HEAP]++;
    fake_heap[items_in_fake_heap++] = docid;
    if (0) fprintf(stderr, "FH: Inserted doc %d as first item\n", docid);
    return;   // --------------------------------->
  }
  
  if (items_in_fake_heap == params.k) {  // It's full
    // Is there going to be a slot for this one?
    if (0) fprintf(stderr, "FH: Inserting %d into full fake heap\n", docid);
    per_query_counter[INSERT_INTO_FULL_HEAP]++;
    for (i = 0; i < items_in_fake_heap; i++) {
      per_query_counter[OTHER_HEAP_COMPARISONS]++;
      if (score >= accumulators[fake_heap[i]]) {
	// push down and insert this new docid at position i, dropping off
	// the current lowest scoring item.
	lowest = params.k - 1;
	for (j = lowest; j > i; j--) {
	  per_query_counter[HEAP_ITEMS_MOVED]++;
	  fake_heap[j] = fake_heap[j - 1];
	}
	fake_heap[i] = docid;
	return;   // --------------------------------->
      }
    }
    return;   // --------------------------------->
  }

  // The fake heap is only partly full, this one's going to go in somewhere
  if (0) fprintf(stderr, "FH: Inserting %d into fake heap with %d items\n",
		 docid, items_in_fake_heap);
  per_query_counter[INSERT_INTO_PARTIAL_HEAP]++;
  for (i = 0; i < items_in_fake_heap; i++) {
    per_query_counter[OTHER_HEAP_COMPARISONS]++;
    if (score >= accumulators[fake_heap[i]]) {
      // push down and insert this new docid at position i.
      lowest = items_in_fake_heap;
      if (lowest >= params.k) lowest = params.k - 1;
      for (j = lowest; j > i; j--) {
	per_query_counter[HEAP_ITEMS_MOVED]++;
	fake_heap[j] = fake_heap[j - 1];
      }
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
  int q, t = 0, terms_still_going = q_len, docid, block = 0;
  byte *vocab_entry;
  u_ll tmp, if_offset;

  if (params.debug) fprintf(stderr, "Q: Processing query %d.  %d accumulators.\n",
			    queryid, params.numDocs);

  //memset(accumulators, 0, params.numDocs * sizeof(int));
  per_query_counter[ACC_BLOCKS] = num_acc_blocks;
  zero_accumulators();
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
      fprintf(stderr, "Warning: Lookup failed for term %d in query %d\n",
	     query_array[q], queryid);
      term_control_block[q].postings_remaining = 0;
      if_offset = 0;
      term_control_block[q].highest_unprocessed_score = 0;
      term_control_block[q].current_run_len = 0;
      terms_still_going--;
    } else {    
      vocab_entry = vocab_in_mem + t * BYTES_IN_VOCAB_ENTRY;
      tmp = make_ull_from_n_bytes(vocab_entry + BYTES_FOR_TERMID, BYTES_FOR_POSTINGS_COUNT);
      term_control_block[q].postings_remaining = (int)tmp;
      if (params.debug > 0)
	fprintf(stderr, "  setting up for term %d in query %d (termid %d, postings remaining %llu):\n",
		q, queryid, t, tmp);
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

      } else {
	fprintf(stderr, "Error: the number of postings for term %d in query %d is zero. That can't be!\n", 
		queryid, t);
	exit(1);
      }
    }
    if (params.debug > 0) fprintf(stderr,
				  "     postings remaining: %d\n"
				  "     index offset: %llu\n"
				  "     highest qscore: %d\n"
				  "     length of run: %d\n",
				  term_control_block[q].postings_remaining,
				  if_offset,
				  term_control_block[q].highest_unprocessed_score,
				  term_control_block[q].current_run_len);
  }
  
  if (params.debug) fprintf(stderr, "Q: Control blocks set up for query %d.\n", queryid);

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
      fprintf(stderr, "Error: Unable to find a best for query %d.  Huh???\n",
	      queryid);
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
	// Dealing with the accumulators
	block = docid / ACC_BLOCK_SIZE;
	if (acc_block_dirty_flags[block] == 0) {
	  per_query_counter[ACC_BLOCKS_USED]++;
	  acc_block_dirty_flags[block] = 1;
	}
	if (accumulators[docid] == 0) per_query_counter[ACCUMULATORS_USED]++;
	accumulators[docid] += max_qscore;
	insert_in_fake_heap(docid, accumulators[docid]);	
	term_control_block[chosen].if_pointer += BYTES_FOR_RUN_LEN;
      }

      term_control_block[chosen].postings_remaining -= term_control_block[chosen].current_run_len;
      per_query_counter[POSTINGS_PROCESSED] += term_control_block[chosen].current_run_len;
     
      if (params.postingsCountCutoff > 0
	  && per_query_counter[POSTINGS_PROCESSED] > params.postingsCountCutoff) {
	if (params.debug)
	  fprintf(stderr, "Early termination of query %d due to postings count: > %d\n",
		  queryid, params.postingsCountCutoff); 
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
      if (params.debug)
	fprintf(stderr, "Early termination of query %d due to low score cutoff: < %d\n",
		queryid, params.lowScoreCutoff); 
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
    //    printf("%3d Q0 %7d %5d %7d SATIRE\n", queryid, fake_heap[t],
    //     t + 1, accumulators[fake_heap[t]]);
	printf("%d\t%d\t%d\tSATIRE\n", queryid, fake_heap[t], t + 1);
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
  double start_time;
    

  setvbuf(stderr, NULL, _IONBF, 0);
  //setvbuf(stdout, NULL, _IONBF, 0);
  
  initialiseParams(&params);
  fprintf(stderr, "Q: Params initialised\n");

  for (a = 1; a < argc; a++) {
    assign_one_arg(argv[a], (arg_t *)(&args), &ignore);
  }
  fprintf(stderr, "Q: Args assigned\n");

  if (params.indexStem == NULL  || params.numDocs <= 0) {
    print_usage(argv[0], (arg_t *)(&args));
  }

  if (params.k < 1) {
    fprintf(stderr, "Warning:  value of k must be at least 1.  Adjusting %d to be 1 instead.\n",
	   params.k);
    params.k = 1;
  }

  fprintf(stderr, "Q: Opening the query input steam, assigning buffers etc.\n");
  
  fgets_buf = (char *)cmalloc(MAX_FGETS, (u_char *)"buffer for fgets()", FALSE);

  stemlen = (int) strlen(params.indexStem);
  fname_buf = cmalloc(stemlen + 50, (u_char *)"fname_buf", FALSE);
  strcpy(fname_buf, params.indexStem);
  if (params.debug) fprintf(stderr, "Q: Memory map the .vocab and .if files\n");
  strcpy(fname_buf + stemlen, ".vocab");
  vocab_in_mem = mmap_all_of((byte *)fname_buf, &vocab_size, FALSE, &vocabh, &vocabmh, &error_code);
  touch_all_pages(vocab_in_mem, vocab_size);   // warmup
  strcpy(fname_buf + stemlen, ".if");
  if_in_mem = mmap_all_of((byte *)fname_buf, &if_size, FALSE, &ifh, &ifmh, &error_code);
  touch_all_pages(if_in_mem, if_size);   // warmup

  num_acc_blocks = (params.numDocs / ACC_BLOCK_SIZE) + 1;   
  accumulators = cmalloc(num_acc_blocks * ACC_BLOCK_SIZE * sizeof(int), (u_char *)"accumulators", FALSE);
  acc_block_dirty_flags = cmalloc(num_acc_blocks, (u_char *)"acc_block_dirty_flags", FALSE);
  memset(acc_block_dirty_flags, 1, num_acc_blocks);  // Set all the accumulator blocks as DIRTY
  fake_heap = cmalloc(params.k * sizeof(int), (u_char *)"fake_heap", FALSE);
 
  free(fname_buf);
  fname_buf = NULL;

  if (params.debug) fprintf(stderr, "Q: About to start reading queries from stdin ...\n"
			    "Queries consist of a numeric query-id, a tab, then a list of\n"
			    "space separated (integer) termids.\n");
  start_time = what_time_is_it();
  zero_counter_array(global_counter);
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
	fprintf(stderr, "Warning: Query %d too long.  Only first %d terms considered.\n",
		queryid, MAX_QTERMS);
	break;
      }
      query_array[t] = termid;
      t++;
      p = q;
    }

    if (params.debug) fprintf(stderr, "    terms in this query: %d\n", t);
    zero_counter_array(per_query_counter);
    process_query(queryid, query_array, t, vocab_in_mem, if_in_mem, vocab_size, if_size,
		  accumulators);
    print_per_query_counters(q_count);
    add_to_global_counters();
    if (q_count % 10 == 0) fprintf(stderr, "%8d\n", q_count);
  }
    

  unmmap_all_of(vocab_in_mem, vocabh, vocabmh, vocab_size);
  unmmap_all_of(if_in_mem, ifh, ifmh, if_size);
  free(accumulators);
  free(fake_heap);

  print_global_counters();

  explain_counters();

  fprintf(stderr, "Q: %d queries processed in %.3f sec. since warmup.\n",
	  q_count, what_time_is_it() - start_time);
  

}

