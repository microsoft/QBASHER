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
#endif

#include "../shared/utility_nodeps.h"
#include "../shared/QBASHER_common_definitions.h"
#include "../utils/dahash.h"
#include "QBASHQ.h"
#include "saat.h"


// ---------------------------------------------------------------------------------------
// SAAT (suggestion-at-a-time) functions.  Just like document-at-a-time
// ---------------------------------------------------------------------------------------
//
// saat.cpp and saat.h define the control structures and low level functions to support
// the SAAT operations.
//
// A saat_control_t block is can either be a leaf or a non-terminal, depending upon its
// type.  If its type is SAAT_WORD it records all the information to control the processing
// of a single postings list, including a pointer to the vocab entry for the term
// to which the postings list corresponds.  OTherwise it is a non-terminal whose children 
// may be either non-terminals or leaves.

// Postings format
// ---------------
// A posting consists of a wdnum followed by a vbyte-encoded docgap. For some reason I
// can't remember a 1 is added to the docgap, so that postings within the same document
// have a docgap of 1

// Note on implementation of skip blocks.
// --------------------------------------
// The low-level SAAT functions are implemented to expect the possibility that any 
// postings list may include skip blocks.  QBASHQ doesn't have to be told whether
// skip blocks are used, or what skipping factor applies.
//
// If the leading byte of a posting is 0xFF (SB_MARKER), that is the signal that a skip block follows immediately.
// If a postings list contains skip blocks, the list will start with SB_MARKER and will be
// in the following format:
//
//  |SB_MARKER|SKIP BLOCK|RUN_OF_COMPRESSED_POSTINGS|SB_MARKER|SKIP BLOCK|RUN_OF_COMPRESSED_POSTINGS| ....

// A skip block consists of a total of 8 bytes:
//
// Lastdocnum (35 bits): The docnum of the last posting in the following run (Allows up to 32 billion docs)
// Lastwdnum (4 bits): The wdnum of the last posting in the following run
// Count (11 bits): The number of postings in the following run  (max poss. 2048)
// Length (14 bits): The offset from this SB_MARKER byte to the next, or zero
//   if there are no more blocks.  (Allows up to 8 bytes per posting average which will never happen.)
//
// Skip blocks are handled as follows:
//
// saat_setup() - if the first byte of a postings list is SB_MARKER then the skip block is skipped
//	   and docnum and wdnum are set from the first posting in the run.
// saat_advance_xxx() - if the first byte of a posting is in fact an SB_MARKER, then the skip block
//     is skipped and wdnum and doccnum difference are extracted from the first posting of the next run
// saat_skipto() - if an SB_MARKER byte is encountered, then the skipblock is read.  The target docnum
//     is compared with Lastdocnum. If less than or equal, the skipblock is skipped and decompression 
//     of the following run proceeds as normal, as the target may lie within the run.  Otherwise:
//	      A. If Length is zero, mark this list as exhausted, otherwise:
//        B. Set docnum from lastdocnum
//        C. Add count to the posting count in the control block
//        D. Increment the indexpointer to the next SB_MARKER byte and keep going.

static int setup_phrase_node(FILE *out, u_char *term, saat_control_t *blok, byte *index, byte *vocab, size_t vsz,
			     int *terms_not_present, op_count_t *op_count, double N, int debug);   // Forward decln


static int leaf_peek_tf(byte *ixptr, docnum_t docno) {
  // Called from saat_skipto() to count the tf of a top-level word.
  // I'm assuming that ixptr points to the first byte of the next posting for this term,
  // or to a skip block.  The first byte of the posting is the wdnum
  int tf = 1;
  byte bight;

  if (ixptr == NULL) {
    // Just a safeguard.
    return 1;  // ------------------------------------------->
  }
  
  while (1) {   
    // ----- HANDLE SKIP BLOCK HERE ------
    // Just skip over it.
    if (*ixptr == SB_MARKER) {
      ixptr += (SB_BYTES + 1);
    }

    // Next byte in .if is word position.    Peek at the next byte beyond that.  If it represents a 
    // doc gap of zero, advance the curpsting pointer by 2 bytes, otherwise zero bytes.

    bight = *(ixptr + 1); 
    if (bight == 1) {
      // The next posting is in the same doc because the docgap is zero.  We 
      // know this because the vbyte representation of a docgap of zero is 1.
      //  Move on to it
      tf++;
      ixptr += 2;
    } else {
      if (0 && tf > 1) printf("    leaf_peek_tf(docno = %lld) - returning tf = %d\n", docno, tf);
      return tf;  //  ---------------------------------------->
    }
  }
  return tf;
}


static int setup_word_node(FILE *out, u_char *word, saat_control_t *blok, byte *index, byte *vocab, size_t vsz,
			   int *terms_not_present, op_count_t *op_count, double N, int debug) {
  // A word node must be a leaf in the query tree.  It has no children but controls the processing
  // of a single postings list.  This function looks up the word and, if found, sets up blok to
  // reference both the vocab entry and the postings list.
  // Return 0 on success, -ve on error  (No errors defined yet.)

  size_t len;

  if (debug >= 1) fprintf(out, "setup_word_node(%s)\n", word);
  blok->type = SAAT_WORD;
  blok->num_children = 0;
  blok->children = NULL;
  blok->repetition_count = 1;  // How many times this word is repeated within the query.

  len = strlen((char *)word);
  if (len > MAX_WD_LEN) {
    // Truncate the word  -- Need to do this properly for UTF-8
    word[MAX_WD_LEN] = 0;
  }


  blok->dicent = lookup_word(word, vocab, vsz, debug);
  op_count[COUNT_TLKP].count++;
  if (blok->dicent == NULL) {
    // If one word is not found no suggestion can be made
    blok->exhausted = TRUE;
    blok->curdoc = CURDOC_EXHAUSTED;
    (*terms_not_present)++;
    if (debug >= 1) fprintf(out, " setup_word_node(): No matches for '%s'.\n", word);
  }
  else {
    u_ll docgap;
    byte bight, last, qidf;
    u_ll payload;

    vocabfile_entry_unpacker(blok->dicent, MAX_WD_LEN + 1, (u_ll *)&blok->occurrence_count, &qidf, &payload);
    blok->qidf = qidf;
    blok->exhausted = FALSE;
    if (blok->occurrence_count == 1) {
      // The posting is kept in the vocab table
      blok->curpsting = NULL;
      blok->curdoc = payload;
      blok->curwpos = (int)(blok->curdoc & WDPOS_MASK);
      blok->curdoc >>= WDPOS_BITS;
      blok->posting_num = 1;
      if (0) printf("Extracted single posting: doc = %lld , wpos=%d\n", blok->curdoc, blok->curwpos);
    }
    else {
      // payload references a chunk of the index file
      byte *ixptr = index + payload;

      // ----- HANDLE SKIP BLOCK HERE ------
      // Just skip over it.
      if (*ixptr == SB_MARKER) {
	if (debug >= 2) fprintf(out, "setup_word_node() - skipping skipblock\n");
	ixptr += (SB_BYTES + 1);
      }

      blok->curwpos = *ixptr;  // Word pos is now a full byte.
      // Docgap is encoded in big-endian vbyte with LSB in each byte signalling whether this is the
      // last byte or not.
      ixptr++;
      docgap = 0;
      do {
	docgap <<= 7;
	bight = *ixptr++;
	last = bight & 1;
	bight >>= 1;
	docgap |= bight;
      } while (!last);
      blok->curdoc = docgap;
      blok->curpsting = ixptr;
      blok->posting_num = 1;
    }
  }
  if (debug >= 2)
    fprintf(out, "SAAT block set up for word '%s'.  Referencing (%lld, %d).\n",
	    word, blok->curdoc, blok->curwpos);
  return 0; // ------------------------------------->

}


