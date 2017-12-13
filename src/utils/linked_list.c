// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#ifdef WIN64
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../shared/utility_nodeps.h"
#include "../shared/QBASHER_common_definitions.h"
#include "../qbashi/QBASHI.h"
#include "linked_list.h"

#ifndef MEGA
#define MEGA 1048576.0
#endif

// Functions for operating on a single linked list augmented for fast appending
// by the use of a list head block which has pointers to both head and tail.
// 
// Calling malloc() once for each posting_t object seems to incure an unacceptable 
// overhead (request 24 bytes, use 96) so I've introduced Dave's Own Heap (DOH)
//
// But even 24 bytes is more than double what's required.  

// 34 bits is sufficient to represent docnums up to more than 10 billion.  We now
// use 5 bytes for that (previously 8)
//
// 8 bits is sufficient to represent word positions up to 255.  Let's use 1 byte for
// that.h
//
// 40 bits is sufficient to address a terabyte of memory.  Let's assume that that's
// enough for the largest forseeable heap size.  Choosing compact representations reduces the
// chance that we would need more memory.  Use 5 bytes to store heap (blocknum, byte_within_block)
// pairs represented as blocknum * blocksize + byte_within_block.
//
// So a linked list element consists of 11 bytes: 
//
//  docnum (5) + wordnum (1) + packed_next_element_pointer (5)
// 
// With chunking, K payloads are stored within a list element, plus one packed pointer. The size of a 
// linked list element is therefore 6 * K + 5.


#ifndef DEBUG
#define DEBUG 0
#endif


// The following tables provide a mapping between the length of a list and the number of 
// payloads (K) in the chunk containing its last element, and also the number of elements
// in the list.  
//
// chunk_length_table[i] (i > 0) contains the longest list representable by chunking parameters
// up to k. The entries in chunk_length_table[] increase monotonically.  chunk_K_table[i] contains the 
// largest value of K in a list of chunk_length_table[].

u_ll chunk_length_table[MAX_K_TABLE_ENTS + 1]; 
u_int chunk_K_table[MAX_K_TABLE_ENTS + 1];



doh_t doh_create_heap(size_t max_blocks, size_t blockbytes) {
	// A DOH consists of a header referencing up to max_blocks malloced blocks, each large enough 
	// to contain up to blockbytes bytes.
	
	size_t header_size, *szptr;
	doh_t header = NULL;
	posting_p *newone;  // posting_p is just byte *
	int b;

	if (max_blocks < 1) error_exit("Doh! Invalid no. blocks requested.\n");
	// Calculate size of header, consisting of:
	// a. max_blocks (size_t)
	// b. no. of current block. (size_t)
	// c. bytes per block (size_t)
	// d. byte offset within current block (size_t)
	// e. total number of requests to doh_allocate()
	// f. sum of all requests to doh_allocate()
	// g. array of pointers to blocks (max_blocks * sizeof(posting_t *)
	header_size = max_blocks * sizeof(byte *) + DOH_HEADER_ENTS * sizeof(size_t);

	// Allocate the memory for the header and fill it in.
	header = (doh_t)malloc(header_size);  // DOn't use lp_malloc here.  It's too small
	if (header == NULL) error_exit("Doh!  Malloc failed for header in doh_create_heap.\n");
	szptr = (size_t *)header;
	szptr[0] = max_blocks;  // The total number of blocks we can allocate
	szptr[1] = 1;  // Num blocks actually allocated
	szptr[2] = blockbytes;   // The number of bytes we can fit in the current (or any) block
	szptr[3] = 0;  // Num bytes used within the current block
	szptr[4] = 0;  // Number of requests to doh_allocate()
	szptr[5] = 0;  // Sum of all requests to doh_allocate()
	newone = (posting_p *)(szptr + DOH_HEADER_ENTS);
	for (b = 0; b < max_blocks; b++) newone[b] = NULL;   // Signal that none of the blocks are allocated.

	// Allocate the first block.
	newone[0] = (posting_p)lp_malloc(blockbytes, FALSE, 0);
	if (newone[0] == NULL) error_exit("doh_create_heap: lp_malloc failed\n");
	printf("doh_create_heap(): Header values are: %zu %zu %zu %zu\n", szptr[0], szptr[1],
		szptr[2], szptr[3]);
	printf("doh_create_heap(): max_blocks: %zu.  bytes per block: %zu \n", max_blocks, blockbytes);

	return header;
}


