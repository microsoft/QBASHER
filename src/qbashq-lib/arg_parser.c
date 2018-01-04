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

#include "../shared/utility_nodeps.h"
#include "../shared/QBASHER_common_definitions.h"
#include "QBASHQ.h"
#include "arg_parser.h"


// While there was only one set of options and that set was in force for the entire program execution, the args[] array
// was lovely and simple, self-documenting, easily maintained, and not prone to error.  However, the need to support 
// multiple query option environments meant that it could no longer include a hard-wired value pointer.  Now there 
// is an extra level of indirection.  The index of an option within the args[] array is now used to access one of 
// potentially many (void *) valueptr arrays, which must be of the same dimension as args[], and whose elements are
// pointers to the values to be used.   
// The downsides of this are that adding an option is no longer a two-step process, and the process is prone to
// errors which were not possible in the original model.  Now the following steps must be taken:

// INSTRUCTIONS FOR ADDING OR DELETING OPTIONS
//   1. Insert or remove a line from the args[] initialisation
//   2. Update the NUMBER_OF_ARGS definition.
//   3. Add or remove a declaration of the value member to/from the query_processing_environment_t definition in QBASHQ.h
//   4. Adjust the numerical comments at the beginning of each args[] initialisation to retain correct numbering
//   5. Carefully insert or remove an assignment to the correctly numbered element in initialize_qoenv_mappings(), making sure
//      that the numbering of elements in the other assignments is still correct.
//   6. Later in the same function assign the new value to a good default, or remove an obsolete
//	    assignment.

#define NUMBER_OF_ARGS 63

