// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// This is the version for the DLL not the Main calling program.

// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the QBASHQ_EXPORTS
// symbol defined on the command line. This symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// QBASHQ_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.

#ifdef WIN64
#ifdef QBASHQ_EXPORTS
#define QBASHQ_API __declspec(dllexport)
#else
#define QBASHQ_API __declspec(dllimport)
#endif
#else
#define QBASHQ_API
#endif

#ifndef PAGESIZE
#define PAGESIZE 1024   // Used in index warm-up, better to be too small than too big.
#endif

#define PCRE2_CODE_UNIT_WIDTH 8  // Must define before PCRE2 include
#include "../imported/pcre2/pcre2.h"    // Always include this

#define QUERY_PROCESSOR "YES"
#define DEBUG 0

#define NUM_COEFFS 8
#define NUM_CF_COEFFS 3
#define EPSILON 0.000001
#define MAX_QLINE 4097
#define MAX_WDS_IN_QUERY 32  // terms_matched_bits are stored in a u_int (assumed 32 bits)
#define MAX_RELAX 4          // The maximum allowable relaxation_level.  Determines array size in qex
#define MAX_ERROR_EXPLANATION 100
#define PARTIAL_CHAR '/'
#define RANK_ONLY_CHAR '~'
#define QBASH_META_CHARS "%\"[]~/"   // Make sure all query special chars are listed here.  *** Must match QBASHI.h ***
#define ELAPSED_MSEC_BUCKETS 1000


// Match flags used in classifier mode
#define MF_FULL_EXACT 1
#define MF_PHRASE 2
#define MF_SEQUENCE 4
#define MF_FULL 8
#define MF_RELAX1 16
#define MF_RELAX2 32

#define FV_ELTS 9 
typedef struct {
  long long doc;
  double score;
  unsigned int terms_matched_bits;
  byte tf[MAX_WDS_IN_QUERY];  // TFs are maxed at 256
  byte qidf[MAX_WDS_IN_QUERY];  // Quantized to 256 values
  byte intervening_words;  // Used in calculating theta feature.
  byte match_flags;  // Used in classifier mode:  what type of match
  double FV[FV_ELTS];      // Used in classifier mode:  feature vector
} candidate_t;



// Definitions of functions which should be shared between QBASHI and QBASHQ but aren't yet because of 
// divergences.

u_char *make_a_copy_of(u_char *in);


unsigned long long quantize_log_freq_ratio(double freq, double log_max_freq);

double get_score_from_dtent(unsigned long long dte);

#ifdef WIN64
LPCWSTR convert_str_to_wide(u_char *str);
#endif


#define NUM_OPS 8  // Must match code in setup_for_op_counting()

enum {
  COUNT_DECO,   // Decompress a posting
  COUNT_SKIP,   // Skip within a postings list
  COUNT_CAND,   // Consider a candidate
  COUNT_SCOR,   // Score or classify a candidate
  COUNT_PART,   // Check a partial word term
  COUNT_ROLY,   // Check a rank-only term
  COUNT_TLKP,	// Lookup a term in a dictionary
  COUNT_BLOM,   // Check a candidate against a Bloom filter
};

// Definition of a structure to facilitate recording and display of
// operation counts.  Such counts can provide 
// a basis for deterministic timeouts.  label and cost are only 
// written by the main thread.  
typedef struct {
  char label[40];
  int cost;
  int count;
} op_count_t;


typedef struct {
  int code;
  char explanation[MAX_ERROR_EXPLANATION + 1];
} err_desc_t;



byte *lookup_word(u_char *wd, byte *vocab, size_t vsz, int debug);

byte *get_doc(unsigned long long *docent, byte *forward, int *doclen_inwords, size_t fsz);

u_char *what_to_show(long long docoff, byte *doc, int *showlen, int displaycol, u_char *bitmap_list);



// ************************************************************************************************************ //
//                                                                                                              //
// Begin API Definitions                                                                                        //
//                                                                                                              //
// ************************************************************************************************************ //


typedef struct {
  // Declarations of all the index structures.
  // Handles for the memory mapped index files: H for the mapped file and MH for the mapping
  CROSS_PLATFORM_FILE_HANDLE doctable_H, forward_H, index_H, vocab_H ;
  HANDLE doctable_MH, forward_MH, vocab_MH, index_MH;
  byte *doctable, *vocab, *index, *forward,
    *other_token_breakers;
  size_t dsz, vsz, isz, fsz;
  double index_format_d;
  BOOL expect_cp1252;
} index_environment_t;