void doh_print_usage_report(doh_t heap) {
	size_t *header;
	double MB, MB_allocated;

	if (heap == NULL) error_exit("doh_print_usage_report: NULL");
	header = (size_t *)heap;
	// total postings is the number of entries per block times the number of full blocks plus
	// the number of entries used in the current block
	printf("\nDave's Own Heap: Chunks created: %zu\n", header[4]);
	MB = (double)(header[5]) / MEGA;
	printf("Dave's Own Heap: Total memory in chunks: %.1fMB\n", MB);
	printf("Dave's Own Heap: Average bytes per chunk: %.1f [Minimum possible: %u, Unchunked: %u]\n", 
		(double)(header[5]) / (double)(header[4]), PAYLOAD_SIZE, (PAYLOAD_SIZE + NEXT_POINTER_SIZE));
	printf("Dave's Own Heap: Average bytes per posting: %.1f\n", 
		(double)(header[5]) / (double)tot_postings);

	MB = (double)(header[2]) / MEGA;
	printf("Dave's Own Heap: Blocks contain %zu bytes.  Memory per block: %.1fMB\n",
		header[2] , MB);
	MB_allocated = (double)(header[1]) * MB;
	printf("Dave's Own Heap: Blocks allocated: %zu out of %zu.  Total memory: %.1fMB\n", 
		header[1], header[0], MB_allocated);
	printf("Dave's Own Heap: Number of allocation requests: %zu\n", header[4]);
	MB = (double)header[5] / MEGA;
	printf("Dave's Own Heap: Sum of all requests: %.1fMB. Therefore average request size: %.2f bytes.\n\n",
		MB, (double)header[5] / (double)header[4]);

}



static u_ll doh_allocate(doh_t heap, int K, size_t *bloknum, size_t *byteoffset) {
	// Allocate K * PAYLOAD_SIZE + NEXT_POINTER_SIZE bytes for a posting from within the current block
	// controlled by this DOH.  If the current one is full
	// allocate a new block.  If max_blocks are already allocated,
	// take an error exit.
	//
	// Return a composite of heapblock number and itemnumber within block.   No longer a pointer.
	size_t *header, request_size = K * PAYLOAD_SIZE + NEXT_POINTER_SIZE;
	posting_p *blocks, newitem;
	u_ll rslt;

	if (heap == NULL) error_exit("doh_allocate: NULL");
	header = (size_t *)heap;
	blocks = (posting_p *)(header + DOH_HEADER_ENTS);
	if (header[3] + request_size > header[2]) {  
		// This DOH block is too full to hold the requested chunk.  Try to allocate a new one.
		if (header[1] == header[0]) {
			// The number of blocks allocated has reached the maximum
			printf("DOH Error: Header values are: %zu %zu %zu %zu\n", header[0], header[1],
				header[2], header[3]);
			error_exit("Doh:  All space exhausted\n");
		}
		// allocate a new block
		blocks[header[1]] = (byte *)lp_malloc(header[2], FALSE, 0);
		if (blocks[header[1]] == NULL) error_exit("doh_allocate: lp_malloc failed\n");
		header[1]++;   // Increment number of blocks allocated
		header[3] = 0; // Haven't used any entries in this block yet
	}
	if (0) printf("Header values: %zu, %zu, %zu, %zu, %zu, %zu\n", header[0], header[1],
		header[2], header[3], header[4], header[5]);
	*bloknum = header[1] - 1;  // It's already been incremented
	*byteoffset = header[3];  //It hasn't yet been incremented
	header[3] += request_size;
	header[4]++;
	header[5] += request_size;

	rslt =  *bloknum * header[2] + *byteoffset;
	newitem = blocks[*bloknum] + *byteoffset;
	memset(newitem, 0xFF, request_size);
	return rslt;
}


