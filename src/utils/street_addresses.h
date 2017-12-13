// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.


#define BSIZE 2000
#define MAXWDS 1000
#define MAXWDBYTES 15

int is_street_number(byte *wd, BOOL unit_number_first);

int remove_suite_number(int wds, byte **words);

void strip_zips(int wds, byte **words);

int remove_and_return_street_number(int wds, byte **words,  int *street_number,
				    BOOL unit_number_first);

void geo_candidate_generation_query(int wds, byte **words, byte *querybuf);

int process_street_address(u_char *query, BOOL unit_number_first);

BOOL street_number_valid_for_this_street(int street_number, char *street_number_specs);

BOOL check_street_number(byte *doc, int field_number, int street_number);

void check_street_number_validity( );
