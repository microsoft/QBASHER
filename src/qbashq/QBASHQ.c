// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// QBASHQ - A main program to run queries from standard input against a QBASH
// index, consisting of files (QBASH.doctable, .vocab, .if, .forward).  It uses
// the QBASHQ-LIB library.

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
#include <process.h>
#include <windows.h>
#include <WinBase.h>
#include <tchar.h>
#include <strsafe.h>
#include <Psapi.h>
#else
#include <pthread.h>
#include <time.h>  // For nanosleep()
#endif

#include "../shared/unicode.h"
#include "../shared/QBASHER_common_definitions.h"
#include "../shared/utility_nodeps.h"
#include "../utils/dahash.h"
#include "../qbashq-lib/QBASHQ.h"
#include "../qbashq-lib/arg_parser.h"
#include "../qbashq-lib/classification.h"
#define PCRE2_CODE_UNIT_WIDTH 8
#include "../imported/pcre2/pcre2.h"


long long input_offset = 0;   // Byte offset into the file of input queries


#define MAX_QUERY_PARALLELISM 100

// define a threadpool context object for multi-stream context processing

typedef struct {
	index_environment_t *ixenv;
	query_processing_environment_t *qoenv;
  u_char multi_query_string[MAX_QLINE + 1],
    mqs_copy[MAX_QLINE + 1],
    query_label[MAX_QLINE + 1];
	int thread;
} multistream_context_t;



static void respond_to_error(int code) {
	err_desc_t *err;
	int severity, category, mcode, ccode;
	err = explain_error(code);
	if (code >= 0) mcode = 0;
	else mcode = -code;   
	severity = mcode / 100000;
	ccode = mcode % 100000;
	category = ccode / 10000;
	ccode = ccode % 10000;
	fflush(stdout);  
	if (severity == 2) fprintf(stderr, "Fatal   - ");
	else if (severity == 1) fprintf(stderr, "Error   - ");
	else fprintf(stderr, "Warning - ");
	if (category == 3) printf("Syscall - ");
	else if (category == 2) fprintf(stderr, "Memory  - ");
	else if (category == 1) fprintf(stderr, "I/O     - ");
	fprintf(stderr, "%s\n", err->explanation);
	if (code < -200000) {
		fprintf(stderr, "Abnormal exit.\n");
		exit(1);
	}
}


static void print_usage(query_processing_environment_t *qoenv) {
	// When run with no arguments or invalid arguments, this function prints a message explaining
	// what arguments are needed.

	printf(
		"Usage: QBASHQ.exe <option>=<value> ... (command line mode)\n\n"
		);
	print_args(qoenv, TEXT);

	printf("Notes:\n    1. index_dir must be given and specify a directory containing QBASH indexes.  (If there\n"
		"   are sub-directories 0, 1, 2, 3, ... containing indexes,  QBASHQ will fire up a thread to search\n"
		"   each of those indexes, and will aggregate the results.) \n"
		"    2. qp must be given in CGI mode.  In commandline mode, absence of qp causes QBASHQ to expect queries from file_query_batch or stdin.\n"
		"    3. if warm_indexes=TRUE, QBASHQ will exit after attempting to load indexes into page cache by touching\n"
		"       all pages.\n"
		"    4. Meaning of debug levels:\n"
		"       0 - no debugging output\n"
		"       1 - course-grained debugging output\n"
		"       2 - fine-grained debugging output\n"
		"       3 - v. fine-grained debugging output, plus run internal tests.\n"
		"       4 - super fine-grained debugging output, but no internal tests.\n\n");

	print_qbasher_version(stdout);

	exit(0);   // OK should only happen on start-up
}

#ifndef NO_THREADS

#ifdef WIN64   
HANDLE h_output_mutex;
HANDLE work_item_finished_event[MAX_QUERY_PARALLELISM];