byte *doh_get_pointer(doh_t heap, unsigned long long compoundref) {
	// Compoundref is a reference to an item within a doh block.  Turn it into a pointer
	size_t *header, bloknum, byteoffset;
	posting_p *blocks;

	if (heap == NULL) error_exit("doh_get_pointer: NULL");
	header = (size_t *)heap;
	blocks = (posting_p *)(header + DOH_HEADER_ENTS);
	bloknum = compoundref / header[2];
	byteoffset = compoundref % header[2];
	return blocks[bloknum] + byteoffset;
}

void doh_free(doh_t *heap) {
	// Free all the allocated blocks and the heap header, then NULL the heap pointer
	size_t *header;
	posting_p *newone;
	int b;
	if (heap == NULL) error_exit("doh_free: NULL");
	header = (size_t *)*heap;
	newone = (posting_p *)(header + DOH_HEADER_ENTS);
	for (b = 0; b < header[1]; b++) lp_free(newone[b], FALSE);
	free(*heap);
	*heap = NULL;
}


void append_posting(doh_t heap, vocab_entry_p list, docnum_t docnum, int wordnum, u_char *word) {
	// Append a (docnum, wordnum) element to the chunked in-memory postings list (list) for a vocab
	// term.  word is only passed for debugging purposes.
	// heap is the large memory area from which space for linked list elements.  DOH stands
	// for Dave's Own Heap doh!
	// list contains pointers to the head and tail of a singly linked list.  The tail 
	// pointer enables us to efficiently append to even the longest list.
	// K is a chunking parameter
	//
	// Note that this function is only called after we've stopped recording postings in the hashtable entry (HTE).
	// Accordingly, we can assume that the organization of the HTTE is 4,5,5,2 NOT 4,6,6
	posting_p chunk_to_useptr, payloadptr, nextptrptr = NULL, tailptr;
	size_t blocknum, byteoffset;
	int bo, i, k, K;
	u_ll head, tail, chunk_to_use, limit;
	u_int count, payload_bytes_in_this_chunk, bytes_used, bytes_needed = PAYLOAD_SIZE;
	docnum_t last_docnum, docnum_diff;
	u_short chunk_count;

	if (list == NULL) {
		printf("Error: append_posting(): list is NULL.\n");
		exit(1);
	}

	if (0) printf("%s\n", word);

	ve_unpack4552(list, &count, &head, &tail, &chunk_count);
	if (0) printf("append_posting(%s) count=%u, head = %llx, tail = %llx, chunk_count=%u\n",
		word, count, head, tail, chunk_count);
	//First we find out how big the current chunk is, or should be.
	k = 1;
	for (i = 1; chunk_length_table[i] < chunk_count; i++) {  // Now driven by chunk_count rather than count
		k++;
	}
	// After this loop chunk_length[k] >= chunk_count
	K = chunk_K_table[k];  // The size of the chunk (in payloads) containing this element of the list.
	payload_bytes_in_this_chunk = K * PAYLOAD_SIZE;


	if (count == 1) {   // count already updated by process_a_word()
		// ------------------------------------------ List was empty ---------------------------------------
		// Insert the first list element
		chunk_to_use = doh_allocate(heap, K, &blocknum, &byteoffset);
		if (0) printf("Inserting first list element: %llu\n", chunk_to_use);
		// Note we stop counting after 65,535 to avoid overflow.
		if (chunk_count < 0xFFFF) ve_store_chunk_count(list, (chunk_count + 1));
		chunk_count = ve_get_chunk_count(list);
		if (0) printf("  New chunk count is %u.  chunk_to_use is %llu\n", chunk_count, chunk_to_use);
		ve_pack455x(list, count, chunk_to_use, chunk_to_use);  // Use 455x to avoid updating the chunk_count
		docnum_diff = docnum;

		//Store just one byte of wordnum
		chunk_to_useptr = doh_get_pointer(heap, chunk_to_use);
		chunk_to_useptr[0] = wordnum & WDPOS_MASK;

		if (x_use_vbyte_in_chunks) {   // ---------------  vbyte section
			byte bight;
			// How many bytes do we need to represent the diff?  Can't be more than 6
			limit = 1ULL << 7;
			bytes_needed = 1;
			while ((unsigned long long) docnum_diff >= limit) {
				bytes_needed++;
				limit <<= 7;
			}
			if (bytes_needed + 1 > payload_bytes_in_this_chunk) {
				// It could be that 6 bytes are needed in the very worst case.
				error_exit("Can't fit vbytes in count=1 chunk\n");
			}
			// Now actually store the bytes
			for (bo = bytes_needed; bo > 0; bo--) {
				bight = docnum_diff & 0x7F;
				bight <<= 1;
				chunk_to_useptr[bo] = bight;
				docnum_diff >>= 7;
			}
			chunk_to_useptr[bytes_needed] |= 1;  // Signal last byte (For some reason we write these bytes out big-endian style)
			bytes_used = bytes_needed + 1;
		}
		else {
			// Store the 5 LS bytes of docnum
			for (bo = 1; bo < 6; bo++) {
				chunk_to_useptr[bo] = docnum_diff & 0xFF;
				docnum_diff >>= 8;
			}
			bytes_used = bytes_needed;
		}
		// Note that the NEXT field is now 6 bytes and is overloaded.  It is a next pointer
		// in all but the last chunk of each list.  In the last chunk it contains a packed last_docno (37 bits) plus 
		// bytes_used (11 bits).  This is the last chunk (so far at least)
		last_docnum = docnum;
		last_docnum &= LL_NEXT_LAST_DOCNUM_MASK;
		last_docnum <<= LL_NEXT_LAST_DOCNUM_SHIFT;
		last_docnum |= (bytes_used & LL_NEXT_BYTES_USED_MASK);


		nextptrptr = chunk_to_useptr + payload_bytes_in_this_chunk;
		for (bo = 0; bo < NEXT_POINTER_SIZE; bo++) {   // Writing bytes in little endian order
			nextptrptr[bo] = last_docnum & 0xFF;
			last_docnum >>= 8;
		}
		if (0) printf("Wrote(%llu, %u) into start of new first chunk (%llu) of size %d * %d\n", 
			docnum, wordnum, chunk_to_use, K, PAYLOAD_SIZE);
		return;    // -------------------------------------->
	}

	// ------------------ List was not empty ---------------------------------

	// We need to find out if there's room for this posting in the chunk at the end of the list.
	tailptr = doh_get_pointer(heap, tail);
	nextptrptr = tailptr + payload_bytes_in_this_chunk;
	last_docnum = 0;
	for (bo = (NEXT_POINTER_SIZE - 1); bo >= 0; bo--) {
		last_docnum <<= 8;
		last_docnum |= nextptrptr[bo];
	}

	bytes_used = last_docnum & LL_NEXT_BYTES_USED_MASK;
	last_docnum >>= LL_NEXT_LAST_DOCNUM_SHIFT;
	docnum_diff = docnum - last_docnum;
	if (x_use_vbyte_in_chunks) {
		// How many bytes do we need to represent the diff?  Can't be more than 6
		limit = 1ULL << 7;
		bytes_needed = 1;
		while ((unsigned long long) docnum_diff >= limit) {
			bytes_needed++;
			limit <<= 7;
		}
		bytes_needed++;  // Have to include the wdpos byte
	}
	else bytes_needed = PAYLOAD_SIZE;

	
	if (bytes_used + bytes_needed > payload_bytes_in_this_chunk) {
		// Won't fit.  We need to create a new one and link it in.  First we may have to update the size of K
		// Note we stop counting after 65,535 to avoid overflow.

		if (chunk_count < 0xFFFF) {
			chunk_count++;
			ve_store_chunk_count(list, chunk_count);
			k = 1;
			for (i = 1; chunk_length_table[i] < chunk_count; i++) {  // Now driven by chunk_count rather than count
				k++;
			}
			// After this loop chunk_length[k] >= chunk_count
			K = chunk_K_table[k];  // The size of the chunk (in payloads) containing this element of the list.
			payload_bytes_in_this_chunk = K * PAYLOAD_SIZE;
			if (bytes_needed > payload_bytes_in_this_chunk) 
				error_exit("Avoiding infinite chunk allocation loop.  bytes_needed is bigger than the whole chunk");
		}
		// Otherwise K stays as it was

		chunk_to_use = doh_allocate(heap, K, &blocknum, &byteoffset);
		chunk_to_useptr = doh_get_pointer(heap, chunk_to_use);
		ve_pack455x(list, count, head, chunk_to_use);  // Use 455x to avoid updating the chunk_count
		if (0) printf("Wrote(%llu, %u) into start of new chunk (%llu) of size %d * %d\n",
			docnum, wordnum, chunk_to_use, K, PAYLOAD_SIZE);

		// Now update the NEXT field of the old last chunk to point to this one.
		if (0) printf("Writing %llu as pointer to next chunk\n", chunk_to_use);
		for (bo = 0; bo < (NEXT_POINTER_SIZE - 2); bo++) {   // Only write 5 bytes cos it's actually a pointer
			nextptrptr[bo] = chunk_to_use & 0xFF;            // Writing 5 bytes in little-endian order.
			chunk_to_use >>= 8;
		}

		// Write in the first payload
		payloadptr = chunk_to_useptr;
		//Store just one byte of wordnum   *** Now write it BEFORE the docnum ***
		payloadptr[0] = wordnum & WDPOS_MASK;

		last_docnum = (docnum & LL_NEXT_LAST_DOCNUM_MASK) << LL_NEXT_LAST_DOCNUM_SHIFT;  // Before docnum gets zapped.
		if (0) {
			printf("Goteborgsposten: Writing %d bytes in new chunk: docnum=%llu, docnum_diff=%llu\n",
				bytes_needed - 1, docnum, docnum_diff);
		}

		if (x_use_vbyte_in_chunks) {
			byte bight;
			// Now actually store the bytes -- Note that bytes_needed includes wdpos
			for (bo = bytes_needed - 1; bo > 0; bo--) {
				bight = docnum_diff & 0x7F;
				bight <<= 1;
				payloadptr[bo] = bight;
				docnum_diff >>= 7;
			}
			payloadptr[bytes_needed - 1] |= 1;  // Signal last byte (For some reason we write these bytes out big-endian style)

		}
		else {
			// Store the 5 LS bytes of docnum into the slot for the new payload
			for (bo = 1; bo < 6; bo++) {
				payloadptr[bo] = docnum & 0xFF;  // Little-endian order
				docnum >>= 8;
			}
		}
		// Finally set up the NEXT field of the new chunk.
		// Note that the NEXT field is now 6 bytes and is overloaded.  It is a next pointer
		// in all but the last chunk of each list.  In the last chunk it contains a packed last_docnum (37 bits) plus 
		// bytes_used (11 bits).  This is the last chunk (so far at least)
		bytes_used = bytes_needed;
		last_docnum |= (bytes_used & LL_NEXT_BYTES_USED_MASK);  // last_docnum has already been shifted.

		nextptrptr = chunk_to_useptr + K * PAYLOAD_SIZE;  // Now operating on the newly created chunk
		for (bo = 0; bo < NEXT_POINTER_SIZE; bo++) {    // Writing 7 bytes in little-endian order
			nextptrptr[bo] = last_docnum & 0xFF;
			last_docnum >>= 8;
		}
		return;    // -------------------------------------->
	}


	// From here we're just inserting a payload into a chunk which already existed and which we know has room.

	if (0) printf("Writing middle payload (%llu, %u) in chunk.  Count is %u, chunk_count is %u, K is %u\n", 
		docnum, wordnum, count, chunk_count, K);
	if (0) printf("Wrote(%llu, %u) into old chunk (%llu) of size %d * %d at offset %u\n", 
			docnum, wordnum, tail, K, PAYLOAD_SIZE, bytes_used);

	// Now store the payload in the right spot in the chunk pointed to by lastptr 
	payloadptr = tailptr + bytes_used;
	nextptrptr = tailptr + K * PAYLOAD_SIZE;

	//Store just one byte of wordnum   *** Now write it BEFORE the docnum ***
	payloadptr[0] = wordnum & WDPOS_MASK;
	if (0) printf("Wrote payload at offset %u.  wdnum = %u\n", bytes_used, wordnum);

	last_docnum = (docnum & LL_NEXT_LAST_DOCNUM_MASK) << LL_NEXT_LAST_DOCNUM_SHIFT;  // Before docnum gets zapped.
	if (x_use_vbyte_in_chunks) {
		byte bight;
		// Now actually store the bytes -- Note that bytes_needed includes wdpos
		for (bo = bytes_needed - 1; bo > 0; bo--) {
			bight = docnum_diff & 0x7F;
			bight <<= 1;
			payloadptr[bo] = bight;
			docnum_diff >>= 7;
		}
		payloadptr[bytes_needed - 1] |= 1;  // Signal last byte (For some reason we write these bytes out big-endian style)

	}
	else {
		// Store the 5 LS bytes of docnum into the slot for the new payload
		for (bo = 1; bo < 6; bo++) {
			payloadptr[bo] = docnum & 0xFF;  // Little-endian order
			docnum >>= 8;
		}
	}
	// Have to pack last_docnum and bytes_written into NEXT
	bytes_used += bytes_needed;
	last_docnum |= (bytes_used & LL_NEXT_BYTES_USED_MASK);    // Last_docnum has already been shifted left.
	if (0) printf("Storing bytes_used = %u and last_docnum = %llu\n", bytes_used, last_docnum >> LL_NEXT_LAST_DOCNUM_SHIFT);
	for (bo = 0; bo < NEXT_POINTER_SIZE; bo++) {
		nextptrptr[bo] = last_docnum & 0xFF;  // Little-endian order
		last_docnum >>= 8;
	}

}


