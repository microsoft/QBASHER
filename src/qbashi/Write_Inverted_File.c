// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// All the information about this inverted file has been accumulated in a hash table.
// This module provides a function to write it out in compressed form.
//
// The inverted file consists of an alphabetically sorted vocabulary file
// QBASH.vocab and the inverted file proper QBASH.if
//
// Records in .vocab are (currently) fixed length, consisting of the 
// null terminated word, truncated at MAX_WD_LEN characters, plus a
// four-byte occurrence count, and an 
// 8-byte (could eventually shrink it to 5-byte) payload which may be
// either:
//   (A) a single posting for a word which occurs only once, in the 
//       form of a document number (max 37 bits needed for 100 billion docs)
//       plus a word position (8 bits needed to represent up to 255 word
//       positions; or
//   (B) a byte offset into the .if file where the postings list for this term
//       start.
// Cases A and B are distinguished by the ocurrence count, with a value
// of one indicating case A.
//
// Individual postings in the .if file comprise a variable number of bytes.
// Now, the first byte is used for word position with the value SB_MARKER reserved
// to introduce a skip block.  Then a variable number of bytes follows, encoding
// a number representing the difference in document numbers between this
// one and the previous one. The bytes are stored in Big-Endian fashion.  Each
// encodes a 7 bit payload with the least significant bit in each byte set to
// zero except for the last byte.

//
// For very commonly occurring words, skip blocks are used.  If the occurrence 
// count OC of a word exceeds 2 * PREFERRED_MAX_BLOCK, then OC / PREFERRED_MAX_BLOCK
// skip blocks will be used.
//

#ifdef WIN64
#include <tchar.h>
#include <strsafe.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ctype.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include <math.h>

#include "../shared/utility_nodeps.h"
#include "../shared/QBASHER_common_definitions.h"
#include "QBASHI.h"
#include "../utils/linked_list.h"
#include "../utils/dahash.h"
#include "arg_parser.h"
#include "Write_Inverted_File.h"




static int compare_keys_alphabetic(const void *i, const void*j) {
	// Alphabetically compare two null-terminated strings 
	char *ia = *(char **)i, *ja = *(char **)j;
	return strcmp(ia, ja);
}

#if 0
static int compare_sortable_postings(const void *i, const void*j) {
	u_ll *iull = (u_ll *)i, *jull = (u_ll *)j;
	u_short *ius, *jus;
	if (*iull > *jull) return 1;
	if (*iull < *jull) return -1;
	ius = (u_short *)(iull + 1);
	jus = (u_short *)(jull + 1);
	if (*ius > *jus) return 1;
	if (*ius < *jus) return -1;
	return 0;  // Should never happen
}

#endif


static byte sb_run_accumulator[SB_MAX_BYTES_PER_RUN];   // Would need to malloc this if we start multi-threading.



// The following functions are used in the experimental mode where we sort accumulated postings instead of 
// building linked lists.


