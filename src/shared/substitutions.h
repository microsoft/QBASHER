// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.


typedef struct {
  int num_substitution_rules;
  pcre2_code **substitution_rules_regex;
  u_char **substitution_rules_rhs;
  u_char *substitution_rules_rhs_has_operator;
} rule_set_t;


typedef struct {  // These are the values in the substitutions hash table
  int num_substitution_rules;
  rule_set_t *rule_set;
} lang_specific_rules_t;
  
  
void unload_substitution_rules(dahash_table_t **substitutions_hash, int debug);

int load_substitution_rules(u_char *srfname, u_char *index_dir, dahash_table_t **substitutions_hash, int debug);

int apply_substitutions_rules_to_string(dahash_table_t *sash, u_char *language,
					u_char *intext, BOOL avoid_operators_in_subject,
					BOOL avoid_operators_in_rule, int debug);

int multisub(const pcre2_code *regex, PCRE2_SPTR sin, PCRE2_SIZE sinlen, PCRE2_SIZE startoff, uint32_t opts,
	pcre2_match_data *p2md, pcre2_match_context *p2mc, PCRE2_SPTR rep, PCRE2_SIZE replen, PCRE2_UCHAR *obuf, PCRE2_SIZE *obuflen);

BOOL re_match(u_char *needle, u_char *haystack, int pcre2_options, int debug);
