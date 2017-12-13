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
#include <errno.h>

#include "../shared/utility_nodeps.h"

#include "../shared/QBASHER_common_definitions.h"
#include "QBASHI.h"
#include "../utils/linked_list.h"
#include "arg_parser.h"


arg_t args[] = {
	{ "index_dir", ASTRING, (void *)&index_dir, "Directory in which to find QBASH.forward and in which to build the index.\n                       (Incompat.with next four options.Either use index - dir or ALL of the next four.)" },
	{ "file_forward", ASTRING, (void *)&fname_forward, "The name of the .forward file containing TSV data to be indexed.  Also used for PDI. (Incompat. with index_dir)" },
	{ "file_if", ASTRING, (void *)&fname_if, "The name of the .if (inverted file) file produced during indexing. (Incompat. with index_dir)" },
	{ "file_vocab", ASTRING, (void *)&fname_vocab, "The name of the .vocab file  produced during indexing. (Incompat. with index_dir)" },
	{ "file_doctable", ASTRING, (void *)&fname_doctable, "The name of the .doctable file produced during indexing. (Incompat. with index_dir)" },
	{ "language", ASTRING, (void *)&language, "Any language-specific processing assumes this language." },
	{ "other_token_breakers", ASTRING, (void *)&other_token_breakers, "The set of non-word characters, other than query metachars, used to delimit words for indexing." },
	{ "case_fold", ABOOL, (void *)&unicode_case_fold, "Lowercase words to be indexed.  Data to be indexed assumed to be UTF-8. (But see expect_cp1252.)" },
	{ "conflate_accents", ABOOL, (void *)&conflate_accents, "For every word indexed which contains a letter with diacritics, also index a version of the word with all accents removed."},
	{ "expect_cp1252", ABOOL, (void *)&expect_cp1252, "If text is likely to contain CodePage 1252 chars, extended punctuation should be token breaking.)" },
	{ "min_wds", AINT, (void *)&min_wds, "Records with fewer than this number of words will not be indexed." },
	{ "max_wds", AINT, (void *)&max_wds, "If greater than zero, records with more than this number of words will not be indexed." },
	{ "max_raw_score", AFLOAT, (void *)&max_raw_score, "When indexing records in file order, scores in column 2 will be divided by this value." },
	{ "score_threshold", AFLOAT, (void *)&score_threshold, "Index only records whose scores in column 2 equals or exceeds the specified value." },
	{ "sb_run_length", AINT, (void *)&SB_POSTINGS_PER_RUN, "How many compressed postings occur in a run between consecutive skip blocks. Zero means set dynamically." },
	{ "sb_trigger", AINT, (void *)&SB_TRIGGER, "Skip blocks will only be inserted in a postings list with at least this number of postings.  Zero means no skip blocks." },
	{ "max_line_prefix", AINT, (void *)&max_line_prefix, "Index prefixes of the first word of a document up to this number of bytes." },
	{ "max_line_prefix_postings", AINT, (void *)&max_line_prefix_postings, "Limit on how many postings are stored for each line_prefix. Ignored unless max_line_prefix > 0." },
	{ "debug", AINT, (void *)&debug, "Activate debugging output.  0 - none, 1 - low, 4 - highest. (Not fully implemented.)" },
#ifndef QBASHER_LITE
	{ "sort_records_by_weight", ABOOL, (void *)&sort_records_by_weight, "If FALSE, records will be indexed in file order, and col. 2 is assumed to contain integer scores in range 0 - max_raw_score." },
	{ "x_max_docs", AINTLL, (void *)&x_max_docs, "Stop indexing once this number of records have been indexed. (Incompatible with [default] sort_records_by_weight.)" },
	{ "x_hashbits", AINT, (void *)&x_hashbits, "Explicitly set the initial size of the vocab hashtable.  " },
	{ "x_hashprobe", AINT, (void *)&x_hashprobe, "Choose collision handling method.  0 - RPR, 1 - linear probing. " },
	{ "x_use_large_pages", ABOOL, (void *)&x_use_large_pages, "If true, attempt to use the VM Large Pages mechanism to improve performance. " },
	{ "x_chunk_func", AINT, (void *)&x_chunk_func, "If non-zero the in-memory linked lists will be chunked using a scheme spedified by number. (Experimental.)" },
	{ "x_minimize_io", ABOOL, (void *)&x_minimize_io, "If TRUE avoid normal i/o.  I.e. don't write index files. (Use for timing purposes). " },
	{ "x_2postings_in_vocab", ABOOL, (void *)&x_2postings_in_vocab, "If TRUE store the first two linked list elements in the hash table entry. " },
	{ "x_use_vbyte_in_chunks", ABOOL, (void *)&x_use_vbyte_in_chunks, "If TRUE the content of chunks for some list chunks may be compressed." },
	{ "x_min_payloads_per_chunk", AINT, (void *)&x_min_payloads_per_chunk, "If non-zero, chunks will always have room for at least this number of payloads." },
	//{ "x_sort_postings_instead", AINT, (void *)&x_sort_postings_instead, "If val > 0, linked lists will not be used.  Up to val million postings will be kept and sorted. (Incomplete.)" },
	{ "x_cpu_affinity", AINT, (void *)&x_cpu_affinity, "The number of the core QBASHI should run on. If not in process mask, will try higher numbers." },
	{ "x_bigger_trigger", ABOOL, (void *)&x_bigger_trigger, "Allow the indexing of more than 255 words per record." },
	{ "x_doc_length_histo", ABOOL, (void *)&x_doc_length_histo, "Whether to create QBASH.doclenhist, a histogram of document lengths. (Only applicable if index_dir is defined.)" },
	{ "x_geo_tile_width", AFLOAT, (void *)&x_geo_tile_width, "The width of geo-spatial tiles in km. If zero, no tiling." },
	{ "x_geo_big_tile_factor", AINT, (void *)&x_geo_big_tile_factor, "If > 1, also index geo-spatial tiles which are this integer factor bigger than the standard ones. (Only if tiling.)" },
	
#endif
	{ "", AEOL, NULL, "" }
};


