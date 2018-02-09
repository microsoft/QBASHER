// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// This module contains functions dealing with Unicode and UTF-8.  It is a merge
// of two partially overlapping modules.  There are two CP1252 conversion tables
// and two versions of utf8_getchar.  Probably the versions could be merged but
// not until I'm certain that things are not going to break. :-)


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>

#include "utility_nodeps.h"
#include "unicode.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions for dealing with Unicode,  UTF-8 and other character sets.
////////////////////////////////////////////////////////////////////////////////////////////////////////

byte ascii_non_tokens[256] = {0};


static byte utf8_b0_payload_masks[5] = {
  0,
  0,
  0x1F,  // The lowest valid index is 0xC implying 2 UTF-8 bytes and a leading payload of 5 bits
  0xF,   // 0xE implies 3 UTF-8 bytes and a leading payload of 4 bits
  0x7,   // 0xF implies 4 UTF-8 bytes and a leading payload of 3 bits
};

static byte utf8_b0_unary[5] = {
  0,
  0,
  0xC0,
  0xE0,
  0xF0,
};



// The following two conversion arrays deal with only the Basic Multilingual Plane (BMP) which can
// be represented in 16 bits.  Unicode defines 32 planes (from memory) meaning that up to 21
// bits may be used.  However the other 31 planes represent relatively obscure character sets.
static u_short map_unicode_to_lower[CODE_POINTS_IN_BMP], map_unicode_to_unaccented[CODE_POINTS_IN_BMP];



// Conversions from CodePage 1252 (based on ISO_8859-1) upper range characters to Unicode.
// Data from https://en.wikipedia.org/wiki/Windows-1252.  
// Note that characters 0xA0 - 0xBF have exactly the same values in Unicode and are a miscellany of symbols
// Note that characters 0xC0 - 0DF have exactly the same values in Unicode and are nearly all letters.


static unicode_t cp1252_to_unicode[0x20] = {
  0x20AC,						// 0x80
  UTF8_INVALID_CHAR,
  0x201A,
  0X0192,
  0x201E,
  0x2026,
  0x2020,
  0x2021,
  0X02C6,
  0x2030,
  0X0160,
  0x2039,
  0X0152,
  UTF8_INVALID_CHAR,
  0X017D,
  UTF8_INVALID_CHAR,
  UTF8_INVALID_CHAR,		//0x90
  0x2018,
  0x2019,
  0x201C,
  0x201D,
  0x2022,
  0x2013,
  0x2014,
  0x02DC,
  0x2122,
  0X0161,
  0x203A,
  0X0153,
  UTF8_INVALID_CHAR,
  0X017E,
  0X0178,
};


// Alternative conversions from CodePage 1252 (based on ISO_8859-1) upper range characters to Unicode.
// Data from http://ascii-table.com/windows-codepage.php?1252.  Differs from the array above in that punctuation 
// characters like dashes and smart quotes are mapped to an ASCII punctuation character, similar where possible.
// Note that characters 192 (0xC0) and above have exactly the same values in Unicode and are all letters.

static unicode_t cp1252_to_unicode_v2[64] = {
	'$',						// 0x80
	UNICODE_INVALID_CHAR,
	'\'',
	0X0192,
	'"',
	'.',
	'|',
	'|',
	0X02C6,
	'%',
	0X0160,
	'`',
	0X0152,
	UNICODE_INVALID_CHAR,
	0X017D,
	UNICODE_INVALID_CHAR,
	UNICODE_INVALID_CHAR,		//0x90
	'`',
	'\'',
	'"',
	'"',
	'-',
	'-',
	'-',
	'~',
	'@',
	0X0161,
	'\'',
	0X0153,
	UNICODE_INVALID_CHAR,
	0X017E,
	0X0178,
	' ',						//A0
	'!',
	'$',
	'$',
	'$',
	'$',
	'|',
	'|',
	0X00A8,
	'@',
	'*',
	'"',
	'!',
	'-',
	'@',
	0X00AF,
	'@',						//B0
	'?',
	'2',
	'3',
	0X00B4,
	'u',
	'?',
	'.',
	0X00B8,
	'1',
	'?',
	'"',
	'?',
	'?',
	'?',
	'?',
};




//***********************************************************************************
//                          Initialisation functions
//***********************************************************************************

void initialize_ascii_non_tokens(byte *non_token_string, BOOL include_cp1252_punctuation) {
  int a;
  for (a = 0; a < 32; a++) { ascii_non_tokens[a] = 1; }  // Controls are always breakers
  while (*non_token_string) {
    if (0) printf("Setting ASCII '%c' (%X) as a token breaker.\n",
		  *non_token_string, *non_token_string);
    ascii_non_tokens[*non_token_string++] = 1;
  }
  if (include_cp1252_punctuation) {
    for (a = 128; a < 160; a++) { ascii_non_tokens[a] = 1; }
  }
}


void display_ascii_non_tokens() {
  int a;
  printf("\nThe following ascii characters are token-breaking...\n");
  fflush(stdout);
  for (a = 0; a < ' '; a++)
    if (ascii_non_tokens[a]) printf("   ASCII control: %02X\n", a);
  for (a = ' '; a < 128; a++)
    if (ascii_non_tokens[a]) printf("   ASCII punct.: '%c' %02X\n", a, a);
  for (a = 128; a < 160; a++)
    if (ascii_non_tokens[a]) printf("   CP1252 punct: %02X\n", a);

  printf("\n");
}


