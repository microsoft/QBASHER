// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// This module provides functions for dealing with command-line arguments in
// attribute=value format.  The functions expect the user to provide
// an array of arg_t structures.  (arg_t is defined in argParser.h)
// The end of the array is signaled by an element like:
//    	{ "", AEOL, NULL, "" }
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>

#include "../definitions.h"
#include "arg_parser.h"


void print_args(FILE *F, format_t f, arg_t *args) {
  int a;
  u_char dflt_txt[MAX_EXPLANATIONLEN + 1];
  if (f == HTML) {
    fprintf(F, "<html>\n<h1>QBASHQ arguments</h1>\n"
	   "<table border=\"1\">\n"
	   "<tr><th>Argument</th><th>Default</th><th>Explanation</th></tr>\n");
  }
  else if (f == TSV) {
    fprintf(F, "Argument\tDefault\tExplanation\n");
  }
  else fprintf(F, "\n\n--------------------------------------------------------------------------\n"
	      "%24s - %11s - %s\n"
	      "--------------------------------------------------------------------------\n",
	      "Argument", "Default", "Explanation");
  for (a = 0; args[a].type != AEOL; a++) {
    switch (args[a].type) {
    case ASTRING:
      if (*(u_char *)(args[a].valueptr) == 0) strcpy((char *)dflt_txt, "None");
      else strncpy((char *)dflt_txt, *(char **)(args[a].valueptr), MAX_EXPLANATIONLEN);
      dflt_txt[MAX_EXPLANATIONLEN] = 0;
      break;
    case ABOOL:
      if (*(BOOL *)args[a].valueptr) strcpy((char *)dflt_txt, "TRUE");
      else strcpy((char *)dflt_txt, "FALSE");
      break;
    case AINT:
      sprintf((char *)dflt_txt, "%d", *(int *)args[a].valueptr);
      break;
    case AINTLL:
      sprintf((char *)dflt_txt, "%lld", *(long long *)args[a].valueptr);
      break;
    case AFLOAT:
      sprintf((char *)dflt_txt, "%.3g", *(double *)args[a].valueptr);
      break;
    default:   // Actually an error, but just ignore it.
      break;
    }

    if (f == HTML) {
      fprintf(F, "<tr><td>%s</td><td>%s</td><td>%s</td></tr>\n", args[a].attr, dflt_txt, args[a].explan);
    }
    else if (f == TSV) {
      fprintf(F, "%s\t%s\t%s\n", args[a].attr, dflt_txt, args[a].explan);
    }
    else fprintf(F, "%24s - %11s - %s\n",
		args[a].attr, dflt_txt, args[a].explan);
  }
  if (f == HTML) {
    fprintf(F, "</table>\n</html>\n");
  }
  else if (f == TEXT) {
    fprintf(F, "---------------------------------------------------------------------------\n");
  }

}


int store_arg_values(u_char *buffer, arg_t *args, size_t buflen,
		     BOOL show_experimentals) {
  // Store values of all parameters in the form of arg=value, one per line in the buffer.
  // Unless show_experimentals don't include arguments beginning with x_
  // Return the number of bytes stored.   
  // If buflen is too small, back off by 11 bytes and write "TRUNCATED"
  int a;
  u_char *one_arg = NULL, *w = buffer;
  size_t needed, left = buflen - 1, l, available = MAX_VALSTRING + 1;

  if (buflen < 400) return -1;   // Buffer too small 
  if (available > buflen / 3) available = buflen / 3;
  one_arg = (u_char *)malloc(available + 1);
  if (one_arg == NULL)  return -2;   // Malloc failed.

  for (a = 0; args[a].type != AEOL; a++) {
    if (!show_experimentals && !strncmp((char *)args[a].attr, "x_", 2)) continue;
    sprintf((char *)one_arg, "%s=", args[a].attr);
    l = strlen((char *)one_arg);
    switch (args[a].type) {
    case ASTRING:
      // This is the only one which could overflow, make sure to use strncpy
      if (*(u_char *)(args[a].valueptr) == 0) strcpy((char *)one_arg + l, "None");
      else strncpy((char *)one_arg + l, *(char **)(args[a].valueptr), available - l - 1);
      break;
    case ABOOL:
      if (*(BOOL *)args[a].valueptr) strcpy((char *)one_arg + l, "TRUE");
      else strcpy((char *)one_arg + l, "FALSE");
      break;
    case AINT:
      sprintf((char *)one_arg + l, "%d", *(int *)args[a].valueptr);
      break;
    case AINTLL:
      sprintf((char *)one_arg + l, "%lld", *(long long *)args[a].valueptr);
      break;
    case AFLOAT:
      sprintf((char *)one_arg + l, "%.3g", *(double *)args[a].valueptr);
      break;
    default:
      break;  // Impossible, but needed to stop compiler whinging
    }

    needed = strlen((char *)one_arg);
    one_arg[needed++] = '\n';
    if (needed > left) {
      strcpy((char *)(w - 11), "TRUNCATED\n");
      free(one_arg);
      return (int)(w - buffer);
    }
    else {
      strcpy((char *)w, (char *)one_arg);
      w[needed] = 0;
      w += needed;
      left -= needed;
    }
  }
  free(one_arg);
  return (int)(w - buffer);
}


