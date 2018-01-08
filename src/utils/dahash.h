// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// dahash - header file  - Written by Developer1, Bing, Dec 2013

// A detailed description of the dahash code is in dahash.cpp 

// Definition of dahash_table_t, an control block for a dahash table

#if !defined(doh_t)
typedef u_char * doh_t;
#endif

typedef struct {
	u_char *name;    // The name of this table.
	void *table;   // A power of two.  Each entry contains key and value each of a fixed size
	// (key_size, val_size) established by the call to dahash_create_table;
	int bits;
	size_t capacity;  // Redundant for convenience == 2 ** bits
	size_t key_size;  // Does not include the terminating NULL
	size_t val_size;
	size_t entry_size;
	double max_full_frac;
	size_t entries_used;
	int times_doubled;
	unsigned long long collisions;
} dahash_table_t;

// Create a hash table
dahash_table_t *dahash_create(u_char *name, int bits, size_t key_len, size_t value_size,
			      double max_full_frac, BOOL verbose);

// Destroy a hash table
void dahash_destroy(dahash_table_t **ht);

// Lookup a key, insert it if necessary and return a pointer to the entry
void *dahash_lookup(dahash_table_t *ht, byte *key, int insert_flag);

// Dump out the (key, val) pairs in a dahash,alphabetically sorted by key and
// using a display_val() function passed as a parameter to display the value.
void dahash_dump_alphabetic(dahash_table_t *ht, doh_t ll_heap, void(dump_key)(const void *),
	void(dump_val)(const void *, doh_t));

void dahash_set_probing_method(int method);
