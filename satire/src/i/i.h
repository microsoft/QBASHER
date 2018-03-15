// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

//

typedef struct {
  char *inputFileName;
  char *outputStem;
  int numDocs;
  int lowScoreCutoff;
  int maxQuantisedValue;
} params_t;

extern params_t params;