int assign_one_arg(char *arg_equals_val, arg_t *args, char **next_arg) {
  // Given an input string in the form key=value, look up key in the args table and assign 
  // the value to the appropriate variable. 
  // Note that the input string may not be null terminated.  A value is terminated
  // by '&' as well.
  // Return 1 iff an assignment successfully occurred. Otherwise zero or negative
  // If no error was detected, next_arg is set to point to the character after the
  // end of the value.
  int a, argnum = -1;
  size_t vallen;
  char *p, *q, saveq, *t;
  double dval;
  long long llval;
  int ival;
  *next_arg = NULL;  // Cover all the error returns.
  if (0) printf("Trying to assign %s\n", arg_equals_val);
  if (arg_equals_val == NULL) return -1;  // ------------------------------->
  p = arg_equals_val;
  while (*arg_equals_val == '-') arg_equals_val++;  // Skip over leading hyphens
  while (*p && *p != '=' && *p != '&') p++;
  if (*p != '=') return -2; // --------------------------------------->
  q = p + 1;
  while (*q && *q != '&') q++;
  vallen = q - p;
  *p = 0;  // Temporarily null terminate to facilitate look-up
  for (a = 0; args[a].type != AEOL; a++) {
    if (!strcmp((char *)arg_equals_val, (char *)args[a].attr)) {
      argnum = a;
      break;
    }
  }
  *p = '=';  // Put back what we disturbed
  if (argnum < 0) {
    printf("Error: Argument '%s' not matched.\n", arg_equals_val);
    exit(1); // ---------------------------------------> XXX
  }
  // Now assign the value.
  saveq = *q;
  *q = 0;  // after temporarily making sure it's null terminated.
  if (0) printf("Found!   Arg %d, value='%s'\n", argnum, p + 1);
  switch (args[argnum].type) {
  case ASTRING:
    if (vallen > MAX_VALSTRING) vallen = MAX_VALSTRING;
    t = (char *)malloc(vallen + 1);
    if (t == NULL) return -3; // --------------------------------------->
    strncpy((char *)t, (char *)p + 1, vallen);
    t[vallen] = 0;
    *(char **)(args[a].valueptr) = t;
    break;
  case ABOOL:
    if (!strcmp((char *)p + 1, "true")
	|| !strcmp((char *)p + 1, "on")
	|| !strcmp((char *)p + 1, "allowed")
	|| !strcmp((char *)p + 1, "yes")
	|| !strcmp((char *)p + 1, "TRUE")
	|| !strcmp((char *)p + 1, "ON")
	|| !strcmp((char *)p + 1, "ALLOWED")
	|| !strcmp((char *)p + 1, "YES")
	|| !strcmp((char *)p + 1, "1")
	) *(BOOL *)(args[a].valueptr) = TRUE;
    else *(BOOL *)(args[a].valueptr) = FALSE;
    break;
  case AINT:
    errno = 0;
    ival = strtol((char *)p + 1, NULL, 10);
    if (errno) {
      *q = saveq;
      return -4; // --------------------------------------->
    }
    *(int *)(args[a].valueptr) = ival;
    break;
  case AINTLL:
    errno = 0;
    llval = strtoll((char *)p + 1, NULL, 10);
    if (errno) {
      *q = saveq;
      return -4; // --------------------------------------->
    }
    *(long long *)(args[a].valueptr) = llval;
    break;
  case AFLOAT:
    errno = 0;
    dval = strtod((char *)p + 1, NULL);
    if (errno) {
      *q = saveq;
      return -5; // --------------------------------------->
    }
    *(double *)(args[a].valueptr) = dval;
    break;
  default:   // Actually an error, but just ignore it.
    break;
  }
  *q = saveq;
  if (*q) *next_arg = (q + 1);
  return 1;
}
