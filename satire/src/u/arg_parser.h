// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.


#define MAX_ARGLEN 25
#define MAX_VALSTRING 2048
#define MAX_EXPLANATIONLEN 199

typedef enum {
	ASTRING,
	ABOOL,
	AINT,
	AINTLL,
	AFLOAT,
	AEOL
} arg_t_t;

typedef struct{
	u_char attr[MAX_ARGLEN + 1];
	arg_t_t type;
	void *valueptr;
	u_char explan[MAX_EXPLANATIONLEN + 1];
} arg_t;


void print_args(FILE *F, format_t f, arg_t *args);

int store_arg_values(u_char *buffer, arg_t *args, size_t buflen, BOOL show_experimentals);

int assign_one_arg(char *arg_equals_val, arg_t *args, char **next_arg);