void initialize_unicode_conversion_arrays(BOOL verbose) {
  int c, length_increases = 0, length_decreases = 0;
  // setup pass through mappings and override using statements from the perl.
  for (c = 0; c < CODE_POINTS_IN_BMP; c++) {
    map_unicode_to_lower[c] = c;
    map_unicode_to_unaccented[c] = c;
  }


  // The following include files are generated by Bodo's C# program called
  // build_unicode_mapping_tables_for_C.cs
  
#include "diacriticsRemoved.txt"

#include "toLower.txt"

  // Reject any mappings which result in an increase in UTF-8 length
  // They would be the death knell of any in-place transformations.
  // In any case, such transformations don't seem to be valid.  Unicode
  // seems to have gone to lengths to ensure that this doesn't normally
  // happen.  
 
  for (c = 0; c < CODE_POINTS_IN_BMP; c++) {
    if (utf8_bytes_needed(map_unicode_to_lower[c]) > utf8_bytes_needed(c)) {
      if (verbose) printf("Note: Removing problematic length-increasing case transformation for %u\n", c);
      map_unicode_to_lower[c] = c;
      length_increases++;
    } else if (utf8_bytes_needed(map_unicode_to_lower[c]) < utf8_bytes_needed(c)) {
      length_decreases++;
    }
    if (utf8_bytes_needed(map_unicode_to_unaccented[c]) > utf8_bytes_needed(c)) {
		if (verbose) printf("Note: Removing problematic length-increasing case transformation for %u\n", c);
      map_unicode_to_unaccented[c] = c;
      length_increases++;
    } else if (utf8_bytes_needed(map_unicode_to_unaccented[c]) < utf8_bytes_needed(c)) {
      length_decreases++;
    }  
  }

  if (verbose) printf("Unicode initialisation complete:  %d length increasing transformations suppressed, %d length decreases\n",
	 length_increases, length_decreases);

}

//***********************************************************************************
//                              Internal functions
//***********************************************************************************

static int count_leading_ones_b(byte b) {
  // Return a count of the number of leading one bits in byte b.
  int count = 0;
  while (b & 0x80) {
    count++;
    b <<= 1;
  }
  return count;
}


//***********************************************************************************
//                                 API functions
//***********************************************************************************


u_char *utf8_get_invalid_char(u_char *str) {
  // Check null-terminated byte string str for invalid UTF-8 sequences and return a pointer to the first invalid
  // byte, or NULL if there is no invalidity
  u_char *p = str, bite;
  int b, bytes_in_char;
  while (*p) {
    if (*p & 0X80) {
      // This is a non-ASCII byte.  Either it must be a valid UTF-8 introducer followed by a sequence of 
      // continuation bytes, or it is invalid.  A UTF-8 introducer has a sequence of 1 bits followed by a 0 bit,
      // which specifies the number of bytes in this character in unary.  That number must lie between 2 and 4
      // inclusive.  Continuation bytes start with a leading '10'
      //
      // Count the number of leading one bits
      bite = *p;
      bytes_in_char = 0;
      while (bite & 0x80) {
	bytes_in_char++;
	bite <<= 1;
      }
      if (bytes_in_char < 2 || bytes_in_char > 4) return p;  // byte-count error --------------------->
      // Now check the continuation bytes
      for (b = 1; b < bytes_in_char; b++) if ((p[b] & 0xC0) != 0X80) return p;  // invalid continuation --------------->
      // Now update the pointer
      p += (bytes_in_char - 1);
    }
    // ASCII is valid UTF-8
    p++;
  }
  return NULL;
}




unicode_t utf8_getchar(byte *s, byte **bafter, BOOL cp1252_conversion) {
  // s is presumed to point to a valid UTF-8 sequence (including ASCII)
  // Return the Unicode value and a pointer to the byte after the end of the sequence.
  // *** WARNING cp1252_conversion may increase the length ***
  byte unary;
  unicode_t rslt = 0;
  if (!(*s & 0x80)) {
    *bafter = s + 1;
    return (unicode_t)*s;  // It's ASCII
  }
  // The unary part of the UTF-8 introducer can't be more than 4 because all valid Unicode can be represented in
  // at most 4 bytes.  4 bytes contain 3 x 6 = 18 + 3 = 21 payload bits which is exactly what's required.
  unary = count_leading_ones_b(*s);
  if (unary < 2) {
    //printf("Invalid UTF-8 start byte %x\n", *s);
    // Let's assume it's a windows 1252 character
    // This byte must be in the range 0x80 - 0xBF.  Between 0x80 and 0x9F we look up the table above
    // Between 0xA0 and 0xBF we just return the byte.
    *bafter = s + 1;
    if (cp1252_conversion && *s < 0xA0) return cp1252_to_unicode[*s - 0x80];
    else return UTF8_INVALID_CHAR;     // UTF8_INVALID_CHAR is normally a space
  }
  rslt = *s & utf8_b0_payload_masks[unary];
  s++;
  unary--;
  while (unary) {
    rslt <<= 6;
    if ((*s & 0xC0) != 0x80) {
      //printf("Invalid UTF-8 continuation byte %x\n", *s);
      *bafter = s;  // Return pointer to invalid byte, in case it's null
                    // otherwise we might continue past end of string
      return UTF8_INVALID_CHAR;
    }
    rslt |= (*s & 0x3F);  // OR-in the 6 payload bits from the next byte
    s++;
    unary--;
  }
  *bafter = s;
  return rslt;
}




