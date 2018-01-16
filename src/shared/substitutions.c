// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// This version of the QBASHER substitution rules (post 1.5.125) now supports multiple simultaneously-
// loaded language-specific rulesets.   Each ruleset is defined by an object of type rule_set_t.  That
// object contains a count of how many rules there are for this language and a set of three parallel
// arrays (compiled pattern, RHS string, flag of whether RHS contains operators), each of count dimension.
//
// The rules are read from a TSV file whose records must have three fields:
//    <LHS> TAB <RHS> TAB <language code>
//
// where LHS is a Perl-compatible regex, RHS is the replacement string, in which $1, $2, etc.
// refer to the first, second and so on capturing sub-patterns in LHS.  
// 
// Language is specified by a two ASCII-character lower-case ISO 639:1 code, e.g. en, fr, zh.
// As at October 2015 there were 184 such codes registered.  If the language specified in the
// QBASHQ -language= option is given in upper case it will be converted to lower case.  If the value
// does not comprise two ASCII letters, a warning will be issued and the option will be ignored.
//
// An example record might be:
//
//          ^(\w+)\W+(\w+)$ TAB $2 $1 TAB en
//
// It specifies that a subject string consisting of two words separated by arbitrary non-word characters
// should be replaced by the two words in reversed order, separated by a single space.  The substitution
// should only be applied if the current language code is en, i.e. English.
//
// Rule sets for languages are accessed via a hash table. Values in the hash table are objects of
// type lang_specific_rules_t.   These structs contain a count of how many rules there are for this
// language and a pointer to a rule_set_t object.  When loading the substitution rules there are
// two passes:  The first creates a hash table entry for each language encountered and maintains the
// count of rules for that language.  At the end of the pass, the rule_set objects are instantiated,
// and the trio of arrays are allocated for each of them.  In the second pass, the entries in the
// arrays are filled in (including calling pcre_compile() for the LHS of the rules).
//
// The dynamic hash system dahash is used so that little space and time is wasted in the most common
// case where there are few languages in simultaneous use, while accommodating an arbitrarily large
// number of languages.  If a rule file contains entries for each of the maximum possible
// number of two-letter combinations (26^2 = 676) then that will be handled seamlessly, though
// performance may suffer.
//
// Comic relief: Two men walked into a bar.  You'd think one of them would have seen it.

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
#include <Psapi.h>
#else
#include <errno.h>
#endif

#include "unicode.h"
#include "../shared/QBASHER_common_definitions.h"
#include "../shared/utility_nodeps.h"
#define PCRE2_CODE_UNIT_WIDTH 8
#include "../imported/pcre2/pcre2.h"
#include "../utils/dahash.h"

#include "substitutions.h"


#define MAX_DIRLEN 1000
#define SLASH '/'   // Change if it needs to be '\\'


void unload_substitution_rules(dahash_table_t **substitutions_hash, int debug) {
  dahash_table_t *sash = *substitutions_hash;
  off_t table_off;
  lang_specific_rules_t *lsr;
  rule_set_t *rs;
  byte *hep;
  int rule, e;
  BOOL explain = (debug >= 1);
 
  table_off = 0;
  for (e = 0; e < sash->capacity; e++) {
    byte *table = (byte *)(sash->table);
    hep = table + table_off;
    if (*hep)  {
      // If first byte of key is NUL, this slot is unused
      lsr = (lang_specific_rules_t *)(hep + sash->key_size);  // key_size includes terminating NUL
      // Destroy the rule_set object and all its components;
      rs = lsr->rule_set;
      if (rs->substitution_rules_regex != NULL) {
	for (rule = 0; rule < rs->num_substitution_rules; rule++) 
	  if ((rs->substitution_rules_regex)[rule] != NULL) free((rs->substitution_rules_regex)[rule]);
	free(rs->substitution_rules_regex);
	rs->substitution_rules_regex = NULL;
      }

      if (rs->substitution_rules_rhs != NULL) {
	for (rule = 0; rule < rs->num_substitution_rules; rule++)
	  if ((rs->substitution_rules_rhs)[rule] != NULL) free((rs->substitution_rules_rhs)[rule]);
	free(rs->substitution_rules_rhs);
	rs->substitution_rules_rhs = NULL;
      }
      if (rs->substitution_rules_rhs_has_operator != NULL) {
	free(rs->substitution_rules_rhs_has_operator);
	rs->substitution_rules_rhs_has_operator = NULL;
      }
      if (explain) printf("Destroyed arrays for %d %s rules.\n",
				  rs->num_substitution_rules, hep);
      free(rs);
      lsr->rule_set = NULL;     
      lsr->num_substitution_rules = 0;
    }
    table_off += sash->entry_size;
  }

  // Finally, destroy the hashtable.
  dahash_destroy(substitutions_hash);
}