void print_args(format_t f) {
	int a;
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
		"%24s - %11s - %s\n"
		"--------------------------------------------------------------------------\n",
		"Argument", "Default", "Explanation");
	for (a = 0; args[a].type != AEOL; a++) {
		switch (args[a].type) {
		case ASTRING:
			if (*(u_char *)(args[a].valueptr) == 0) strcpy((char *)dflt_txt, "None");
			else strncpy((char *)dflt_txt, *(char **)(args[a].valueptr), MAX_EXPLANATIONLEN);
			dflt_txt[MAX_EXPLANATIONLEN] = 0;
			break;
		case ABOOL:
			if (*(BOOL *)args[a].valueptr) strcpy((char *)dflt_txt, "TRUE");
			else strcpy((char *)dflt_txt, "FALSE");
			break;
		case AINT:
			sprintf((char *)dflt_txt, "%d", *(int *)args[a].valueptr);
			break;
		case AINTLL:
			sprintf((char *)dflt_txt, "%lld", *(long long *)args[a].valueptr);
			break;
		case AFLOAT:
			sprintf((char *)dflt_txt, "%.3g", *(double *)args[a].valueptr);
			break;
		default:   // Actually an error, but just ignore it.
		  break;
		}

		if (f == HTML) {
			printf("<tr><td>%s</td><td>%s</td><td>%s</td></tr>\n", args[a].attr, dflt_txt, args[a].explan);
		}
		else if (f == TSV) {
			printf("%s\t%s\t%s\n", args[a].attr, dflt_txt, args[a].explan);
		}
		else printf("%24s - %11s - %s\n",
			args[a].attr, dflt_txt, args[a].explan);
	}
	if (f == HTML) {
		printf("</table>\n</html>\n");
	}
	else if (f == TEXT) {
		printf("---------------------------------------------------------------------------\n");
	}

}


int store_arg_values(u_char *buffer, size_t buflen, BOOL show_experimentals) {
	// Store values of all QBASHI arguments in the form of arg=value, one per line in the buffer.
	// Unless show_experimentals don't include arguments beginning with x_
	// Return the number of bytes stored.   
	// If buflen is too small, back off by 11 bytes and write "TRUNCATED"
	int a;
	u_char *one_arg = NULL, *w = buffer;
	size_t needed, left = buflen - 1, l, available = MAX_VALSTRING + 1;

	if (buflen < 400) return -1;   // Buffer too small 
	if (available > buflen / 3) available = buflen / 3;
	one_arg = (u_char *)malloc(available + 1);
	if (one_arg == NULL)  return -2;   // Malloc failed.

	for (a = 0; args[a].type != AEOL; a++) {
		if (!show_experimentals && !strncmp((char *)args[a].attr, "x_", 2)) continue;
		sprintf((char *)one_arg, "%s=", args[a].attr);
		l = strlen((char *)one_arg);
		switch (args[a].type) {
		case ASTRING:
			// This is the only one which could overflow, make sure to use strncpy
			if (*(u_char *)(args[a].valueptr) == 0) strcpy((char *)one_arg + l, "None");
			else strncpy((char *)one_arg + l, *(char **)(args[a].valueptr), available - l - 1);
			break;
		case ABOOL:
			if (*(BOOL *)args[a].valueptr) strcpy((char *)one_arg + l, "TRUE");
			else strcpy((char *)one_arg + l, "FALSE");
			break;
		case AINT:
			sprintf((char *)one_arg + l, "%d", *(int *)args[a].valueptr);
			break;
		case AINTLL:
			sprintf((char *)one_arg + l, "%lld", *(long long *)args[a].valueptr);
			break;
		case AFLOAT:
			sprintf((char *)one_arg + l, "%.3g", *(double *)args[a].valueptr);
			break;
		default:
		  break;  // Impossible, but needed to stop compiler whinging
		}

		needed = strlen((char *)one_arg);
		one_arg[needed++] = '\n';
		if (needed > left) {
			strcpy((char *)(w - 11), "TRUNCATED\n");
			free(one_arg);
			return (int)(w - buffer);
		}
		else {
			strcpy((char *)w, (char *)one_arg);
			w[needed] = 0;
			w += needed;
			left -= needed;
		}
	}
	free(one_arg);
	return (int)(w - buffer);
}