#if 0  // Not used, don't use
unicode_t utf8_getchar2(u_char **str) {
  // Extract a unicode value from the sequence of bytes starting at *str and advance the pointer.
  // If we find an invalid UTF-8 sequence, assume that it was supposed to be CodePage 1252 and 
  // return an appropriate Unicode value.
  // 
  unicode_t u = 0;
  int b, bytes_in_char = 0;
  u_char *p = *str, bite;

  if ((*p & 0X80) == 0) {
    u = (unicode_t)*p;   // It's ASCII
    (*str)++;
    return u;
  }
  bite = *p;
  while (bite & 0x80) {
    bytes_in_char++;
    bite <<= 1;
  }
  if (bytes_in_char < 2 || bytes_in_char > 4) {
    if (*p >= 0XC0) u = (unicode_t)*p;
    else u = cp1252_to_unicode_v2[*p - 128];
    (*str)++;
    return u;  // byte-count error --------------------->
  }
  // Now check the continuation bytes
  u = (*p & utf8_b0_payload_masks[bytes_in_char]);
  for (b = 1; b < bytes_in_char; b++) {
    if ((p[b] & 0XC0) != 0X80) {
      if (*p >= 0XC0) u = (unicode_t)*p;
      else u = cp1252_to_unicode_v2[*p - 128];
      (*str)++;
      return u;  // invalid continuation --------------->
    }
    u <<= 6;
    u |= (p[b] & 0X3F);
  }
  (*str) += bytes_in_char;
  return u;
}
#endif


size_t utf8_copy(u_char *dest, u_char *src) {
  // Copy src to dest as follows:
  //  - ASCII bytes are copied byte for byte
  //  - valid UTF-8 characters are copied across
  //  - Invalid UTF-8 characters are treated as a sequence of CodePage 1252 characters
  //	  and converted to a UTF-8 sequence.
  // The third type of conversion may result in a significant increase in string length.  In the
  // worst case, a string of bytes, each with the value 0x88 (modifier letter circumflex accent) would 
  // be doubled in length because 0x88 maps to 0x2C6 in Unicode.  That takes ten bits needing two bytes
  // to represent in UTF-8
  //
  // *** It is assumed that dest already has sufficient memory allocated. ***

  u_char *r = src, *w = dest, bite, outbuf[4];
  int bytes_in_char, b;
  unicode_t u;
  BOOL invalid;

  while (*r) {
    if (*r & 0X80) {   // -- High bit is set.
      invalid = FALSE;
      bite = *r;
      bytes_in_char = 0;
      u = 0;
      while (bite & 0x80) {
	bytes_in_char++;
	bite <<= 1;
      }
      if (bytes_in_char < 2 || bytes_in_char > 4) {
	// byte-count error --------------------->
	invalid = TRUE;
      }
      else {
	u = (*r & utf8_b0_payload_masks[bytes_in_char]);
	// Now check the continuation bytes, building up the Unicode value as we go.
	for (b = 1; b < bytes_in_char; b++) {
	  if ((r[b] & 0XC0) != 0X80) {
	    // Invalid continuation
	    invalid = TRUE;
	    break;
	  }
	  u <<= 6;
	  u |= (r[b] & 0X3F);
	}
      }

      if (invalid) {
	// Assume that the first byte of an illegal UTF-8 sequence is a CodePage 1252 character and convert it
	// to an appropriate ASCII or Unicode value.
	if (*r >= 0XC0) u = (unicode_t)*r;
	else u = cp1252_to_unicode_v2[*r - 128];

	if (0) {
	  printf("invalid character %X.  u = %X\n", (int)*r, u);
	  fflush(stdout);
	}
	r++;
      }
      else {
	r += bytes_in_char;
	if (u >= 0X2000 && u <= 0X206F) {
	  // A valid UTF-8 character lies within the non-ASCII General Punctuation group  -- convert it into 
	  // an ASCII space.
	  u = ' ';
	}
      }

      if (u) {
	if (u < 128) *w++ = (u_char)u;  // u represents an ASCII character.  Just write it out.
	else {
	  // u requires multiple bytes to represent it in UTF-8
	  // Copy over the UTF-8 bytes representing u.  Assemble first in outbuf
	  bytes_in_char = 0;
	  for (b = 3; b >= 0; b--) {
	    bytes_in_char++;
	    if (u > 0X3F) {
	      // Need more than 6 bits to represent what's left
	      outbuf[b] = (u_char)(u & 0X3F) | 0x80;
	      u >>= 6;
	    }
	    else {
	      outbuf[b] = utf8_b0_unary[bytes_in_char];
	      outbuf[b] |= ((u_char)(u & utf8_b0_payload_masks[bytes_in_char]));
	      if (0){
		printf("byte %d: %X..  bytes_in_char = %d\n", b, outbuf[b], bytes_in_char);
		fflush(stdout);
	      }
	      break;
	    }
	  }
	  // Now write them out
	  for (b = bytes_in_char; b > 0;  b--) *w++ = outbuf[4 - b];
	}
      }

    }
    else {
      // Simple ASCII
      *w++ = *r++;
    }
  }
  // Null terminate and return length
  *w = 0;
  if (0) printf(" --121-- utf8_copied '%s', len = %zd\n", dest, w - dest);
  return (w - dest);
}


size_t utf8_ncopy(u_char *dest, u_char *src, size_t nbytes) {

  // Sorry, not yet properly implemented!
  strncpy((char *)dest, (char *)src, nbytes);
  return strlen((char *)dest);
}