static void create_arrays_for_rule_set(rule_set_t *rs, int num_rules) {
  // Malloc the memory for the arrays representing the rules in this rule_set.
  // Use of cmalloc causes any memory allocation failure to cause an error exit.
  int rule;
  
  rs->substitution_rules_regex =
    (pcre2_code **)cmalloc((num_rules + 1) * sizeof(pcre2_code *), (u_char *)"LHS", FALSE);

  rs->substitution_rules_rhs =
    (u_char **)cmalloc((num_rules + 1) * sizeof(u_char *), (u_char *)"RHS", FALSE);

  rs->substitution_rules_rhs_has_operator =
    (u_char *)cmalloc((num_rules + 1) * sizeof(u_char), (u_char *)"has_operator", FALSE);

  for (rule = 0; rule < num_rules; rule++) {
    rs->substitution_rules_regex[rule] = NULL;
    rs->substitution_rules_rhs[rule] = NULL;
    rs->substitution_rules_rhs_has_operator[rule] = 0;
  }
}



int load_substitution_rules(u_char *srfname, dahash_table_t **substitutions_hash, int debug) {
  // Attempt to load the srfname file.
  // If not found, return 0, otherwise expect to find lines of the form <LHS> TAB <RHS> TAB <language code>
  // in the file and return a count of the rules found, or a negative error code
  lang_specific_rules_t *lsr;
  dahash_table_t *sash = NULL;
  u_char *rulesfile_in_mem, *p, *line_start = NULL, *rhs_start = NULL, *current_field = NULL,
    lang_code[3];
  off_t table_off;
  byte *hep;  // Hash entry pointer
  size_t rulesfile_size, patlen = 0, rhslen = 0, error_offset;
  CROSS_PLATFORM_FILE_HANDLE H;
  HANDLE MH;
  int error_code = 0, lncnt = 0, fldcnt, rule, rules_with_operators_in_RHS = 0, e;
  BOOL explain = (debug >= 1);

  if (srfname == NULL) return 0; // ----------------------------------------------------->
  
  if (!exists((char *)srfname, "")) {
    if (explain) printf("load_substitution_rules() - file %s not found\n", srfname);
    return 0;  // ----------------------------------------------------->
  }
  if (explain) printf("Loading substitution_rules from %s\n", srfname);
  fflush(stdout);
  rulesfile_in_mem = (u_char *)mmap_all_of(srfname, &rulesfile_size, FALSE, &H, &MH, &error_code);
  if (explain) printf("Loaded substitution_rules from %s.  Error code is %d\n", srfname, error_code);
  fflush(stdout);


  // Create a dynamic hash table with initially 8 entries and with a key length of 2 bytes
  // (The terminating null is allowed for.
  *substitutions_hash = dahash_create((u_char *)"Substitutions", 3, 2,
				      sizeof(lang_specific_rules_t), 0.90, FALSE);
  sash = *substitutions_hash;

  // ---------------------------------------------------------------------------------------
  // Count the lines in the substitution_rules file corresponding to each distinct language.
  // ---------------------------------------------------------------------------------------
  p = rulesfile_in_mem;
  current_field = p;
  line_start = p;
  fldcnt = 1;
  while (p < rulesfile_in_mem + rulesfile_size) {
    if (*p == '\t') {
      fldcnt++;
      current_field = p + 1;
    } else if (*p == '#' || *p == '\n') {    // \n works for both Unix and Windows line termination.
      if (*p == '#') {
	// The rest of this line is a comment.  Skip forward to '\n' or EOF and process as end of line
	while (p < rulesfile_in_mem + rulesfile_size && *p != '\n') p++;
      }
      if (fldcnt == 3) {
	int rslt;
	if (0) {
	  printf("Validating: ");
	  show_string_upto_nator(current_field, '\n', 0);
	}

	// Must copy to writeable memory before calling v_and_n
	rslt = -1;  // Assume the worst
	if (current_field != NULL && current_field[0] != 0) {
	  lang_code[0] = current_field[0];
	  lang_code[1] = current_field[1];
	  lang_code[2] = 0;
	
	  rslt = validate_and_normalise_language_code(lang_code);
	}
	if (rslt == 0) {
	  // Lookup lang_code in the substitutions_hash.
	  lsr = (lang_specific_rules_t *)dahash_lookup(sash, lang_code, 1);  // 1 means add the key if it's not already there.
	  if (lsr == NULL) {
	    printf("Error: dahash_lookup failed for %s\n", lang_code);  // Shouldn't ever happen.
	    return -1;  // --------------------- Need to make up an error code --------------->
	  }
	  lsr->num_substitution_rules++;   // This will have been zeroed if it's a newly created entry
	  lncnt++;  
	} else {
	  // This was not recognized as a valid ISO 639:1 language code.  Ignore the record.
	  if (explain) printf(" .. validation failed.\n");
	}
      } else {
	// There are the wrong number of fields in this rule - Ignore the record.
	// (Don't complain about comment lines or blank lines)
	if (explain && fldcnt > 1) {
	  printf(" .. wrong number of fields %d in rule %d:\n", fldcnt, lncnt);
	  show_string_upto_nator(line_start, '\n', 0);
	  printf("\n");
	}
      }
      fldcnt = 1;
      line_start = p + 1;
    }
    p++;
  } // end of while

  if (explain) printf("  Counted %d probably-valid rules. Possibly some are empty.\n", lncnt);

  // ------------------------------------------------------------------------------------------------
  // Now run through the hash table and deal with non empty entries.  I.e. languages which have rules
  // ------------------------------------------------------------------------------------------------
  table_off = 0;
  for (e = 0; e < sash->capacity; e++) {
    byte *table = (byte *)(sash->table);
    hep = table + table_off;
    if (*hep)  {
      // If first byte of key is null, this slot is unused
      lsr = (lang_specific_rules_t *)(hep + sash->key_size);  // key_size includes terminating NUL
      // Create the new rule_set object
      lsr->rule_set = (rule_set_t *)cmalloc(sizeof(rule_set_t), (u_char *)"rule_set", FALSE);
      lsr->rule_set->num_substitution_rules = 0;
      create_arrays_for_rule_set(lsr->rule_set, lsr->num_substitution_rules);
      if (explain) printf("Created arrays for %d %s rules.\n",
				  lsr->num_substitution_rules, hep);
    }
    table_off += sash->entry_size;
  }

  //----------------------------------------------------------------------------------------
  // Now scan the rules file again and set up the LHS, RHS and has_operator array entries.
  //----------------------------------------------------------------------------------------
  //
  // We have to scan the whole line before doing anything because the language is
  // in field 3.

  if (explain) printf("Scanning the rules again..\n");
  
  p = rulesfile_in_mem;
  current_field = p;
  line_start = p;
  fldcnt = 1;
  while (p < rulesfile_in_mem + rulesfile_size) {
    if (*p == '\t') {
      fldcnt++;
      current_field = p + 1;
      if (fldcnt == 2) {
	rhs_start = current_field;
	patlen = p - line_start;
      } else if (fldcnt == 3) {
	rhslen = p - rhs_start;
      }
    } else if (*p == '#' || *p == '\n') {    // \n works for both Unix and Windows line termination.
      if (*p == '#') {
	// The rest of this line is a comment.  Skip forward to '\n' or EOF and process as end of line
	while (p < rulesfile_in_mem + rulesfile_size && *p != '\n') p++;
      }
       if (fldcnt == 3) {
	// We're at the end of line.
	int rslt;
	// Must copy to writeable memory before calling v_and_n
	rslt = -1;  // Assume the worst
	if (current_field != NULL && current_field[0] != 0) {
	  lang_code[0] = current_field[0];
	  lang_code[1] = current_field[1];
	  lang_code[2] = 0;
	
	  rslt = validate_and_normalise_language_code(lang_code);
	}

	if (rslt == 0) {
	  // Lookup lang_code in the substitutions_hash.
	  lsr = (lang_specific_rules_t *)dahash_lookup(sash, lang_code, 0);  // 0 means don't add the key
	  if (lsr != NULL) {
	    rule = lsr->rule_set->num_substitution_rules;
	    lsr->rule_set->num_substitution_rules++;

	    // Here's where all the action happens.  The relevant rule set is lsr->rule_set
	    if (explain ) {
	      printf("%03d LHS(%s, %zd): '", rule, lang_code, patlen);
	      putchars(line_start, patlen);
	      printf("' -->  ");
	      // Line will be completed by RHS below.
	    }

	    lsr->rule_set->substitution_rules_regex[rule] = pcre2_compile(line_start, patlen, PCRE2_UTF|PCRE2_CASELESS,
									  &error_code, &error_offset, NULL);
	    // Will be NULL if compilation failed.
	    if (lsr->rule_set->substitution_rules_regex[rule] == NULL) {
	      u_char errbuf[200];
	      pcre2_get_error_message(error_code, errbuf, 199);
	      if (explain) printf("Compile failed for rule starting with %s.  Error_code: %d: %s\n",
				     line_start, error_code, errbuf);
	    }

	    lsr->rule_set->substitution_rules_rhs[rule] = cmalloc(rhslen + 1, (u_char *)"RHS", FALSE);
	    utf8_lowering_ncopy(lsr->rule_set->substitution_rules_rhs[rule], rhs_start, rhslen);
			
	    lsr->rule_set->substitution_rules_rhs[rule][rhslen] = 0;

	    if (strchr((char *)lsr->rule_set->substitution_rules_rhs[rule], '[') != NULL
		|| strchr((char *)lsr->rule_set->substitution_rules_rhs[rule], '"') != NULL) {
	      lsr->rule_set->substitution_rules_rhs_has_operator[rule] = 1;
	      rules_with_operators_in_RHS++;
	    } else lsr->rule_set->substitution_rules_rhs_has_operator[rule] = 0;
	    if (explain) printf("RHS: '%s'\n", lsr->rule_set->substitution_rules_rhs[rule]);
	  }  // end of if (lsr != NULL) 
	} else {
	  // This was not recognized as a valid ISO 639:1 language code.  Ignore the record.
	}
      } else {
	// There are the wrong number of fields in this rule - Ignore the record.
      }
      fldcnt = 1;
      line_start = p + 1;
    }  // end of else if (*p == '\n')
    p++;  // Skip the newline
  }  // --- end of while (p < rulesfile in memory)

  // Unload the memory-mapped file
  unmmap_all_of(rulesfile_in_mem, H, MH, rulesfile_size);

  if (error_code < 0) {
    printf("Error code is %d\n", error_code);
    unload_substitution_rules(substitutions_hash, debug);
    return error_code;
  }


  if (debug >= 1) printf("Substitution rules loaded: %d.  (Possibly some are empty.)\n", lncnt);
  fflush(stdout);
  return lncnt;
}
  

