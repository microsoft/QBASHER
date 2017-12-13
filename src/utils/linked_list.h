// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.



typedef byte *doh_t;
typedef byte *posting_p;

//typedef struct {    // No longer used to avoid using 
//	posting_p head, tail;
//} list_head_t;

#define DOH_HEADER_ENTS 6
#define MAX_K_TABLE_ENTS 4096
extern u_ll chunk_length_table[MAX_K_TABLE_ENTS + 1]; 
extern u_int chunk_K_table[MAX_K_TABLE_ENTS + 1];
#define PAYLOAD_SIZE 6 
#define NEXT_POINTER_SIZE 7  // Increased by one byte to allow overloading 37 bits + 19 bits



// The heap means memory heap rather than min-heap or max-heap.  It's me avoiding doing a malloc for every list element.
// That was impossibly slow and wasted huge amounts of memory. Instead I define a heap to be a list of very large
// memory blocks, I allocate a large block whenever one is needed and parcel it out to the list elements in units of
// exactly the right size with no overhead.


doh_t doh_create_heap(size_t max_blocks, size_t blockents);

byte *doh_get_pointer(doh_t heap, unsigned long long compoundref);

void doh_print_usage_report(doh_t heap);

void doh_free(doh_t *heap);

void append_posting(doh_t heap, vocab_entry_p list, long long docnum, int wordnum, u_char *word);

posting_p get_next_posting(doh_t heap, posting_p ppp, int K);

void unpack_posting(doh_t heap, posting_p ppp, docnum_t *docnum, int *wdnum, posting_p *next, int K);