arg_t args[] = {
  // ------------- If you edit these initialisations, be sure to follow the INSTRUCTIONS above --------------
  // Attribute, Type, Immutable, Min Val, Max Val, Explanation
  /*  0 */{ "index_dir", ASTRING, TRUE, 0, 0, "Directory containing the QBASHER indexes.  Specify either this, or all four file_ options." },
  /*  1 */{ "file_forward", ASTRING, TRUE, 0, 0, "The name of the .forward file containing TSV data to be indexed.  Also used for PDI. (Incompat. with index_dir)" },
  /*  2 */{ "file_if", ASTRING, TRUE, 0, 0, "The name of the .if (inverted file) file produced during indexing. (Incompat. with index_dir)" },
  /*  3 */{ "file_vocab", ASTRING, TRUE, 0, 0, "The name of the .vocab file  produced during indexing. (Incompat. with index_dir)" },
  /*  4 */{ "file_doctable", ASTRING, TRUE, 0, 0, "The name of the .doctable file produced during indexing. (Incompat. with index_dir)" },
  /*  5 */{ "file_substitution_rules", ASTRING, TRUE, 0, 0, "The name of a file containing regex substitution rules. (Incompat. with index_dir)" },
  /*  6 */{ "file_query_batch", ASTRING, TRUE, 0, 0, "The name of a file containing queries to be processed. (Incompat. with pq)" },
  /*  7 */{ "file_output", ASTRING, TRUE, 0, 0, "The name of a file to which output will be written." },
  /*  8 */{ "file_config", ASTRING, TRUE, 0, 0, "The name of a config file containing additional QBASHQ arguments." },
  /*  9 */{ "pq", ASTRING, TRUE, 0, 0, "The partial query typed by the user.  If absent, QBASHQ will expect a stream of partial queries from file_query_batch or STDIN" },
  /* 10 */{ "max_to_show", AINT, TRUE, 0, 1000, "Maximum number of results to display. [Experimental]: Zero activates a special mode which reports a full match count but no results" },
  /* 11 */{ "max_candidates", AINT, TRUE, 1, 1000, "Maximum number of results to collect before ranking and display.  If not set, default value is max_to_show." },
  /* 12 */{ "max_length_diff", AINT, FALSE, 0, 999, "Ignore results which are more than X words longer than the input.  If X is greater than 99, the actual value is dynamically set." },
  /* 13 */{ "timeout_kops", AINT, FALSE, 0, 1000000, "If non zero, sets a timeout limit on number of K-operations performed per query" },
  /* 14 */{ "timeout_msec", AINT, FALSE, 0, 1000000, "If non zero, sets a timeout limit on elapsed milliseconds per query" },
  /* 15 */{ "alpha", AFLOAT, FALSE, 0, 1, "Ranking: Coeff of static score" },
  /* 16 */{ "beta", AFLOAT, FALSE, 0, 1, "Ranking: Coeff of phrase feature" },
  /* 17 */{ "gamma", AFLOAT, FALSE, 0, 1, "Ranking: Coeff of words-in-sequence feature" },
  /* 18 */{ "delta", AFLOAT, FALSE, 0, 1, "Ranking: Coeff of primacy feature" },
  /* 19 */{ "epsilon", AFLOAT, FALSE, 0, 1, "Ranking: Coeff of excess length feature" },
  /* 20 */{ "zeta", AFLOAT, FALSE, 0, 1, "Ranking: Coeff of BM25 score.  [Experimental: Only implemented for bag-of-words queries thus far.]" },
  /* 21 */{ "eta", AFLOAT, FALSE, 0, 1, "Ranking: Coeff of score derived from geographical distance from the searcher's origin." },
  /* 22 */{ "theta", AFLOAT, FALSE, 0, 1, "Ranking: Coeff of score derived from intervening word count (Only with partial matches)." },
  
  /* 23 */{ "chi", AFLOAT, FALSE, 0, 1, "Classification: Weight of degree of match in linear combination to make classification score." },
  /* 24 */{ "psi", AFLOAT, FALSE, 0, 1, "Classification (lyrics only): Weight of record type (e.g. T, AT, TA) in linear combo to make classification score." },
  /* 25 */{ "omega", AFLOAT, FALSE, 0, 1, "Classification: Weight of static score in linear combination to make classification score." },
  /* 26 */{ "auto_partials", ABOOL, FALSE, 0, 0, "If TRUE the last word in pq will be treated as a word prefix (unless followed by a space). (See Note 1.)" },
  /* 27 */{ "auto_line_prefix", ABOOL, FALSE, 0, 0, "If TRUE a query with no space will be prefixed with '>'. (See Note 2.)" },
  /* 28 */{ "warm_indexes", ABOOL, FALSE, 0, 0, "If TRUE, QBASHQ will touch all the pages in the indexes before processing a query." },
  /* 29 */{ "relaxation_level", AINT, FALSE, 0, MAX_RELAX, "To what extent should we relax the requirement of a full-AND match. (How many words can be missing.)" },
  /* 30 */{ "display_col", AINT, FALSE, -1, 999999, "TSV cols to display instead of col 1, unless absent or empty.  0 -> whole record, -1 -> line number in .forward (from 0).  Up to 3 2-digit cols as in: 130401 means col 13, col 4, col 1" },
  /* 31 */{ "query_streams", AINT, TRUE, 1, 100, "How many parallel query streams to run." },
  /* 32 */{ "duplicate_handling", AINT, FALSE, 0, 2, "0 - never eliminate dup.s; 1 - suppress adjacent duplicate display strings from final result ranking; 2 - eliminate all duplicates." },
  /* 33 */{ "classifier_mode", AINT, FALSE, 0, 4, "0 - Operate normally, not as a classifier; 1 - classify using counts; 2 - classify using idfs." },
  /* 34 */{ "classifier_threshold", AFLOAT, FALSE, 0, 1, "If classifier_mode > 0 a Yes decision will be made if the score exceeds this value." },
  /* 35 */{ "classifier_min_words", AINT, FALSE, 0, 100, "If classifier_mode > 0 then a No decision will be made for any query with fewer than this number of words." },
  /* 36 */{ "classifier_max_words", AINT, FALSE, 1, 255, "If classifier_mode > 0 then a No decision will be made for any query with more than this number of words." },
  /* 37 */{ "classifier_segment", ASTRING, FALSE, 0, 0, "The name of a segment (lyrics, magic_songs, magic_movie, amazon, wikipedia, academic, carousel) which needs special query treament and scoring (classifier_mode > 0)." },
  /* 38 */{ "segment_intent_multiplier", AFLOAT, FALSE, 0, 1, "The classifier_threshold will be multiplied by this if segment intent words are detected." },
  /* 39 */{ "classifier_stop_thresh1", AFLOAT, FALSE, 0, 1, "Terminate early if the highest-ranked candidate exceeds this value. (classifier_mode > 0)." },
  /* 40 */{ "classifier_stop_thresh2", AFLOAT, FALSE, 0, 1, "Terminate early if the lowest-ranked of max_to_show candidates exceeds this value. (classifier_mode > 0)." },
  /* 41 */{ "display_parsed_query", ABOOL, TRUE, 0, 0, "If TRUE, QBASHQ will display the parsed (and possibly re-written query, according to other parameters) query." },
  /* 42 */{ "debug", AINT, FALSE, 0, 10, "Activate debugging output.  0 - none, 1 - low, 4 - highest; 3 - runs tests; 10 - no debugging but unbuffer stdout" },
  /* 43 */ { "x_show_qtimes", ABOOL, TRUE, 0, 10, "Set query_streams to one and print a QTIMES: line for each query processed, giving elapsed msec.  (experimental)" },
  /* 44 */{ "object_store_files", ASTRING, TRUE, 0, 0, "A comma separated list of four index files + config." },
  /* 45 */{ "language", ASTRING, TRUE, 0, 0, "Any language specific processing will be done in this language, if supported.  Two-char language code. E.g. EN, de, FR, zh" },
  /* 46 */{ "use_substitutions", ABOOL, FALSE, 0, 0, "If TRUE, and there is a QBASH.substitution_rules file, substitutions for the current language will be applied to queries." },
  /* 47 */{ "include_result_details", ABOOL, FALSE, 0, 0, "If TRUE, each search result will include 3 extra tab separated fields with additional information. (classifier modes only." },
  /* 48 */{ "extra_col", AINT, FALSE, 0, 10, "An extra TSV column to include in classifier-mode results display.  If extra_col=0 the output column will be present but empty." },
  /* 49 */{ "include_extra_features", ABOOL, FALSE, 0, 0, "If TRUE, each search result will include 6 extra tab separated fields with classifier feature values. (classifier modes only" },
  /* 50 */{ "x_batch_testing", ABOOL, FALSE, 0, 0, "If TRUE, results lines will be presented in a special format including the query." },
  /* 51 */{ "allow_per_query_options", ABOOL, FALSE, 0, 0, "If TRUE, overriding options can be included in a query after a TAB.  If FALSE, TABs are stripped." },
  /* 52 */{ "generate_JO_path", ABOOL, FALSE, 0, 0, "Classifier_mode only. When we are very confident, we may return a query with intent words added." },
  /* 53 */{ "x_conflate_accents", ABOOL, FALSE, 0, 0, "Query and candidate documents will have all accents removed (internally). Experimental at this stage." },
  /* 54 */{ "chatty", ABOOL, TRUE, 0, 0, "When run in batch mode, default is to print a lot of status information.  if FALSE, most of this will be avoided." },
  /* 55 */{ "lat", AFLOAT, FALSE, -90, 90, "Latitude of the location associated with the searcher." },
  /* 56 */{ "long", AFLOAT, FALSE,-180, 180, "Longitude of the location associated with the searcher" },
  /* 57*/{ "x_max_span_length", AINT, FALSE, 0, 10000, "When checking for partial words, impose a limit on the no. of intervening words in the matched part of the record" },
  /* 58 */{ "geo_filter_radius", AFLOAT, FALSE, 0, 20038, "Results further than this distance from (lat,long) in km will be filtered out.  No filtering unless value > 0.0 and lat/longs are known for both query and document." },
  /* 59 */{ "street_address_processing", AINT, FALSE, 0, 10000, "if > 0, delete suite part and street number from query. If > 1, reject candidates for which this street number is not valid." },
  /* 60 */{ "street_specs_col", AINT, FALSE, 0, 10000, "The column in the .forward file containing a list specifying valid street numbers for this doc (assumed to be a street)." },

  /* 61 */{ "query_shortening_threshold", AINT, FALSE, 0, 100, "Queries with more terms than the given value will be shortened to this length. 0 => no shortening" },
  /* 62 */{ "", AEOL, FALSE, 0, 0, "" }
};