#define INITIAL_SUBJECT_LEN_LIMIT 256  // If an input subject is longer than this no substitutions will occur.
#define MAX_SUBLINE MAX_RESULT_LEN  // This should be significantly larger than INITIAL_SUBJECT_LEN_LIMIT to allow for growth due to substitutions.

int apply_substitutions_rules_to_string(dahash_table_t *sash, u_char *language,
					u_char *intext, BOOL avoid_operators_in_subject,
					BOOL avoid_operators_in_rule, int debug) {

  // First task is to find the rule set which applies to this language, ... if any
  // Refuse to make substitutions if subject contains a '['
  // Attempt to apply ALL rules in file order, even if substitutions are made.  Two buffers are used,
  // referenced by sin and sout.  Substitutions are always attempted from sin to sout, and if a
  // substitution occurs sin and sout are swapped.
  // (intext is first copied into buf1 which starts of as sin, with sout referencing buf2

  lang_specific_rules_t *lsr;
  rule_set_t *rs;
  int rule, num_subs, rules_matched = 0;
  u_char buf1[MAX_SUBLINE + 2], buf2[MAX_SUBLINE + 2], *sin = buf1, *sout = buf2, *t, *r, *w;
  size_t buflen, l;
  pcre2_match_data *p2md;
  BOOL explain = (debug >= 1);

  if (sash == NULL || language == NULL || language[0] == 0) return 0;  // -------------------------------R>

  lsr = (lang_specific_rules_t *)dahash_lookup(sash, language, 0);  // 0 means don't add the key if it's not already there.
  if (lsr == NULL)  return 0;  // ----------------------------------------------------------------------R>
  rs = lsr->rule_set;

  if (rs == NULL
      || rs->num_substitution_rules == 0
      || rs->substitution_rules_regex == NULL
      || rs->substitution_rules_rhs == NULL) return 0;          // -----------------------------------------R>

  // copy intext to buf1 (up to MAX_SUBLINE characters)
  r = intext;
  w = buf1;
  if (avoid_operators_in_subject) {
    // This bit added to assist with substitution rules in Local search autocomplete where
    // user query may be prefixed with geotile expressions such as [x$5 x$7]
    t = (u_char *)strrchr((char *)intext, ']');  // Find last occurrence of a square bracket
    if (t != NULL) {
      // Copy over everything up to and including the last square bracket
      // and then do the substitutions after that.
      while (r <= t) *w++ = *r++;
      // r ends up pointing to the character beyond the last square bracket
    }
  }
  l = 0;  // Impose the length limit only on the part after ]	
  while (l <= INITIAL_SUBJECT_LEN_LIMIT && *r) {
    if (*r == '[' && avoid_operators_in_subject) return 0; // ------------------------------------------R>
		                            
    if (*r > 127 && *r < 160) {
      *w++ = ' ';  r++;   // Replace Windows-1252 punctuation chars with spaces.
    }
    else {
      *w++ = *r++;
    }
    l++;
  }
  if (*r) {
    if (explain) printf("Substitutions skipped due to length > %d\n", INITIAL_SUBJECT_LEN_LIMIT);
    return (0);    // We scanned INITIAL_SUBJECT_LEN_LIMIT bytes and didn't get to the end. Don't do any substitutions.
  }
  *w = 0;  

  p2md = pcre2_match_data_create(10, NULL);  // Allow for up to ten different capturing sub-patterns

  if (p2md == NULL) return 0;   // Maybe this should be an error?  

  if (explain)
    printf("apply_substitions_to_query_text(%s) called for language %s.  %d rules\n",
	   intext, language, rs->num_substitution_rules);


  // Try all the substitution rules
  for (rule = 0; rule < rs->num_substitution_rules; rule++) {
    if (debug >= 2) printf("Rule %d\n", rule);
    buflen = MAX_SUBLINE + 1;  // Have to reset this each time, as unsuccessful substitute calls reset it.
    //  buflen sets the size of the output of each substitution.
    
    if (avoid_operators_in_rule && rs->substitution_rules_rhs_has_operator[rule])
      continue; // ------------------------------C>
    if (rs->substitution_rules_regex[rule] == NULL || rs->substitution_rules_rhs[rule] == NULL) {
      if (0) printf("Left or right is NULL!\n");
      continue; // ------------------------------C>
    }
    if (debug >= 2) printf("Rule %d: RHS = '%s'.  Subject = %s\n", rule, rs->substitution_rules_rhs[rule], sin);

    num_subs = multisub(rs->substitution_rules_regex[rule], sin, strlen((char *)sin), 0,
			PCRE2_SUBSTITUTE_GLOBAL, p2md, NULL, rs->substitution_rules_rhs[rule],
			strlen((char *)(rs->substitution_rules_rhs[rule])), sout, &buflen);
    if (num_subs > 0) {
      if (debug >= 1) printf("Query substitution occurred: %s\n", sout);
      // Now switch in and out buffers
      t = sin;
      sin = sout;
      sout = t;
      rules_matched++;
    }
    else if (num_subs < 0 && debug >=1) {
      u_char errbuf[200];
      pcre2_get_error_message(num_subs, errbuf, 199);
      printf("Substitute failed for rule %d.  Error_code: %d: %s\n"
	     " - sin is %s, RHS is %s\n", rule, num_subs, errbuf, sin, rs->substitution_rules_rhs[rule]);
    }
  }

  if (rules_matched > 0) strcpy((char *)intext, (char *)sin);
  if (debug >= 1) printf("Rules matched: %d\n", rules_matched);
  pcre2_match_data_free(p2md);

  return rules_matched;
}