// Rules for Disjunction blocks:
//   1. A disjunction is exhausted iff all of its descendants are
//   2. The (curdoc, curwpos) of a disjunction is the minimum of those of its descendants

static int setup_disjunction_node(FILE *out, u_char *interm, saat_control_t *blok, byte *index, byte *vocab, size_t vsz,
				  int *terms_not_present, op_count_t *op_count, double N, int debug) {
  // Return 0 on success, -ve on error  (No errors defined yet.)
  u_char *term, *p, *start, savep;
  int children = 0, ltnp = 0, code;  // lntp - Local terms not present
  saat_control_t *child;

  term = make_a_copy_of(interm);   // It has to be a copy because other shard threads may operate on interm. NO LONGER TRUE
  if (term == NULL) {
    return(-220052);  // -------------------------------------------->
  }
  blok->exhausted = TRUE;  // Assume the worst
  blok->curdoc = CURDOC_EXHAUSTED;
  blok->curwpos = IHUGE;
  blok->dicent = NULL;
  blok->children = NULL;
  blok->type = SAAT_DISJUNCTION;

  if (debug >= 1) fprintf(out, "setup_disjunction_node(%s)\n", term);

  // Count how many children.
  p = term + 1;  // Skip '[' 
  while (*p && *p != ']') {
    while (*p && *p == ' ') p++; // Skip leading spaces
    if (!*p) break;
    if (*p == '"') {
      children++;
      p++;
      while (*p && *p != '"') p++;
      if (*p != '"') {
	// Phrase within disjunction is malformed - ignore disjunction.
	children = 0;
	break;
      }
      p++;
    }
    else {
      children++;
      while (*p && *p != ']' && *p != ' ') p++;
    }
  }

  if (*p != ']'  || children <= 0) {
    // Disjunction is malformed ignore it.
    blok->exhausted = TRUE;
    (*terms_not_present)++;
    free(term);
    return(-53);  // ---------------------------------->
  }

  if (debug >= 1) fprintf(out, "  disjunction children: %d\n", children);


  blok->num_children = children;
  blok->children = (struct saat_struct *) malloc(children * sizeof(struct saat_struct));
  if (blok->children == NULL) {
    free(term);
    (*terms_not_present)++;
    return(-220054);  // ---------------------------------->
  }
  // Now set up the children
  children = 0;
  p = term + 1;  // Skip '[' 
  while (*p && *p != ']') {
    while (*p && *p == ' ') p++; // Skip leading spaces
    if (!*p) break;
    start = p;
    if (*p == '"') {
      p++;
      while (*p && *p != '"') p++;
      if (*p) p++;
      savep = *p;
      *p = 0;
      child = blok->children + children;
      code = setup_phrase_node(out, start, child, index, vocab, vsz, &ltnp, op_count, N, debug);
      *p = savep;
      if (code < 0) return(code);  // ------------------------------------------>
      children++;
    }
    else {
      while (*p && *p != ']' && *p != ' ') p++;
      savep = *p;
      *p = 0;
      child = blok->children + children;
      code = setup_word_node(out, start, child, index, vocab, vsz, &ltnp, op_count, N, debug);
      *p = savep;
      if (code < 0) return(code);  // ------------------------------------------>
      children++;
    }
    // Apply rule 2 for disjunctions to the newly created child.   I.e. set (doc, wpos)
    // to the lowest of those of the children
    MACdisjrule2();  // See comments on macro definition in saat.h
  }
  // This term is not present iff all of its children are not present
  if (ltnp == children) {
    blok->curdoc = CURDOC_EXHAUSTED;
    (*terms_not_present)++;
  }
  else {
    blok->exhausted = FALSE;
  }
  free(term);
  return 0;
}


static int freq_comparator(const void *bloki, const void *blokj)  {
  byte *dei, *dej;
  int *ocptri, *ocptrj;
  // For sorting phrase children into increasing frequency order, with non-terminals at the end.
  dei = ((saat_control_t *)bloki)->dicent;
  dej = ((saat_control_t *)blokj)->dicent;

  // Deal with non-word children --- put them all at the end.
  if (dei == NULL && dej == NULL) return 0;
  if (dei == NULL) return 1;
  if (dej == NULL) return -1;
	
  // Deal with the words
  ocptri = (int *)(dei + (MAX_WD_LEN + 1));
  ocptrj = (int *)(dej + (MAX_WD_LEN + 1));
  return (*ocptri - *ocptrj);
}


// Rules for Phrase blocks:
//   1. A phrase is exhausted if any of its descendants are
//   2. The (curdoc, curwpos) of a phrase is the minimum of those of its descendants (only relevant if not exhausted.)


