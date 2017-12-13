// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "../shared/QBASHER_common_definitions.h"
#include "../shared/utility_nodeps.h"
#include "../shared/unicode.h"
#include "street_addresses.h"


int is_street_number(byte *wdin, BOOL unit_number_first) {
  // A street number must be in one of the following forms:
  //   1. all digits
  //   2. digits/digits unit/house (or house/unit)
  //   3. digits-digits (a range of house numbers)
  //   4. One of 1-3 formats preceded by a slash and/or followed by 'bis'(French for ditto) or A,B,C
  //   5. We also allow any of 1-4 to preceded by '#' or 'n'
  // If wd is in one of these formats, return the number as an integer.  Otherwise return zero.
  size_t len;
  int i, housenum;
  byte *housenumstart, *wd = wdin;
  if (*wd == '#' || *wd == 'n') wd++;   // 'n' added in 1.5.111, 30 Aug 2017
  housenumstart = wd;  // Starting assumption.
  len = strlen((char *)wd);
  if (len >= 4 && !strcmp((char *)wd + len - 3, "bis")) len -=3;
  else if (len >= 2 && (wd[len -1] == 'a' || wd[len -1] == 'b' || wd[len -1] == 'c')) len --;
  for (i = 0; i < len; i++) {
    if (isdigit(wd[i])) continue;
    // Attention:  z may have replaced / or y replaced - in digits/digits context
    if ((wd[i] == 'z' || wd[i] == 'y') && i > 0 && isdigit(wd[i + 1]) && housenumstart == wd) {
      if (unit_number_first) housenumstart = wd + i + 1;  // Otherwise just leave it where it is.
      continue;
    }
    return 0;  // Not a house number.
  }
  housenum = strtol((char *)housenumstart, NULL, 10);
  if (0) printf("is_street_number(%s) -> %d\n", wdin, housenum);
  return housenum;
}


int remove_suite_number(int wds, byte **words) {
  // Look for the word "suite" (words are case folded) in the list of words.
  // ... similarly for "unit", "apt", and "apartment"
  // If found, and it's not the last word, remove it and the word following.
  int r, w;
  for (r = 0; r < wds - 1; r++) {
    if (!strcmp((char *)words[r], "suite")
	|| !strcmp((char *)words[r], "unit")
	|| !strcmp((char *)words[r], "apt")
	|| !strcmp((char *)words[r], "apartment")
	) {
      for (w = r + 2; w < wds; w++) {
	words[r++] = words[w];
      }
      wds -= 2;
      break;
    }
  }
  return wds;
}

void strip_zips(int wds, byte **words) {
  // US ZIP codes are sometimes written in 5+4 format, e.g. 90210-3456, in queries
  // but not in the MAPS database.  Check words for this format and remove the hyphen
  // (replaced by a 'y') and trailing digits.
  int r, i;
  byte *w;
  BOOL could_be;
  if (0) printf("strip_zips(%d words)\n", wds);
  for (r = 0; r < wds; r++) {
    w = words[r];
    could_be = TRUE;
    for (i = 0; i < 5; i++) {
      if (!isdigit(*w)) {
	could_be = FALSE;
	break;
      }
      w++;
    }
    if (could_be && *w == 'y' && isdigit(*(w + 1))) {
      *w = 0;  // Strip the hyphen and what's left
      if (0) printf("ZIP5+4 --> '%s'\n", words[r]);
      return;
    }
  }
}



int remove_and_return_street_number(int wds, byte **words,  int *street_number, BOOL unit_number_first) {
  // the first all-numeric word is assumed to be (or to contain) the street number. It is removed
  // and it's numeric value is returned via the third parameter.  Also allow for the
  // possibility that the word is number/number.  The third parameter allows us to get the
  // street number as either the first number in a number/number pair or the second.
  //
  // Note that we allow for a number starting with '#'
  int r, w, sn;
  *street_number = -1;
  for (r = 0; r < wds; r++) {
    if ((sn = is_street_number(words[r], unit_number_first))) {
      *street_number = sn;
      for (w = r + 1; w < wds; w++) words[r++] = words[w];
      wds --;
      break;
    }
  }
  return wds;
}


void geo_candidate_generation_query(int wds, byte **words, byte *querybuf) {
  // Write wds into querybuf -- guaranteed not to overflow.
  int w;
  byte *rp, *wdp;
  
  rp = querybuf;
  for (w = 0; w < wds; w++) {
    if (w > 0) *rp++ = ' ';
    wdp = words[w];
    while (*wdp) *rp++ = *wdp++;
  }
  *rp = 0;
}