static void WINAPI thread_run_query(PTP_CALLBACK_INSTANCE instance, void *context, PTP_WORK work)  {
  // Used as a callback via the threadpool
  double start;
  int how_many_results;
  multistream_context_t *mscon = (multistream_context_t *)context;
  u_char **returned_results = NULL, *qopstring = NULL;
  double *corresponding_scores = NULL;
  FILE *query_output = mscon->qoenv->query_output;
  DWORD code;
  BOOL success, ok, timed_out = FALSE;

  start = what_time_is_it();
  // We run the query asynchronously but then we must synchronize to write out the result packet and
  // update the response time statistics.

  // Assumptions about mscon->multi_query_string:
  //  - It's stored in writeable memory, so we can insert NULs where we need to
  //  - Null terminated
  //  - Leading whitespace has been skipped.
  //  - May contain 'TAB option-string' or 'TAB option-string TAB query-label'  after the actual query,
  //    where 
  //  - query_plus_options string is passed to handle_multi_query()
  //  - If present, query-label is just remembered and printed out with Query and Options.
  //  - May end with LF or CRLF

  strcpy(mscon->mqs_copy, mscon->multi_query_string); // Because the original gets altered.
                                                        // Guaranteed to have space.
  
  qopstring = mscon->multi_query_string;

  // Note that ms->qoenv refers to a global version which doesn't yet take into account any options which 
  // may be set in options_string.  Options set there, only affect a local qoenv which only lives for the
  // duration of the query.  When we return from that handle_multi_query(), ms->qoenv still refers to the
  // global version which means we can correctly record response time statistics.
  how_many_results = handle_multi_query(mscon->ixenv, mscon->qoenv, qopstring,
				      &returned_results, &corresponding_scores, &timed_out);
  if (0) printf("returned from h_m_q() with %d results\n", how_many_results);
  // WaitForSingleObject apparently assigns the mutex to us when it stops timing out.
  while ((code = WaitForSingleObject(h_output_mutex, 5L)) == WAIT_TIMEOUT)   // The timeout is in milliseconds
    if (mscon->qoenv->debug) fprintf(stderr, "Thread %d waiting\n", mscon->thread);

  if (code != WAIT_OBJECT_0) {
    fprintf(stderr, "Error code %u from WaitForSingleObject() before presenting results\n"
	    "qopstring = %s\n", code, qopstring);
    error_exit("Error waiting for Mutex.\n");
  }
  // -----------------------------  Mutex Acquired ----------------------------------------------
  if (mscon->qoenv->chatty) {
    present_results(mscon->qoenv, mscon->mqs_copy, mscon->query_label, returned_results, corresponding_scores, 
		    how_many_results, start);
  } else {
    terse_show(mscon->qoenv, returned_results, corresponding_scores, how_many_results);
  }

  if (0) printf("About to call F_R_M\n");
  if (returned_results != NULL) free_results_memory(&returned_results, &corresponding_scores, how_many_results);   // f_r_m() tests pointer args for NULL
  if (0) printf("F_R_M returned\n");

  if (mscon->qoenv->x_show_qtimes)
    // This is just an experimental feature to enable finding the slowest queries in a batch (e.g.)
    fprintf(mscon->qoenv->query_output, "QTIME: %s\t%.1f msec.\n", mscon->mqs_copy, (what_time_is_it() - start) * 1000.0);

  if (mscon->qoenv->queries_run % 1000 == 0) {
    report_milestone(mscon->qoenv);
    fprintf(mscon->qoenv->query_output, "Milestone: Input file offset (approximate): %lld\n", input_offset);
  }
  ok = ReleaseMutex(h_output_mutex);
  if (!ok) {
    fprintf(stderr, "Failure %ufrom ReleaseMutex after presenting results\n"
	    "qopstring = %s\n", code, qopstring);
    error_exit("Error in ReleaseMutex.\n");
  }
  // -----------------------------  Mutex Released ----------------------------------------------
  if (0) printf("Mutex released\n");
  if (mscon->thread < 0 || mscon->thread >= MAX_QUERY_PARALLELISM) {
    fprintf(stderr, "Thread value %d is out of range.\n"
	    "qopstring = %s\n", mscon->thread, qopstring);
    error_exit("Error in ReleaseMutex.\n");
  }
  success = SetEvent(work_item_finished_event[mscon->thread]);  // Signal that this query has been processed
  if (!success) {
    fprintf(stderr, "SetEvent failed in thread %d with code %d\n", mscon->thread, GetLastError());
    exit(1);   // Unavoidable????
  }
}