u_char *utf8_putchar(unicode_t unichar, u_char *where) {
  // Emit a series of bytes representing the Unicode code point 'unichar', starting at where.
  // Return a pointer to the byte after the last byte written.  Note that Unicode is limited
  // to 21 bits (the Basic Multilingual Plane and 31 other planes). Thus the maximum possible
  // code point can be represented in 4 bytes.  We mask unichar to enforce this.
  //
  // A UTF-8 sequence is either a single ASCII byte or a leading byte followed by
  // a number of trailing bytes.  Trailing bytes have a leading '10' followed by 6 payload
  // bits.   A leading byte consists of a unary representation of the total number of bytes
  // a zero and then a number of payload bits.  A leading byte of '1110xxxx' includes 4
  // payload bits and indicates that there are two trailing bytes.  Any UTF-8 byte starting
  // with '0' is ASCII.  Any starting with '11' is a leading byte.  Any starting with '10'
  // is a trailing byte.
  //
  // Caller's responsibility to provide enough storage at where.
  
  unicode_t u = unichar & UNICODE_MASK;
  byte temp[4];   // We produce the bytes in reverse order
  int x = 0;
  u_int max_poss_in_leading_byte = 0x3F;
  if (unichar <= 0x7F) {
    // ASCII
    *where = (byte) u;
    return (where + 1);
  }
  while (u > max_poss_in_leading_byte) {
    temp[x++] = (u & SIXBIT_MASK) | 0x80;
    u >>= 6;
    max_poss_in_leading_byte >>= 1;  // Every trailing byte halves max val in leading byte.
  }

  // x is a count of the trailing bytes - insert the leading byte
  temp[x] = utf8_b0_unary[x + 1] | (u & utf8_b0_payload_masks[x + 1]);

  // Output the bytes in reverse order.
  while (x >= 0) {
    *where++ = temp[x--];
  }
  return where;
}


int utf8_ispunct(byte *s, byte **bafter) {
  unicode_t unicode;
  byte *lbafter;
  if (!(*s & 0x80)) {  // ASCII
    (*bafter)++;
    return ispunct(*s);
  }
  unicode = utf8_getchar(s, &lbafter, TRUE);
  *bafter = lbafter;
  if unicode_ispunct(unicode) return 1;  
  return 0;
}

int utf8_count_characters(byte *s){
  int count = 0;
  byte *b = s, msb2;
  while (*b) {
    msb2 = *b & 0xC0;
    if (msb2 != 0x80) count++; //  Increment the count unless it's a UTF-8 continuation char
    b++;
  }
  return count;
}


BOOL utf8_contains_accented(byte *s) {
  // s is assumed to be a null-terminated UTF-8 string.
  byte *next;
  unicode_t ucs;
  while (*s) {
    if ((*s & 0xC0) == 0xC0) {
      // The first byte in a multi-byte UTF-8 code.
      ucs = utf8_getchar(s, &next, TRUE) & BMP_MASK;  // Make sure not to access beyond the mapping table.
      if (ucs != map_unicode_to_unaccented[ucs]) return TRUE;
      s = next;
    } else s++;   // ASCII is unaccented.
  }
  return FALSE;   // We didn't find any accented letters.
}


int utf8_remove_accents(u_char *string) {
  // In-place accent removal from null-terminated UTF-8 string.  Note that the function
  // which initialises the unicode mapping tables removes any length-increasing mapping.
  //
  // Return value is a count of the characters changed.
  int characters_changed = 0;
  unicode_t ucs;
  u_char *r = string, *w = string, *next;
  if (0) printf("utf8_remove_accents(%s)\n", string);
  while (*r) {
    if (*r & 0x80) { // This weak test allows us to handle CP-1252 
      // Since string is UTF-8 r marks the start of a muti-byte UTF-8 sequence
      ucs = utf8_getchar(r, &next, FALSE); 
      r = next;
      if (ucs > BMP_MASK) { // Make sure not to access beyond the mapping table.
	w = utf8_putchar(ucs, w);  // Leave these other non-BMP chars alone.
      } else {
	w = utf8_putchar(map_unicode_to_unaccented[ucs], w);
	if (ucs != map_unicode_to_unaccented[ucs]) characters_changed++;
      }
    } else {
      *w++ = (u_char)map_unicode_to_unaccented[*r++];  // Shouldn't change anything
    }
  }
  *w = 0;  // String might be shorter.
  return characters_changed;
}


int utf8_lower_case(u_char *string) {
  // In-place lower casing from null-terminated UTF-8 string.  Note that the function
  // which initialises the unicode mapping tables removes any length-increasing mapping.
  // Returns the length of the result in bytes.  It might be shorter.
  int len = 0;
  unicode_t ucs;
  u_char *r = string, *w = string, *next;
  while (*r) {
    if (*r & 0x80) { // This weak test allows us to handle CP-1252 
      // Since string is UTF-8 this r marks the start of a UTF-8 string
      ucs = utf8_getchar(r, &next, FALSE);
      if (ucs > BMP_MASK) { // Make sure not to access beyond the mapping table.
	w = utf8_putchar(ucs, w);  // Leave these other non-BMP chars alone.
      } else {
	w = utf8_putchar(map_unicode_to_lower[ucs], w);
      }
      r = next;
    } else {
      *w++ = (u_char)map_unicode_to_lower[*r++];  // No risk here.
    }
  }
  *w = 0;  // String might be shorter.
  len = (int)( w - string);
  return len;
}


size_t utf8_lowering_copy(u_char *dest, u_char *src) {
  // Lower case copying from null-terminated UTF-8 string.  Note that the function
  // which initialises the unicode mapping tables removes any length-increasing mapping.
  // Callers responsibility to ensure that dest is at least strlen(src) + 1 bytes long
  // Returns the length of the result in bytes.  It might be shorter.
  size_t len = 0;
  unicode_t ucs;
  u_char *r = src, *w = dest, *next;

  while (*r) {
    if (*r & 0x80) { // This weak test allows us to handle CP-1252
      // Since string is UTF-8 this r marks the start of a UTF-8 string
      ucs = utf8_getchar(r, &next, FALSE);   // Avoid potential length increases from CP-1252
      if (ucs > BMP_MASK) { // Make sure not to access beyond the mapping table.
        w = utf8_putchar(ucs, w);  // Leave these other non-BMP chars alone.
      } else {
        w = utf8_putchar(map_unicode_to_lower[ucs], w);
      }
      r = next;
    } else {
      *w++ = (u_char)map_unicode_to_lower[*r++];  // No risk here.
    }
  }
  *w = 0;  // String might be shorter.
  len = (int)(w - dest);
  return len;
}


