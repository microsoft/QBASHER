// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// Table of command line argument definitions for the corpus
// property extractor.  The functions in argParser.c operate
// on this array.

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "../definitions.h"
#include "../u/arg_parser.h"
#include "q.h"
#include "qargTable.h"

arg_t args[] = {
  { "indexStem", ASTRING, (void *)&(params.indexStem), "This is the stem of the three files making up the SATIRE index to be used: <stem>.cfg, <stem>.vocab and <stem>.if."},
  { "numDocs", AINT, (void *)&(params.numDocs), "How many documents in the corpus."},
  { "k", AINT, (void *)&(params.k), "The number of ranked results required."},
  { "lowScoreCutoff", AINT, (void *)&(params.lowScoreCutoff), "An early termination mechanism (ETM).  Don't process any postings with scores below this value."},
  { "postingsCountCutoff", AINT, (void *)&(params.postingsCountCutoff), "Another ETM. Stop if the total number of postings processed exceeds this value. (Only checked at the end of a run, and if value > 0.)"},
  { "debug", AINT, (void *)&(params.debug), "Controls verbosity of debugging output."},
  { "", AEOL, NULL, "" }
  };


void initialiseParams(params_t *params) {
  params->indexStem = NULL;
  params->numDocs = 0;
  params->k = 10;
  params->lowScoreCutoff = 1;
  params->postingsCountCutoff = 0;
  params->debug = 0;
}


void sanitiseParams(params_t *params) {
  // Make sure the parameters make some kind of sense
}




