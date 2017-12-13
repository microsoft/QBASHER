// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.


void qsort_r(void *base, size_t nmemb, size_t size,
                  int (*compar)(const void *, const void *, void *),
                  void *arg);


void create_candidate_generation_query(query_processing_environment_t *qoenv,
				       book_keeping_for_one_query_t *qex);
