// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#ifndef WIN64
#define WINAPI 
typedef void *LPVOID;
#endif
#define BUF_EMPTY 0
#define BUF_FILLING 1
#define BUF_FULL 2
#define BUF_EOF 3
#define BUF_ABORT_READING 4

typedef struct {
	// These items are set on creation and never changed.
	HANDLE buffer_control_mutex;
	HANDLE infile;
	int queue_depth;
	size_t buffer_size;
	byte **buffers;

	// These items are solely used by the emptier and can be used by it without getting a lock
	// (unless, in future, there are multiple emptier threads).
	int buf2empty;
	byte *emptyingptr;  //  emptying ptr maintains state across successive calls to get_line

	// These items are solely modified by the filler and can be used by it without getting a lock
	int buf2fill;
	size_t *bytes_in_buffer;  

	// This array of items potentially needs to be locked before access because both
	// filler and emptier read and modify it.
	int *buffer_state;    // 0 - Empty; 1 - Filling;  2 - Full; 3 - EOF
}  buffer_queue_t;


buffer_queue_t *create_a_buffer_queue(int number_of_buffers, size_t buffer_size);

void destroy_buffer_queue(buffer_queue_t **bqp);

int WINAPI fill_buffers(LPVOID vbq);

void acquire_lock_on_state_of(buffer_queue_t *bq, byte *msg);

void release_lock_on_state_of(buffer_queue_t *bq, byte *msg);
