// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// dahash - function definitions  - Written by developer1, Bing, Dec 2013 

// A dahash hashtable consists of a control block (an instance of dahash_table_t) which 
// records all the properties of the table, and includes a link to a block of memory
// representing the hashtable proper:
//   1. The number of entries in the hashtable is a power of two.   The number is set
//      by the bits parameter to dahash_create_table()
//   2. Each entry consists of a fixed-length key and a fixed-length value
//      a. the numbers of bytes in the key and the value are determined in the
//         call to dahash_create_table()
//      b. the key is a null-terminated string which are assumed to be in UTF-8 encoding.
//      c. if an attempt is made to insert a key which is longer than the maximum defined
//         for the table, the key will be truncated back to the end of a UTF-8 character.
//         (Fixed length truncation would sometimes result in a malformed UTF-8 character.)
//      d. It is not possible to insert an empty key into the table.  A zero first byte
//         in the key part of an entry signals that the entry is unused.
//   3. This type of hashtable has no buckets.  If a collision occurs at the initially calculated
//      hash index (h) for a key, the method of 'relatively prime rehash' is used to find 
//      an available spot.  A constant r which is relatively prime to the table size is 
//      added to h, modulo the table size (t), to give a new value of h.  This process is repeated
//      using the same r until an empty spot is found.  Because of the relatively prime property
//      every element of the table is reachable.  Since the table size is a power of two, 
//      any odd number is relatively prime.  Therefore we can set r = h|1
//   4. The control block records how many of the available entries have been used.  Once the
//      table becomes full to an extent where long collision chains are likely, an internal 
//      function is called to double the size of the table:
//      a. memory is allocated for a new hashtable of twice the size
//      b. each item in the old table is hashed into the new one
//      c. pointers are switched
//      d. the old memory is freed
//
//      NOTE: In general, when the eventual table size is known, it is better to create 
//      upfront a table of the necessary size.  Otherwise, at the time of the last doubling,
//      memory usage peaks at 50% greater than would have been the case if the final size
//      had been initially allocated, because of the need to simultaneously allocate both 
//      old and new tables.  Also, the cost of rehashing during doubling would be avoided.
//
//      THREAD SAFETY:  The functions defined in this module use no non-local variables,
//      hence parallel threads using them will not interfere with each other, unless
//      concurrent lookup/insert operations are performed on the same hashtable.  If 
//      it is necessary to support such operations, it will be necessary to introduce
//      per-hashtable locks.  For maximum efficiency, the locking mechanism for a particular
//      hash table would have the following properties:
//      a. multiple concurrent lookups allowed
//      b. lookups blocked until insertions and doublings completed
//      c. Things are complicated because insert is not a pure insert but rather 
//         lookup-and-if-necessary-insert.  
//          i. lookup-and-if-necessary-insert can start even if lookups are active, but not
//             if another lookup-and-if-necessary-insert or doubling is active
//         ii. Once lookup-and-if-necessary-insert has decided that insert is needed, it should 
//             acquire an exclusive lock (no other lookups, no other inserts, no doubling) before proceeding.
//             It's OK to assume that nothing has changed since the lookup which identified the
//             need for insertion, because of (i).
//     d. Doubling requires an exclusive lock (no lookups, no other inserts, no other doubling)
//
//     ERROR HANDLING:  This code is written for a demo.  No fault tolerance or error recovery 
//     is required.  Hence an error exit is taken whenever any error condition is detected.


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ctype.h"
#include "sys/types.h"

#include "../shared/utility_nodeps.h"   // Needed for some type definitions and Large Page malloc and free lp_blah
#include "../imported/Fowler-Noll-Vo-hash/fnv.h"
#include "dahash.h"