int multisub(const pcre2_code *regex, PCRE2_SPTR sin, PCRE2_SIZE sinlen, PCRE2_SIZE startoff, uint32_t opts,
	     pcre2_match_data *p2md, pcre2_match_context *p2mc, PCRE2_SPTR rep, PCRE2_SIZE replen, PCRE2_UCHAR *obuf, PCRE2_SIZE *obuflen) {
  // We know (or suspect) that sin contains an operator AND that the replacement string contains one too.
  // We don't want to apply the regex to sections of sin within an operator section. E.g. within [ ... ] or " ... "
  // Therefore we have to divide the string into operator and non-operator sections and apply or not 
  // apply the rules as we go.
  u_char *section_start = (u_char *)sin, *obufupto = obuf, *obuf_end = obuf + *obuflen, *q;
  PCRE2_SIZE seclen;
  int num_subs = 0;
  // Loop over the sections
  while (*section_start) {
    // Look for an operator
    q = section_start;
    while (*q && *q != '[' && *q != '"') q++;
    seclen = q - section_start;
    if (seclen > 0) {
      PCRE2_SIZE lobufleft = obuf_end - obufupto + 1;
      if (0) printf("About to substitute.  Inlen = %d,  Outlen = %d\n", (int)seclen, (int)lobufleft);
      num_subs += pcre2_substitute(regex, section_start, seclen, 0,
				   PCRE2_SUBSTITUTE_GLOBAL, p2md, p2mc, rep,
				   strlen((char *)rep), obufupto, &lobufleft);
      obufupto += lobufleft;
    }
    if (*q == 0) break;  // ------------------------------------------------->

    // *q isn't null so it must be an operator.  Skip to the matching closing operator, copying into obuf
    u_char closer = *q;
    if (closer == '[') closer = ']';
    while (*q && *q != closer && obufupto < obuf_end) {
      *obufupto++ = *q++;
    }
    if (obufupto >= obuf_end) break; // ------------------------------------------------->

    *obufupto++ = closer;

    // q now points either to the end of the string or to a closing operator
    if (*q == 0) break;  // ------------------------------------------------->
    q++;
    section_start = q;
  }

  *obufupto = 0;  // Null terminate
  return num_subs;
}


