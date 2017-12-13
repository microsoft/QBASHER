// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.


#define PREFERRED_MAX_BLOCK 2000


// definitions of functions to do I/O with large buffers

void buffered_flush(CROSS_PLATFORM_FILE_HANDLE wh, byte **buffer, size_t *bytes_in_buffer, char *label, BOOL cleanup);

void buffered_write(CROSS_PLATFORM_FILE_HANDLE wh, byte **buffer, size_t buffer_size, size_t *bytes_in_buffer,
	byte *data, size_t bytes2write, char *label);


double write_inverted_file(dahash_table_t *vht, u_char *vocab_fname, u_char *if_fname, doh_t ll_heap,
	u_int SB_POSTINGS_PER_RUN, u_int SB_TRIGGER, docnum_t doccount, long long fsz, u_ll max_plist_len);