static int setup_phrase_node(FILE *out, u_char *interm, saat_control_t *blok, byte *index, byte *vocab, size_t vsz,
			     int *terms_not_present, op_count_t *op_count, double N, int debug) {
  // Return 0 on success, -ve on error
  u_char *p, *start, savep, *term;
  int children = 0, ltnp = 0, error_code = 0;  // lntp - Local terms not present
	
  blok->type = SAAT_PHRASE;
  blok->exhausted = FALSE;  // Assume the best
  blok->dicent = NULL;
  blok->children = NULL;
  term = make_a_copy_of(interm);   // It has to be a copy because other shard threads may operate on interm.  NO LONGER TRUE
  if (term == NULL) {
    if (debug) fprintf(out, "Malloc failed in setup_phrase_node\n");
    return(-220055);
  }
  if (debug >= 1) fprintf(out, "setup_phrase_node(%s)\n", term);


  // Count how many children.
  p = term + 1;  // Skip '"' 
  while (*p && *p != '"') {
    while (*p && *p == ' ') p++; // Skip leading spaces
    if (!*p) break;
    if (*p == '[') {
      children++;
      while (*p && *p != ']') p++;
      if (*p != ']') {
	// Phrase is malformed it includes unmatched brackets - let's ignore all of it.
	children = 0;
	break;
      }
      p++;
    }
    else {
      children++;
      while (*p && *p != '"' && *p != ' ') p++;
    }
  }

  if (*p != '"' || children <= 0) {
    blok->exhausted = TRUE;  
    (*terms_not_present)++;
    return -56;
  }

  blok->num_children = children;
  blok->children = (struct saat_struct *) malloc(children * sizeof(struct saat_struct));
  if (blok->children == NULL) {
    free(term);
    return(-220057);  // ------------------------------------>
  }

  // Now set up the children
  children = 0;
  ltnp = 0;
  p = term + 1;  // Skip '"' 
  while (*p && *p != '"') {
    blok->children[children].offset_within_phrase = children;
    while (*p && *p == ' ') p++; // Skip leading spaces
    if (!*p) break;
    start = p;
    if (*p == '[') {
      while (*p && *p != ']') p++;
      p++;
      savep = *p;
      *p = 0;
      setup_disjunction_node(out, start, blok->children + children, index, vocab,
			     vsz, &ltnp, op_count, N, debug);
      *p = savep;
      children++;
    }
    else {
      while (*p && *p != '"' && *p != ' ') p++;
      savep = *p;
      *p = 0;
      setup_word_node(out, start, blok->children + children, index, vocab, vsz,
		      &ltnp, op_count, N, debug);
      *p = savep;
      children++;
    }
  }   // -- End of while (*p && *p != '"')


  // Now sort the children in increasing frequency order with non-terminals at the end.
  qsort(blok->children, children, sizeof(saat_control_t), freq_comparator);


  // This term is not present if any of its children are not present
  if (ltnp) {
    if (debug >= 2) fprintf(out, "setup_phrase: At least one phrase term not present %d.\n", ltnp);
    (*terms_not_present)++;
    blok->exhausted = TRUE;
    blok->curdoc = CURDOC_EXHAUSTED;
  }
  else {
    // Have to try to position on an actual phrase.
    int c, code = 0, failed_child = 0;
    saat_control_t *first_phrase_element = blok->children;
    while (!first_phrase_element->exhausted) {
      if (debug >= 1) fprintf(out, "Phrase setup: Looking for phrase starting in doc %lld at wpos=%d\n", 
			      first_phrase_element->curdoc, first_phrase_element->curwpos);
      for (c = 1; c < blok->num_children; c++) {
	code = saat_skipto(out, blok->children + c, -1, first_phrase_element->curdoc, 
			   first_phrase_element->curwpos - first_phrase_element->offset_within_phrase 
			   + blok->children[c].offset_within_phrase, index,
			   op_count, debug, &error_code);
	if (error_code < -200000) return(error_code);  // ----------------------------------->
	if (debug >= 1) fprintf(out, "Phrase setup: Child %d advanced to (%lld, %d). Code was %d\n", 
				c, blok->children[c].curdoc, blok->children[c].curwpos, code);
	if (code != 0) {
	  if (0) printf("   - Phrase not found at that starting point.\n");
	  failed_child = c;
	  break;    // Exit inner for loop
	}
      }  // -- end of for loop;

      
      if (code <= 0) break;	// Exit while loop.  Either we've found a phrase or there are none
      else {
	// See if we can advance the first phrase element, so that it creates a phrase 
	// with the other phrase element which caused the last attempt to fail.
	if (debug >= 1) fprintf(out, "Phrase setup: Skipping first_phrase_element to doc %lld\n",
				blok->children[failed_child].curdoc);
	code = saat_skipto(out, first_phrase_element, -1, blok->children[failed_child].curdoc,
			   blok->children[failed_child].curwpos - blok->children[failed_child].offset_within_phrase 
			   + first_phrase_element->offset_within_phrase, index,
			   op_count, debug, &error_code);
	if (0) printf("Advanced first phrase element. Doesn't matter what the code was.\n");
	if (error_code < -200000) return(error_code);  // ----------------------------------->
      }
    }  // -- end of while loop;
    if (code == 0) {
      if (debug >= 1) fprintf(out, "Phrase setup: Success at (%lld, %d)\n",
			      first_phrase_element->curdoc, first_phrase_element->curwpos);
      blok->curdoc = first_phrase_element->curdoc;
      blok->curwpos = first_phrase_element->curwpos - first_phrase_element->offset_within_phrase;
    }
    else {
      (*terms_not_present)++;
      blok->exhausted = TRUE;
      blok->curdoc = CURDOC_EXHAUSTED;
    }
  }
  free(term);
  return error_code;
}


static BOOL find_and_update_prior_instance(u_char *qword, saat_control_t *blox, int n) {
  // If a saat_block of type SAAT_WORD representing a previous occurrence of this qword
  // already exists, update its repetition_count and return TRUE.  Otherwise return FALSE.
  // (n is the number of blocks already created)
  int b;
  for (b = 0; b < n; b++) {
    if (blox[b].type == SAAT_WORD && !blox[b].exhausted) {
      byte *blok_word = NULL;
      blok_word = blox[b].dicent;
      if (0) printf("Find_and_update_prior_instance(%s) checking against '%s'\n", qword, blok_word);
      if (blok_word == NULL) printf("Blast! for block %d\n", b);
      if (!strcmp((char *)blok_word, (char *)qword)) {  // Word is null-terminated in first bytes of dicent
	blox[b].repetition_count++;
	if (0) printf("   - returning TRUE\n");
	return TRUE;
      }
    }
  }
  if (0) printf("   - returning FALSE\n");
  return FALSE;
}