int initialize_qoenv_mappings(query_processing_environment_t *qoenv) {
  void **vptra = (void **)malloc(NUMBER_OF_ARGS * sizeof(void *));

  if (vptra == NULL) return(-220002);
  qoenv->vptra = vptra;

  // Create the mappings from the vptra elements to actual values in the query options environment
  vptra[0] = (void *)&(qoenv->index_dir);
  vptra[1] = (void *)&(qoenv->fname_forward);
  vptra[2] = (void *)&(qoenv->fname_if);
  vptra[3] = (void *)&(qoenv->fname_vocab);
  vptra[4] = (void *)&(qoenv->fname_doctable);
  vptra[5] = (void *)&(qoenv->fname_substitution_rules);
  vptra[6] = (void *)&(qoenv->fname_query_batch);
  vptra[7] = (void *)&(qoenv->fname_output);
  vptra[8] = (void *)&(qoenv->fname_config);
  vptra[9] = (void *)&(qoenv->partial_query);
  vptra[10] = (void *)&(qoenv->max_to_show);
  vptra[11] = (void *)&(qoenv->max_candidates_to_consider);
  vptra[12] = (void *)&(qoenv->max_length_diff);
  vptra[13] = (void *)&(qoenv->timeout_kops);
  vptra[14] = (void *)&(qoenv->timeout_msec);
  vptra[15] = (void *)&(qoenv->rr_coeffs[0]);
  vptra[16] = (void *)&(qoenv->rr_coeffs[1]);
  vptra[17] = (void *)&(qoenv->rr_coeffs[2]);
  vptra[18] = (void *)&(qoenv->rr_coeffs[3]);
  vptra[19] = (void *)&(qoenv->rr_coeffs[4]);
  vptra[20] = (void *)&(qoenv->rr_coeffs[5]);
  vptra[21] = (void *)&(qoenv->rr_coeffs[6]);
  vptra[22] = (void *)&(qoenv->rr_coeffs[7]);
  vptra[23] = (void *)&(qoenv->cf_coeffs[0]);
  vptra[24] = (void *)&(qoenv->cf_coeffs[1]);
  vptra[25] = (void *)&(qoenv->cf_coeffs[2]);
  vptra[26] = (void *)&(qoenv->auto_partials);
  vptra[27] = (void *)&(qoenv->auto_line_prefix);
  vptra[28] = (void *)&(qoenv->warm_indexes);
  vptra[29] = (void *)&(qoenv->relaxation_level);
  vptra[30] = (void *)&(qoenv->displaycol);
  vptra[31] = (void *)&(qoenv->query_streams);
  vptra[32] = (void *)&(qoenv->duplicate_handling);
  vptra[33] = (void *)&(qoenv->classifier_mode);
  vptra[34] = (void *)&(qoenv->classifier_threshold);
  vptra[35] = (void *)&(qoenv->classifier_min_words);
  vptra[36] = (void *)&(qoenv->classifier_max_words);
  vptra[37] = (void *)&(qoenv->classifier_segment);
  vptra[38] = (void *)&(qoenv->segment_intent_multiplier);
  vptra[39] = (void *)&(qoenv->classifier_stop_thresh1);
  vptra[40] = (void *)&(qoenv->classifier_stop_thresh2);
  vptra[41] = (void *)&(qoenv->display_parsed_query);
  vptra[42] = (void *)&(qoenv->debug);
  vptra[43] = (void *)&(qoenv->x_show_qtimes);
  vptra[44] = (void *)&(qoenv->object_store_files);
  vptra[45] = (void *)&(qoenv->language);
  vptra[46] = (void *)&(qoenv->use_substitutions);
  vptra[47] = (void *)&(qoenv->include_result_details);
  vptra[48] = (void *)&(qoenv->extracol);
  vptra[49] = (void *)&(qoenv->include_extra_features);
  vptra[50] = (void *)&(qoenv->x_batch_testing);
  vptra[51] = (void *)&(qoenv->allow_per_query_options);
  vptra[52] = (void *)&(qoenv->generate_JO_path);
  vptra[53] = (void *)&(qoenv->conflate_accents);
  vptra[54] = (void *)&(qoenv->chatty);
  vptra[55] = (void *)&(qoenv->location_lat);
  vptra[56] = (void *)&(qoenv->location_long);
  vptra[57] = (void *)&(qoenv->x_max_span_length);
  vptra[58] = (void *)&(qoenv->geo_filter_radius);
  vptra[59] = (void *)&(qoenv->street_address_processing);
  vptra[60] = (void *)&(qoenv->street_specs_col);
  vptra[61] = (void *)&(qoenv->query_shortening_threshold);
  return 0;
} 


