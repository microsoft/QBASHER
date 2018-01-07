// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#ifdef WIN64
#include <windows.h>
#include <io.h>
#include <tchar.h>
#include <strsafe.h>
#ifndef QBASHER_LITE
#include <Psapi.h>
#endif
#endif


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

#if defined(WIN64) && !defined(QBASHER_LITE)
void report_memory_usage(FILE *printto, u_char *msg, DWORD *pagefaultcount);
void set_cpu_affinity(u_int cpu);
#endif


double what_time_is_it();

int validate_and_normalise_language_code(u_char *str);

size_t map_bytes(u_char *dest, u_char *src, size_t n, u_char *map);

int strcasecmp(const char *s, const char *t);

void strncasecpy(u_char *dest, u_char *src, size_t len);

void map_bytes_in_place(u_char *str, size_t n, u_char *map);

u_char *tailstr(u_char *str, u_char *s);

int substitute(u_char *str, u_char *toreplace, u_char *replacement, u_char *map,
	       BOOL check_word_boundaries);

u_char *make_a_copy_of(u_char *in);

u_char *make_a_copy_of_len_bytes(u_char *in, size_t len);

BOOL is_a_directory(char *arg);

BOOL exists(char *fstem, char *suffix);

size_t get_filesize(u_char *fname, BOOL verbose, int *error_code);

CROSS_PLATFORM_FILE_HANDLE open_ro(const char *fname, int *error_code);

CROSS_PLATFORM_FILE_HANDLE open_w(const char *fname, int *error_code);

void close_file(CROSS_PLATFORM_FILE_HANDLE h);

void buffered_flush(CROSS_PLATFORM_FILE_HANDLE wh, byte **buffer, size_t *bytes_in_buffer, char *label, BOOL cleanup);

void buffered_write(CROSS_PLATFORM_FILE_HANDLE wh, byte **buffer, size_t buffer_size, size_t *bytes_in_buffer,
	byte *data, size_t bytes2write, char *label);


void *mmap_all_of(u_char *fname, size_t *sighs, BOOL verbose, CROSS_PLATFORM_FILE_HANDLE *H, HANDLE *MH, int *error_code);

void unmmap_all_of(void *inmem, CROSS_PLATFORM_FILE_HANDLE H, HANDLE MH, size_t length);

byte **load_all_lines_from_textfile(u_char *fname, int *line_count, CROSS_PLATFORM_FILE_HANDLE *H,
				    HANDLE *MH, byte **file_in_mem, size_t *sighs);

void unload_all_lines_from_textfile(CROSS_PLATFORM_FILE_HANDLE H, HANDLE MH, byte ***lines,
				    byte **file_in_memory, size_t sighs);

#if defined(WIN64) && !defined(QBASHER_LITE)
void Privilege(TCHAR* pszPrivilege, BOOL bEnable, BOOL *x_use_large_pages, size_t *large_page_minimum);
#endif

void *lp_malloc(size_t how_many_bytes, BOOL x_use_large_pages, size_t large_page_minimum);

void lp_free(void *memory_to_free, BOOL x_use_large_pages);

void *cmalloc(size_t s, u_char *msg, BOOL verbose);

void error_exit(char *msg);

void putchars(u_char *str, size_t n);

void show_string_upto_nator(u_char *str, u_char nator, int indent);

void show_string_upto_nator_nolf(u_char *str, u_char nator, int indent);

int replace_tabs_with_single_spaces_in(u_char *str);

u_char *find_nth_occurrence_in_record(u_char *record, u_char c, int n);

u_char *extract_field_from_record(u_char *record, int n, size_t *len);

int split_up_first_3_fields_in_record(u_char *record, u_char **f1, u_char **f2 , u_char **f3);

u_ll estimate_lines_in_mmapped_textfile(u_char *file_in_mem, size_t file_length, int samples);

u_ll estimate_lines_in_textfile(CROSS_PLATFORM_FILE_HANDLE file, size_t file_length, int samples);

size_t get_dirlen_from_path(u_char *file_path);

void url_decode(u_char *str);

void vocabfile_entry_packer(byte *entry_start, size_t termflen, byte *term, u_ll occurrence_count, byte qidf, u_ll payload);

void vocabfile_entry_unpacker(byte *entry_start, size_t termflen, u_ll *occurrence_count, byte *qidf, u_ll *payload);

void vocabfile_test_pack_unpack(size_t termflen);

u_int quantized_idf(double N, double n, u_int bit_mask);

double get_idf_from_quantized(double N, u_int bit_mask, u_int qidf);

void clean_query(u_char *str);

void test_quantized_idf();

int count_one_bits_ull(unsigned long long x);

int count_one_bits_u(unsigned int x);

int count_ones_b(byte b);

void test_count_ones_b();

unsigned long long calculate_signature_from_first_letters(u_char *str, int bits);
