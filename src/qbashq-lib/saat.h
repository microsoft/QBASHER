// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

typedef enum {
	SAAT_DISJUNCTION,
	SAAT_PHRASE,
	SAAT_WORD,
	SAAT_NOT_USED
} saat_node_type_t;


typedef struct saat_struct{
  saat_node_type_t type;
  byte *dicent;   // Vocab entry                       [ONLY FOR SAAT_WORD]
  byte qidf;     //                                    [SO FAR ONLY FOR SAAT_WORD]
  int tf;         //                                   [ONLY USED IN BM25 SCORING]
  int repetition_count; //                             [ONLY FOR SAAT_WORD] - tf within query.
  long long occurrence_count;   //                     [ONLY FOR SAAT_WORD]
  byte *curpsting;  // Pointer to current posting      [ONLY FOR SAAT_WORD]
  int offset_within_phrase;  // 0 for first word       [ONLY FOR SAAT_PHRASE]
  long long posting_num;  // Index of last decoded posting in postings list, counting
  // from one for easy comparison with no. occurrences [ONLY FOR SAAT_WORD]
  docnum_t curdoc;       // Doc number of last decoded posting
  int curwpos;            // Word pos of last decoded posting
  BOOL exhausted;         // Set when we attempt to advance beyond the end of the list
  int num_children;       //                            [0 FOR SAAT_WORD]
  struct saat_struct *children;  // An array of immediate descendents [FOR ALL BUT SAAT_WORD]
} saat_control_t;


// Macros for implementing disjunction and phrase rules.  NOTE THAT the macros
// assume that variables child and blok are appropriately declared and assigned
// in the context of the macro call
#define IHUGE 999999999
#define LLHUGE 9999999999999LL
#define CURDOC_EXHAUSTED LLHUGE
#define DONT_CARE 999999999    // Word pos don't care value

// The following macro compares the (doc,wpos) of an unexhausted child node with
// those of its disjunction parent -- These are set to LLHUGE, IHUGE before processing
// the children using the following:
// 1. If the child is already past the (doc, pos) of the parent, or it's exhausted,
//    do nothing.
// 2. If the docs are the same and the wpos of the child is less than that
//    of the parent, update the wpos of the parent.
// 3. Otherwise (child doc < parent doc) set parent doc and wpos from the child.
//
// After looping through all children, parent is set to lowest (doc,wpos) consistent
// with the goals of the skipto.

#define MACdisjrule2() \
if (!child->exhausted && child->curdoc <= blok->curdoc) {\
\
  if (child->curdoc == blok->curdoc) {					\
    if (child->curwpos < blok->curwpos) blok->curwpos = child->curwpos;	\
  }									\
  else {								\
    blok->curdoc = child->curdoc;					\
    blok->curwpos = child->curwpos;					\
  }									\
 }									\

saat_control_t *saat_setup(query_processing_environment_t *qoenv, book_keeping_for_one_query_t *qex,
			   int *terms_not_present, int *error_code);

int saat_advance_within_doc(FILE *out, saat_control_t *pl_blok, byte *index, op_count_t *op_count, int debug);

int saat_get_tf(FILE *out, saat_control_t *blok, byte *index, op_count_t *op_count, int debug);

int saat_skipto(FILE *out, saat_control_t *pl_blok, int blokno, docnum_t desired_docnum, int desired_wpos,
	byte *index, op_count_t *op_count, int debug, int *error_code);

void free_querytree_memory(saat_control_t **plists, int blok_count);

void saat_relaxed_and(FILE *out, query_processing_environment_t *qoenv, book_keeping_for_one_query_t *qex,
		      saat_control_t *pl_blox, byte *forward, byte *index, byte *doctable, size_t fsz,
		      int *error_code);

void show_doc(byte *doctable, byte *forward, size_t fsz, saat_control_t *pl_blok);

int possibly_record_candidate(query_processing_environment_t *qoenv,
			      book_keeping_for_one_query_t *qex, saat_control_t *pl_blox, 
			      byte *forward, byte *index, byte *doctable,
			      size_t fsz, long long candid8, int terms_missing, 
			      u_int terms_matched_bits);

