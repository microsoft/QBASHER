// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

//

typedef struct {
  char *indexStem;
  int numDocs, k, lowScoreCutoff, postingsCountCutoff, debug;
} params_t;

extern params_t params;