static TP_WORK *work[MAX_QUERY_PARALLELISM];
static multistream_context_t work_context[MAX_QUERY_PARALLELISM];



#else
// Here's the POSIX version of the parallel code.  In this model, we create a
// a fixed number of threads.  Each thread is controlled by an array element
// comprising a state variable, a mutex, and potentially a work item.
// The thread code has a loop which repeatedly waits on the mutex, tests
// the state, performs the action appropriate to the state, and releases the mutex.

typedef enum {
  NO_WORK_TO_DO,
  WORK_WAITING,
  KNOCK_OFF_WORK,
} work_state_t;

static struct thread_control {
  work_state_t state;
  pthread_mutex_t mutti;
  pthread_t thread;
  multistream_context_t work_item;
} thread_controls[MAX_QUERY_PARALLELISM];

static pthread_mutex_t access_to_output;

static void repeatedly_try_for_lock(pthread_mutex_t *lock, char *id, int thread) {
  struct timespec req = {0, 1000};
  int code;
  do {
    code = pthread_mutex_trylock(lock);
    if (code == 0) return;
    if (code != EBUSY) {
      printf("Error %d: mutex_lock(%s) in thread %d\n", code, id, thread);
      exit(1);
    }
    nanosleep(&req, NULL);
  } while (1);
}