// Next define an options environment for running one or more queries.  The same object can be used
// for multiple queries as long as they use the same options.

typedef struct {
  // ---- Settable options.
  void **vptra;  // Array of pointers to the value variables.  Set up in setup_valueptr_array()
  BOOL auto_partials, auto_line_prefix, warm_indexes, display_parsed_query, x_show_qtimes,
    x_batch_testing, chatty;
  u_char *partial_query, *index_dir, *fname_forward, *fname_if, *fname_doctable, *fname_vocab,
    *fname_query_batch, *fname_output, *fname_config, *fname_substitution_rules,
    *fname_segment_rules, *object_store_files, *language;
  double rr_coeffs[NUM_COEFFS], cf_coeffs[NUM_CF_COEFFS], classifier_threshold;
  int relaxation_level, max_to_show, max_candidates_to_consider, max_length_diff, 
    timeout_kops, timeout_msec, displaycol, extracol, query_streams, duplicate_handling,
    classifier_mode, classifier_min_words, classifier_max_words, x_max_span_length,
    query_shortening_threshold, street_address_processing, street_specs_col, debug;
  double segment_intent_multiplier;
  double classifier_stop_thresh1, classifier_stop_thresh2;
  double location_lat, location_long, geo_filter_radius;

  // ---- Derived from settable options
  BOOL scoring_needed, report_match_counts_only;
  FILE *query_output;  // File used to output debug and status information plus query and results
  // With static linking, it must be owned by the API library not by the main
  // program -- see msdn.microsoft.com/en-us/library/ms235460
  // By default this is the library's stdout

  // ---- Substitution rules are read from a TSV file, such as QBASH.substitution_rules.
  // ---- Segment rules are also read from a TSV file, such as QBASH.segment_rules.
  BOOL use_substitutions, include_result_details, include_extra_features, allow_per_query_options,
    generate_JO_path, conflate_accents;
  dahash_table_t *substitutions_hash, *segment_rules_hash;  

  // ---- Statistics recorded across the batch of queries run with this set of options
  double inthebeginning;
  u_char slowest_q[MAX_QLINE];
  long long queries_run, queries_without_answer, query_timeout_count, global_idf_lookups;
  double total_elapsed_msec_d, max_elapsed_msec_d;
  int elapsed_msec_histo[ELAPSED_MSEC_BUCKETS];

  // ---- Index and properties used in BM25 document scoring  
  index_environment_t *ixenv;   // Initially only used when run from Object Store
  double N;      // Number of documents in the index
  double avdoclen; // Average document length in indexable words
} query_processing_environment_t;


QBASHQ_API int test_sb_macros();

QBASHQ_API int test_isprefixmatch();

QBASHQ_API int test_isduplicate(int debug);

QBASHQ_API int test_postings_list(u_char *word, byte *doctable, byte *index, byte *forward, size_t fsz,
				  byte *vocab, size_t vsz, int max_to_show);

QBASHQ_API void test_result_blocks_needed();

QBASHQ_API int test_substitute();

QBASHQ_API int run_bagsim_tests();

QBASHQ_API void report_query_response_times(query_processing_environment_t *qoenv);

QBASHQ_API void terse_show(query_processing_environment_t *qoenv, u_char **returned_strings, double *corresponding_scores, int how_many_results);

QBASHQ_API void experimental_show(query_processing_environment_t *qoenv, u_char *qstr,
				  u_char **returned_strings, double *corresponding_scores,
				  int how_many_results, u_char *lblstr);

QBASHQ_API void present_results(query_processing_environment_t *qoenv, u_char *qopstr, u_char *lblstr,
				u_char **returned_strings, double *corresponding_scores, int how_many_results, double query_start_time);
	
QBASHQ_API void print_qbasher_version(FILE *f);

//QBASHQ_API int handle_one_query(index_environment_t *ixenv, query_processing_environment_t *qoenv,
//				  u_char *query_string, u_char *options_string, u_char ***returned_results,
//		        	  double **corresponding_scores,
//				  BOOL *timed_out);

QBASHQ_API int handle_multi_query(index_environment_t *ixenv, query_processing_environment_t *qoenv,
				  u_char *multi_query_string, u_char ***returned_results,
				  double **corresponding_scores, BOOL *timed_out);