dahash_table_t *dahash_create(u_char *name, int bits, size_t key_len, size_t val_size,
			      double max_full_frac) {
  // Create and return the control block for a hash table.  Allocate and clear the memory for
  // its power of two table.
  size_t table_ents;
  size_t entsize = key_len + 1 + val_size;
  dahash_table_t *ht = NULL;
  if (bits < 2 || bits > 40) {
    printf("Error: dahash_create(): Table size must be between 4 entries (2 bits) and 1 trillion (40 bits), but bits = %d\n",
	   bits);
    exit(1);
  }
  // Allocate the control block
  ht = (dahash_table_t *)malloc(sizeof(dahash_table_t));  // Freed by dahash_destroy()  Too small for lp_
  if (ht == NULL)	{
    printf("Error: dahash_create(): Failed to malloc da_hash_table_t\n");
    exit(1);
  }

  ht->name = name;
  ht->bits = bits;
  table_ents = 1ULL << bits;
  ht->capacity = table_ents;
  ht->key_size = key_len + 1;  // For the trailing NULL
  ht->val_size = val_size;
  ht->entry_size = ht->key_size + ht->val_size;
  ht->entries_used = 0;
  ht->times_doubled = 0;
  ht->collisions = 0;
	
  if (max_full_frac < 0.01 || max_full_frac > 0.99) {
    printf("Error: dahash_create(): max_full_frac was %f but should lie between 0.01 and 0.99\n", max_full_frac);
    exit(1);
  }
  ht->max_full_frac = max_full_frac;

  // Allocate the power of two table
  ht->table = lp_malloc(entsize * table_ents, FALSE, 0);    // Freed by dahash_destroy()
  if (ht->table == NULL)	{
    printf("Error: dahash_table(): Failed to malloc ht->table\n");
    exit(1);
  }
  memset(ht->table, 0, entsize * table_ents);
  printf("Hash table %s created. (Bits = %d.) Memory allocated: %zu * %zu = %.1fMB\n",
	 name, ht->bits, table_ents, entsize, (double)(entsize * table_ents) /1048576.0);
  return ht;
}


void dahash_destroy(dahash_table_t **ht) {
  // Free memory associated with *ht and set *ht to NULL.
  if (*ht == NULL) return;
  if ((*ht)->table != NULL) lp_free((*ht)->table, FALSE);
  free(*ht);
  *ht = NULL;
}



static unsigned long long dahash(byte *key) {
  unsigned long long fnv_hash = 0;
  if (key == NULL) {
    printf("Error: dahash(): attempt to insert null key into table\n");
    exit(1);
  }
  fnv_hash = fnv_64a_str((char *)key, FNV1A_64_INIT);

  return fnv_hash;  
}



static void dahash_double(dahash_table_t *ht) {
  // double the capacity of ht, and hash the old entries into the new
  size_t table_ents, e, old_capacity;
  void *old_table, *new_table;
  long long idx_off;

  if (ht == NULL) {
    printf("Error: dahash_double(): Attempt to double non-existent table\n");
    exit(1);
  }

  ht->bits++;
  if (ht->bits > 40) {
    printf("Error: dahash_double(): Table size must be between 4 entries (2 bits) and 1 trillion (40 bits), but bits = %d\n",
	   ht->bits);
    exit(1);
  }

  // Try to malloc double memory
  table_ents = 1ULL << ht->bits;
  old_capacity = ht->capacity;
  ht->capacity = table_ents;
  new_table = (void *)lp_malloc(ht->entry_size * table_ents, FALSE, 0);
  // Old table freed later in this function
  if (new_table == NULL)	{
    printf("Error: dahash_table(): Failed to malloc ht->table\n");
    exit(1);
  }

  memset(new_table, 0, ht->entry_size * table_ents);
  // Switch in the new table and keep a pointer to the old one
  // Future: Obtain exclusive lock on ht.
  old_table = ht->table;
  ht->table = new_table;
  // Rehash all the items in the original table into the bigger one
  idx_off = 0;
  ht->entries_used = 0;
  for (e = 0; e < old_capacity; e++) {
    if (((byte *)old_table)[idx_off])  {
      // If first byte of key is non-null, this slot is unused
      byte *val;
      val = (byte *)dahash_lookup(ht, (byte *)old_table + idx_off, 1);
      memcpy(val, (byte *)old_table + idx_off + ht->key_size, ht->val_size);
    }
    idx_off += ht->entry_size;
  }
  lp_free(old_table, FALSE);
  ht->times_doubled++;
  printf("Dahash: Hash table capacity doubled to %zu entries.  Used: %zu\n",
	 ht->capacity, ht->entries_used);
}


static int probing_method = 0;  // 0 - means relatively prime rehash,  1 - means linear

void dahash_set_probing_method(int method) {
  probing_method = method;
}