void set_qoenv_defaults(query_processing_environment_t *qoenv) {
  int i;
  qoenv->index_dir = NULL;
  qoenv->fname_forward = NULL;
  qoenv->fname_if = NULL;
  qoenv->fname_vocab = NULL;
  qoenv->fname_doctable = NULL;
  qoenv->fname_substitution_rules = NULL;
  qoenv->fname_query_batch = NULL;
  qoenv->fname_output = NULL;
  qoenv->partial_query = NULL;
  qoenv->max_to_show = 8;
  qoenv->max_candidates_to_consider = IUNDEF;
  qoenv->max_length_diff = IUNDEF;
  qoenv->timeout_kops = 0;
  qoenv->timeout_msec = 0;
  qoenv->rr_coeffs[0] = 1.0;
  qoenv->rr_coeffs[1] = 0.0;
  qoenv->rr_coeffs[2] = 0.0;
  qoenv->rr_coeffs[3] = 0.0;
  qoenv->rr_coeffs[4] = 0.0;
  qoenv->rr_coeffs[5] = 0.0;
  qoenv->rr_coeffs[6] = 0.0;
  qoenv->rr_coeffs[7] = 0.0;
  qoenv->cf_coeffs[0] = 1.0;
  qoenv->cf_coeffs[1] = 0.0;
  qoenv->cf_coeffs[2] = 0.0;
  qoenv->auto_partials = FALSE;
  qoenv->auto_line_prefix = FALSE;
  qoenv->warm_indexes = FALSE;
  qoenv->relaxation_level = 0;
  qoenv->displaycol = 3;
  qoenv->extracol = 4;
  qoenv->query_streams = 10;
  qoenv->duplicate_handling = 1;
  qoenv->classifier_mode = 0;
  qoenv->classifier_threshold = 0.75;
  qoenv->classifier_min_words = 0;
  qoenv->classifier_max_words = 255;
  qoenv->classifier_segment = NULL;
  qoenv->segment_intent_multiplier = 1.0;
  qoenv->classifier_stop_thresh1 = 1.0;
  qoenv->classifier_stop_thresh2 = 1.0;
  qoenv->display_parsed_query = FALSE;
  qoenv->debug = 0;
  qoenv->x_show_qtimes = FALSE;
  qoenv->object_store_files = NULL;
  qoenv->language = make_a_copy_of((u_char *)"EN");
  qoenv->use_substitutions = FALSE;
  qoenv->include_result_details = TRUE;
  qoenv->include_extra_features = FALSE;
  qoenv->x_batch_testing = FALSE;
  qoenv->allow_per_query_options = FALSE;
  qoenv->generate_JO_path = FALSE;
  qoenv->conflate_accents = FALSE;
  qoenv->chatty = TRUE;
  qoenv->location_lat = 0.0;
  qoenv->location_long = 0.0;
  qoenv->x_max_span_length = 10000;
  qoenv->geo_filter_radius = 0.0;  
  qoenv->street_address_processing = 0;
  qoenv->street_specs_col = 5;  
  qoenv->query_shortening_threshold = 0;  // No shortening.

  // Not directly settable
  qoenv->scoring_needed = TRUE;
  qoenv->report_match_counts_only = FALSE;
  qoenv->query_output = stdout;
  qoenv->num_substitution_rules = 0;
  qoenv->substitution_rules_regex = NULL;
  qoenv->substitution_rules_rhs = NULL;

  // Setting up for statistics recording for the batch of queries run with these options
  qoenv->inthebeginning = what_time_is_it();  //Probably not in the right place. Reset in QBASHQ.c
  qoenv->queries_run = 0;
  qoenv->query_timeout_count = 0;
  qoenv->global_idf_lookups = 0;
  qoenv->total_elapsed_msec_d = 0.0;
  qoenv->max_elapsed_msec_d = 0.0;
  for (i = 0; i < ELAPSED_MSEC_BUCKETS; i++)
    qoenv->elapsed_msec_histo[i] = 0;

  // ---- Index and properties used in BM25 document scoring  	
  qoenv->ixenv = NULL;
  qoenv->N = UNDEFINED_DOUBLE;
  qoenv->avdoclen = UNDEFINED_DOUBLE;

}


