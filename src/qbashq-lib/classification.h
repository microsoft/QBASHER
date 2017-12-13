// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.


void classifier_validate_settings(query_processing_environment_t *qoenv, book_keeping_for_one_query_t *qex);

int apply_lyrics_specific_rules(char *qstring);

int apply_carousel_specific_rules(char *qstring);

int apply_magic_songs_specific_rules(char *qstring);

int apply_magic_movie_specific_rules(char *qstring);

int apply_academic_specific_rules(char *qstring);

int apply_wikipedia_specific_rules(char *qstring);

int apply_amazon_specific_rules(char *qstring);

double classification_score(query_processing_environment_t *qoenv, book_keeping_for_one_query_t *qex,
			    unsigned long long *dtent, u_char *doc_content, size_t dc_len,
			    int dwd_cnt, byte *match_flags, double *FV, u_int *terms_matched_bits);

void classifier(query_processing_environment_t *qoenv, book_keeping_for_one_query_t *qex,
		byte *forward, byte *doctable, size_t fsz, double score_multiplier);

double get_rectype_score_from_forward(u_ll *dtent, byte *forward, size_t fsz, int rectype_field);