QBASHQ_API u_char *extract_result_at_rank(u_char **returned_results, double *scores, int rank, int *length, double *score);   // Just a convenience for C# access.

QBASHQ_API void free_results_memory(u_char ***result_strings, double **corresponding_scores, int num_results);

QBASHQ_API index_environment_t *load_indexes(query_processing_environment_t *qoenv, BOOL verbose,
					     BOOL run_tests, int *error_code);

QBASHQ_API int warmup_indexes(query_processing_environment_t *qoenv, index_environment_t *ixenv);

QBASHQ_API void unload_indexes(index_environment_t **ixenvp);

QBASHQ_API int assign_one_arg(query_processing_environment_t *qoenv, u_char *arg_equals_val,
			      BOOL initialising, BOOL enforce_limits, BOOL explain);

QBASHQ_API query_processing_environment_t *load_query_processing_environment();

QBASHQ_API int finalize_query_processing_environment(query_processing_environment_t *qoenv,
						     BOOL verbose, BOOL explain_errors);

QBASHQ_API void show_mode_settings(query_processing_environment_t *qoenv);

QBASHQ_API void report_milestone(query_processing_environment_t *qoenv);

QBASHQ_API void unload_query_processing_environment(query_processing_environment_t **qoenvp, 
						    BOOL report_final_memory_usage, BOOL full_clean);

QBASHQ_API void print_args(query_processing_environment_t *qoenv, format_t f);

QBASHQ_API void free_options_memory(query_processing_environment_t *qoenv);

QBASHQ_API void print_errors();

QBASHQ_API err_desc_t *explain_error(int code);


#ifdef WIN64
// THe following are the functions called from ObjectStore.  
QBASHQ_API int NativeInitializeSharedFiles(u_char *pFilenames, query_processing_environment_t **qoenv);

QBASHQ_API void NativeDeInitialize(query_processing_environment_t **qoenv);

typedef void(__stdcall *PFN_ISSUERESPONSE)(LPWSTR pResponse);

QBASHQ_API int NativeExecuteQueryAsync(LPWSTR query, query_processing_environment_t *qoenv, PFN_ISSUERESPONSE issueResponse);
//QBASHQ_API int NativeExecuteQueryAsync(query_processing_environment_t *qoenv, query_processing_environment_t *qoenv2, PFN_ISSUERESPONSE issueResponse);

//QBASHQ_API double msec_elapsed_since(LARGE_INTEGER then);  // Don't think this is needed.
#endif


// *******************************************************************************************************************************//
//                                                                                                                                //
// End API Definitions                                                                                                            //
//                                                                                                                                //
// *******************************************************************************************************************************//

#define SHORTEN_NOEXIST 1
#define SHORTEN_REPEATED 2
#define SHORTEN_ALL_DIGITS 4
#define SHORTEN_HIGH_FREQ 8

typedef struct {
  u_char *query, qcopy[MAX_QLINE + 1], query_as_processed[MAX_QLINE + 1],
    candidate_generation_query[MAX_QLINE + 1],
    *qterms[MAX_WDS_IN_QUERY], *cg_qterms[MAX_WDS_IN_QUERY],
    *partials[MAX_WDS_IN_QUERY], *rank_only[MAX_WDS_IN_QUERY];
  // qwd_cnt is the count of terms in the query where a term may be a phrase or a disjunction
  // q_max_mat_len is the maximum number of document words the query may match
  // For example: the query {[a "b c" "one two three"]} has a qwd_cnt of 1 but a q_max_mat_len of 3.
  int qwd_cnt, cg_qwd_cnt, tl_saat_blocks_allocated, tl_saat_blocks_used, partial_cnt, rank_only_cnt, q_max_mat_len;
  long long full_match_count;
  unsigned long long q_signature;
  int candidates_recorded[MAX_RELAX + 1];
  candidate_t **candidatesa;
  byte **rank_only_countsa;

  u_char **tl_suggestions;
  double *tl_scores;
  docnum_t *tl_docids;
  int tl_returned;
  BOOL timed_out, vertical_intent_signaled, query_contains_operators;
  op_count_t op_count[NUM_OPS];
  int max_length_diff;
  double segment_intent_multiplier;
  int street_number;
  double start_time;   // Time of day when execution of this query started.
  u_char shortening_codes;  
} book_keeping_for_one_query_t;


int op_cost(book_keeping_for_one_query_t *qex);