saat_control_t *saat_setup(query_processing_environment_t *qoenv, book_keeping_for_one_query_t *qex,
			   int *terms_not_present, int *error_code) {

  // Set up the control structures for processing of multiple
  // postings lists in malloced storage.  Return a pointer to that array
  // as the function result.
  
  // qterm_cnt counts the number of top level terms, not the total number of literals, 
  // or the total number of terms in the tree.

  // saat_setup() must be called before any queries can be run.

  // Terms_not_present is a count of the input words which don't exist in the dictionary

  // Return NULL in case of error, e.g. erroneous input parameters

  saat_control_t *blox = NULL;
  int w, tnp = 0, n;
  
  byte *index = qoenv->ixenv->index, *vocab = qoenv->ixenv->vocab;
  size_t vsz = qoenv->ixenv->vsz;
  
  *error_code = 0;
  qex->tl_saat_blocks_allocated = 0;
  
  if (qex->cg_qwd_cnt < 1 || qex->cg_qwd_cnt > MAX_WDS_IN_QUERY || index == NULL || vocab == NULL) {
    // ssaat_setup(): invalid parameters"
    *error_code = -100046;
    return NULL;
  }
  // Note that if some of the terms are disjunctions or phrases, they will 
  // cause further mallocs of their children. Eventually the allocated storage
  // will be freed by the recursive function free_querytree_memory()
  blox = (saat_control_t *)malloc(qex->cg_qwd_cnt * sizeof(saat_control_t));  // MAL0005
  if (blox == NULL) {
    *error_code = -220047;
    return NULL;
  }
  qex->tl_saat_blocks_allocated = qex->cg_qwd_cnt;
  if (0) printf("  . SAAT blocks allocated: %d\n", qex->tl_saat_blocks_allocated);

  n = 0;    // w is the number of the term within the query, n is the number of the saat block
            // As of 1.5.118-OS, they may be different due to repeated words.
  for (w = 0; w < qex->cg_qwd_cnt; w++) {
    blox[w].type = SAAT_NOT_USED;  // Make sure all blocks have a type.
    blox[w].num_children = 0;      // and don't have children unless they're given them.
    
    if (qoenv->debug >= 2)
      fprintf(qoenv->query_output, " saat_setup(): Setting up control block for '%s'\n", qex->cg_qterms[w]);

    if (qex->cg_qterms[w][0] == '[') {
      *error_code = setup_disjunction_node(qoenv->query_output, qex->cg_qterms[w], blox + n, index, vocab,
					   vsz, &tnp, qex->op_count, qoenv->N, qoenv->debug);
      n++;
    }
    else if (qex->cg_qterms[w][0] == '"') {
      *error_code = setup_phrase_node(qoenv->query_output, qex->cg_qterms[w], blox + n, index, vocab, vsz,
				      &tnp, qex->op_count, qoenv->N, qoenv->debug);
      n++;
    }
    else {
      // 1.5.118-OS Before creating the word node, check whether this word is a repetition
      // of one which has gone before.  If it is we just update the word count.
      BOOL seen_before = FALSE;
      seen_before = find_and_update_prior_instance(qex->cg_qterms[w], blox, n);
      if (!seen_before) {
	*error_code = setup_word_node(qoenv->query_output, qex->cg_qterms[w], blox + n, index, vocab, vsz,
				      &tnp, qex->op_count, qoenv->N, qoenv->debug);
	n++;
      }

    }
    if (*error_code < 0) {
      free_querytree_memory(&blox, w - 1);   // Have to avoid leaving memory allocated.
      return NULL;
    }
  }
  qex->tl_saat_blocks_used = n;
  if (0) printf("  . SAAT blocks used: %d\n", qex->tl_saat_blocks_used);

  // Modify the repetition counts in the case of relaxation. 
  if (qoenv->relaxation_level > 0) {
    for (w = 0; w < n; w++) {
      if (blox[w].type == SAAT_WORD && blox[w].repetition_count > 1) {
	blox[w].repetition_count -= qoenv->relaxation_level;
	if (blox[w].repetition_count < 1) blox[w].repetition_count = 1;
      }
    }
  }

  // Call saat_skipto() for top level words which have a repetition count > 1
  // when the first posting doesn't satisfy the repetition count.
  for (w = 0; w < n; w++) {
    if (!blox[w].exhausted && blox[w].type == SAAT_WORD && blox[w].repetition_count > 1) {
      if (blox[w].curpsting == NULL) {
	// The first one or two postings may be stored in the .vocab table
	if (qoenv->debug >= 1) printf("Postings in .vocab entry.  Occurrence count = %lld\n", blox[w].occurrence_count);
	if (blox[w].occurrence_count == 2) {
	  printf("WARNING!!!  We maybe should skipto but it's hard to tell.\n");
	} else {
	  blox[w].exhausted = TRUE;
	  blox[w].curdoc = CURDOC_EXHAUSTED;
	}
      } else if (leaf_peek_tf(blox[w].curpsting, blox[w].curdoc) < blox[w].repetition_count) {
	if (qoenv->debug >= 1) printf("Calling preliminary skipto()\n");
	saat_skipto(qoenv->query_output, blox + w, w, blox[w].curdoc + 1, DONT_CARE,
		    qoenv->ixenv->index,qex->op_count, qoenv->debug, error_code);
      }
    }
  }

  *terms_not_present = tnp;
  return blox;
}


static int leaf_peek_ahead_in_same_doc(FILE *out, saat_control_t *leaf, byte *index, int debug) {
  // Check whether the next posting for leaf is within the same document, and if so, return
  // its wordpos.  Otherwise return -1;
  // In both cases, don't actually advance in the postings list.
  byte bight, *ixptr;

  if (leaf->type != SAAT_WORD) return -1;  // -------------------------->  Not a leaf!
  // Locate the occurrence frequency for this term.  This enables us
  // to monitor list exhaustion. 

  if (leaf->posting_num >= leaf->occurrence_count) return -1;  // ----------------->

  if (0) printf("leaf_peek_ahead_in_same_doc(%lld, %d)\n", leaf->curdoc, leaf->curwpos);

  ixptr = leaf->curpsting;

  // ----- HANDLE SKIP BLOCK HERE ------
  // Just skip over it.
  if (*ixptr == SB_MARKER) {
    if (debug >= 2) fprintf(out, "saat_advance_within_doc() - skipping skipblock\n");
    ixptr += (SB_BYTES + 1);

  }

  // Next byte in .if is word position, of next.    Peek at the next byte beyond that.  If it represents a 
  // doc gap of zero, then return the word position.

  bight = *(ixptr + 1);  // Peek ahead
  if (bight == 1) {
    // The next posting is in the same doc because the docgap is zero.  We 
    // know this because the vbyte representation of a docgap of zero is 1.
    if (0) printf("leaf_peek_ahead_in_same_doc(%lld, %d) -- RETURNING wpos %d\n", leaf->curdoc, leaf->curwpos, *ixptr);

    return *ixptr;   // Word position is in a full byte now
  }
  return -1;   // It's in a different doc
}