void print_args(query_processing_environment_t *qoenv, format_t f) {
  int a, intval;
  u_char dflt_txt[MAX_EXPLANATIONLEN + 1];
  if (f == HTML) {
    printf("<html>\n<h1>QBASHQ arguments</h1>\n"
	   "<table border=\"1\">\n"
	   "<tr><th>Argument</th><th>Default</th><th>Explanation</th></tr>\n");
  }
  else if (f == TSV) {
    printf("Argument\tDefault\tExplanation\n");
  }
  else printf("\n\n--------------------------------------------------------------------------\n"
	      "%25s - %10s - %s\n"
	      "--------------------------------------------------------------------------\n",
	      "Argument", "Default", "Explanation");
  for (a = 0; args[a].type != AEOL; a++) {
    switch (args[a].type) {
    case ASTRING:
      if (*(char **)(qoenv->vptra[a]) == NULL) strcpy((char *)dflt_txt, "None");
      else strncpy((char *)dflt_txt, *(char **)(qoenv->vptra[a]), MAX_EXPLANATIONLEN);
      dflt_txt[MAX_EXPLANATIONLEN] = 0;
      break;
    case ABOOL:
      if (*(BOOL *)qoenv->vptra[a]) strcpy((char *)dflt_txt, "TRUE");
      else strcpy((char *)dflt_txt, "FALSE");
      break;
    case AINT:
      intval = *(int *)qoenv->vptra[a];
      if (intval == IUNDEF) sprintf((char *)dflt_txt, "AutoSet");
      else sprintf((char *)dflt_txt, "%d", intval);
      break;
    case AFLOAT:
      sprintf((char *)dflt_txt, "%.3g", *(double *)qoenv->vptra[a]);
      break;
    default:  // to stop the moaning
      break;
    }

    if (f == HTML) {
      printf("<tr><td>%s</td><td>%s</td><td>%s</td></tr>\n", args[a].attr, dflt_txt, args[a].explan);
    }
    else if (f == TSV) {
      printf("%s\t%s\t%s\n", args[a].attr, dflt_txt, args[a].explan);
    }
    else printf("%25s - %10s - %s\n",
		args[a].attr, dflt_txt, args[a].explan);
  }
  if (f == HTML) {
    printf("</table>\n<br/><p>Some combinations of options are not supported and may cause\n"
	   "undesirable effects.  In particular, relaxation may not work well with ranking.</p>\n</html>\n");
  }
  else if (f == TEXT) {
    printf("---------------------------------------------------------------------------\n\n"
	   "Some combinations of options are not supported and may cause\n"
	   "undesirable effects.  In particular, relaxation may not work well with ranking.\n\n"
	   "Note 1.  When relaxation_level is non-zero, auto_partials is forced to FALSE.\n"
	   "Note 2.  When prefixes of the first word in a document are indexed (QBASHI option) they are\n"
	   "         prefixed with '>'.  To retrieve documents using a query with no full words, the query must\n"
	   "         be prefixed by '>'.  This option does that, but only with appropriately built indexes.\n"
	   "		  When either relaxation_level is non-zero, this option is also forced FALSE.\n");
  }
}