void *dahash_lookup(dahash_table_t *ht, byte *key, int insert_flag) {
  // Lookup key in ht.
  // If found, 
  //   return a pointer to the first byte of the value part of the entry.
  // If not found and insert_flag is not set, 
  //   return NULL
  // Otherwise, 
  //   insert it and return a pointer to the empty value part.
  //
  // NOTE: Key must be NULL terminated.  
  // NOTE: If key is longer than allowed by ht, it will be truncated
  unsigned long long index, rehash_step;
  long long index_off;
  size_t kl;
  if (*key == 0) {
    printf("Error: dahash_lookup(): Attempt to lookup empty key.\n");
    return NULL;
  }
  if (ht == NULL) {
    printf("Error: dahash_lookup(): attempt to lookup key %s in NULL table.\n", key);
    return NULL;
  }
  kl = strlen((char *)key);
  
  if (kl > (ht->key_size - 1))  {
    // Truncate overlong key taking care not to garble UTF-8 characters
    kl = ht->key_size - 1;
    // If the two most significant (MS) bits of the byte we want to null out are 10
    // then this is a UTF-8 continuation byte and we must go further back to 
    // find the UTF-8 start byte (MS bits == 11). If the MS bits are 00 or 01 then 
    // the character is a single byte.
    while ((key[kl] & 0xC0) == 0x80 && kl) kl--;
    if (kl == 0) {
      printf("Error: dahash_lookup(): Attempt to lookup key which became empty after UTF-8 truncation.\n");
      return NULL;
    }
    key[kl] = 0;
  }
  index = (dahash(key) % ht->capacity);
  if (probing_method != 0) rehash_step = 1;  //  linear probing
  else rehash_step = index | 1;   // Ensure that rehash_step is odd and therefore relatively
  // prime to the power-of-two table size.   

  index_off = index * ht->entry_size;
  // Skip over collisions using relatively prime rehash (RPR)
  while (((byte *)(ht->table))[index_off] != 0
	 && strcmp((char *)key, ((char *)ht->table) + index_off)) {
    index = (index + rehash_step) % ht->capacity;
    index_off = (long long)index * (long long)ht->entry_size;
    ht->collisions++;
  }

  if (((byte *)(ht->table))[index_off] != 0) {
    // It's a hit.
    return(((byte *)(ht->table)) + index_off + ht->key_size);    // ------------------------------------>
  }
  else {
    // The key at this location is empty.  
    if (insert_flag) {
      // Future: wait for exclusive lockind
      strcpy((char *)(ht->table) + index_off, (char *)key);
      ht->entries_used++;
      // Future: release exclusive lock.
      if (0) printf("  ---> Check need to double for '%s'. %zu / %zu cf %f\n.",
		    (u_char *)key, ht->entries_used, ht->capacity, ht->max_full_frac);
      if ((double)(ht->entries_used) / (double)(ht->capacity) > ht->max_full_frac) {
	if (0) printf("  ---> Need to double for '%s'. %zu / %zu cf %f\n.",
		      (u_char *)key, ht->entries_used, ht->capacity, ht->max_full_frac);
	dahash_double(ht);
	// After doubling, the index for the just-added key has changed
	return dahash_lookup(ht, key, 0); // find it again.
      }
      return(((byte *)(ht->table)) + index_off + ht->key_size);    // ------------------------------------>
    }
    else {
      return NULL;
    }
  }
}


static int compare_keys_alphabetic(const void *i, const void*j) {
  // Alphabetically compare two null-terminated strings 
  char *ia = *(char **)i, *ja = *(char **)j;
  return strcmp(ia, ja);
}


void dahash_dump_alphabetic(dahash_table_t *ht, u_char *ll_heap, void(dump_key)(const void *),
			    void(dump_val)(const void *, u_char *)) {
  // Sort the keys stored in ht into alphabetic order, then for each entry
  // in the sorted list call dump_key() on the key and dump_val() on the
  // value.
  // 
  // By supplying appropriate dump functions this function can be used 
  // to print the hash table for debugging or informational purposes, or 
  // perhaps for other purposes


  unsigned long long e, p;
  byte **permute = NULL;
  long long idx_off;
  if (ht == NULL) {
    printf("Error: dahash_dump_alphabetic(): attempt to dump NULL table.\n");
    exit(1);
  }
  permute = (byte **)malloc(ht->entries_used * sizeof(byte *)); 
  if (permute == NULL) {
    printf("Malloc(%zu) failed in dahash_dump_alphabetic()\n",
	   ht->entries_used * sizeof(byte *));
    exit(1);
  }
  // permute is freed at the end of this function.
  idx_off = 0;
  p = 0;
  for (e = 0; e < ht->capacity; e++) {
    if (((byte *)(ht->table))[idx_off]) {
      permute[p++] = ((byte *)(ht->table)) + idx_off;
    }
    idx_off += ht->entry_size;
  }

  qsort(permute, p, sizeof(byte *), compare_keys_alphabetic);
  // NOTE: Could save a bit of memory using qsort_r() and defining permute
  // as an array of integers rather than an array of pointers.

  for (e = 0; e < p; e++) {
    dump_key(((byte *)permute[e]));
    dump_val(((byte *)permute[e]) + ht->key_size, ll_heap);
  }
  free(permute);
}


