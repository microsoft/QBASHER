// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// ********** error_explanations.cpp **********
// Functions in the QBASHQ API, particularly handle_query(), signal error conditions by returning a negative integer.  
// Error codes are assigned contiguously in the range 0-9,999 and may have severity and error category codes added: 
//
// SEVERITY CODES:
//  200,000 - A fatal error.  Not only has it been possible to correctly carry out the requested operation,
//              but the same is likely to occur on subsequent similar requests.  For example, it has not been
//              possible to open index files,or to allocate the memory necessary to perform the operation.
//              The caller should consider resetting the system.
//  100,000 - An error has prevented the correct processing of this query, but as far as we know, the problem
//              is specific to this query.  We know of no reason why queries should not continue to be submitted.
//          0 - A warning or status message.
//
// ERROR CATEGORY CODES: 
//          0 
//    10,000 - I/O error
//    20,000 - Memory allocation error
//    30,000 - Error reported by a system call


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
#include <WinBase.h>
#include <tchar.h>
#include <strsafe.h>
#endif

#include "../shared/utility_nodeps.h"
#include "../shared/QBASHER_common_definitions.h"
#include "../utils/dahash.h"
#include "QBASHQ.h"

#define QBASHER_DEFINED_ERROR_CODES 81

// Severity (0, 1, 2) * 100000 + Category (0, 1, 2, 3) * 10000 + error number % 10000
// 
// Severity: Unknown, Error, Fatal Error
// Category: Unknown, I/O, Memory, Syscall
//
err_desc_t error_code_explanations[] = {
	{ 0, "No errror.\n" },
	{ 1, "Undefined error code.  (Must be an internal code fault.)\n" },
	{ 220002, "Failed to allocate memory for vptra initialize_qoenv_mappings()\n" },
	{ 3, "Empty or invalid option=value string in assign_one_arg()\n" },
	{ 4, "Unrecognized option in assign_one_arg()\n" },
	{ 220005, "Failed to allocate memory in assign_one_arg()\n" },
	{ 210006, "Failed to open file for reading.\n" },
	{ 210007, "Failed to stat file in mmap_all_of()\n" },
	{ 210008, "Failed to createFileMapping in mmap_all_of()\n" },
	{ 210009, "Failed to MapViewOfFile in mmap_all_of()\n" },
	{ 200010, "Internal sanity test failed: Doctable width is not 8 bytes\n" },
	{ 200011, "Internal sanity test failed: Doctable MASK and MASK2 don't match.\n" },
	{ 200012, "Internal sanity test failed: Doctable fields don't add to totbits.\n" },
	{ 200013, "Internal sanity test failed: DOCOFF_SHIFT has wrong value.\n" },
	{ 200014, "Internal sanity test failed: DOCSCORE_SHIFT has wrong value.\n" },
	{ 200015, "Internal sanity test failed: DOCBLOOM_SHIFT has wrong value.\n" },
	{ 200016, "Internal sanity test failed: Doctable entries are not 8 bytes long.\n" },
	{ 200017, "Internal sanity test failed: test_doctable_n_forward().\n" },
	{ 18, "Internal sanity test failed: invalid arguments passed to show_postings().\n" },
	{ 19, "Internal sanity test failed: unable to get_doc() in show_postings().\n" },
	{ 200020, "Internal sanity test failed: skip block macros (a).\n" },
	{ 200021, "Internal sanity test failed: skip block macros (b).\n" },
	{ 200022, "Internal sanity test failed: skip block macros (c).\n" },
	{ 200023, "Internal sanity test failed: skip block macros (d).\n" },
	{ 200024, "Internal sanity test failed: test for is_prefix_match().\n" },
	{ 200025, "Index format error.  Format header line in .if.\n" },
	{ 26, "Index is in old format.  Handling in compatibility mode.\n" },
	{ 200027, "Index is in old format.  Unable to handle using compatibility mode.\n" },
	{ 200028, "Index format error.  Query_meta_chars header line in .if.\n" },
	{ 200029, "Index format error.  Incompatible set of query_meta_chars.\n" },
	{ 200030, "Index format error.  Other_token_breakers.\n" },
	{ 200031, "Size of index file has changed since indexing: .forward\n" },
	{ 200032, "Size of index file has changed since indexing: .dt\n" },
	{ 200033, "Size of index file has changed since indexing: .vocab\n" },
	{ 200034, "Size of index file has changed since indexing: .if\n" },
	{ 35, "Internal sanity test failed: word lookup failed.\n" },
	{ 36, "Internal sanity test failed: test_is_duplicate() failed.\n" },
	{ 220037, "Failed to allocate memory for book_keeping_structure for current query. Can't proceed. \n" },
	{ 20038, "Failed to allocate memory for local options.  Using global ones instead.\n" },
	{ 20039, "No longer used.\n" },
	{ 220040, "Failed to allocate memory for query results in handle_query(). \n" },
	{ 41, "Empty query.\n" },
	{ 220402, "Failed to allocate memory for candidate result blocks. \n" },
	{ 220403, "Failed to allocate memory for rank_only_counts array. \n" },
	{ 220404, "Failed to allocate memory for candidates result block array. \n" },
	{ 220045, "Failed to allocate memory for rank_only_counts array.\n" },
	{ 100046, "Invalid arguments to saat_setup().\n" },
	{ 220047, "Failed to allocate memory for term blocks in saat_setup().\n" },
	{ 48, "Invalid arguments to saat_advance_within_doc().\n" },
	{ 49, "Invalid arguments to saat_skipto().\n" },
	{ 100050, "Query longer than MAX_WDS_IN_QUERY (32) words in possibly_record_candidate().\n" },
	{ 200051, "Internal error.  Number of signature bits requested exceeds 64.\n" },
	{ 220052, "Failed to allocate memory for term copy in saat_setup_disjunction().\n" },
	{ 53, "Malformed disjunction in query.\n" },
	{ 220054, "Failed to allocate memory for saat_block in saat_setup_disjunction().\n" },
	{ 220055, "Failed to allocate memory for term copy in saat_setup_phrase().\n" },
	{ 56, "Malformed phrase in query.\n" },
	{ 220057, "Failed to allocate memory for saat_block in saat_setup_phrase().\n" },
	{ 100058, "Invalid parameters to saat_relaxed_and().\n" },
	{ 220059, "Failed to allocate memory for recorded in saat_relaxed_and().\n" },
	{ 100060, "No longer used.\n" },
	{ 220061, "No longer used.\n" },
	{ 200062, "Invalid qoenv on call to load_indexes().\n" },
	{ 200063, "Failed to allocate memory in load_indexes().\n" },
	{ 200064, "If index_dir is not given, all four index files must be individually specified.\n" },
	{ 200065, "It is not permitted to specify both index_dir and individual input/output files.\n" },
	{ 210066, "Unable to open file_output for writing.\n" },
	{ 200067, "Object Store: internal tests failed.\n" },
	{ 200068, "Object Store: QBASHQ_LIB.dll must be built for 64 bit architecture but isn't.\n" },
	{ 220069, "Object Store: Failed to create a query processing environment.\n" },
	{ 220070, "Object Store: Unable to allocate memory for filename.\n" },
	{ 200071, "Object Store: Unrecognized file passed to NativeInitializeSharedFiles()\n" },
	{ 200072, "Object Store: Incomplete file list passed to NativeInitializeSharedFiles().\n" },
	{ 200073, "Internal sanity test failed: test_tailstr().\n" },
	{ 200074, "Internal sanity test failed: test_substitute().\n" },
	{ 200075, "Internal sanity test failed: bag similarity calculations.\n" },
	{ 200076, "Internal sanity test failed: prefix signature test.\n" },
	{ 200077, "Internal sanity test failed: signature test.\n" },  // Not used!!
	{ 78, "Internal sanity test failed: zero skip block length in show_postings().\n" },
	{ 20079, "Failed to allocate memory for substitution rules.  Substitutions turned off.\n" },
	{ 23080, "Error return from WideCharToMultiByte() in NativeExecuteQueryAsync().\n" },
};


err_desc_t *explain_error(int code) {
  // int severity, category;
  int tableoff;

	if (code >= 0) code = 0;
	else code = -code;   // Change the sign to permit access to the table
	//severity = code / 100000;
	code = code % 100000;
	//category = code / 10000;
	code = code % 10000;
	tableoff = code;
	if (tableoff > QBASHER_DEFINED_ERROR_CODES) tableoff = 1;
	return error_code_explanations + tableoff;
}


void print_errors() {
	int c, code, severity, category;
	printf("Table of QBASHQ API error return codes:\n"
		"========================================\n");
	for (c = 0; c < QBASHER_DEFINED_ERROR_CODES; c++) {
		code = error_code_explanations[c].code;
		severity = code / 100000;
		code = code % 100000;
		category = code / 10000;
		code = code % 10000;
		if (severity == 2) printf("Fatal   - ");
		else if (severity == 1) printf("Error   - ");
		else printf("Warning - ");
		if (category == 3) printf("Syscall - ");
		else if (category == 2) printf("Memory  - ");
		else if (category == 1) printf("I/O     - ");
		printf("%s\n", error_code_explanations[c].explanation);
	}
	printf("========================================\n\n");

}