static void *pthread_run_queries(void *control) {
  struct thread_control *tc = (struct thread_control *) control;
  multistream_context_t *mscon = &(tc->work_item);
  double start;
  int how_many_results, code;
  u_char **returned_results = NULL, *qopstring = NULL, *tab1 = NULL,
    *tab2 = NULL, *query_label = NULL;
  double *corresponding_scores = NULL;
  BOOL timed_out = FALSE;
  struct timespec req = {0, 1};


  while (1) {
    if (mscon->qoenv->query_streams > 1)
      repeatedly_try_for_lock(&(tc->mutti), "work item", tc->work_item.thread);  // Will wait forever if the mutex remains locked.
    switch (tc->state) {
    case KNOCK_OFF_WORK:
      if (mscon->qoenv->query_streams > 1) {
	code = pthread_mutex_unlock(&(tc->mutti));
	if (code) {
	  printf("Error %d: mutex_unlock() in thread %d\n", code, mscon->thread);
	  exit(1);
	}
      }
      pthread_exit(NULL);
      break;
    case WORK_WAITING:
       start = what_time_is_it();
      // Assumptions about mscon->multi_query_string:
      //  - It's stored in writeable memory, so we can insert NULs where we need to
      //  - Null terminated
      //  - Leading whitespace has been skipped.
      //  - May contain 'TAB option-string' or 'TAB option-string TAB query-label'  after the actual query,
      //    where 
      //  - If present, query-label is just remembered and printed out with Query and Options.
      //  - May end with LF or CRLF
       if (mscon->qoenv->allow_per_query_options) {
	 tab2 = find_nth_occurrence_in_record(mscon->multi_query_string, '\t', 2);
	 if (tab2 != NULL) {
	   if (*tab2 == '\t') query_label = tab2 + 1;
	   *tab2 = 0;  //  Terminate query + options
	 }
       } else {
	 // Strip out TABs unless per_query_options are allowed
	 tab1 = find_nth_occurrence_in_record(mscon->multi_query_string, '\t', 1);
	 if (tab1 != NULL && *tab1 == '\t') *tab1 = 0;  // Zap out everything beyond the first tab
       }
       qopstring = mscon->multi_query_string;
      // Note that ms->qoenv refers to a global version which doesn't yet take into account any options which 
      // may be set in options_string.  Options set there, only affect a local qoenv which only lives for the
      // duration of the query.  When we return from the handle_multi_query() call, ms->qoenv still refers to the
      // global version which means we can correctly record response time statistics.

       how_many_results = handle_multi_query(mscon->ixenv, mscon->qoenv, qopstring,
					  &returned_results, &corresponding_scores, &timed_out);


      // To present results we need to grab a lock on the output stream     
     if (mscon->qoenv->query_streams > 1)
       repeatedly_try_for_lock(&access_to_output, "output stream", tc->work_item.thread);  // Will wait forever if the mutex remains locked.
      present_results(mscon->qoenv, qopstring, query_label,
		      returned_results, corresponding_scores, 
		      how_many_results, start);
      if (mscon->qoenv->query_streams > 1) {
	code = pthread_mutex_unlock(&access_to_output);  
	if (code) {
	  printf("Error %d: mutex_unlock(access_to_to_output) in thread %d\n", code, mscon->thread);
	  exit(1);
	}
      }
      // Can release the thread control lock at this point.
      if (mscon->qoenv->query_streams > 1) {
	code = pthread_mutex_unlock(&(tc->mutti));
	if (code) {
	  printf("Error %d: mutex_unlock() in thread %d\n", code, mscon->thread);
	  exit(1);
	}
      }

      
     if (0) printf("About to call F_R_M\n");
      if (returned_results != NULL)
	free_results_memory(&returned_results, &corresponding_scores,
			    how_many_results);   // f_r_m() tests pointer args for NULL
      if (0) printf("F_R_M returned\n");

      if (mscon->qoenv->x_show_qtimes)
	// This is just an experimental feature to enable finding the slowest queries in a batch (e.g.)
	fprintf(mscon->qoenv->query_output, "QTIME: %s\t%.1f msec.\n", qstring, (what_time_is_it() - start) * 1000.0);

      if (mscon->qoenv->queries_run % 1000 == 0) {
	report_milestone(mscon->qoenv);
	fprintf(mscon->qoenv->query_output, "Milestone: Input file offset (approximate): %lld\n", input_offset);
      }

      // We still have the lock on the thread_controls entry
      tc->state = NO_WORK_TO_DO;
      break;
    default:
      if (mscon->qoenv->query_streams > 1) {
	code = pthread_mutex_unlock(&(tc->mutti));
	if (code) {
	  printf("Error %d: mutex_unlock() in thread %d\n", code, mscon->thread);
	  exit(1);
	}
	nanosleep(&req, NULL);
      }
      break;
    }  // end of switch

   } // End of loop forever

  pthread_exit(NULL);
}

#endif
#endif // ifndef NO_THREADS




// -----------------------------------------------------------------------------------
// Main prog
// -----------------------------------------------------------------------------------