static int disjunction_peek_ahead_in_same_doc(FILE *out, saat_control_t *dj, byte *index, int debug) {
  // Check whether the next posting for dj is within the same document, and if so, return
  // its wordpos.  Otherwise return -1;
  // In both cases, do not actually advance in the postings list.
  byte bight, *ixptr;
  int min_wpos = 999999999, c, best_c = -1;
  saat_control_t *child;
  if (dj->type != SAAT_DISJUNCTION) return -1;  // -------------------------->  Not a dj!

  if (0) printf("disjunction_peek_ahead_in_same_doc(%lld, %d)\n", dj->curdoc, dj->curwpos);


  for (c = 0; c < dj->num_children; c++) {
    child = dj->children + c;  
    ixptr = child->curpsting;
    if (child->curdoc > dj->curdoc) break;  // A component of a disjunction may be beyond the doc we're looking at.

    // ----- HANDLE SKIP BLOCK HERE ------
    // Just skip over it.
    if (*ixptr == SB_MARKER) {
      if (debug >= 2) fprintf(out, "saat_advance_within_doc() - skipping skipblock\n");
      ixptr += (SB_BYTES + 1);

    }

    // Next byte in .if is word position, of next.    Peek at the next byte beyond that.  If it represents a 
    // doc gap of zero, then return the word position.

    bight = *(ixptr + 1);  // Peek ahead
    if (bight == 1) {
      // The next posting is in the same doc because the docgap is zero.  We 
      // know this because the vbyte representation of a docgap of zero is 1.
      if (0) {
	printf("dj_peek_ahead_in_same_doc(%lld, %d) -- wpos for child %d = %d\n",
	       child->curdoc, child->curwpos, c, *ixptr);
	if (child->type == SAAT_WORD) printf("  -  child %d is '%s'\n", c, child->dicent);
      }
      if (*ixptr < min_wpos) {
	if (0) printf("Setting min_wpos to %d\n", *ixptr);
	min_wpos = *ixptr;
	best_c = c;
      }
    } else {
      if (0) printf("        Nothing further in this doc for '%s'\n", child->dicent);
    }
  }  // End of for loop over children

  if (best_c >= 0) {
    if (0) printf(" ---- ---- ---- returning %d\n", min_wpos);
    return min_wpos;  // Success ------------------------------------>
  }
  return -1;   // It's in a different doc ------------------------------------------>
}


static int phrase_peek_ahead_in_same_doc(FILE *out, saat_control_t *phrase, byte *index, int debug) {
  // *** This is necessarily more complex than the leaf version. ***
  
  // Check whether the next posting for phrase is within the same document, and if so, return
  // its wordpos.  Otherwise return -1;
  // In neither case, actually advance in the postings list.
  byte bight, anchor_bight, *ixptr, *anchor_ixptr;
  int c, anchor_wpos, wpos;
  saat_control_t *leaf, *anchor;
  BOOL try_a_new_anchor;
  
  if (phrase->type != SAAT_PHRASE) return -1;  // -------------------------->  Not a phrase

  if (0) printf("phrase_peek_ahead_in_same_doc(%lld, %d)\n", phrase->curdoc, phrase->curwpos);
  anchor = phrase->children;
  anchor_ixptr = anchor->curpsting;
  while (1) {   // Loop over all the possible anchor positions within this doc.
      // ----- HANDLE ANCHOR SKIP BLOCK HERE ------
      // Just skip over it.
      if (*anchor_ixptr == SB_MARKER) {
	if (debug >= 2) fprintf(out, "saat_advance_within_doc() - skipping skipblock\n");
	anchor_ixptr += (SB_BYTES + 1);

      }
    anchor_bight = *(anchor_ixptr + 1);  // Peek ahead
    if (anchor_bight == 1) {
      // The next posting is in the same doc because the docgap is zero.  We 
      // know this because the vbyte representation of a docgap of zero is 1.
      if (0) printf("phrase_anchor_peek_ahead_in_same_doc(%lld, %d) -- %d\n",
		    anchor->curdoc, anchor->curwpos, *anchor_ixptr);
      anchor_wpos = *anchor_ixptr;
    } else {
      if (0) printf("  Can't move anchor within current doc\n");
      return -1;  // ------------------------------------------>
    }

    // Now see if we can make a phrase around the new anchor
    for (c = 1; c < phrase->num_children; c++) {
      leaf = phrase->children + c;
      if (leaf->type != SAAT_WORD) {
	printf("Down in flames\n");
	exit(1);
      }
      ixptr = leaf->curpsting;
      try_a_new_anchor = FALSE;
      while (1) {  // Have to loop here too because this word may occur outside a phrase
	// ----- HANDLE NON-ANCHOR SKIP BLOCK HERE ------
	// Just skip over it.
	if (*ixptr == SB_MARKER) {
	  if (debug >= 2) fprintf(out, "saat_advance_within_doc() - skipping skipblock\n");
	  ixptr += (SB_BYTES + 1);

	}

	// Next byte in .if is word position, of next.    Peek at the next byte beyond that.  If it represents a 
	// doc gap of zero, then return the word position.

	bight = *(ixptr + 1);  // Peek ahead
	if (bight == 1) {
	  // The next posting is in the same doc because the docgap is zero.  We 
	  // know this because the vbyte representation of a docgap of zero is 1.
	  if (0) printf("phrase_peek_ahead_in_same_doc(%lld, %d) -- wpos %d\n",
			leaf->curdoc, leaf->curwpos, *ixptr);
	  wpos = *ixptr;
	  // Is this phrase-compatible with the anchor point
	  if ((wpos - leaf->offset_within_phrase) == (anchor_wpos - anchor->offset_within_phrase)) {
	    if (0) printf(" ... phrase-compatible\n");
	    break;  // out of inner while(1);
	  } else if((wpos - leaf->offset_within_phrase) > (anchor_wpos - anchor->offset_within_phrase)) {
	    if (0) printf(" ... within doc but phrase-incompatible\n");
	    try_a_new_anchor = TRUE;
	    break;  // out of inner while(1) and then the for;
	  } else {
	    // Still possible that this anchor might be a goer
	    // Move to the next posting and then loop
	    ixptr += 2;    // We know that the current posting is within the current doc so the vbyte has only 1 byte
	  }
	} else {
	  return -1;   // ------------------------------>
	}
      }  // end of inner while(1)  

    }

    if (try_a_new_anchor) {
      if (0) printf("   ... Trying a new anchor point\n");
      anchor_ixptr += 2;
    } else {
      // Success!
      if (0) printf("   ... Success: returning %d\n", anchor_wpos - anchor->offset_within_phrase);
      return anchor_wpos - anchor->offset_within_phrase;  // ---------------------->
    }
  }  // End of outer while (1);
    
  return -1;   // It's in a different doc  ----------------->
}