int assign_one_arg(query_processing_environment_t *qoenv, u_char *arg_equals_val,
		   BOOL initialising, BOOL enforce_limits, BOOL explain) {
  // Given an input string in the form key=value, look up key in the args table and assign 
  // the value to the appropriate variable. 
  // Note that the input string may not be null terminated.  
  // Return 0 iff an assignment successfully occurred. Otherwise a negative error code
  // If no error was detected, next_arg is set to point to the character after the end of the value.
  int a, argnum = -1;
  size_t vallen;
  u_char *p, *q, *t;
  double dval;
  int ival;
  if (0) printf("Trying to assign %s\n", arg_equals_val);
  if (arg_equals_val == NULL) return(-3);  // --------------------------------------->
  while (*arg_equals_val && isspace(*arg_equals_val)) arg_equals_val++;  // Skip leading whitespace
  while (*arg_equals_val == '-') arg_equals_val++;  // Skip leading hyphens
  p = arg_equals_val;
  while (*p && *p != '=') p++;
  if (*p != '=') {
    if (explain) printf("%s: ", arg_equals_val);
    return(-3); // --------------------------------------->
  }
  *p = 0;  // Temporarily null terminate to facilitate arg look-up
  for (a = 0; args[a].type != AEOL; a++) {
    if (!strcmp((char *)arg_equals_val, (char *)args[a].attr)) {
      argnum = a;
      break;
    }
  }
  *p = '=';  // Put back what we disturbed
  if (argnum < 0) {
    if (explain) printf("%s: ", arg_equals_val);
    return(-4);  // --------------------------------------->
  }
  p++;   // Make p point to the start of the value part
  q = p;
  while (*q) q++;  // Find the end of the value part.
  vallen = q - p;

  // Now assign the value.  Unless this is an immutable option and we are not initialising
  if (initialising || !args[argnum].immutable) {
    u_char **ptr2string = NULL;
    if (0) fprintf(stderr, "Found!   Arg %d, value='%s'\n", argnum, p);
    switch (args[argnum].type) {
    case ASTRING:
      if (0) fprintf(stderr, "Assigning ASTRING argument of length %zu. value is '%s'\n",
		     vallen, p);
      ptr2string = (u_char **)qoenv->vptra[a];
      if (*ptr2string != NULL) {
	free(*ptr2string);  // Avoid memory leak if arg is repeated.
	*ptr2string = NULL;
      }
      while (*p && isspace(*p)) { p++; vallen--; }
      if (vallen < 1) {
	if (0) fprintf(stderr, "Skipping empty ASTRING value\n");
	break;  // If we have an empty arg we null out what was there before but otherwise ignore it.
      }
      if (vallen > MAX_VALSTRING) vallen = MAX_VALSTRING;
      t = (u_char *)malloc(vallen + 1);  // MAL3001
      if (t == NULL) {
	if (explain) printf("%s: ", arg_equals_val);
	return(-220005); // --------------------------------------->
      }
      strncpy((char *)t, (char *)p, vallen);
      t[vallen] = 0;
      //url_decode(t);  // Not needed now we don't do CGI any more
      *(u_char **)(qoenv->vptra[a]) = t;
      break;
    case ABOOL:
      if (vallen == 0) {
	if (explain) printf("%s: ", arg_equals_val);
	return(-3); //  --------------------------------------->
      }

      if (!strcmp((char *)p, "true")
	  || !strcmp((char *)p, "on")
	  || !strcmp((char *)p, "allowed")
	  || !strcmp((char *)p, "yes")
	  || !strcmp((char *)p, "TRUE")
	  || !strcmp((char *)p, "ON")
	  || !strcmp((char *)p, "ALLOWED")
	  || !strcmp((char *)p, "YES")
	  || !strcmp((char *)p, "1")
	  ) *(BOOL *)(qoenv->vptra[a]) = TRUE;
      else if (!strcmp((char *)p, "false")
	       || !strcmp((char *)p, "off")
	       || !strcmp((char *)p, "prohibited")
	       || !strcmp((char *)p, "no")
	       || !strcmp((char *)p, "FALSE")
	       || !strcmp((char *)p, "OFF")
	       || !strcmp((char *)p, "PROHIBITED")
	       || !strcmp((char *)p, "NO")
	       || !strcmp((char *)p, "0")
	       )*(BOOL *)(qoenv->vptra[a]) = FALSE;
      else {
	if (explain) printf("%s: ", arg_equals_val);
	return(-3); // --------------------------------------->

      }
      break;
    case AINT:
      if (vallen == 0 || (*p != '+' && *p != '-' && !isdigit(*p))) {
	if (explain) printf("%s: ", arg_equals_val);
	return(-3); // --------------------------------------->
      }
      errno = 0;
      ival = strtol((char *)p, NULL, 10);  // Check re-entrancy
      if (errno) {
	if (explain) printf("%s: ", arg_equals_val);
	return(-3); // --------------------------------------->
      }
      if (enforce_limits) {
	if (ival < (int)args[argnum].minval) ival = (int)args[argnum].minval;
	else if (ival >(int)args[argnum].maxval) ival = (int)args[argnum].maxval;
      }
      *(int *)(qoenv->vptra[a]) = ival;
      break;
    case AFLOAT:
      if (vallen == 0 || (*p != '+' && *p != '-' && *p != '.' && !isdigit(*p))) {
	if (explain) printf("%s: ", arg_equals_val);
	return(-3); // --------------------------------------->
      }
      errno = 0;
      dval = strtod((char *)p, NULL);  // Check re-entrancy
      if (errno) {
	if (explain) printf("%s: ", arg_equals_val);
	return(-3); // --------------------------------------->
      }
      if (enforce_limits) {
	if (dval < args[argnum].minval) dval = args[argnum].minval;
	else if (dval > args[argnum].maxval) dval = args[argnum].maxval;
      }
      *(double *)(qoenv->vptra[a]) = dval;
      break;
    default:
      // This is a structural error.  How should it be handled?
      break;
    }
  }  // End of if (initialising || !args[argnum].immutable)
  // Should we signal an error or warning if an attempt is made to change an immutable attribute?

  return 0;
}


