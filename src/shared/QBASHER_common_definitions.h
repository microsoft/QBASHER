// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

//----------------------------------------------------------------------------------------------------
// This file contains the definitions which are shared between the QBASHER indexer and query processor
//----------------------------------------------------------------------------------------------------

#define IF_HEADER_LEN 4096   // Mustn't change this, except in connection with a change in INDEX_FORMAT
#define INDEX_FORMAT "QBASHER 1.5"  // This will be written into the header area of the .if file.
#define QBASHER_VERSION ".129-OS"   // This is relative to the INDEX_FORMAT.  Whenever the index format
				    // changes this should be reset to .0.  Whenever QBASHI or QBASHQ are
				    // edited it should be incremented.  It's also written into the
				    // .if header.

// Following are the maximum number of bytes considered for indexing in a record. (QBASHI)
#define MAX_DOCBYTES_NORMAL 10240     // In the "normal" case
#define MAX_DOCBYTES_BIGGER 10240000  // With the bigger_trigger option

#define MAX_RESULT_LEN 2000    // This is the maximum number of bytes in the string returned for any result. (QBASHQ)
#define MAX_WD_LEN 15  // Words longer than this will be truncated.
#define MAX_BIGRAM_LEN 31 //
#define MAX_REP_LEN 20
#define MAX_NGRAM_LEN 55  // Should be enough for most quad-grams

#define VOCABFILE_INFO_LEN 12  // 5 bytes for occurrence count, 1 byte for quantized IDF, 6 bytes for .if offset or docnum + wdpos
#define VOCABFILE_REC_LEN 28   // Must be equal to VOCAB_INFO_LEN + MAX_WD_LEN + 1
#define MEGA 1048576.0  // For conversion to megabytes
// The next two constants control the behaviour of the input_buffer_management system
#define IBM_IOBUFSIZE 10485760
#define IBM_BUFFERS_IN_RING 10
#define EASTER_EGG_PATTERN "^gonebut notforgotten$"

#define ASCII_RS 0x1E // ASCII Record Separator (RS)
#define ASCII_GS 0x1E // ASCII Group Separator (GS)

#define HUGEBUFSIZE 4194304   // 4 MB (for i/o buffers)
#define IUNDEF 987654321  // Undefined parameter for integer arguments
#define UNDEFINED_DOUBLE  999999999999.9

#define A_BILLION_AND_ONE 1000000001  // Effectively unlimited ... for an int

#define QBASH_META_CHARS "%\"[]~/"   // Make sure all query special chars are listed here.  *** Must match QBASHQ.h ***
#define OTHER_TOKEN_BREAKERS_DFLT "&'( ),-.:;<=>?@\\^_`{|}!"  // ! added 01 April 2016


// QBASHER 1.0 format allows word positions up to 255 within document and for input files
// of up to 256 GB.  These values are about 16 times the limits for previous versions.
// AutoSuggest is no longer the target application, and there is no longer any need to 
// try to fit AS shard indexes in 2GB.   Room for the excess bits was achieved by shrinking
// the doc score field from 10 to 8 bits and the Bloom filter from 16 to 10.  If speed of
// partial matching becomes critical we can add an extra file containing stronger Bloom
// signatures e.g. 16, 32 or 64 additional bits, and use the 10 bits as a preliminary 
// coarse filter.

// Note that if support for word positions > 255 is required, changes will be needed to 
// linked list posting_t definitions.

#define WDPOS_BITS 8  // (Allowing word positions up to 254  -- 255 reserved for skip block marker)
#define WDPOS_MASK 255  // For writing postings 
#define MAX_WDPOS 254   // Because of using WDPOS_BITS bits for word pos (counting from zero).  Later words may be ignored.
// Must not store value 255 in wdpos byte in .if because it's a skip block marker	 // 

// A doctable entry has the offset into the .forward file, plus the number
// of words in the document, packed into a 64-bit word.  X bits (currently X = 10)
// are now used for the document's default score.   It's an unsigned number
// between 0 and 2^X - 1,  probably calculated as a quantized ratio of log frequencies.


#define DTE_LENGTH 8  // An unsigned long long - the total number of bytes in a .doctable entry. (Best if it's 8)
#define DTE_WDCNT_BITS 5  // To save bits we only store lengths up to 31 in the table.  31 means 31 or above.
#define DTE_WDCNT_MAX 31
#define DTE_DOCOFF_BITS 42 // (allowing up to 4 tB)
#define DTE_SCORE_BITS 9  // 
#define DTE_BLOOM_BITS 8   //

// These are all calculated from the above _BITS definitions by calculate_dte_shifts_and_masks()
// The MASK2 items are assuming that the relevant field has been shifted into the least significant bits,
// while the MASK items apply to the field in the actual doctable entry.
extern unsigned long long
DTE_WDCNT_SHIFT,
DTE_WDCNT_MASK,
DTE_WDCNT_MASK2,
DTE_DOCOFF_SHIFT,
DTE_DOCOFF_MASK,
DTE_DOCOFF_MASK2,
DTE_DOCSCORE_SHIFT,
DTE_DOCSCORE_MASK,
DTE_DOCSCORE_MASK2,
DTE_DOCBLOOM_MASK,
DTE_DOCBLOOM_MASK2,
DTE_DOCBLOOM_SHIFT;

// Definitions for skip blocks.  These must match those in QBASHQ.h
// A skip block is a sequence of 8 bytes packed as follows:  (Changed in version 1.4.0)
//	37 bits - document number of the last posting in the run described by this block
//  12 bits - number of postings in the run described by this block
//  15 bits - the number of bytes in the run described by this block
//  64 bits - total

#define SB_MARKER 0xFF
#define SB_MAX_DOCNO 0x1FFFFFFFFF    // 37 bits as per comment above
#define SB_MAX_BYTES_PER_RUN 0x7FFF  // 15 bits as per comment above
#define SB_MAX_COUNT 0xFFF  // 12 bits as per comment above
#define SB_BYTES 8   // Has to be sizeof(unsigned long long)

#define sb_get_lastdocnum(x) ((x >> 27) & SB_MAX_DOCNO)
#define sb_get_count(x)      ((x >> 15) & SB_MAX_COUNT)
#define sb_get_length(x)     (x & SB_MAX_BYTES_PER_RUN)

// a = docno, b = no of postings in this run, c = no of bytes in this run
#define sb_assemble(a,b,c) (((a & SB_MAX_DOCNO) << 27) | ((b & SB_MAX_COUNT) << 15) | (c & SB_MAX_BYTES_PER_RUN))


// ------------------------------------------------------------------------------------------

typedef enum {   // Types of output allowed by the arg_parser (both qbashi and qbashq)
	TEXT,
	TSV,
	HTML
} format_t;


typedef long long docnum_t;