size_t utf8_lowering_ncopy(u_char *dest, u_char *src, size_t nbytes) {
  // Lower case copying from UTF-8 string src to dest.  Note that the function
  // which initialises the unicode mapping tables removes any length-increasing
  // mapping.  Like strncpy() no more than nbytes bytes are copied.  Iff less
  // than nbytes bytes are copied, dest will be null-terminated.
  // Callers responsibility to ensure that dest is at least nbytes bytes long
  // Returns the length of the result in bytes.
  //
  // Coding this is a little tricky because the nbytes cutoff could fall in the
  // middle of a UTF-8 sequence.
  size_t len = 0, room_left = nbytes;
  unicode_t ucs;
  u_char *r = src, *w = dest, *next, *start_utf8_seq = NULL;
  int bius = 0;  // Bytes in UTF-8 sequence

  while (*r && room_left > 0) {
    if (*r & 0x80) { // This weak test allows us to handle CP-1252 
      // Since string is UTF-8 this r marks the start of a UTF-8 sequence of bytes
      start_utf8_seq = w;
      bius = count_leading_ones_b(*r);  // This is the number of bytes in this UTF-8 sequence
      if (bius > room_left) {
	*w = 0;  // Don't copy any of it.
	return (w - dest);   // -------------------------->
      }
      ucs = utf8_getchar(r, &next, TRUE);
      if (ucs > BMP_MASK) { // Make sure not to access beyond the mapping table.
	w = utf8_putchar(ucs, w);  // Leave these other non-BMP chars alone.
      } else {
	w = utf8_putchar(map_unicode_to_lower[ucs], w);
      }
      r = next;
      room_left -= (w - start_utf8_seq);  // Have to do it this way in case lower casing reduces width
    } else {  // ASCII
      *w++ = (u_char)map_unicode_to_lower[*r++];  // No risk here.
      room_left--;
    }
  }
  *w = 0;  // String might be shorter.
  len = (w - dest);
  return len;
}




int unicode_isvowel(unicode_t ucs) {
  // Not very refined but should pick up upper and lower case versions of accented or
  // unaccented English/French vowels 
  if (ucs > BMP_MASK) return 0;
  ucs = map_unicode_to_lower[map_unicode_to_unaccented[ucs]];
  if (ucs == 'a' || ucs == 'e' || ucs == 'i' || ucs == 'o' || ucs == 'u' || ucs == 'y')
    return 1;
  else return 0;
}


int utf8_bytes_needed(unicode_t ucs) {
  // How many UTF-8 bytes are needed to represent up to 21 bits of ucs?
  unicode_t limit = 0x80;   // 7 bits
  int bytes = 1;
  ucs &= UNICODE_MASK;

  while (ucs >= limit) {
    limit <<= 5;  // Each extra byte brings 5 extra bits of payload;
    bytes++;
  }
  return bytes;
}


