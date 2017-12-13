// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// A QBASHER module containing the get_index_path() function which takes either an index directory name
// or an index name and returns a path to the index directory

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <string.h>

#include "../shared/utility_nodeps.h"
#include "index_finder.h"

static char *known_ixroots[] = {
  "C:/Users/dahawkin/GIT/Qbasher/QBASHER/indexes/",  // Dave Laptop
  "D:/dahawkin/GIT/Qbasher/QBASHER/indexes/",  // Redsar
  "S:/dahawkin/GIT/Qbasher/QBASHER/indexes/",  // StCMHeavyMetal
  "F:/dahawkin/GIT/Qbasher/QBASHER/indexes/",  // CCPRelSci00

  // add as many as you like here, but note that the length of the whole string is limited!

  "",
};

u_char *get_index_path(u_char *arg) {
  // always returns a copy of the path in new storage.
  if (is_a_directory((char *)arg)) {
    if (! exists((char *) arg, "/QBASH.forward")) {
      printf("Warning: get_index_path(%s) - arg is a directory, but there's no QBASH.forward\n", arg);
    }
    return make_a_copy_of(arg);
  } else {
    char *pathbuf = NULL;
    int i;
    size_t len;
    pathbuf = (char *)cmalloc(IX_PATH_MAX + 1, (u_char *)"get_index_path()", FALSE);
    for (i = 0; (len = strlen(known_ixroots[i])) > 0; i++) {   
      strcpy(pathbuf, known_ixroots[i]);
      strcpy(pathbuf + len, (char *)arg);
      printf(" ... trying %s\n", pathbuf);
      if (is_a_directory((char *)pathbuf)) {
	if (! exists((char *)pathbuf, "/QBASH.forward")) {
	      printf("Warning: get_index_path(%s) - arg is a directory, but there's no QBASH.forward\n", pathbuf);
	}
	return (u_char *)pathbuf;
      }
    }
    printf("Warning: get_index_path(%s) - arg is neither a directory, nor the name of a known index\n", arg);
   
    return NULL;  // Can't find a suitable directory
  }
 }