#if 0
static void sort_accumulated_postings(dahash_table_t *ht, LPCSTR fname_vocab, LPCSTR fname_if, doh_t ll_heap,
	u_int SB_POSTINGS_PER_RUN, u_int SB_TRIGGER, docnum_t doccount, long long fsz) {
	int termid, prev_termid, p, e;
	byte *entryp, *vep, **permute, *if_buf = NULL;
	u_int count;
	u_ll i, *ullp, ht_off, if_off = 0, termidll, ignore, vocab_file_size, t;
	u_short *usp;
	double then;
	double permute_MB;
	docnum_t docnum;
	byte wdpos;

	vocab_file_size = (ht->entries_used * (sizeof(unsigned long long) + sizeof(int) + MAX_WD_LEN + 1));

	printf("About to sort %lld accumulated postings.  Memory used for postings: %.2fMB\n",
		postings_accumulated, (double)(postings_accumulated * SORTABLE_POSTING_SIZE) / 1000000.0);
	fflush(stdout);
	then = what_time_is_it();
	qsort(postings_accumulator_for_sort, postings_accumulated, SORTABLE_POSTING_SIZE, compare_sortable_postings);
	printf("Qsort of accumulated postings: elapsed time %.1f sec.\n", what_time_is_it() - then);
	fflush(stdout);

	// Now scan through the sorted postings and use each distinct termid to update the first pointer 
	// in the hashtable entry.
	prev_termid = -1;
	entryp = postings_accumulator_for_sort;
	then = what_time_is_it();
	for (i = 0; i < postings_accumulated; i++) {
		ullp = (u_ll *)entryp;
		termid = (u_int)(*ullp  >> 32);
		if (termid != prev_termid){
			vep = ((byte *)(ht->table)) + (termid * ht->entry_size);
			if (0) printf("     %llu/%llu - Termid: %u (%s)\n", i, postings_accumulated, termid, vep);
			count = ve_get_count(vep);
			// Store the unchanged count and the index of the first posting for this term in the postings array.
			ve_pack466(vep, count, i, 0);
			prev_termid = termid;
		}
		entryp += SORTABLE_POSTING_SIZE;
	}

	printf("Swizzling of pointers in hashtable: elapsed time %.1f sec.\n", what_time_is_it() - then);
	fflush(stdout);

	permute = (byte **)malloc(ht->entries_used * sizeof(byte *));  // MAL600
	if (permute == NULL) {
		printf("Error: malloc of permute failed in sort_accumulated_postings()\n");
		exit(1);
	}
	permute_MB = (double)(ht->entries_used * sizeof(byte *)) / MEGA;

#ifndef QBASHER_LITE
	report_memory_usage(stdout, (u_char *)"before writing the inverted file", &pfc_list_scan_start);
#endif
	then = what_time_is_it();

	p = 0;
	ht_off = 0;
	for (e = 0; e < ht->capacity; e++) {
		if (((byte *)(ht->table))[ht_off]) {
			permute[p++] = ((byte *)(ht->table)) + ht_off;
		}
		ht_off += ht->entry_size;
	}

	qsort(permute, p, sizeof(byte *), compare_keys_alphabetic);


	for (e = 0; e < p; e++) {
		char *key = (char *)(permute[e]);
		vocab_entry_p vep = ((vocab_entry_p)(((byte *)permute[e]) + ht->key_size));
		count = ve_get_count(vep);
		ve_unpack466(vep, &count, &termidll, &ignore);
		// Postings for this term start at postings_accumulator_for_sort[termidll]
		entryp = postings_accumulator_for_sort + termidll * SORTABLE_POSTING_SIZE;
		for (t = 0; t < count; t++) {
			ullp = (u_ll *)entryp;
			usp = (u_short *)(ullp + 1);
			docnum = (*ullp & 0xFFFFFFFFULL << 8) | (*usp >> 8);  // Is this right?????????? should the FFF mask be 64 bit?  Is the order of ops right?
			wdpos = *usp & 0xFF;


			//
			//
			//**** Have to insert the code to write the .iF here 
			//
			//

			entryp += SORTABLE_POSTING_SIZE;
		}

	}

	printf("Writing the .vocab and .if  (except we didn't!) elapsed time: %.1f sec.\n", what_time_is_it() - then);
	fflush(stdout);
}
#endif


static byte vocabfile_record[VOCABFILE_REC_LEN + 10], arg_list[IF_HEADER_LEN - 250];