int utf8_split_line_into_null_terminated_words(byte *input, byte **word_starts,
					       int max_words, int max_word_bytes,
					       BOOL case_fold,  // case-fold line before splitting
					       BOOL remove_accents, // before splitting
					       BOOL maxwellize,  // Perform some substitutions to match
					                        // those in David Maxwell's code
					       BOOL words_must_have_an_ASCII_alnum
					       ) {
  // It is assumed that input is a null-terminated string which we are allowed to
  // write in. (To put NULs at the end of each word.)  We scan input and make
  // an array of pointers to the word starts, inserting a NUL at each word end.
  // (Note that a maximum is imposed on the number of bytes in a word.   When
  // this limit is imposed, care is taken not to split up a UTF-8 sequence.)
  // Scanning stops either at the terminating NUL or at the first ASCII control
  // character.  Relies on ascii_non_tokens[] array having been initialised.
  //
  // Make the i-th element of word_starts point to the first byte of the i-th word
  // and return the number (up to max_words) of words found.

  byte *p = input, *q, *bafter, *wdstart;
  int wds = 0, char_width = 0, len;
  unicode_t unicode;
  BOOL finished = FALSE, contains_ASCII_alnum;
  if (input == NULL || word_starts == NULL || max_words <= 0 || input[0] == 0) return 0;

  if (case_fold) utf8_lower_case(input);
  
  if (remove_accents) utf8_remove_accents(input);

  if (maxwellize) {
    while (*p) {
      if (*p == '\'') {
	// Remove both apostrophe and trailing s if there is one
	if (*(p + 1) == 's') { *p++ = ' '; *p = ' ';}
      } else if (*p == '%') {
	// Remove %20 if it's there
	if (*(p + 1) == '2' && *(p + 2) == '0') { *p++ = ' '; *p++ = ' ';  *p = ' ';}
      }
      p++;
    }
  }

  p = input;

  while (*p >= ' ') { // We'll break out if we reach any of a few stopping conditions
    // --------------------- Scan text before, between, or after words ---------------
    while (*p >= ' ') {
      // Skip over successive non word chars.  On exit, p should point either to the first byte of
      // an indexable items or to a terminating ASCII control
      if (*p & 0x80) {
	
	unicode = utf8_getchar(p, &bafter, FALSE);
	char_width = (int)(bafter - p);
	// Assume (falsely) that all non-punctuation unicode is indexable
	if (!(unicode_ispunct(unicode) || unicode == 0xA0)) break;  // 00A0 is no-break space
	else p = bafter;
      }
      else if (ascii_non_tokens[*p]) p++;
      else {
	char_width = 1;
	break;  // This is an indexable ASCII character.
      }
    }

    // p may point to either a NUL, an ASCII control, an indexable ASCII char, or the first byte of indexable UTF-8
    if (*p < ' ') return wds;   // An ASCII control (e.g. TAB) :- we've finished.

    wdstart = p;      // Record where this word started
    if (0) printf("wdstart = %X\n", *wdstart);
    p += char_width;  // Skip the first char (already scanned.) 

    // ------------------------------ Scan the text of a word --------------------------
    contains_ASCII_alnum = FALSE;
    while (*p >= ' ') {  // Traverse the sequence of indexable characters.  On exit
                        // p will point either to the first byte of a non-indexable, or to an ASCII control
      if (*p & 0x80) {
	unicode = utf8_getchar(p, &bafter, FALSE);
	if (unicode_ispunct(unicode) || unicode == 0xA0) {  // A0 is non breaking space
	  if (0) printf("Unicode %X is classed as punctuation\n", unicode);
	  char_width = (int)(bafter - p);
	  break;  // Position on first byte of UTF-8 punct sequence
	} else {
	  if (0) printf("Unicode %X considered indexable\n", unicode);
	  p = bafter;
	}
      }
      else {
	// ASCII
	if (ascii_non_tokens[*p]) {
	  char_width = 1;
	  if (0) printf("Non-indexable ASCII '%c'\n", *p);
	  break;  // This is a non-indexable ASCII character.
	} else {
	  // indexable ASCII
	  if (isalpha(*p)) contains_ASCII_alnum = TRUE;
	  p++;
	}
      }
    }

    // ------------ Null terminate, truncate if necessary, and record the start of this word --------
    if (*p < ' ') {
      finished = TRUE;
      if (0) printf("  FINISHED\n");
    }
    *p = 0;  // Null terminate the word -- We've declared we're going to do this
    q = p; 
    len = p - wdstart;
    if (len > max_word_bytes) {
      // Need to truncate this word but have to make sure that the last
      // byte we null out is either an ASCII character (first bit is 
      // zero) or the start of a UTF-8 sequence (first two bits are
      // ones).  The bytes we can't null out all start with '10'
      q = wdstart + max_word_bytes;  // Chop off at the maximum length
      *q-- = 0;
      while (q > wdstart && ((*q & 0xC0) == 0x80)) *q-- = 0;  // move back over UTF-8 continuation bytes
      // q either points to the start of the word, or to an ASCII byte or to the first byte of a UTF-8 character
      if (*q & 0x80) *q = 0;
    }

    if (0) {
      u_char *x = wdstart;
      printf("  word %d found [%s], length %zd bytes:", wds, wdstart, strlen((char *)wdstart));
      while (*x) printf(" %X", *x++);
      printf("\n");
    }
    if (contains_ASCII_alnum  || (! words_must_have_an_ASCII_alnum)) 
      word_starts[wds++] = wdstart;
    if (finished  || wds == max_words) return wds;
    p += char_width;
  }  // End of word-breaking loop

  return wds;
}


int utf8_count_words_in_string(u_char *input, BOOL case_fold,  // case-fold line before splitting
					      BOOL remove_accents, // before splitting
					      BOOL maxwellize,  // Perform some substitutions to match
					                        // those in David Maxwell's code
					      BOOL words_must_have_an_ASCII_alnum
					       ) {
  // Note:  very similar code to utf8_split_line_into_null_terminated_words()
  //
  // Scanning stops either at the terminating NUL, or at the first ASCII control
  // character.  Relies on ascii_non_tokens[] array having been initialised.
  //
  // Return the number of words found.

  byte *p = input, *bafter;
  int wds = 0, char_width = 0;
  unicode_t unicode;
  BOOL contains_ASCII_alnum = FALSE;

  if (input == NULL) return 0;

  if (case_fold) utf8_lower_case(input);
  
  if (remove_accents) utf8_remove_accents(input);

  if (maxwellize) {
    while (*p) {
      if (*p == '\'') {
	// Remove both apostrophe and trailing s if there is one
	if (*(p + 1) == 's') { *p++ = ' '; *p = ' ';}
      } else if (*p == '%') {
	// Remove %20 if it's there
	if (*(p + 1) == '2' && *(p + 2) == '0') { *p++ = ' '; *p++ = ' ';  *p = ' ';}
      }
      p++;
    }
  }

  p = input;

  

  while (*p >= ' ') {  // Outer loop over input
    // --------------------- Scan text before, between, or after words ---------------
    while (*p >= ' ') {
      // Skip over successive non word chars.  On exit, p should point either to the first byte of
      // an indexable items or to a terminating ASCII control
      if (*p & 0x80) {
	unicode = utf8_getchar(p, &bafter, FALSE);
	char_width = (int)(bafter - p);
	// Assume (falsely) that all non-punctuation unicode is indexable
	if (!(unicode_ispunct(unicode) || unicode == 0xA0)) break;   // A0 is non-breaking space
	else p = bafter;
      }
      else if (ascii_non_tokens[*p]) p++;
      else {
	char_width = 1;
	break;  // This is an indexable ASCII character.
      }
    }

    // p may point to either a NUL, an ASCII control, an indexable ASCII char, or the first byte of indexable UTF-8
    if (*p < ' ') return wds;   // An ASCII control (e.g. TAB) :- we've finished.

    
    p += char_width;  // Skip the first char (already scanned.) 

    // ------------------------------ Scan the text of a word --------------------------
	contains_ASCII_alnum = FALSE;
    while (*p > ' ') {  // Traverse the sequence of indexable characters.  On exit
                        // p will point either to the first byte of a non-indexable, or to an ASCII control
      if (*p & 0x80) {
	unicode = utf8_getchar(p, &bafter, FALSE);
	if (unicode_ispunct(unicode) || unicode == 0xA0) {  // A0 is non-breaking space
	  char_width = (int)(bafter - p);
	  break;  // Position on first byte of UTF-8 punct sequence
	}
	else p = bafter;
      }
      else {
	// ASCII
	if (ascii_non_tokens[*p]) {
	  char_width = 1;
	  if (0) printf("Non-indexable ASCII '%c'\n", *p);
	  break;  // This is a non-indexable ASCII character.
	} else {
	  // indexable ASCII
	  if (isalpha(*p)) contains_ASCII_alnum = TRUE;
	  p++;
	}
      }
    }

	if (contains_ASCII_alnum || (!words_must_have_an_ASCII_alnum)) wds++;


    if (*p < ' ') return wds;
    p += char_width;
  }  // End of word-breaking loop

  return wds;
}