int saat_advance_within_doc(FILE *out, saat_control_t *blok, byte *index, op_count_t *op_count, int debug) {
  // Advance within the postings list controlled by blok to the next posting
  // referencing the same doc -- if there is one.
  // Return:
  //   1 - success
  //   0 - not possible.  blok left as it was.
  //  -ve - error code
  // Note: up until 28 Jan 2015, success was 0 and not possible was -1
  byte *ixptr;

  if (blok == NULL) {
    // Error in saat_advance() args
    return -48;
  }
  if (debug >= 2) fprintf(out, "Called: saat_advance_within_doc(type = %d)\n", blok->type);

  if (blok->exhausted) {
    return 0;
  }

  if (blok->type == SAAT_DISJUNCTION) {
    // ==================== NON-TERMINAL ========  DISJUNCTION ==========================
    // Foreach child
    // If not_exhausted and child->curdoc <= curdoc saat_skipto(child)
    // Set curdoc and curwpos to minimum of non-exhausted children, WITHOUT advancing the
    // other children.
    // Note that saat_setup() and saat_skipto() will have ensure that all non-exhausted
    // children are positioned either at or beyond the (doc, wpos) of the disjunction.
    int c, wpos, min_wpos = 999999999, best_c = -1;
    BOOL success = FALSE;
    saat_control_t *child;

    if (0) printf("Disjunction block is at (%lld, %d)\n", blok->curdoc, blok->curwpos);

    for (c = 0; c < blok->num_children; c++) {
      child = blok->children + c;
      if (child->curdoc > blok->curdoc) continue;   
      if (0) printf("Child block %d of type %d is at (%lld, %d)\n",
		    c, child->type, child->curdoc, child->curwpos);
      if (child->type == SAAT_WORD)
	wpos = leaf_peek_ahead_in_same_doc(out, child, index, debug);
      else  // Only words and phrases can be components of a disjunction
	wpos = phrase_peek_ahead_in_same_doc(out, child, index, debug);
      if (wpos > blok->curwpos) {
	if (wpos < min_wpos) {
	  min_wpos = wpos;
	  best_c = c;
	  success = TRUE;
	  if (0) printf("Setting min_wpos to %d and best_c to %d\n", min_wpos, best_c);
	}
      }
    }
    if (success) {
      // Need to actually advance all the children (where it's possible)  *** OK ??????*
      if (0) printf("Success: Advancing child %d to %d (parent %d)\n",
		    best_c, (blok->children + best_c)->curwpos, blok->curwpos);
      for (c = 0; c < blok->num_children; c++) {
	child = blok->children + c;
	saat_advance_within_doc(out, child, index, op_count, debug);
      }
      blok->curwpos = min_wpos;
      blok->posting_num++;
      return 1;
    } else {
      return 0;  // not possible.  blok left as it was.
    }
  }
  else if (blok->type == SAAT_PHRASE) {
    // ==================== NON-TERMINAL ========  PHRASE ===============================
    // Have to try to position on an actual phrase.
    int c, code = 0, phrase_start_wpos, target_wpos, wpos;
    saat_control_t *anchor = blok->children, *child;
    BOOL try_another_anchor;

    if (0) printf(" oo Phrase anchor is of type %d (words ahead of disjunctions). Offset = %d\n",
		  anchor->type, anchor->offset_within_phrase);
    code = saat_advance_within_doc(out, anchor, index, op_count, debug);
    while (code == 1) {   // Allow for the possible need to try multiple anchor points within this doc
      phrase_start_wpos = anchor->curwpos - anchor->offset_within_phrase;
      if (0) printf("     phrase_start_wpos = %d\n", phrase_start_wpos);
      try_another_anchor = FALSE;   // Full of optimism
      for (c = 1; c < blok->num_children; c++) {
	// Before advancing the second and subsequent terms in the phrase,
	// we need to check that this won't take us beyond the scope of the phrase
	// anchored by the current first element.  Otherwise we may eliminate the
	// possibility of finding a phrase when the anchor moves forward.
	// Say we're looking for a second occurrence of "A B" in the following
	// document:  A B B C A C B C A B (assuming A is the phrase anchor).
	// 1. Advance A to position 4
	// 2. Peek B.  OK because next B is 2 (< 4)
	// 3. Advance B to position 2 -- doesn't make a phrase, OK to advance 3 < 4
	// 4. Peek B.  Not OK because 6 > 4 + 1.
	// 5. Advance A to position 8.
	// 6. Peek B -- OK
	// 7. Advance B to position 6 - Still not a phrase
	// 8. Peek B -- OK
	// 9. Advance B to position 9 - Brilliant. We have a phrase
	child = blok->children + c;
	target_wpos = phrase_start_wpos + child->offset_within_phrase;
	while (1) { // Search for an occurrence of child which is compatible with the current phrase anchor
	  if (child->type == SAAT_WORD)
	    wpos = leaf_peek_ahead_in_same_doc(out, child, index, debug);
	  else
	    wpos = disjunction_peek_ahead_in_same_doc(out, child, index, debug);
	  if (wpos < 0) {
	    if (0) printf("     No more instances of term %d in this doc.\n", c);
	    return 0;  // There are no more occurrences of child in this doc  -------->
	  }
	  if (wpos > target_wpos) {
	    if (0) printf("     Need to try another anchor due to word %d.\n", c);
	    try_another_anchor = TRUE;   // The current anchor is not good
	    break;  // Out of inner while (1)
	  } else if (wpos == target_wpos) {
	    // Yippee, things are looking good.
	    if (0) printf("    Yippee, things are looking good for word %d.\n", c);
	    saat_advance_within_doc(out, child, index, op_count, debug);
	    break;  // out of while (1) loop
	  } else {
	    // This isn't part of a phrase starting with anchor, advance and loop agan
	    if (0) printf("    Hope lingers for word %d.\n", c);
	    saat_advance_within_doc(out, child, index, op_count, debug);
	  }
	}

	if (try_another_anchor) break;
      }  // End  of for
	    
      if (!try_another_anchor) {
	if (0) printf("    We've won!\n");
	blok->curwpos = phrase_start_wpos;
	blok->posting_num++;

	return 1; // Success! ------------------------------>
      }
      
      // Have another go at the first_element
      code = saat_advance_within_doc(out, anchor, index, op_count, debug);
    }  // -- end of outer while loop;

    if (0) printf(" --- returning %d\n", code);
    return code;
  } 
  else {
    // ==================== LEAF ========================================================
    byte bight;

    // Locate the occurrence frequency for this term.  This enables us
    // to monitor list exhaustion. 
    op_count[COUNT_DECO].count++;

    if (blok->posting_num >= blok->occurrence_count) return 0;  // ----------------->

    ixptr = blok->curpsting;

    // ----- HANDLE SKIP BLOCK HERE ------
    // Just skip over it.
    if (*ixptr == SB_MARKER) {
      if (debug >= 2) fprintf(out, "saat_advance_within_doc() - skipping skipblock\n");
      ixptr += (SB_BYTES + 1);

    }

    // Next byte in .if is word position.    Peek at the next byte beyond that.  If it represents a 
    // doc gap of zero, advance the curpsting pointer by 2 bytes, otherwise zero bytes.

    bight = *(ixptr + 1);  // Peek ahead
    if (bight == 1) {
      // The next posting is in the same doc because the docgap is zero.  We 
      // know this because the vbyte representation of a docgap of zero is 1.
      //  Move on to it
      blok->curwpos = *ixptr;   // Word position is in a full byte now
      if (debug >= 4) fprintf(out, " ........... curwpos(adv_within): %d\n", blok->curwpos);
      ixptr += 2;
      blok->curpsting = ixptr;
      blok->posting_num++;
      return 1;
    }
    return 0;
  }
}



