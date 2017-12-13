// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/types.h>
#include <ctype.h>
#include <string.h>
#include "../shared/utility_nodeps.h"
#include "dynamic_arrays.h"


// Please note that dyna_t is just a pointer to a single malloced block of memory
// (even after growing).  Bookkeeping information is held within the first
// DYNA_HEADER_LEN bytes of the array.  Hence there is no need for a
// dyna_free() function, you can just use free().  e.g.
//
//    dyna_t da;
//    da = dyna_create(100, sizeof(int));
//    dyna_store(&da, 1000, 99, sizeof(int), DYNA_DOUBLE);
//    ...
//    free(da);
//    da = NULL;



// Create an array of count elements, each of size syz and zero them.
// Return a pointer to the dynamic array or NULL
dyna_t dyna_create(long long count, long long syz) {
  dyna_t rslt;
  long long *llp;
  rslt = (dyna_t)malloc(count * syz + DYNA_HEADER_LEN);
  if (rslt == NULL) return NULL;
  llp = ((long long *)rslt);
  *(llp++) = count;
  *(llp++) = syz;
  memset((void *)llp, 0, (size_t)(syz * count));
  return rslt;
}


// Check whether da already defines an element elt_num.  If not, increase
// the size of the array usnign the prefered mechanism.  If howgrow > 0
// that number of elements will be 
int dyna_store(dyna_t *dap, long long elt_num, void *value, size_t valsize,
	       dyna_growth_t howgrow) {
  dyna_t da = *dap, nda = NULL, elp = NULL;
  long long new_elts = 0, elts = *(long long *)da, 
    syz = *(long long *)(da + sizeof(long long));
  if (elt_num >= elts ) {
    // Need to grow
    if (howgrow == DYNA_DOUBLE) 
      new_elts = 2 * elts;
    else if (howgrow == DYNA_ROOT2) 
      new_elts = (long long)round((double)elts * 1.414213562373095);
    else if (howgrow == DYNA_MIN)
      new_elts = elt_num + 1;
    else {
      new_elts = elt_num + howgrow;
    }
	// If the growth method doesn't take us far enough, grow to just the size requested.
	if (new_elts <= elt_num) new_elts = elt_num + 1;

    nda = dyna_create(new_elts, syz);
    if (nda == NULL) return -1;
    memcpy((void *)(nda + DYNA_HEADER_LEN), (void *)(da + DYNA_HEADER_LEN), elts * syz);
    free(da);
    *dap = nda;
    da = nda;
  }
  
  elp = da + DYNA_HEADER_LEN + elt_num * syz;
  memcpy(elp, value, valsize);
  return 0;
}

// Return a pointer to the elt_num th element of da or NULL if that element isn't
// defined.
void *dyna_get(dyna_t *dap, long long elt_num, dyna_growth_t howgrow) {
	dyna_t nda = NULL, da = *dap;
	long long elts = *(long long *)(da),
		syz = *(long long *)(da + sizeof(long long)),
		new_elts = 0;

	//if (0) printf("dyna_get called for elt %lld:  elts = %lld, syz = %lld\n", elt_num, elts, syz);
	if (elt_num >= elts) {
	  // Need to grow
	  if (howgrow == DYNA_DOUBLE)
		  new_elts = 2 * elts;
	  else if (howgrow == DYNA_ROOT2)
		  new_elts = (long long)round((double)elts * 1.414213562373095);
	  else if (howgrow == DYNA_MIN)
		  new_elts = elt_num + 1;
	  else {
		  new_elts = elt_num + howgrow;
	  }
	  // If the growth method doesn't take us far enough, grow to just the size requested.
	  if (new_elts <= elt_num) new_elts = elt_num + 1;
	  if (0) printf("Dyna_get() - increasing to %lld elements\n", new_elts);
	  nda = dyna_create(new_elts, syz);
	  if (nda == NULL) return NULL;
	  memcpy((void *)(nda + DYNA_HEADER_LEN), (void *)(da + DYNA_HEADER_LEN), elts * syz);
	  free(da);
	  *dap = nda;
	  da = nda;
	}
	return (void *)(da + DYNA_HEADER_LEN + elt_num * syz);
}