#define MAX_ARGVAL_LEN 1024

int assign_args_from_config_file(query_processing_environment_t *qoenv, u_char *config_filename,
				 BOOL initializing, BOOL explain_errors) {
  u_char *fileinmem, *eofileinmem, *r, *argstart, *eofargval, this_argval[MAX_ARGVAL_LEN + 1];
  size_t sighs, argval_len;
  CROSS_PLATFORM_FILE_HANDLE H;
  HANDLE MH;
  int error_code;

  fileinmem = (u_char *)mmap_all_of(config_filename, &sighs, FALSE, &H, &MH, &error_code);
  if (error_code || fileinmem == NULL || sighs < 1) return error_code;

  eofileinmem = fileinmem + sighs;
  r = fileinmem;
  while (r < eofileinmem) {
    while (r < eofileinmem && *r <= ' ') r++;  // Skip control chars, newlines and spaces to find start of arg=val
    argstart = r;
    while (r < eofileinmem && *r > ' ') r++;   // Skip valid chars to find the end of this arg=val
    eofargval = r;
    argval_len = eofargval - argstart;
    if (argval_len >= 3 && argval_len <= MAX_ARGVAL_LEN) {
      // assign_one_arg requires null-terminated string but mmapped file is RO, hence must copy.
      strncpy((char *)this_argval, (char *)argstart, argval_len);
      this_argval[argval_len] = 0; 
      assign_one_arg(qoenv, this_argval, initializing, TRUE, explain_errors);
      if (0) printf("Assigned: %s\n", this_argval);
    }
  }

  unmmap_all_of(fileinmem, H, MH, sighs);
  return(0);
}



void free_options_memory(query_processing_environment_t *qoenv) {
  int a;
  for (a = 0; args[a].type != AEOL; a++) {
    if (args[a].type == ASTRING) {
      // strings are the only args which use malloced memory.
      if (*((u_char **)qoenv->vptra[a]) != NULL) {
	free(*((u_char **)qoenv->vptra[a]));
	*((u_char **)qoenv->vptra[a]) = NULL;
      }
    }
  }
  if (qoenv->debug >= 1) printf("Memory malloced for string-valued options has been freed.\n");
}
