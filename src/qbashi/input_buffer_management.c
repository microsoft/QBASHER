// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <math.h>

#include "../shared/utility_nodeps.h"
#include "../shared/QBASHER_common_definitions.h"
#include "QBASHI.h"
#include "input_buffer_management.h"

#if defined(WIN64) && !defined(QBASHER_LITE) // None of the code in this module is compiled for the LITE version or in non-windows

buffer_queue_t *create_a_buffer_queue(int number_of_buffers, size_t buffer_size){
	int b;
	buffer_queue_t *bq;
	
	if (0) printf("create_a_bq(%d, %I64d) sizeof(bq) = %I64d\n", number_of_buffers, buffer_size, sizeof(buffer_queue_t));

	bq = (buffer_queue_t *)malloc(sizeof(buffer_queue_t));
	if (bq == NULL) error_exit("malloc failed for buffer_queue in create_a_buffer_queue()\n");
	bq->queue_depth = number_of_buffers;
	bq->buffer_size = buffer_size;
	bq->buf2empty = 0;
	bq->buf2fill = 0;
	bq->emptyingptr = NULL;

	bq->buffer_state = (int *)malloc(number_of_buffers * sizeof(int));
	if (bq->buffer_state == NULL) error_exit("malloc failed for buffer_state in create_a_buffer_queue()\n");
	bq->bytes_in_buffer = (size_t *)malloc(number_of_buffers * sizeof(size_t));
	if (bq->bytes_in_buffer == NULL) error_exit("malloc failed for bq->bytes_in_buffer in create_a_buffer_queue()\n");
	bq->buffers = (byte **)malloc(number_of_buffers * sizeof(byte *));
	if (bq->buffers == NULL) error_exit("malloc failed for bq->buffers in create_a_buffer_queue()\n");

	for (b = 0; b < number_of_buffers; b++) {
		// Allocate each buffer
		bq->buffers[b] = (byte *)malloc(buffer_size);
		bq->buffer_state[b] = BUF_EMPTY;  // empty state
		bq->bytes_in_buffer[b] = 0;
	}

	bq->buffer_control_mutex = CreateMutex(NULL, FALSE, NULL);  // To synchronise alteration of BQ metadata.  Initially not owned.
	if (bq->buffer_control_mutex == NULL) error_exit("Fatal Error: Can't create bq->buffer_control_mutex\n");   // OK - this happens once at start-up

	return bq;
}


void destroy_buffer_queue(buffer_queue_t **bqp) {
	int b;
	buffer_queue_t *bq = *bqp;

	if (0) printf("About to destroy buffer queue!\n");

	for (b = 0; b < bq->queue_depth; b++) free(bq->buffers[b]);
	free(bq->buffer_state);
	free(bq->bytes_in_buffer);
	free(bq->buffers);
	CloseHandle(bq->buffer_control_mutex);
	CloseHandle(bq->infile);
	free(*bqp);
	if (0) printf("Buffer queue destroyed!\n");
}




int WINAPI fill_buffers(LPVOID vbq) {
	// This function will be called once only for each infile and will continue to loop until EOF is reached on
	// that file.
	// 
	//                                  *** It runs in its own thread. ***
	//
	// Each iteration of the loop:
	//	 a. Waits until the buffer it is supposed to fill is in Empty state.
	//   b. Sets Filling state
	//   c. Calls ReadFile() to fill it.
	//		  i. Exits on error
	//       ii. Sets Full state if not EOF
	//	    iii. Sets EOF state and returns if EOF
	//   d. Advances buf2read around the ring of buffers
	//
	buffer_queue_t *bq = (buffer_queue_t *)vbq;
	int b;
	BOOL ok;

	while (1) {
		acquire_lock_on_state_of(bq, (byte *)"fill_buffers A");
		if (0) printf("Checking buffer %d\n", bq->buf2fill);
		//Check state of the buffer to fill
		b = bq->buf2fill;
		if (bq->buffer_state[b] != BUF_EMPTY) {
			release_lock_on_state_of(bq, (byte *)"fill_buffers A");
			// Check whether 
			if (bq->buffer_state[b] == BUF_ABORT_READING) break;  // ---------------------------->
			Sleep(10);  // Delay is in milliseconds
			continue;  // The buffer we're supposed to use is not empty.
		}
		bq->buffer_state[b] = BUF_FILLING;  // Filling
		if (0) printf("FILLING\n");
		release_lock_on_state_of(bq, (byte *)"fill_buffers A");

		if (0) printf("About to read\n");
		ok = ReadFile(bq->infile, bq->buffers[b], (DWORD)bq->buffer_size, (LPDWORD)&(bq->bytes_in_buffer[b]), NULL);
		if (!ok) {
			printf("Error code: %d\n", GetLastError());
			error_exit("I/O error A in fill_buffers()");    // OK - probably better to fail if we've only indexed part of the data
		}

		if (0) printf("FILLER: Bytes read into buffer %d: %I64d\n", b, bq->bytes_in_buffer[b]);

		acquire_lock_on_state_of(bq, (byte *)"fill_buffers B");

		//Change state of the buffer just filled
		b = bq->buf2fill;
		//  How to test for EOF on synchronous reads:  http://msdn.microsoft.com/en-us/library/aa365690%28VS.85%29.aspx
		if (ok && bq->bytes_in_buffer[b] == 0) {
			bq->buffer_state[b] = BUF_EOF;  // EOF
			release_lock_on_state_of(bq, (byte *)"fill_buffers B");
			break;  // --------------------------------------------------------------------->
		}

		bq->buffer_state[b] = BUF_FULL;  // Full
		bq->buf2fill = (b + 1) % bq->queue_depth;  // Move to the next buffer in the ring and carry on.
		if (0) printf("buffer %d set to FULL.  buf2fill is now %d\n", b, bq->buf2fill);
		release_lock_on_state_of(bq, (byte *)"fill_buffers B");	
	}  // end of loop indefinitely

	// Only get here on EOF or ABORT_READING
	if (0) printf("FILLER: Reached EOF or Reading aborted.\n");
	return 0;
}


void acquire_lock_on_state_of(buffer_queue_t *bq, byte *msg){
	// Get exclusive control of the Mutex for this file
	DWORD code;
	if (0) printf("acquire %s\n", msg);
	while ((code = WaitForSingleObject(bq->buffer_control_mutex, 1L)) == WAIT_TIMEOUT);   // The timeout is in milliseconds
	if (code != WAIT_OBJECT_0) {
		fprintf(stderr, "Error code %u from WaitForSingleObject() %s\n", code, msg);
		error_exit("Error waiting for Mutex.\n");
	}
	if (0)printf("ACQD %s\n", msg);
}

void release_lock_on_state_of(buffer_queue_t *bq, byte *msg) {
	BOOL ok;
	DWORD code;

	if (0) printf("Releasing %s\n", msg);
	ok = ReleaseMutex(bq->buffer_control_mutex);
	code = GetLastError();
	if (!ok) {
		fprintf(stderr, "Failure %ufrom ReleaseMutex %s\n", code, msg);
		error_exit("Error in ReleaseMutex.\n");
	}
}

#endif