int process_street_address(u_char *query, BOOL unit_number_first) {
  // In-place modification of the query.  Remove stuff like 'unit 10' or 'suite 4', then
  // remove and return the street number
  byte *word_starts[50];  // 50 should be way more than any address query.
  int wds, street_number = -1;
  byte *r;

  // Have to do a bit of mumbo jumbo here because we need to preserve e.g. 1/450
  // as a single word, but in QBASHER / is a word breaker.  So we replace
  // <digit>/<digit> with <digit>z<digit> and take care of that later.
  // The same applies with 5+4 digit zip codes, such as 90210-3456 where the
  // hyphen is a word-breaker.  In this case we substitute a y.  This could also
  // happen for a range of street numbers, e.g. 4-12 Doonkuna St

  r = query + 1;
  while (*r) {
    if (isdigit(*(r - 1)) && isdigit(*(r + 1))) {
      if (*r == '/') *r = 'z';
      else if (*r == '-') *r = 'y';
    }
    r++;
  }
  
  
  wds = utf8_split_line_into_null_terminated_words(query, word_starts,
						   50, MAX_WD_LEN,
						   TRUE,  FALSE, FALSE, FALSE);
  wds = remove_suite_number(wds, word_starts);
  strip_zips(wds, word_starts);
  wds = remove_and_return_street_number(wds, word_starts,  &street_number, unit_number_first);
    
  geo_candidate_generation_query(wds, word_starts, query);  // Overwrite the original query

  return street_number;

}




///////////////////////////  Checking street number validity //////////////////////////

BOOL street_number_valid_for_this_street(int street_number, char *street_number_specs) {
  // Check whether street_number is matched by one of the specifications in the comma-separated spec list
  // Each spec in the list is either a single number (e.g. 57), a one-step range (e.g. 1:40, meaning every
  // integer between 1 and 40 is valid, or a two-step range (e.g. 1-39, meaning all the odd numbers in that
  // range or 2-40, meaning even numbers in the range.

  char *ss = street_number_specs, *se, *sd;
  int spectype, lo, hi;
  int desired_remainder2;

  if (0) printf("Checking for validity of %d in '%s'\n", street_number, street_number_specs);

  if (street_number <= 0  || street_number_specs == NULL) return FALSE;

  while (*ss) {  // Loop over specs
    spectype = 0;  // 0 - single number
    se = ss;
    while (*se && *se != ',') {
      if (*se == ':') spectype = 1;
      else if (*se == '-') spectype = 2;
      se++;
    }

    // Check the spec
    if (spectype == 0) {
      lo = strtol(ss, NULL, 10);
      if (0) printf("Checking %d == %d\n", street_number, lo);
      if (street_number == lo) return TRUE;
    } else if (spectype == 1) {
      lo = strtol(ss, &sd, 10);
      hi = strtol(sd + 1, NULL, 10);
      if (street_number >= lo && street_number <= hi) return TRUE;
    } else if (spectype == 2) {
      lo = strtol(ss, &sd, 10);
      desired_remainder2 = lo % 2;
      hi = strtol(sd + 1, NULL, 10);
      if (street_number % 2 == desired_remainder2 &&
	  street_number >= lo && street_number <= hi) return TRUE;
    } 

    // Move on to the next spec
    if (*se) ss = se + 1;
    else ss = se;
  }

  return FALSE;
}

BOOL check_street_number(byte *doc, int f, int street_number) {
  // Given a record from the .forward file, extract the f-th field,
  // numbering from one, and expect it to be the spec list for
  // a street.  Check the validity of street_number
  byte *spec_list;
  BOOL outcome = FALSE;
  size_t spec_list_len;

  if (doc == NULL) return FALSE;
  
  spec_list = extract_field_from_record(doc, f, &spec_list_len);  // Returns empty string if no such field.
  if (spec_list == NULL) return FALSE;  // Only NULL if malloc() failed
  if (*spec_list != 0)
    outcome = street_number_valid_for_this_street(street_number, (char *)spec_list);
  free(spec_list);
  return outcome;
}



static int one_test(int num, char *specs, BOOL desired_answer) {
  BOOL rslt = street_number_valid_for_this_street(num, specs);
  
  if (rslt && desired_answer) {
    printf("[OK] yes, yes - %d in %s\n", num, specs);
    return 0;
  }  else if (!rslt && !desired_answer) {
    printf("[OK] no, no - %d in %s\n", num, specs);
    return 0;
  } else if (rslt && !desired_answer) {
    printf("[FAIL] yes, no - %d in %s\n", num, specs);
    return 1;
  } else {
    printf("[FAIL] no, yes - %d in %s\n", num, specs);
    return 1;
  }
}



void check_street_number_validity( ) {
  int errs = 0;
  errs += one_test(57, "57", TRUE);
  errs += one_test(57, "10, 11, 92, 57, 8", TRUE);
  errs += one_test(58, "10, 11, 92, 57, 8", FALSE);
  errs += one_test(57, "2-60", FALSE);
  errs += one_test(57, "2:60", TRUE);
  errs += one_test(58, "2-60", TRUE);
  errs += one_test(58, "1-3,10:19,7-49,10-600,97,101:119", TRUE);
  errs += one_test(599, "1-3,10:19,7-49,10-600,97,101:119", FALSE);

  printf("Street number validity tests completed.  %d errors\n", errs);
  if (errs) exit(1);
}