//***********************************************************************************
//                            Testing functions
//***********************************************************************************

static char *test_strings[] = {
  "A Note on MÃ¶bius Functions and the Communication Complexity of the Graph-Accessability-Problem",
  "Représentation sémantique des langues naturelles en Prolog",
  "Définitions et premières expériences en apprentissage par analogie dans les séquences",
  "23èmes Journées Bases de Données Avancées, BDA 2007, Marseille, 23-26 Octobre 2007, Actes (Informal Proceedings",
  "Anais do WER98 - Workshop em Engenharia de Requisitos, Maringá-PR, Brasil, Outubro 12, 1998",
  "WEAPON: Modelo de Workflow con OntologÃ­as para Procesos Administrativos",
  "Première mesure avec le kit SCHUSS",
  "protein\x97protein",     // CP 1252 em-dash
  "protein\xE2\x80\x94protein",   // UTF-8 em-dash
  "",
};


void utf8_internal_tests() {
  int i;
  u_char *p, buf[1000];
  for (i = 0; test_strings[i][0]; i++) {
    printf("%3d  %s\n", i, test_strings[i]);
    fflush(stdout);
    p = utf8_get_invalid_char((u_char *)test_strings[i]);
    if (p != NULL) printf("     INVALID starting at: %s\n", p);
    fflush(stdout);

    utf8_copy(buf, (u_char *)test_strings[i]);
    printf("     COPY: %s\n", buf);
    fflush(stdout);
  }
}


