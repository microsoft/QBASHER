// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// Table of command line argument definitions for the corpus
// property extractor.  The functions in argParser.c operate
// on this array.

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "../definitions.h"
#include "i.h"
#include "../u/arg_parser.h"
#include "iArgTable.h"

arg_t args[] = {
  { "inputFileName", ASTRING, (void *)&(params.inputFileName), "This is the file of text containing the T-D scores for each term, in TSV format. "},
  { "outputStem", ASTRING, (void *)&(params.outputStem), "This will be the stem of the index files produced."},
  { "numDocs", AINT, (void *)&(params.numDocs), "How many documents in the corpus."},
  { "lowScoreCutoff", AINT, (void *)&(params.lowScoreCutoff), "If a quantised T-D score is less than this, it will not be included inthe index."},
  { "maxQuantisedValue", AINT, (void *)&(params.maxQuantisedValue), "The floating point scores are multiplied by this and then floor()ed."},
  { "", AEOL, NULL, "" }
  };


void initialiseParams(params_t *params) {
  params->inputFileName = NULL;
  params->outputStem = NULL;
  params->numDocs = 0;
  params->lowScoreCutoff = 1;  // No point in indexing scores of zero
  params->maxQuantisedValue = 10000; 
}


void sanitiseParams(params_t *params) {
  // Any range checking needed?
  int max = (1 << (8 * BYTES_FOR_QSCORE)) - 1;
  if (params->maxQuantisedValue < 2 || params->maxQuantisedValue > max) {
    printf("Error: maxQuantisedValue must be at least 2 and at most %d\n", max);
    exit(1);
  }
}