int main(int argc, char **argv) {
  int rslt, error_code = 0;
  BOOL verbose, run_index_tests;
  u_char *p, *q, qline[MAX_QLINE + 1], * multiqstr = NULL, *mqs_copy = NULL,
    *query_label = NULL;
  BOOL output_statistics = TRUE;   // TRUE iff we're running a batch of queries
  index_environment_t *ixenv;
  query_processing_environment_t *qoenv;
  FILE *query_stream = stdin;
  BOOL timed_out;
#ifdef WIN64
   u_char *r, *w;
   size_t qlen;
#endif

#ifdef NO_THREADS
   double query_started;
#endif

  //Needed to use the new API ....
  u_char **returned_results;
  double *corresponding_scores;
  int how_many_results;

  //////////////////////////////////////////////////////////////////////////////
  // Run a bunch of internal tests to make sure all is well with the code.
  //////////////////////////////////////////////////////////////////////////////
  test_sb_macros();
  test_isprefixmatch();
  test_isduplicate(0);
  test_substitute();
  run_bagsim_tests();
  //utf8_internal_tests();

  if (sizeof(size_t) != 8) {
    printf("sizeof(size_t) = %zu\n", sizeof(size_t));
    error_exit("This program must be compiled for 64 bits\n");  // OK - a necessary start up condition
  }


  //////fget////////////////////////////////////////////////////////////////////////
  // Load a query processing environment and process all the command line args.
  //////////////////////////////////////////////////////////////////////////////
	

  qoenv = load_query_processing_environment();

  if (qoenv == NULL) error_exit("Fatal error: Can't proceed without a query processing environment\n");  // OK - a necessary start up condition
  if (argc < 2) {
    // No command line arguments 
    print_usage(qoenv);
  }
  else {
    int a;
    for (a = 1; a < argc; a++) {
      p = (u_char *)argv[a];
	  if (0) printf("Arg: '%s'\n", p);
      rslt = assign_one_arg(qoenv, p, TRUE, TRUE, TRUE);  // Exit on error, enforce limits, explain errors
      if (rslt < 0) {
	respond_to_error(rslt);
	printf("Arg: '%s'\n", p);
      }
    }

  }


  if (qoenv->partial_query != NULL || !qoenv->chatty) output_statistics = FALSE;
  if (finalize_query_processing_environment(qoenv, output_statistics || qoenv->debug, qoenv->chatty) < 0) {
    fprintf(stderr, "Error: Failed to finalize the query processing environment.  Code: %d\n",
	    error_code);
    print_usage(qoenv);
  }

	
  if (qoenv->fname_query_batch != NULL) {
    if (qoenv->partial_query != NULL) {
      fprintf(stderr, "Error: It is not permitted to specify both pq and file_query_batch.\n");
      return 0;
    }
    query_stream = fopen((char *)qoenv->fname_query_batch, "rb");
    if (query_stream == NULL) {
      fprintf(stderr, "Error: Unable to open query stream '%s' for reading.\n", qoenv->fname_query_batch);
      return 0;
    }
  }




  verbose = (qoenv->debug > 0);
  run_index_tests = (qoenv->debug == 3);
  ixenv = load_indexes(qoenv, verbose, run_index_tests, &error_code);
  if (error_code < 0) respond_to_error(error_code);
  if (qoenv->warm_indexes) {
    double start;
    start = what_time_is_it();
    warmup_indexes(qoenv, ixenv);

    fprintf(qoenv->query_output, "... warmup completed in %.1f sec.\n",
	    what_time_is_it() - start);
  }

	
  //////////////////////////////////////////////////////////////////////////////
  // Now run a single partial query (-pq) or a batch of queries, either multi-
  // or single threaded.
  //////////////////////////////////////////////////////////////////////////////


  // Start the clock which will be used for calculating QPS rates
  qoenv->inthebeginning = what_time_is_it();
  
  if (qoenv->partial_query != NULL) {
    //-------------------------------------------------------------------------
    // Single query -- No multithreading and no per-query overrides, EVER
    //-------------------------------------------------------------------------

    if (0) printf("Single threading\n");
    u_char *p = qoenv->partial_query;
    while (*p) {
      if (*p == '\t') *p = ' ';
      p++;
    }
    how_many_results = handle_multi_query(ixenv, qoenv, qoenv->partial_query, &returned_results, &corresponding_scores, &timed_out);

    if (qoenv->report_match_counts_only) {
          fprintf(qoenv->query_output, "Match count for AND of\t%s\t%d\n", qoenv->partial_query, how_many_results);
   
    } else if (qoenv->x_batch_testing) {
      if (how_many_results > 0)
	experimental_show(qoenv, qoenv->partial_query, returned_results, corresponding_scores,
			  how_many_results, query_label);
      else
	fprintf(qoenv->query_output, "Query:\t%s\n", qoenv->partial_query);
    }
    else {
      if (how_many_results > 0)
	terse_show(qoenv, returned_results, corresponding_scores, how_many_results);
      else if (how_many_results < 0) {
	respond_to_error(how_many_results);
      }
    }
    free_results_memory(&returned_results, &corresponding_scores, how_many_results);  // f_r_m() tests pointer args for NULL
  } else {
    //-------------------------------------------------------------------------
    // Query batch -- Multithreading setup.
    //-------------------------------------------------------------------------

#ifndef NO_THREADS
#ifdef WIN64   	  
    // Loop over input queries from query_stream and run them using QUERY_PARALLELISM parallel threads.
    int th, code;
    BOOL thread_busy[MAX_QUERY_PARALLELISM] = { FALSE }, query_launched;

    if (0) printf("Multi Windows\n");

    h_output_mutex = CreateMutex(NULL, FALSE, NULL);  // To synchronise output to query_output.  Initially not owned.
    if (h_output_mutex == NULL) error_exit("Fatal Error: Can't create h_output_mutex\n");   // OK - this happens once at start-up

    if (qoenv->query_streams > MAX_QUERY_PARALLELISM) qoenv->query_streams = MAX_QUERY_PARALLELISM;

    for (th = 0; th < qoenv->query_streams; th++) {
      work_context[th].ixenv = ixenv;
      work_context[th].qoenv = qoenv;
      work_context[th].thread = th;
      thread_busy[th] = FALSE;
      // query_string will be filled in later for each query.
      work[th] = CreateThreadpoolWork(thread_run_query, work_context + th, NULL);
      if (work[th] == NULL)
	fprintf(stderr, "Error %d in CreateThreadpoolWork", GetLastError());
    }

#else   // pthreads branch
    int th, code;
    struct timespec req = {0, 1};
    code = pthread_mutex_init(&access_to_output, NULL);
    if (code) {
      printf("Error %d: mutex_mutex_init(access_to_output) in main thread\n", code);
      exit(1);
    }
    for (th = 0; th < qoenv->query_streams; th++) {
      thread_controls[th].work_item.ixenv = ixenv;
      thread_controls[th].work_item.qoenv = qoenv;
      thread_controls[th].work_item.thread = th;
      // query_string will be filled in later for each query.
      thread_controls[th].state = NO_WORK_TO_DO;
      code = pthread_mutex_init(&(thread_controls[th].mutti), NULL);
      if (code) {
	printf("Error %d: pthread_mutex_init() for worker thread %d\n", code, th);
	exit(1);
      }
      
      code = pthread_create(&(thread_controls[th].thread), NULL,
			    pthread_run_queries, thread_controls + th);
      if (code) {
	printf("Error %d: pthread_create() for worker thread %d\n", code, th);
	exit(1);
      }
    }
    if (0) printf(" ... all threads set up\n");
#endif
#endif

    
    //-------------------------------------------------------------------------
    // Query batch -- common to multithreaded and single threaded
    //-------------------------------------------------------------------------

    if (qoenv->chatty) {
      print_qbasher_version(qoenv->query_output);
      fprintf(qoenv->query_output, "Format of index: %.1f\n", ixenv->index_format_d);
      show_mode_settings(qoenv);
    }


    while (fgets((char *)qline, MAX_QLINE, query_stream) != NULL) {
      q = qline;
      while (*q && isspace(*q)) q++;

      if (*q) { // Only do this for non-blank queries

#ifndef NO_THREADS			  
#ifdef WIN64   // ------ Multi-threaded path:  At the moment threading is not supported from gcc			  
	query_launched = FALSE;
	while (!query_launched) {
	  // Find first non-busy thread.  If there is one, submit a work item and mark thread as busy.
	  // If none of the threads are free we just fall through.
	  for (th = 0; th < qoenv->query_streams; th++) {
	    if (!thread_busy[th]) {
	      thread_busy[th] = TRUE;
	      // The following strcpy loop should be fine because the qline buffer is null-terminated by
	      // fgets() at or before position MAX_QLINE - 1 and multi_query_string is MAX_QLINE + 1
	      r = q;
	      w = work_context[th].multi_query_string;
	      while (*r  && *r != 0x1D) *w++ = *r++;
	      *w = 0;
	      qlen = r - q;
	      if (*r == 0x1D) {
		// Copy the query label following the group separator (GS)
		r++;  // Skip the GS
		w = work_context[th].query_label;  // This buffer's also guaranteed big enough
		while (*r >= ' ') *w++ = *r++;
		*w = 0;
	      }
	      else work_context[th].query_label[0] = 0;
	      input_offset += qlen;
	      work_item_finished_event[th] = CreateEvent(NULL, TRUE, FALSE, NULL);
	      if (work_item_finished_event[th] == NULL) {
		fprintf(stderr, "ERROR: Raspberry! CreateEvent failed with %u\n", GetLastError());
		exit(1);
	      }
	      SubmitThreadpoolWork(work[th]);
	      query_launched = TRUE;
	      if (qoenv->debug >= 1) fprintf(stdout, "Work item submitted for thread %d.\n", th);
	      break;
	    }
	  }

	  // Now check whether any threads have finished.
	  for (th = 0; th < qoenv->query_streams; th++) {
	    BOOL ok;
	    if (thread_busy[th]) {
	      code = WaitForSingleObject(work_item_finished_event[th], 0L);   // 0 means zero, don't wait
	      if (code == WAIT_OBJECT_0) {
		ok = CloseHandle(work_item_finished_event[th]);
		if (!ok){ 
		  fprintf(stderr, "ERROR: Blackberry! CloseHandle failed with %u\n", GetLastError());
		  exit(1);
		}
		thread_busy[th] = FALSE;
		if (qoenv->debug >= 1) printf("           Q finished on thread %d\n", th);
	      }
	      else if (code == WAIT_FAILED) {
		fprintf(stderr, "Warning: WAIT_FAILED with code %d\n", GetLastError());
		fprintf(stderr, "Proceeding but unsure what will happen.\n");
	      }
	    }
	  }  
					
	  // Keep looping until this query is launched.
	}
#else  // pthreads branch  --- Note that this needs debugging!  On Windows it runs horribly slowly
	BOOL query_launched;
	u_char *r, *w;
	size_t qlen;
	query_launched = FALSE;
	while (!query_launched) {
	  // Find first free thread and slot in a work item
	  // If none of the threads are free we just fall through.
	  for (th = 0; th < qoenv->query_streams; th++) {
	    // Try to get a lock on this thread's state variable.
	    if (qoenv->query_streams > 1) {
	      code = pthread_mutex_trylock(&(thread_controls[th].mutti));
	    }
	    else code = 0;
	    if (code == 0) {
	      // Got the lock
	      if (thread_controls[th].state == NO_WORK_TO_DO) {
		query_launched = TRUE;
		thread_controls[th].state = WORK_WAITING;
		// Copy the input string into the relevant part of the work item
		r = q;
		w = thread_controls[th].work_item.multi_query_string;
		while (*r  && *r != 0x1D) *w++ = *r++;
		*w = 0;
		qlen = r - q;
		if (*r == 0x1D) {
		  // Copy the query label following the group separator (GS)
		  r++;  // Skip the GS
		  w = work_context[th].query_label;  // This buffer's also guaranteed big enough
		  while (*r >= ' ') *w++ = *r++;
		  *w = 0;
		}
		else work_context[th].query_label[0] = 0;

		input_offset += qlen;
		if (0) printf("OK, the request is set up for thread %d (%s).\n",
			      th, thread_controls[th].work_item.multi_query_string);
	      }
	      if (qoenv->query_streams > 1) {
		code = pthread_mutex_unlock(&(thread_controls[th].mutti));
	      }
	      else {
		code = 0;
	      }
	      if (code) {
		printf("Error %d: pthread_mutex_unlock() for worker thread %d\n", code, th);
		exit(1);
	      }
	      if (query_launched) break;    // ------------------------>
	    }
	    else if (code != EBUSY) {  // If the lock is busy, just move on
	      printf("Error %d: pthread_mutex_trylock() for worker thread %d\n", code, th);
	      exit(1);
	    }
	  }  // End of loop over threads

	  if (!query_launched) {
	    // Nanosleep for a microsecond here to give the other threads a chance to make progress
	    code = nanosleep(&req, NULL);
	    if (code) {
	      printf("Error %d: nanosleep()\n", code);
	      exit(1);
	    }
	  }
	}  // while (!query_launched)

#endif
      

#else   // ------------  Unthreaded path for batched queries ------------------------------------


	multiqstr = q;
	  
	      
	while (*q  && *q != 0x1D && *q != '\n' && *q != '\r') q++;
	if (*q == 0x1D) {
		// The query label follows the group separator (GS)
	  *q++ = 0;  // zap the GS
	  query_label = q;
	  while (*q >= ' ') q++;  // Skip to the CR or LF at the end of the label
	} else query_label = NULL;

	*q = 0;  // Zap newlines etc at the end

	if (qoenv->chatty) 
	  mqs_copy = make_a_copy_of(multiqstr);

	query_started = what_time_is_it();
	how_many_results = handle_multi_query(ixenv, qoenv, multiqstr,
					      &returned_results, &corresponding_scores, &timed_out);

	if (qoenv->chatty) {
	  present_results(qoenv, mqs_copy, query_label, returned_results, corresponding_scores, 
			  how_many_results, query_started);
	} else {
	  terse_show(qoenv, returned_results, corresponding_scores, how_many_results);
	}
	if (0) printf("About to free results\n");
	
	free_results_memory(&returned_results, &corresponding_scores, how_many_results);  // f_r_m() tests pointer args for NULL
		
#endif				
      }  // End of only-do-this-for-non-blank-queries

    }   // End of fgets() loop

#ifndef NO_THREADS
#ifdef WIN64
    // Wait for all the straggler threads to finish.
    for (th = 0; th < qoenv->query_streams; th++) {
      if (thread_busy[th]) {
	code = WaitForSingleObject(work_item_finished_event[th], INFINITE);
	if (code == WAIT_OBJECT_0) {
	  thread_busy[th] = FALSE;
	}
	else if (code == WAIT_FAILED) {
	  fprintf(stderr, "Warning: WAIT_FAILED (final) with code %d\n", GetLastError());
	  fprintf(stderr, "Proceeding but unsure what will happen.\n");
	}
      }
    }

    for (th = 0; th < qoenv->query_streams; th++) {
      CloseThreadpoolWork(work[th]);
      CloseHandle(work_item_finished_event[th]);
    }

    CloseHandle(h_output_mutex);

#else
    // Tell all the threads to knock off, and wait until they do.
    
    for (th = 0; th < qoenv->query_streams; th++) {
      code = pthread_mutex_lock(&(thread_controls[th].mutti));
      if (code) {
	printf("Error %d: pthread_mutex_lock() for worker thread %d\n", code, th);
	exit(1);
      }
      // Got the lock
      thread_controls[th].state = KNOCK_OFF_WORK;
      code = pthread_mutex_unlock(&(thread_controls[th].mutti));
      if (code) {
	printf("Error %d: pthread_mutex_unlock() for worker thread %d\n", code, th);
	exit(1);
      }

      // Note that this thread should finish almost instantaneously because we only get the lock
      // once it's finished a query, and we're not giving it any more.

      code = pthread_join(thread_controls[th].thread, NULL);
      if (code) {
	printf("Error %d: pthread_join() for worker thread %d\n", code, th);
	exit(1);
      }
    
    }
   
#endif
#endif  // Unthreaded


    if (qoenv->chatty && qoenv->queries_run > 0) {
      report_query_response_times(qoenv);
      fprintf(qoenv->query_output, "Milestone: Input file offset (approximate): %lld\n", input_offset);
    }

  }

  unload_indexes(&ixenv);
  unload_query_processing_environment(&qoenv, output_statistics, TRUE);
  if (query_stream != stdin) {
    //close the query stream file, unless it's stdin
    fclose(query_stream);
  }
  exit(0);   // OK normal exit
}
