// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.



#define BYTES_FOR_TERMID 4
#define BYTES_FOR_INDEX_OFFSET 8
#define BYTES_FOR_QSCORE 2
#define BYTES_FOR_DOCID 3
#define BYTES_FOR_RUN_LEN BYTES_FOR_DOCID
#define BYTES_FOR_POSTINGS_COUNT BYTES_FOR_DOCID
#define BYTES_IN_VOCAB_ENTRY (BYTES_FOR_TERMID + BYTES_FOR_POSTINGS_COUNT + BYTES_FOR_INDEX_OFFSET)

typedef enum {   // Types of output allowed by the arg_parser (both qbashi and qbashq)
        TEXT,
        TSV,
        HTML
} format_t;


//----------------------------------------------------------------------------------------
// Providing alternate definitions for items pre-declared in a Windows environment.

#ifndef HANDLE
typedef void *HANDLE;
#endif
//----------------------------------------------------------------------------------------

#if defined(WIN32) || defined(WIN64)
typedef HANDLE CROSS_PLATFORM_FILE_HANDLE;
#else
typedef int CROSS_PLATFORM_FILE_HANDLE;
#endif

#ifndef BOOL
typedef int BOOL;
#endif

#ifndef u_char
typedef unsigned char u_char;
#endif

#ifndef byte
typedef unsigned char byte;
#endif

#ifndef u_short
typedef unsigned short u_short;
#endif


#ifndef u_int
typedef unsigned int u_int;
#endif

#ifndef u_ll
typedef unsigned long long u_ll;
#endif

#ifndef LPCSTR
typedef const char *LPCSTR;
#endif

#ifndef DWORD
typedef unsigned long DWORD;
typedef DWORD *LPDWORD;
#endif


#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