BOOL re_match(u_char *needle, u_char *haystack, int pcre2_options, int debug) {
  // Return TRUE iff there are no PCRE2 errors and there is a non-empty match
  // for needle in haystack.  If debug > 0, any PCRE2 errors will be explained
  int error_code, rc;
  size_t error_offset;
  pcre2_code *compiled_pat;
  pcre2_match_data *p2md;
  u_char error_text[201];

  if (debug >= 2) printf("re_match called with (%s, %s)\n", needle, haystack);
  
  pcre2_options |= PCRE2_UTF;  // Always UTF-8!
  compiled_pat = pcre2_compile(needle, strlen((char *)needle), pcre2_options, &error_code, &error_offset, NULL);
  if (compiled_pat == NULL) {
    printf("Error: pcre2_compile error %d\n", error_code);
    return FALSE;
  }

  p2md = pcre2_match_data_create_from_pattern(compiled_pat, NULL);
  rc = pcre2_match(compiled_pat, haystack, strlen((char *)haystack), 0,
		   PCRE2_NOTEMPTY, p2md, NULL);
  if (rc < 0) {
    switch(rc) {
    case PCRE2_ERROR_NOMATCH:
      break;
    default: pcre2_get_error_message(rc, error_text, 100);
      if (debug >= 1) printf("Matching error %d: %s\n", rc, error_text);
      break;
    }
    pcre2_match_data_free(p2md);   
    pcre2_code_free(compiled_pat);
    return FALSE;
  }
  pcre2_match_data_free(p2md);   
  pcre2_code_free(compiled_pat); 
  return TRUE;
}
