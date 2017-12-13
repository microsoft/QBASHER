// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#define DEBUG 0
#define CLEAN_UP_BEFORE_EXIT 1    // Set to zero to save time

#define VOCAB_ENTRY_SIZE 16   // 4 byte unsigned postings counter plus either two 6-byte (docnum, wpos) pairs  or a 4-byte counter, plus two 5-byte pointers + a 2 byte chunk counter
							  // With MAX_WD_LEN == 15, this will result in hashtable entries which are 32 bytes long (well aligned)

typedef unsigned long long u_ll;

typedef byte *vocab_entry_p;

#define VEP_NULL 0xFFFFFFFFFF  //  A 5-byte pointer with all ones is NULL.

// Macros to get count out of a vocabulary value in the hash table.
#define ve_get_count(x) (*((u_int *)x + 1));  // count - 2nd 4 bytes due to little-endian
#define ve_get_chunk_count(x) (*((u_short *)x + 4));  //  Due to little-endian

int ve_unpack4552(byte *vep, u_int *count, u_ll *p1, u_ll *p2, u_short *us);

int ve_unpack466(byte *vep, u_int *count, u_ll *p1, u_ll *p2);


// Macros to store into a vocabulary value in the hash table
#define ve_store_count(x,c) *((u_int *)x + 1) = c;   // count - 2nd 4 bytes due to little-endian
#define ve_increment_count(x) (*((u_int *)x + 1))++;   // count - 2nd 4 bytes due to little-endian
#define ve_store_chunk_count(x,c) *((u_short *)x + 4) = c;   // due to little-endian
#define ve_increment_chunk_count(x) (*((u_short *)x + 4))++;

int ve_pack4552(byte *vep, u_int count, u_ll p1, u_ll p2, u_short us);

int ve_pack455x(byte *vep, u_int count, u_ll p1, u_ll p2);

int ve_pack466(byte *vep, u_int count, u_ll p1, u_ll p2);

#define LL_NEXT_LAST_DOCNUM_MASK 0X1FFFFFFFFF  // 37 bits (which will be shifted 19 bits left)
#define LL_NEXT_LAST_DOCNUM_SHIFT 19
#define LL_NEXT_BYTES_USED_MASK 0X7FFFF
#define MAX_PAYLOADS ((1 << LL_NEXT_LAST_DOCNUM_SHIFT) / 6)  
#define SORTABLE_POSTING_SIZE 10 // 4 byte termid, 5 byte docnum, 1 byte wordpos.

unsigned long long calculate_signature_from_first_letters(u_char *str, int bits);


#ifndef doh_t
typedef byte *doh_t;
#endif

extern doh_t ll_heap;
void allocate_hashtable_and_heap(docnum_t doccount_estimate);

void process_a_word(u_char *wd, docnum_t doccount, u_int wdpos, u_ll *max_plist_len,
	doh_t ll_heap);

extern byte *postings_accumulator_for_sort;
extern u_ll postings_accumulated, chunks_allocated;
extern double hashtable_MB, linkedlists_MB;

extern CROSS_PLATFORM_FILE_HANDLE dt_handle;
extern u_ll *doc_length_histo;
extern int MAX_WDS_INDEXED_PER_DOC;
extern double msec_elapsed_list_building, msec_elapsed_list_traversal;


// Variables settable from the command line.
extern docnum_t x_max_docs;
extern u_int SB_POSTINGS_PER_RUN, SB_TRIGGER;
extern docnum_t x_max_docs;
extern u_int min_wds, max_wds, max_line_prefix, max_line_prefix_postings, x_min_payloads_per_chunk, x_sort_postings_instead;
extern int head_terms;
extern int debug, x_hashbits, x_hashprobe, x_chunk_func, x_cpu_affinity;
extern double x_geo_tile_width;
extern int x_geo_big_tile_factor;
extern u_char *index_dir, *fname_forward, *fname_if, *fname_doctable, *fname_vocab, *fname_synthetic_docs,
*other_token_breakers, *language, *x_head_term_percentages, *x_zipf_middle_pieces, *x_synth_dl_segments,
*x_synth_dl_read_histo;
extern BOOL sort_records_by_weight, unicode_case_fold, conflate_accents, expect_cp1252, 
  x_use_large_pages, x_fileorder_use_mmap, x_minimize_io, x_2postings_in_vocab,
  x_use_vbyte_in_chunks, x_bigger_trigger, x_doc_length_histo, x_zipf_generate_terms;
extern size_t large_page_minimum;
extern u_ll tot_postings;
extern 	DWORD pfc_list_build_start, pfc_list_build_end, pfc_list_scan_start, pfc_list_scan_end;
extern double score_threshold, max_raw_score, x_synth_doc_length, x_synth_doc_length_stdev, 
x_synth_dl_gamma_shape, x_synth_dl_gamma_scale, x_zipf_alpha, 
x_zipf_tail_perc, x_synth_overgen_adjust, *head_term_cumprobs, x_synth_postings, x_synth_vocab_size;

