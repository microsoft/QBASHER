// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.


#define MAX_ARGLEN 30
#define MAX_VALSTRING 4096
#define MAX_EXPLANATIONLEN 199

typedef enum {
	ASTRING,
	ABOOL,
	AINT,
	AFLOAT,
	AEOL
} arg_t_t;


typedef struct{
	u_char attr[MAX_ARGLEN + 1];
	arg_t_t type;
	BOOL immutable;    // If TRUE, once set, this cannot be changed.
	double minval, maxval;  // Minimum and maximum allowed values for AINT and AFLOAT arguments.
	u_char explan[MAX_EXPLANATIONLEN + 1];
} arg_t;


int initialize_qoenv_mappings(query_processing_environment_t *qoenv);

void set_qoenv_defaults(query_processing_environment_t *qoenv);

int assign_args_from_config_file(query_processing_environment_t *qoenv, u_char *config_filename,
				 BOOL initializing, BOOL explain_errors);