// ---------------------------------------------------------------------------
// Functions for unpacking an 11-byte posting
// ---------------------------------------------------------------------------

#if 0
posting_p get_next_posting(doh_t heap, byte *ppp, int K) {
	// ppp is a pointer to a possibly chunked packed posting.  The next element of the
	// posting (itself packed) is the last 5 bytes.  They need to be assembled 
	// into a long long, split into block and item references within the heap
	// and then turned into a pointer. 
	// K is the chunking parameter.
	int b;
	byte *cp = ppp + (K * PAYLOAD_SIZE);
	unsigned long long packed_pair = 0;
	size_t *header, blocknum, byteoffset;
	posting_p *pblock;

	header = (size_t *)heap;
	pblock = (posting_p *)(header + DOH_HEADER_ENTS);

	for (b = 4; b > 0; b--) {
		packed_pair <<= 8;
		packed_pair |= cp[b];
	}

	if (packed_pair == 0xFFFFFFFFFF) return NULL; // ----------->

	byteoffset = packed_pair % header[2];
	blocknum = packed_pair / header[2];
	return pblock[blocknum] + byteoffset;

}

#endif

void unpack_posting(doh_t heap, posting_p ppp, docnum_t *docnum, int *wdnum, posting_p *next, int K) {
	// ppp is a pointer to an 11-byte packed posting.  
	int b;
	u_char *cp = (u_char *)ppp;
	unsigned long long packed_pair = 0;
	docnum_t ldocnum = 0;
	size_t *header, blocknum, byteoffset;
	posting_p *pblock;

	header = (size_t *)heap;
	pblock = (posting_p *)(header + DOH_HEADER_ENTS);
		
	// Get the docnum out of five bytes
	for(b = 4; b >= 0; b--){
		ldocnum <<= 8;
		ldocnum |= cp[b];
	}

	*docnum = ldocnum;
	
	// Get the wordnum - just a single byte.
	*wdnum = cp[5];

	// Get the next pointer - same code as in get next pointer.
	for (b = 10; b > 5; b--) {
		packed_pair <<= 8;
		packed_pair |= cp[b];
	}

	if (packed_pair == 0xFFFFFFFFFF) {
		*next = NULL;
	}
	else {
		byteoffset = packed_pair % header[2];
		blocknum = packed_pair / header[2];
		*next = pblock[blocknum] + byteoffset;
	}
}