double write_inverted_file(dahash_table_t *ht, u_char *fname_vocab, u_char *fname_if, doh_t ll_heap,
			   u_int SB_POSTINGS_PER_RUN, u_int SB_TRIGGER, docnum_t doccount, long long fsz, u_ll max_plist_len) {
  // Sort the keys stored in ht into alphabetic order, then write the .vocab an
  // .if files
  // 
  // doccount and fsz are passed in only to enable file lengths to be written into the .if header
  // Return size of .if and .vocab files in MB (as a double)

  int b, e, p, interval = 1000, error_code = 0;
  byte **permute, *vocab_buf = NULL, *if_buf = NULL, qidf = 1;
  posting_p headptr = NULL, currptr = NULL, nextptr = NULL, tailptr = NULL;    // posting_p is just byte *
  size_t entry_size, vocab_buf_used = 0, if_buf_used = 0;
  u_ll ht_off = 0, if_off = 0, list_elts = 0, histo[7] = { 0 }, vocab_file_size,
								  postings_lists_with_skip_blocks = 0, skip_blocks_written = 0, tot_skip_blocks_written = 0,
								  max_sb_runs_per_list = 0;
  CROSS_PLATFORM_FILE_HANDLE vocab_handle, if_handle;
  double invfile_MB, permute_MB;
  u_char *if_header = NULL;
  u_short chunk_count = 0;
  u_int count, current_sb_postings_per_run, chunkno = 1;  // I assume we start from one.
  u_ll head, tail;
  size_t *header, blocknum, byteoffset, bytes_used_in_header;
  posting_p *pblock;
  BOOL verbose = (debug >= 2);
#ifdef WIN64
  vocab_handle = NULL;
  if_handle = NULL;
#else
  vocab_handle = -1;
  if_handle = -1;
#endif

  header = (size_t *)ll_heap;
  pblock = (posting_p *)(header + DOH_HEADER_ENTS);

  if (verbose) printf("write_inverted_file()\n");


  if (ht == NULL) {
    printf("Error: write_inverted_file(): attempt to dump NULL table.\n");
    exit(1);
  }

  vocab_file_size = ht->entries_used * VOCABFILE_REC_LEN;
  if (0) printf("CANBERRA: vfs = %zu * %d = %lld\n",
		ht->entries_used, VOCABFILE_REC_LEN, vocab_file_size);

  entry_size = ht->key_size + ht->val_size;
  permute = (byte **)malloc(ht->entries_used * sizeof(byte *));  // MAL600
  permute_MB = (double)(ht->entries_used * sizeof(byte *)) / MEGA;


#ifdef WIN64
  report_memory_usage(stdout, (u_char *)"before writing the inverted file", &pfc_list_scan_start);
#endif

  // permute is freed at the end of this function
  p = 0;
  for (e = 0; e < ht->capacity; e++) {
    if (((byte *)(ht->table))[ht_off]) {
      permute[p++] = ((byte *)(ht->table)) + ht_off;
    }
    ht_off += ht->entry_size;
  }

  qsort(permute, p, sizeof(byte *), compare_keys_alphabetic);

  printf("QSORT of vocabulary permuter complete.\n");

  if (!x_minimize_io) {
    vocab_handle = open_w((char *)fname_vocab, &error_code);
    fflush(stdout);
    if (error_code) {
      error_exit("Unable to open .vocab file for writing.");
    }

    if_handle = open_w((char *)fname_if, &error_code);
    if (error_code) {
      error_exit("Unable to open .if file for writing.");
    }

    // Write a version header into the .if file.
    if_header = (u_char *)malloc(IF_HEADER_LEN);    // MAL601
    if (if_header == NULL) {
      error_exit("Error: malloc failed for if_header");
    }
    memset((void *)if_header, 0, IF_HEADER_LEN);

    store_arg_values(arg_list, IF_HEADER_LEN - 250, TRUE);
    if (0) puts((char *)arg_list);
    // Don't change the sprintf lightly, because what it writes is read and checked by QBASHQ.exe
    sprintf((char *)if_header, "Index_format: %s\nQBASHER version:%s%s\nQuery_meta_chars: %s\nOther_token_breakers: %s\n"
	    "Size of .forward: %lld\nSize of .dt: %lld\nSize of .vocab: %llu\nTotal postings: %llu\nNumber of documents: %lld\n"
	    "Vocabulary size: %llu\n%s",
	    INDEX_FORMAT, INDEX_FORMAT, QBASHER_VERSION, QBASH_META_CHARS, other_token_breakers,
	    fsz, doccount * DTE_LENGTH, vocab_file_size, tot_postings, doccount, vocab_file_size / VOCABFILE_REC_LEN,
	    arg_list);
		
    bytes_used_in_header = strlen((char *)if_header);
    printf("Bytes written in header %zu/%d\n", bytes_used_in_header, IF_HEADER_LEN);
    if (!x_minimize_io) buffered_write(if_handle, &if_buf, HUGEBUFSIZE, &if_buf_used, if_header, IF_HEADER_LEN, "IF header");
    if_off += IF_HEADER_LEN;

    free(if_header);   //FRE601
  }   // End of if (!x_minimize_io)

  // Report memory use prior to writing .vocab and .if so we can see what might be 
  // causing slowness at that stage.
  printf("write_inverted_file: permute array occupies %.1f MB\n", permute_MB);
  printf("write_inverted_file: hash table occupies: %.1fMB\n", (double)ht->capacity * (double)entry_size / MEGA);

  doh_print_usage_report(ll_heap);
  fflush(stdout);


  printf("Note: The working set size reported in the immediately previous memory usage summary\n"
	 "should only be a small amount larger than the sum of the memory used by the permute array,\n"
	 "the hash table and Dave's own heap.  If not, look for a file still memory mapped. If the\n"
	 "working set size is more than say 90%% of the physical RAM available, the final phase of\n"
	 "is likely to be very slow because access patterns are random -- apart from the moderating\n"
	 "effect of chunking.\n\n");

  fflush(stdout);

#if 0
  if (x_sort_postings_instead > 0) {
    sort_accumulated_postings(ht, (LPCSTR)fname_vocab, (LPCSTR)fname_if, ll_heap,
			      SB_POSTINGS_PER_RUN, SB_TRIGGER, doccount, fsz);
  }
  else
#endif
    {
      printf("Starting to write out postings and vocab table entries....\n");
      for (e = 0; e < p; e++) {
	char *key = (char *)(permute[e]);
	vocab_entry_p vep = ((vocab_entry_p)(((byte *)permute[e]) + ht->key_size));
	// The following declarations are to support chunking
	u_int current_k = 1, K = chunk_K_table[current_k];
	u_ll count_limit_for_current_k = chunk_length_table[current_k];
	byte *payloadptr, *nextptrptr;
	u_ll next;

	// The vocab entry may be packed either 4,6,6 or 4,5,5,2
	count = ve_get_count(vep);
	if (x_2postings_in_vocab && count < 3)
	  ve_unpack466(vep, &count, &head, &tail);     // Head and tail should be 5-byte (or 6-byte) compound references within the DOH
	else
	  ve_unpack4552(vep, &count, &head, &tail, &chunk_count);


	if (verbose) printf("   - %s %u, %lld, %lld\n", key, count, head, tail);
	headptr = doh_get_pointer(ll_heap, head);
	tailptr = doh_get_pointer(ll_heap, tail);

	list_elts = 0;

	if (count <= 1) {
	  // There's only one posting, write docnum and wdnum into .vocab entry
	  unsigned long long towrite;
	  docnum_t docnum;
	  int wdnum, b;
	  if (verbose) printf("Single\n");
	  if (x_2postings_in_vocab) {
	    //  ---- The posting is actually in the .vocab entry.  head is not actually a list pointer
	    docnum = head >> WDPOS_BITS;
	    wdnum = head & WDPOS_MASK;
	    if (verbose) printf("Extracted single posting (%llu, %u) from the vocab entry for %s.\n", docnum, wdnum, key);
	  }
	  else {  // ---- The posting is in a list chunk.  It may be vbyte compressed
	    // Get the wordnum - just a single byte.
	    wdnum = headptr[0];
	    docnum = 0;
	    if (x_use_vbyte_in_chunks) {
	      byte bight;
	      b = 1;
	      do {
		bight = headptr[b] >> 1;  // Continuation bit is LSB
		docnum <<= 7;
		docnum |= bight;
		b++;
	      } while (!(headptr[b - 1] & 1));
	    }
	    else {
	      for (b = 5; b > 0; b--){   // bytes were written little-endian
		docnum <<= 8;
		docnum |= headptr[b];
	      }
	    }
	    if (verbose) printf("Extracted single posting (%llu, %u) from the only list chunk for %s.\n", docnum, wdnum, key);
	  }

	  towrite = (docnum << WDPOS_BITS) | (wdnum & WDPOS_MASK);
	  qidf = (byte)quantized_idf(max_plist_len * 1.5, count, 0XFF);    // The constant makes the QIDF of the most common term come out to be 1
	  if (0) printf("  -- count = %u,  idf = %.4f,  qidf = %u\n", count, log(max_plist_len * 1.004008 / (double)count), qidf);
	  vocabfile_entry_packer(vocabfile_record, MAX_WD_LEN + 1, (byte *)key, count, qidf, towrite);
	  if (!x_minimize_io) {
	    buffered_write(vocab_handle, &vocab_buf, HUGEBUFSIZE, &vocab_buf_used, vocabfile_record,
			   VOCABFILE_REC_LEN, "vocab single posting");
	  }


	  // In this case there's nothing to be written to .if
	  histo[0]++;
	  if (verbose) printf("Single. done\n");
	}
	else {
	  // There are multiple postings.  
	  docnum_t last_docnum = 0, docnum_diff = 0, docnum;
	  u_int payload_bytes_available = K * PAYLOAD_SIZE;


	  // Write the .if offset into .vocab
	  if (verbose) printf("Multiple\n");
	  qidf = (byte)quantized_idf(max_plist_len * 1.05, count, 0XFF);    // The constant makes the QIDF of the most common term come out to be 1
	  if (0) printf("  -- count = %u,  idf = %.4f,  qidf = %u\n", count, log(max_plist_len * 1.05 / (double)count), qidf);
	  vocabfile_entry_packer(vocabfile_record, MAX_WD_LEN + 1, (byte *)key, count, qidf, if_off);
	  if (!x_minimize_io) {
	    buffered_write(vocab_handle, &vocab_buf, HUGEBUFSIZE, &vocab_buf_used, vocabfile_record,
			   VOCABFILE_REC_LEN, "vocab if offset");
	  }
	  // Then write the postings list entries into .if and update if_off
	  currptr = headptr;
	  //printf(" Multiple: e=%d\n", e);

	  if (SB_TRIGGER > 0 && count >= SB_TRIGGER) {  // No skip blocks unless SB_TRIGGER is non-zero
	    // ---------------------------- We're writing skip blocks for this inverted file.  -----------
	    u_int sb_postings_accumulated = 0, sb_bytes_accumulated = SB_BYTES + 1;  // Allow for SB_MARKER and SKIP BLOCK
	    byte bight;
	    u_ll *ullp, limit;
	    int wdnum, bytes_needed;

	    if (SB_POSTINGS_PER_RUN == 0) {
	      // Dynamic setting of run lengths
	      current_sb_postings_per_run = (u_int)round(sqrt((double)count));
	      // Since the number of postings per run is limited to SB_MAX_COUNT (4096 at present)
	      // we need to limit the run lengths for terms with more than 16 million postings.
	      if (current_sb_postings_per_run > SB_MAX_COUNT) current_sb_postings_per_run = SB_MAX_COUNT;
	    }
	    else current_sb_postings_per_run = SB_POSTINGS_PER_RUN;  // Static setting


	    if (verbose) printf("Skip blocks.  Count = %u; Current run length = %u\n", count, current_sb_postings_per_run);
	    skip_blocks_written = 0;
	    postings_lists_with_skip_blocks++;

	    list_elts = 0;   // How many postings have been written so far.  (compare against count, the nummber to be written)
	    chunkno = 1;
	    currptr = headptr;
	    if (0) printf("Head of list (with skipping) for '%s' is %llu\n", key, head);
	    while (currptr != NULL) {  // Zooming through the linked list of postings for this term
	      if (chunkno > count_limit_for_current_k) {
		current_k++;
		count_limit_for_current_k = chunk_length_table[current_k];
		K = chunk_K_table[current_k];
		payload_bytes_available = K * PAYLOAD_SIZE;
		if (chunkno > count_limit_for_current_k) {
		  error_exit("Chunking stuffed!\n");
		}
	      }
	      if (0) printf("'%s' looping through chunks.  chunkno=%u/chunk_count=%u list_elts=%llu, count=%u, K = %u\n", key, chunkno, chunk_count, list_elts, count, K);
	      // Get all the postings out of the current chunk.
	      payloadptr = currptr;
	      while (payloadptr < (currptr + payload_bytes_available)) {
		if (list_elts >= count) break;
		// Get the wordnum - just a single byte.
		wdnum = payloadptr[0];
		if (wdnum == 0xFF)  {
		  if (0) printf("Breaking out because of wdnum == 0xFF\n");
		  break;  // --------------------------------------->
		}
		list_elts++;

		if (x_use_vbyte_in_chunks) {
		  // Get the vbyte-encoded docnum_diff
		  byte bight;
		  b = 1;
		  docnum_diff = 0;
		  do {
		    bight = payloadptr[b] >> 1;  // Continuation bit is LSB
		    docnum_diff <<= 7;
		    docnum_diff |= bight;
		    b++;
		  } while (!(payloadptr[b - 1] & 1));
		  docnum = last_docnum + docnum_diff;
		  payloadptr += b;
		}
		else {
		  // Get the docnum out of five bytes
		  docnum = 0;
		  for (b = 5; b > 0; b--){
		    docnum <<= 8;
		    docnum |= payloadptr[b];
		  }
		  payloadptr += PAYLOAD_SIZE;
		}
		if (0) printf("Extracted a posting (%llu, %u) from list chunk %d for %s.\n",
			      docnum, wdnum, chunkno, key);

		if (docnum > x_max_docs) {
		  printf("Error in postings for '%s': x-th payload out of chunk of %d. wdnum=%u, docnum = %llu\n",
			 key, K, wdnum, docnum);
		  printf("  -- list_elts=%llu, count = %u\n", list_elts, count);
		  error_exit("Error: Erroneous docnum encountered while writing inverted file.\n");
		}

		// NOTE:  Here we're writing vbytes, no longer reading them.
		docnum_diff = docnum - last_docnum;
		last_docnum = docnum;
		// How many bytes do we need to represent the diff?  Can't be more than 6
		limit = 1ULL << 7;
		bytes_needed = 1;
		while ((unsigned long long) docnum_diff >= limit) {
		  bytes_needed++;
		  limit <<= 7;
		}

		//printf(" Docnumdiff = %lld, bytes_needed = %d ", docnum_diff, bytes_needed);
		// Write the first byte
		bight = (byte)(wdnum);
		if (bight == 255) {
		  // We've come to the end of the valid postings in this chunk.
		  // Shouldn't ever happen
		  error_exit("Error:  invalid wdpos (AXE)\n"); // -------------------------------------------------------------------------------------------------->
		}
		sb_run_accumulator[sb_bytes_accumulated++] = bight;
		// Need a loop and an array to be able to write the bytes
		// in order of decreasing significance.
		for (b = bytes_needed - 1; b >= 0; b--) {
		  bight = docnum_diff & 0x7F;
		  bight <<= 1;
		  sb_run_accumulator[sb_bytes_accumulated + b] = bight;
		  docnum_diff >>= 7;
		}
		sb_run_accumulator[sb_bytes_accumulated + bytes_needed - 1] |= 1;  // Signal last byte
		sb_bytes_accumulated += bytes_needed;
		histo[(bytes_needed + 1)]++;
		sb_postings_accumulated++;
		if (sb_postings_accumulated >= current_sb_postings_per_run) {
		  // Need to output SB_MARKER, skipblock and run.
		  sb_run_accumulator[0] = SB_MARKER;
		  ullp = (unsigned long long *) (sb_run_accumulator + 1);
		  if (list_elts >= count) {
		    // If this run happens to end at the end of the list, write a length of zero.
		    *ullp = sb_assemble(docnum, (u_ll)sb_postings_accumulated, 0ULL);
		  }
		  else {
		    *ullp = sb_assemble(docnum, (u_ll)sb_postings_accumulated, (u_ll)sb_bytes_accumulated);
		  }
		  if (!x_minimize_io) buffered_write(if_handle, &if_buf, HUGEBUFSIZE, &if_buf_used, sb_run_accumulator, sb_bytes_accumulated, "SB full run");
		  if_off += sb_bytes_accumulated;
		  skip_blocks_written++;
		  tot_skip_blocks_written++;
		  sb_postings_accumulated = 0;
		  sb_bytes_accumulated = SB_BYTES + 1;
		}
	      } // End of looping through payloads in a chunk.

	      if (0) printf(" -- Loop over chunk %u finished. K = %u\n", chunkno, K);
	      if (chunkno < 0xFFFF) chunkno++;
	      // Get the next pointer --- Note that the NEXT field is now 6 bytes and is overloaded.  It is a next pointer
	      // in all but the last chunk.  In the last chunk it contains a packed last_docno (37 bits) plus 
	      // bytes_used (11 bits).

	      if (currptr == tailptr) {
		// This is how we detect the end of list now.
		if (0) printf("Finished last chunk\n");
		break;  //-------------------------------------------------------------------->  Drop out of loop.
	      }

	      // We know that NEXT actually is a pointer on this occasion.
	      nextptrptr = currptr + payload_bytes_available;   // Have to calculate because chunk may have empties
	      next = 0;
	      for (b = (NEXT_POINTER_SIZE - 3); b >= 0; b--) {
		next <<= 8;
		next |= nextptrptr[b];
	      }
	      // We know that this is not the last chunk, so NEXT is actually a pointer
	      byteoffset = next % header[2];
	      blocknum = next / header[2];
	      nextptr = pblock[blocknum] + byteoffset;
	      currptr = nextptr;
	      if (0) printf("Moving on to next chunk or next term.  Next = %llu\n", next);
	    }  // End of zooming through the linked list of postings for this term

	    // May need to write a partial run
	    if (sb_postings_accumulated) {
	      // Need to output SB_MARKER, skipblock and run.
	      sb_run_accumulator[0] = SB_MARKER;
	      ullp = (u_ll *)(sb_run_accumulator + 1);
	      *ullp = sb_assemble(docnum, (u_ll)sb_postings_accumulated, 0ULL);  // Zero because this is the last one.
	      if (!x_minimize_io) buffered_write(if_handle, &if_buf, HUGEBUFSIZE, &if_buf_used, sb_run_accumulator, sb_bytes_accumulated, "SB part run");
	      if_off += sb_bytes_accumulated;
	      skip_blocks_written++;
	      tot_skip_blocks_written++;
	      sb_postings_accumulated = 0;
	      sb_bytes_accumulated = SB_BYTES + 1;
	    }


	    if (skip_blocks_written > max_sb_runs_per_list) max_sb_runs_per_list = skip_blocks_written;
	    // ---------------------------- We've written skip blocks for this inverted file.  -----------
	  }
	  else {
	    // This is the old code path, used if we're not doing skip blocks
	    byte bight, bytes[5];
	    docnum_t docnum, limit;
	    int wdnum, bytes_needed;

	    if (verbose) printf("Old code path\n");

	    if (x_2postings_in_vocab  && count < 3) {
	      // Note that the single posting case (count == 1) is handled specially at the top of this function.
	      u_int c, p;
	      u_ll dnwp[2];
	      if (verbose) printf("Extracting %d postings for %s directly from the hash table entry\n", count, key);
	      // Unpack the one or two postings into the dnwp array
	      ve_unpack466(vep, &c, dnwp, dnwp + 1);
	      last_docnum = 0;
	      for (p = 0; p < count; p++) {
		docnum = dnwp[p] >> WDPOS_BITS;
		wdnum = dnwp[p] & WDPOS_MASK;

		// Write a byte with the wordnum
		bight = (byte)wdnum;
		if (!x_minimize_io) buffered_write(if_handle, &if_buf, HUGEBUFSIZE, &if_buf_used, &bight, 1, "if wdnum");
		// Now vbyte encode the docnum_diff
		docnum_diff = docnum - last_docnum;
		last_docnum = docnum;

		// How many bytes do we need to represent the diff?  Can't be more than 6 (allowing up to 100 billion)
		limit = 1ULL << 7;
		bytes_needed = 1;
		while (docnum_diff >= limit) {
		  bytes_needed++;
		  limit <<= 7;
		}
		if (verbose || debug >= 4) printf(" Word '%s': wdnum = %d, docnum = %lld docnumdiff = %lld, bytes_needed = %d\n",
						  key, bight, docnum, docnum_diff, bytes_needed);
		// Need a loop and an array to be able to write the bytes
		// in order of decreasing significance.
		for (b = bytes_needed - 1; b >= 0; b--) {
		  bight = docnum_diff & 0x7F;
		  bight <<= 1;
		  bytes[b] = bight;
		  docnum_diff >>= 7;
		  if (debug >= 4) printf(", (%d, %X)", b, bytes[b]);
		}
		bytes[bytes_needed - 1] |= 1;   // Set the termination bit on the last byte
		if (debug >= 4) printf("\n");
		if (!x_minimize_io) buffered_write(if_handle, &if_buf, HUGEBUFSIZE, &if_buf_used, bytes, bytes_needed, "rest of multiple bytes");
		if_off += (bytes_needed + 1);
		histo[bytes_needed + 1]++;
	      }
	    }
	    else {
	      // No skip blocks, following the chunked linked list.
	      u_int payload_bytes_available = K * PAYLOAD_SIZE;
	      chunkno = 1;
	      currptr = headptr;
	      if (0) printf("Head of list for '%s' is %llu\n", key, head);
	      while (currptr != NULL) {
		if (chunkno > count_limit_for_current_k) {
		  current_k++;
		  count_limit_for_current_k = chunk_length_table[current_k];
		  K = chunk_K_table[current_k];
		  payload_bytes_available = K * PAYLOAD_SIZE;
		  if (chunkno > count_limit_for_current_k) {
		    error_exit("Chunking stuffed!\n");
		  }
		}
		if (0)
		  printf("'%s' looping through chunks.  chunk_count=%u list_elts=%llu, count=%u, K = %u\n", key, chunk_count, list_elts, count, K);

		// Get all postings out of the current chunk.
		payloadptr = currptr;
		while (payloadptr < (currptr + payload_bytes_available)) {
		  if (list_elts >= count) {
		    if (0) printf("All needed elements found for %s\n", key);
		    break;  // The whole list is finished.
		  }
		  // Get the wordnum - just a single byte.
		  wdnum = payloadptr[0];
		  if (wdnum == 0xFF)  {
		    if (0) printf("Breaking out because of wdnum == 0xFF\n");
		    break;  // --------------------------------------->
		  }
		  list_elts++;

		  if (x_use_vbyte_in_chunks) {
		    // Get the vbyte-encoded docnum_diff
		    byte bight;
		    b = 1;
		    docnum_diff = 0;
		    do {
		      bight = payloadptr[b] >> 1;  // Continuation bit is LSB
		      docnum_diff <<= 7;
		      docnum_diff |= bight;
		      b++;
		    } while (!(payloadptr[b - 1] & 1));
		    docnum = last_docnum + docnum_diff;
		    payloadptr += b;
		  }
		  else {
		    // Get the docnum out of five bytes
		    docnum = 0;
		    for (b = 5; b > 0; b--){
		      docnum <<= 8;
		      docnum |= payloadptr[b];
		    }
		    payloadptr += PAYLOAD_SIZE;
		  }
		  if (0) printf("Extracted a posting (%llu, %u) from list chunk %d for %s.\n",
				docnum, wdnum, chunkno, key);
		  if (docnum > x_max_docs) {
		    printf("Error in postings for '%s': x-th payload out of chunk of %d. wdnum=%u, docnum = %llu\n",
			   key, K, wdnum, docnum);
		    printf("  -- list_elts=%llu, count = %u\n", list_elts, count);
		    error_exit("Error: Erroneous docnum encountered while writing inverted file.\n");
		  }

		  // Write a byte with the wordnum
		  bight = (byte)wdnum;
		  if (!x_minimize_io) buffered_write(if_handle, &if_buf, HUGEBUFSIZE, &if_buf_used, &bight, 1, "if wdnum");
		  // Now vbyte encode the docnum_diff
		  docnum_diff = docnum - last_docnum;
		  last_docnum = docnum;

		  // How many bytes do we need to represent the diff?  Can't be more than 5
		  limit = 1ULL << 7;
		  bytes_needed = 1;
		  while (docnum_diff >= limit) {
		    bytes_needed++;
		    limit <<= 7;
		  }
		  if (0) printf(" Word '%s': wdnum = %d, docnum = %lld docnumdiff = %lld, bytes_needed = %d\n",
				key, bight, docnum, docnum_diff, bytes_needed);
		  // Need a loop and an array to be able to write the bytes
		  // in order of decreasing significance.
		  for (b = bytes_needed - 1; b >= 0; b--) {
		    bight = docnum_diff & 0x7F;
		    bight <<= 1;
		    bytes[b] = bight;
		    docnum_diff >>= 7;
		    if (debug >= 4) printf(", (%d, %X)", b, bytes[b]);
		  }
		  bytes[bytes_needed - 1] |= 1;   // Set the termination bit on the last byte
		  if (debug >= 4) printf("\n");
		  if (!x_minimize_io) buffered_write(if_handle, &if_buf, HUGEBUFSIZE, &if_buf_used, bytes, bytes_needed, "rest of multiple bytes");
		  if_off += (bytes_needed + 1);
		  histo[bytes_needed + 1]++;
		}   // End of loop over payloads in a chunk

		if (0) printf(" -- Loop over chunk %u finished. K = %u\n", chunkno, K);
		if (chunkno < 0xFFFF) chunkno++;

		// Get the next pointer --- Note that the NEXT field is now 6 bytes and is overloaded.  It is a next pointer
		// in all but the last chunk. 

		if (currptr == tailptr) {
		  // This is how we detect the last chunk
		  if (0) printf("Finished last chunk\n");
		  break;  //-------------------------------------------------------------------->  Drop out of loop.
		}

		// We know that this is not the last chunk, so NEXT is actually a pointer
		nextptrptr = currptr + K * PAYLOAD_SIZE;   // Have to calculate because chunk may have empties
		next = 0;
		for (b = (NEXT_POINTER_SIZE - 3); b >= 0; b--) {
		  next <<= 8;
		  next |= nextptrptr[b];
		}
		byteoffset = next % header[2];
		blocknum = next / header[2];
		nextptr = pblock[blocknum] + byteoffset;

		currptr = nextptr;

		if (0) printf("Moving on to next chunk or next term.  Next = %llu\n", next);
	      }
	    }
	  }
	}

	if (e && e % interval == 0) {
	  printf("%d - %s (%u)\n", e, key, count);
	  fflush(stdout);
	  if (e % (10 * interval) == 0)  interval *= 10;
	}
      }
    }

  // Write the length of the file into the last 8 bytes so we may be able to  tell if it's truncated
  if_off += sizeof(if_off);
  if (!x_minimize_io) buffered_write(if_handle, &if_buf, HUGEBUFSIZE, &if_buf_used, (byte *)&if_off, sizeof(if_off), ".if file length");

  printf("\nDistribution of postings sizes\n==============================\n");
  printf("  0 bytes: %lld (single posting kept in vocab file)\n", histo[0]);
  for (b = 1; b < 7; b++) {
    printf("  %d bytes: %lld\n", b, histo[b]);
  }
  printf("==============================\n\n");

  printf("Skip block statistics\n====================\n");
  printf("Postings lists with skip blocks: %lld\n", postings_lists_with_skip_blocks);
  printf("Total skip blocks written: %lld\n", tot_skip_blocks_written);
  printf("Maximum skip blocks per list: %lld\n", max_sb_runs_per_list);
  printf("=====================\n\n");

  printf("\nSignificant memory users\n==============================\n");
  hashtable_MB = (double)ht->capacity * (double)entry_size / MEGA;
  linkedlists_MB = (double)header[1] * (double)header[2] / MEGA;
  chunks_allocated = header[4];
  printf("Hash table: %.1fMB\n", hashtable_MB);
  printf("Linked lists: %.1fMB (Total size of the %lld DOH blocks allocated)\n", linkedlists_MB, chunks_allocated);
  printf("Permute Array: %.1fMB\n", (double)ht->entries_used * (double)sizeof(byte *) / MEGA);
  printf("==============================\n\n");

  printf("\nIndex files needed for query processing\n=======================================\n");
  printf("QBASH.vocab file:    %8.1fMB\n", (double)vocab_file_size / MEGA);
  printf("QBASH.if file:       %8.1fMB\n", (double)if_off / MEGA);
  // This output block will be completed by the main program.

  // Clean up
  free(permute);    // FRE600
  if (!x_minimize_io) {
    if (vocab_buf_used > 0) buffered_flush(vocab_handle, &vocab_buf, &vocab_buf_used, ".vocab", TRUE);
    if (if_buf_used > 0) buffered_flush(if_handle, &if_buf, &if_buf_used, ".if", TRUE);
  }

  invfile_MB = ((double)vocab_file_size + (double)if_off) / MEGA;
  return invfile_MB;
}
