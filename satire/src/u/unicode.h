#ifndef BOOL
typedef int BOOL;
#define FALSE 0
#define TRUE 1
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

typedef unsigned int unicode_t;

#define UTF8_INVALID_CHAR ' '
#define UNICODE_INVALID_CHAR '?'

#define CODE_POINTS_IN_BMP 65536  // The number of Unicode code points available in the Basic
                                  // Multilingual Plane.
#define BMP_MASK 0xFFFF           // Mask an integer down to code points in the BMP

#define UNICODE_MASK 0x1FFFFF     // 2^21 - 1  The highest available code point in Unicode.
#define UTF8_LEADING_MASK 0xC0    // The first byte in a non-ASCII UTF-8 sequence must have these bits set.
#define UTF8_MASK 0xC0            // All bytes in a non-ASCII UTF-8 sequence must have this bit set.

#define SIXBIT_MASK 0x3F

extern byte ascii_non_tokens[];

u_char *utf8_get_invalid_char(u_char *str);

u_int utf8_getchar(byte *s, byte **bafter, BOOL cp1252_conversion);

unicode_t utf8_getchar2(u_char **p);

u_char *utf8_putchar(u_int ucs, u_char *where);

size_t utf8_copy(u_char *src, u_char *dest);

size_t utf8_ncopy(u_char *src, u_char *dest, size_t nbytes);

int utf8_lower_case(u_char *string);

size_t utf8_lowering_copy(u_char *dest, u_char *src);

size_t utf8_lowering_ncopy(u_char *dest, u_char *src, size_t nbytes);

BOOL utf8_contains_accented(byte *s);

int utf8_remove_accents(u_char *string);

// See UnicodeData.txt for explanation of the following ....
#define unicode_ispunct(u) ((u >= 0x0080 && u <= 0x00BF)	\
			    ||(u >= 0x2000 && u <= 0x206F)	\
			    ||(u >= 0x2200 && u <= 0x244A)	\
			    ||(u >= 0x2500 && u <= 0x2BEF)	\
			    ||(u >= 0x2E00 && u <= 0x2E49))


int utf8_ispunct(byte *s, byte **bafter);

int unicode_isvowel(u_int ucs);

int utf8_bytes_needed(u_int ucs);

int utf8_count_characters(byte *s);

int utf8_split_line_into_null_terminated_words(byte *input, byte **word_starts,
					       int max_words, int max_word_bytes,
					       BOOL case_fold,  BOOL remove_accents, 
					       BOOL maxwellize,
					       BOOL words_must_have_an_ASCII_alnum
);

int utf8_count_words_in_string(u_char *input,
					       BOOL case_fold,  BOOL remove_accents, 
					       BOOL maxwellize,
					       BOOL words_must_have_an_ASCII_alnum);

void test_utf8_functions();

void test_count_leading_ones_b();

void initialize_unicode_conversion_arrays(BOOL verbose);

void initialize_ascii_non_tokens(byte *non_token_string, BOOL include_cp1252_punctuation);

void display_ascii_non_tokens();

