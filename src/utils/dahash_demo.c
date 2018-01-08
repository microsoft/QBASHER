// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdio.h>
#include <stdlib.h>


#include "../shared/utility_nodeps.h"   // Needed for some type definitions and Large Page malloc and free lp_blah
#include "dahash.h"

// Demonstration program for the dahash hash table system
// You only need three basic functions: dahash_create(), dahash_lookup() and dahash_destroy()
// Table size is always a power of 2.  You set the number of bits.  E.g. 10 bits gives a table of
// 1024 entries.  If the table gets above a threshold percentage full (say 90%), the size is automatically
// doubled.
// Keys are of fixed maximum length which you specify at creation time
// Values are also of fixed maximum length  which you specify at creation time.  You can store
// single scalars, structs, pointers or whatever you like. 




int main(int argc, char**argv) {
   dahash_table_t *demo_hash;
   int *value_ptr;
  // Create a hash table called Demo with an initial theoretical maximum size of 1024 (10 bits), whose keys 
  // are strings of max length 20 bytes and whose values are ints.
   demo_hash =  dahash_create((u_char *)"Demo", 10, 20, sizeof(int), 0.90, TRUE); 
 // Insert a bunch of items
 value_ptr = (int *)dahash_lookup(demo_hash, (u_char *)"orange", 1);   // 1 means add the key if it's not already there.
(*value_ptr)++;
 value_ptr = (int *)dahash_lookup(demo_hash, (u_char *)"apple", 1);   
(*value_ptr)++;
 value_ptr = (int *)dahash_lookup(demo_hash, (u_char *)"pear", 1);   
(*value_ptr)++;
 value_ptr = (int *)dahash_lookup(demo_hash, (u_char *)"orange", 1);   
(*value_ptr)++;
 value_ptr = (int *)dahash_lookup(demo_hash, (u_char *)"banana", 1);   
(*value_ptr)++;
 value_ptr = (int *)dahash_lookup(demo_hash, (u_char *)"orange", 1);   
(*value_ptr)++;

// How many oranges have we seen?
 value_ptr = (int *)dahash_lookup(demo_hash, (u_char *)"orange", 0);   // Return NULL if not present
if (value_ptr != NULL) printf("Oranges: %d\n", *value_ptr);
else printf("Oranges: none\n");

// Finished
dahash_destroy(&demo_hash);
}