int assign_one_arg(u_char *arg_equals_val, u_char **next_arg) {
	// Given an input string in the form key=value, look up key in the args table and assign 
	// the value to the appropriate variable. 
	// Note that the input string may not be null terminated.  A value is terminated by '&' as well.
	// Return 1 iff an assignment successfully occurred. Otherwise zero or negative
	// If no error was detected, next_arg is set to point to the character after the end of the value.
	int a, argnum = -1;
	size_t vallen;
	u_char *p, *q, saveq, *t;
	double dval;
	long long llval;
	int ival;
	*next_arg = NULL;  // Cover all the error returns.
	if (0) printf("Trying to assign %s\n", arg_equals_val);
	if (arg_equals_val == NULL) return -1;  // --------------------------------------->
	p = arg_equals_val;
	while (*p && *p != '=' && *p != '&') p++;
	if (*p != '=') return -2; // --------------------------------------->
	q = p + 1;
	while (*q && *q != '&') q++;
	vallen = q - p;
	*p = 0;  // Temporarily null terminate to facilitate look-up
	for (a = 0; args[a].type != AEOL; a++) {
		if (!strcmp((char *)arg_equals_val, (char *)args[a].attr)) {
			argnum = a;
			break;
		}
	}
	*p = '=';  // Put back what we disturbed
	if (argnum < 0) {
		printf("Error: Argument '%s' not matched.\n", arg_equals_val);
		exit(1); // ---------------------------------------> XXX
	}
	// Now assign the value.
	saveq = *q;
	*q = 0;  // after temporarily making sure it's null terminated.
	if (0) printf("Found!   Arg %d, value='%s'\n", argnum, p + 1);
	switch (args[argnum].type) {
	case ASTRING:
		if (vallen > MAX_VALSTRING) vallen = MAX_VALSTRING;
		t = (u_char *)malloc(vallen + 1);
		if (t == NULL) return -3; // --------------------------------------->
		strncpy((char *)t, (char *)p + 1, vallen);
		t[vallen] = 0;
		*(u_char **)(args[a].valueptr) = t;
		break;
	case ABOOL:
		if (!strcmp((char *)p + 1, "true")
			|| !strcmp((char *)p + 1, "on")
			|| !strcmp((char *)p + 1, "allowed")
			|| !strcmp((char *)p + 1, "yes")
			|| !strcmp((char *)p + 1, "TRUE")
			|| !strcmp((char *)p + 1, "ON")
			|| !strcmp((char *)p + 1, "ALLOWED")
			|| !strcmp((char *)p + 1, "YES")
			|| !strcmp((char *)p + 1, "1")
			) *(BOOL *)(args[a].valueptr) = TRUE;
		else *(BOOL *)(args[a].valueptr) = FALSE;
		break;
	case AINT:
		errno = 0;
		ival = strtol((char *)p + 1, NULL, 10);
		if (errno) {
			*q = saveq;
			return -4; // --------------------------------------->
		}
		*(int *)(args[a].valueptr) = ival;
		break;
	case AINTLL:
		errno = 0;
		llval = strtoll((char *)p + 1, NULL, 10);
		if (errno) {
			*q = saveq;
			return -4; // --------------------------------------->
		}
		*(long long *)(args[a].valueptr) = llval;
		break;
	case AFLOAT:
		errno = 0;
		dval = strtod((char *)p + 1, NULL);
		if (errno) {
			*q = saveq;
			return -5; // --------------------------------------->
		}
		*(double *)(args[a].valueptr) = dval;
		break;
	default:   // Actually an error, but just ignore it.
	  break;
	}
	*q = saveq;
	if (*q) *next_arg = (q + 1);
	return 1;
}