void test_utf8_functions() {
  // Also tests utf8_ispunct() and utf8_count_characters()
  unicode_t unicode;
  int len, chars_changed, errs = 0;
  byte string[10], *next;

  // Test ASCII
  string[0] = 'A';
  string[1] = 'B';
  string[2] = 0xD0;   // The next two bytes represent CYRILLIC SMALL LETTER I  (0x438)
  string[3] = 0xB8;
  string[4] = 0;
  unicode = utf8_getchar(string, &next, FALSE);
  if (unicode != 'A' || (next - string) != 1) {
    printf("test_utf8_getchar(1) failed.  Unicode = %x.\n", unicode);
    errs++;
  }
  if (utf8_count_characters(string) != 3) {
    printf("utf8_count_characters(1) failed.  Length = %d.\n",
	   utf8_count_characters(string));
    errs++;
  }
  // Test utf8_lower_case
  len = utf8_lower_case(string);
  if (string[0] != 'a' || string[1] != 'b') {
    printf("utf8_lower_case(1) failed.  Length = %d.\n",
	   len); 
    errs++;
  }
  if (len != 4) {
    printf("utf8_lower_case(1) failed.  Wrong length = %d.\n",
	   len);
    errs++;
  }
  unicode = utf8_getchar(string + 2, &next, FALSE);
  if (unicode != 0x438) {
    printf("utf8_getchar(1A) failed. Unicode = %x.\n", unicode);
    errs++;
  }
  // Test utf8_putchar
  next = utf8_putchar('A', string);
  if (string[0] != 'A') {
    printf("test_utf8_putchar(1) bad ASCII.\n");
    errs++;
  }
  if (next != string + 1) {
    printf("test_utf8_putchar(1) bad function return.\n");
    errs++;
  }

  
  // Test two-byte accented letter.   A with circumflex 0xC2
  string[0] = 0xC3;
  string[1] = 0x82;
  string[2] = 0xC3;
  string[3] = 0x82;
  string[4] = 0;
  unicode = utf8_getchar(string, &next, FALSE);
  if (unicode != 0xC2 || (next - string) != 2) {
    printf("test_utf8_getchar(2) failed.  Unicode = %x.\n", unicode);
    errs++;
  }
  if (utf8_ispunct(string, &next) || (next - string) != 2) {
    printf("utf8_ispunct(2) failed.  Unicode = %x.\n", unicode);
    errs++;
  }
  if (utf8_count_characters(string) != 2) {
    printf("utf8_count_characters(2) failed.  Length = %x.\n",
	   utf8_count_characters(string));
    errs++;
  }
  // Test utf8_lower_case
  len = utf8_lower_case(string);
  unicode = utf8_getchar(string, &next, FALSE);
  if (unicode != 0xE2) {
    printf("utf8_lower_case(2/1) failed: %X.\n", unicode);
    errs++;
  }
  if (len != 4) {
    printf("utf8_lower_case(2/1) failed.  Wrong length = %d.\n",
	   len);
    errs++;
  }
  unicode = utf8_getchar(string, &next, FALSE);
  if (unicode != 0xE2) {
    printf("utf8_lower_case(2/2) failed: %X.\n", unicode);
    errs++;
  }
  // Test utf8_remove_accents
  chars_changed = utf8_remove_accents(string);
  unicode = utf8_getchar(string, &next, FALSE);
  if (unicode != 'a') {
    printf("utf8_remove_accents(2/1) failed: %X.\n", unicode);
    errs++;
  }
  unicode = utf8_getchar(next, &next, FALSE);
  if (unicode != 'a') {
    printf("utf8_remove_accents(2/2) failed: %X.\n", unicode);
    errs++;
  }
  if (chars_changed != 2) {
    printf("utf8_remove_accents(2) failed.  Wrong count of chars changed = %d.\n",
	   len);
    errs++;
  }
  // Test utf8_putchar
  next = utf8_putchar(0xC2, string);
  if (string[0] != 0xC3) {
    printf("test_utf8_putchar(2) bad leading byte.\n");
    errs++;
  }
  if (string[1] != 0x82) {
    printf("test_utf8_putchar(2) bad trailing byte.\n");
    errs++;
  }
  if (next != string + 2) {
    printf("test_utf8_putchar(2) bad function return.\n");
    errs++;
  }
  
  // Test two-byte CYRILLIC letter.   CYRILLIC SMALL LETTER I  
  string[0] = 0xD0;
  string[1] = 0xB8;
  string[2] = 'A';
  string[3] = 0;  
  unicode = utf8_getchar(string, &next, FALSE);
  if (unicode != 0x438 || (next - string) != 2) {
    printf("test_utf8_getchar(2B) failed.  Unicode = %x.\n", unicode);
    errs++;
  }
  if (utf8_count_characters(string) != 2) {
    printf("utf8_count_characters(2B) failed.  Length = %x.\n",
	   utf8_count_characters(string));
    errs++;
  }

  // Test utf8_putchar
  next = utf8_putchar(0x438, string);
  if (string[0] != 0xD0) {
    printf("test_utf8_putchar(2B) bad leading byte.\n");
    errs++;
  }
  if (string[1] != 0xB8) {
    printf("test_utf8_putchar(2B) bad trailing byte.\n");
    errs++;
  }
  if (next != string + 2) {
    printf("test_utf8_putchar(2B) bad function return.\n");
    errs++;
  }
  

  // Test three-byte punc.  right single quotation mark  
  string[0] = 0xE2;
  string[1] = 0x80;
  string[2] = 0x99;
  string[3] = 0;  
  unicode = utf8_getchar(string, &next, FALSE);
  if (unicode != 0x2019 || (next - string) != 3) {
    printf("test_utf8_getchar(3) failed.  Unicode = %x.\n", unicode);
    errs++;
  }
  if ((! utf8_ispunct(string, &next)) || (next - string) != 3) {
    printf("utf8_ispunct(3) failed.  Unicode = %x.\n", unicode);
    errs++;
  }
  if (utf8_count_characters(string) != 1) {
    printf("utf8_count_characters(3) failed.  Length = %x.\n",
	   utf8_count_characters(string));
    errs++;
  }

   // Test utf8_putchar
  next = utf8_putchar(0x2019, string);
  if (string[0] != 0xE2) {
    printf("test_utf8_putchar(3) bad leading byte. %X\n", string[0]);
    errs++;
  }
  if (string[1] != 0x80) {
    printf("test_utf8_putchar(3) bad trailing byte 1.\n");
    errs++;
  }
  if (string[2] != 0x99) {
    printf("test_utf8_putchar(3) bad trailing byte 2. \n");
    errs++;
  }
  if (next != string + 3) {
    printf("test_utf8_putchar(3) bad function return.\n");
    errs++;
  }
  
 // Test four byte Chinese character.  U+20D7C
  string[0] = 0xF0;
  string[1] = 0xA0;
  string[2] = 0xB5;
  string[3] = 0xBC;
  string[4] = 0xF0;
  string[5] = 0xA0;
  string[6] = 0xB5;
  string[7] = 0xBC;
  string[8] = 0;
  unicode = utf8_getchar(string, &next, FALSE);
  if (unicode != 0x20D7C || (next - string) != 4) {
    printf("test_utf8_getchar(4) failed.  Unicode = %x.\n", unicode);
    errs++;
  }
  if (utf8_ispunct(string, &next) || (next - string) != 4) {
    printf("utf8_ispunct(4) failed.  Unicode = %x.\n", unicode);
    errs++;
  }
  if (utf8_count_characters(string) != 2) {
    printf("utf8_count_characters(4) failed.  Length = %x.\n",
	   utf8_count_characters(string));
    errs++;
  }

  // Test utf8_putchar
  next = utf8_putchar(0x20D7C, string);
  if (string[0] != 0xF0) {
    printf("test_utf8_putchar(4) bad leading byte.\n");
    errs++;
  }
  if (string[1] != 0xA0) {
    printf("test_utf8_putchar(4) bad trailing byte1.\n");
    errs++;
  }
  if (string[2] != 0xB5) {
    printf("test_utf8_putchar(4) bad leading byte2.\n");
    errs++;
  }
  if (string[3] != 0xBC) {
    printf("test_utf8_putchar(4) bad trailing byte3.\n");
    errs++;
  }
  if (next != string + 4) {
    printf("test_utf8_putchar(4) bad function return.\n");
    errs++;
  }
  

  if (errs) exit(1);  // Only run when debugging
  printf("Test of UTF-8 functions passed.\n");
}


void test_count_leading_ones_b() {
  byte b = 0;
  if (count_leading_ones_b(b = 0) != 0
      || count_leading_ones_b(b = 1) != 0
      || count_leading_ones_b(b = 0x80) != 1
      || count_leading_ones_b(b = 0xF0) != 4
      || count_leading_ones_b(b = 0xFF) != 8
      || count_leading_ones_b(b = 0xC0) != 2) {
    printf("Error in count_leading_ones_b(%X)\n", b);
    exit(1);  // Only run when debugging ... actually, is it run at all.
  }

  printf("Test of count_ones_b() passed.\n");
}


