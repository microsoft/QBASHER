// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// Definitions for a simple, dynamically expandable one-d array mechanism
//
// The said dynamic arrays are stored in contiguous malloced memory
// and consist of an 8-byte element count (C) and an 8-byte element 
// size (S) followed by SC bytes.
//

typedef unsigned char byte;
typedef byte *dyna_t;

typedef long long  dyna_growth_t;

#define DYNA_HEADER_LEN (2 * sizeof(long long))

#define DYNA_DOUBLE 0  // Grow by doubling
#define DYNA_ROOT2 -1   // Grow by multiplying by sqrt(2)
#define DYNA_MIN -2     // Grow by the minimum necessary to satisfy a request


// Create an array of count elements, each of size syz and zero them.
// Return a pointer to the dynamic array or NULL
dyna_t dyna_create(long long count, long long syz);

// Check whether da already defines an element elt_num.  If not, increase
// the size of the array using the prefered mechanism.  If howgrow > 0
// that number of elements will be 
int dyna_store(dyna_t *da, long long elt_num, void *value, size_t valsize,
		 dyna_growth_t howgrow);

// Return a pointer to the elt_num th element of da, growing the array if necessary.
void *dyna_get(dyna_t *da, long long elt_num, dyna_growth_t howgrow);