int saat_get_tf(FILE *out, saat_control_t *blok, byte *index, op_count_t *op_count, int debug) {
  // It is assumed that saat_relaxed_and() has found a match and that blok describes the first
  // posting within the matching document.  We repeatedly call saat_advance_within_doc() to count
  // the tf for this term (which may be a phrase or disjunction).
  //
  // On return, blok points to the last posting within the matching doc and the tf value (at
  // least one) is returned.
  int tf = 1;
  if (debug) printf("saat_get_tf()\n");
  
  while (saat_advance_within_doc(out, blok, index, op_count, debug) == 1) {
    tf++;
  }
  return tf;
}




int saat_skipto(FILE *out, saat_control_t *blok, int blokno, docnum_t desired_docnum, int desired_wpos,
		byte *index, op_count_t *op_count, int debug, int *error_code) {
  // Tries to skip the postings list referenced by blok forward to a posting
  // referencing desired_docnum.  If there are more than one (different word 
  // positions within the same doc) then the first will be referenced.
  // Return
  //  +1 - desired doc,wpos not found, blok has moved to a posting past there
  //   0 - desired doc,wpos found
  //  -1 - desired dow,wpos not found, and list is exhausted.
  //
  // blokno is just for tracing and debugging purposes.  It is -1 in case of non-top-level
  
  docnum_t docgap;
  byte *ixptr, bight, last;
  BOOL explain = (debug >= 2);  // Setting explain allows for tracing of saat_skipto() operation.

  *error_code = 0;

  if (blok == NULL || desired_docnum < 0LL) {
    // Error in saat_skipto() args"
    if (0) printf("Error return.  desired docnum = %lld\n", desired_docnum);
    *error_code = -49;
    return -1;
  }

  if (debug >= 3)
    fprintf(out,
	    "  STARTING saat_skipto(%lld, %d) for TL term %d.  curdoc = %lld, curwpos = %d, blok->type = %d, posting_num=%lld\n",
	    desired_docnum, desired_wpos, blokno, blok->curdoc, blok->curwpos, blok->type, blok->posting_num);


  if (blok->exhausted) {
    if (debug >= 2) fprintf(out, "  saat_skipto - list already exhausted\n");
    return -1;
  }
  if (blok->curdoc == desired_docnum 
      && (desired_wpos == DONT_CARE || blok->curwpos == desired_wpos)) return 0;  // Already there.

  if (blok->curdoc > desired_docnum ) return 1;  // Already past it. 1.5.107  
  if (blok->curdoc == desired_docnum && blok->curwpos > desired_wpos)return 1; // Already past it. 1.5.107

  if (blok->type == SAAT_DISJUNCTION) {
    // ==================== NON-TERMINAL ========  DISJUNCTION ==========================
    // Foreach child
    //   If not_exhausted and child->curdoc <= curdoc saat_skipto(child)
    // Set curdoc and curwpos to minimum of non-exhausted children.
    int c;
    saat_control_t *child;
    blok->curdoc = LLHUGE;
    blok->curwpos = IHUGE;
    for (c = 0; c < blok->num_children; c++) {
      child = blok->children + c;
      saat_skipto(out, child, -1, desired_docnum, desired_wpos, index, op_count, debug, error_code);
      MACdisjrule2();   // See comments on macro definition in saat.h
    }
    if (blok->curdoc == LLHUGE) {
      blok->exhausted = TRUE;
      blok->curdoc = CURDOC_EXHAUSTED;
      return -1;
    }
    else if (blok->curdoc > desired_docnum) return 1;
    else return 0;
  }
  else if (blok->type == SAAT_PHRASE) {
    // ==================== NON-TERMINAL ========  PHRASE ===============================
    // Have to try to position on an actual phrase.
    int c, code = 0, failed_child = 0;
    saat_control_t *first_phrase_element = blok->children;
    code = saat_skipto(out, first_phrase_element, -1, desired_docnum, desired_wpos, index, op_count, debug, error_code);

    while (!first_phrase_element->exhausted) {
      for (c = 1; c < blok->num_children; c++) {
	if (explain) fprintf(out, "    saat_skipto(phrase) c = %d\n", c);
	code = saat_skipto(out, blok->children + c, -1, first_phrase_element->curdoc, 
			   first_phrase_element->curwpos - first_phrase_element->offset_within_phrase 
			   + blok->children[c].offset_within_phrase, index,
			   op_count, debug, error_code);
	if (code != 0) {
	  failed_child = c;
	  break;    // Exit inner for loop
	}
      }  // -- end of for loop;
      if (code <= 0) break;	// Exit while loop 
      else {
	// See if we can advance the first phrase element, so that it creates a phrase 
	// with the other phrase element which caused the last attempt to fail.
	code = saat_skipto(out, first_phrase_element, -1, blok->children[failed_child].curdoc,
			   blok->children[failed_child].curwpos - blok->children[failed_child].offset_within_phrase
			   + first_phrase_element->offset_within_phrase, index,
			   op_count, debug, error_code);
      }
    }  // -- end of while loop;
    if (code == 0) {
      blok->curdoc = first_phrase_element->curdoc;
      blok->curwpos = first_phrase_element->curwpos - first_phrase_element->offset_within_phrase;
    } else {
      blok->exhausted = TRUE;
      blok->curdoc = CURDOC_EXHAUSTED;
      return -1;
    }

    if (blok->curdoc > desired_docnum) return 1;
    else {
      if (explain) fprintf(out, "    Found a phrase.\n");
      return 0;
    }
  }
  else {
    // ==================== LEAF ========================================================

    // The occurrence frequency for this term enables us to monitor list exhaustion. 

    if (explain) {
      fprintf(out, "T%d Skipto(doc %lld) from doc %lld - %lld/%lld occurrences. ", 
	      blokno, desired_docnum, blok->curdoc, blok->posting_num, blok->occurrence_count);
      if (blok->repetition_count > 1) fprintf(out, " - DESIRED REPCOUNT %d\n", blok->repetition_count);
      else fprintf(out, "\n");
    }

    while (blok->curdoc < desired_docnum
	   || (blok->curdoc == desired_docnum && desired_wpos != DONT_CARE && blok->curwpos < desired_wpos)
	   || (blok->type == SAAT_WORD && blok->repetition_count > 1
	       && leaf_peek_tf(blok->curpsting, blok->curdoc) < blok->repetition_count)) {
      if (blok->posting_num >= blok->occurrence_count) {
	blok->exhausted = TRUE;
	blok->curdoc = CURDOC_EXHAUSTED;
	if (explain) fprintf(out, "    Exhausted\n");
	return -1;  // ------------------------------------------------------------>
      }
      ixptr = blok->curpsting;
      // ----- HANDLE SKIP BLOCK HERE ------
      // This is where we actually want to take notice of the skip block
      // saat_skipto() - if an SB_MARKER byte is encountered, then the skipblock is read.  The target docnum
      //     is compared with Lastdocnum. If less than or equal, the skipblock is skipped and decompression 
      //     of the following run proceeds as normal, as the target may lie within the run.  Otherwise:
      //	A. If Length is zero, mark this list as exhausted, otherwise:
      //        B. Set docnum from lastdocnum
      //        C. Add count to the posting count in the control block
      //        D. Increment the indexpointer to the next SB_MARKER byte and keep going.


      if (*ixptr == SB_MARKER) {
	docnum_t sb_last_docnum;
	unsigned long long *sbp, sb_count, sb_length;
	op_count[COUNT_SKIP].count++;
	sbp = (unsigned long long *)(ixptr + 1);
	sb_last_docnum = sb_get_lastdocnum(*sbp);
	if (0) fprintf(out, "  Skip block encountered while skipping to %lld. Last docnum = %lld\n", 
		       desired_docnum, sb_last_docnum);
	if (desired_docnum > sb_last_docnum) {
	  if (0) fprintf(out, "    ... skipping!\n");
	  sb_count = sb_get_count(*sbp);
	  sb_length = sb_get_length(*sbp);
	  if (sb_length == 0) {
	    // The target is not in the current run and there are no more runs
	    blok->exhausted = TRUE;
	    blok->curdoc = CURDOC_EXHAUSTED;
	    if (debug >= 3) fprintf(out, "    SAAT_SKIPTO: Exhausted (sb_length == 0 in skip block).\n");
	    return -1;  // ------------------------------------------------------------>
	  }
	  // Target is not in this run. Skip to the next skip block 
	  ixptr += sb_length;  // Want to position on another SB_MARKER
	  blok->curpsting = ixptr;
	  blok->curdoc = sb_last_docnum;
	  blok->curwpos = -1;  // I don't think this value is ever used
	  //if (0) fprintf(out, "      SKIPPING to %I64d, %d\n", blok->curdoc, blok->curwpos);

	  blok->posting_num += sb_count;
	  continue;
	}
	else {
	  // the target may be in this run, just skip the skip block and continue as per normal
	  if (0) fprintf(out, "      SEARCHING WITHIN RUN\n");
	  ixptr += (SB_BYTES + 1);
	  blok->curpsting = ixptr;
	}
      }


      op_count[COUNT_DECO].count++;
      blok->curwpos = *ixptr;  // wdnum is a full byte now
      if (debug >= 3) fprintf(out, "    Curwpos(skipto): %d\n", blok->curwpos);

      // Docgap is encoded in big-endian vbyte with LSB in each byte signalling whether this is the
      // last byte or not.
      ixptr++;
      docgap = 0;
      do {
	docgap <<= 7;
	bight = *ixptr++;
	last = bight & 1;
	bight >>= 1;
	docgap |= bight;
      } while (!last);
      blok->curdoc += docgap;
      blok->curpsting = ixptr;  // curposting now points at a wpos.
      blok->posting_num++;
    }
    if (blok->curdoc == desired_docnum
	&& (desired_wpos == DONT_CARE || blok->curwpos == desired_wpos)) {
      if (explain) fprintf(out, "    Success.  docnum = %lld\n", desired_docnum);

      
      return 0;   // ------------------------------------------------------------>
    }
    if (blok->curdoc > desired_docnum
	|| (blok->curdoc == desired_docnum  && blok->curwpos > desired_wpos)) {
      if (explain)
	fprintf(out,
		"    Overshot to document %lld.  Posting number = %lld\n",
		blok->curdoc, blok->posting_num);
      return +1;  // ------------------------------------------------------------>
    }
    if (debug >= 3) fprintf(out, "    Looping around\n");
  }
  return 1;  // ------------------------------------------------------------>
}


void free_querytree_memory(saat_control_t **plists, int blok_count) {
  int n;
  saat_control_t *blok;
  if (0) printf("free_querytree_memory(%d)\n", blok_count);
  if (plists == NULL) return;
  for (n = 0; n < blok_count; n++) {
    blok = (*plists) + n;
    if (blok != NULL && blok->num_children)
      free_querytree_memory(&(blok->children), blok->num_children); // RECURSION
  }
  free(*plists);
  *plists = NULL;
}



