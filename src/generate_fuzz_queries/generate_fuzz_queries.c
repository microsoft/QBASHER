// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// Generate a specified number of lines (CRLF terminated) containing
// random ASCII bytes (7-bit), with the following special rules:
//   - NULs are not generated
//   - CRs are generated as per normal, as are other controls and RUBOUT
//   - If a LF is generated, CRLF is actually output
//   - Once the specified maximum linelength is reached, a CRLF is emitted


#include <stdio.h>
#include <stdlib.h>


int main(int argc, char **argv) {
  int seed, line_count = 0, lines_req, charset, line_len = 0, longest_line = 0,
    r, max_line;
  if (argc < 5) {
    fprintf(stderr,
	    "Usage: %s <random seed (u_short)>  <number of lines> <max_line_len> 0|1|2\n"
	    "   The third argument chooses ASCII (0), Windows 1252 (1), or UTF-8 (2).\n\n",
	    argv[0]);
    exit(0);
  }

  seed = abs(atoi(argv[1])) % (1 << 15);
  lines_req = abs(atoi(argv[2]));
  max_line = abs(atoi(argv[3]));
  charset = atoi(argv[4]);
  if (charset < 0 || charset > 2) {
    fprintf(stderr, "Error: charset must be 0|1|2 but was %d\n", charset);
    exit(1);
  }

  if (charset == 2) {
    fprintf(stderr, "Error: Charset 2 (UTF-8) not yet implemented. Sorry\n");
    exit(1);
  }
  srand(seed);
  while (line_count < lines_req) {
    r = rand();

    if (charset == 0) {
      // ASCII
      r = r % 127 + 1;
      if (r == '\n') {
	putchar('\r');
	putchar('\n');
	line_count++;
	if (line_len > longest_line)  longest_line = line_len;
      } else {
	putchar(r);
	line_len++;
	if (line_len > longest_line)  longest_line = line_len;
	if (line_len >= max_line) {
	  putchar('\r');
	  putchar('\n');
	  line_len = 0;
	  line_count++;
	}
      }
    } else if (charset == 1) {
      //code page 1252
      r = r % 255 + 1;
      if (r == '\n') {
	putchar('\r');
	putchar('\n');
	line_count++;
	if (line_len > longest_line)  longest_line = line_len;
      } else {
	putchar(r);
	line_len++;
 	if (line_len > longest_line)  longest_line = line_len;
	if (line_len >= max_line) {
	  putchar('\r');
	  putchar('\n');
	  line_len = 0;
	  line_count++;
	}
      }
    }
  }

  fprintf(stderr, "Lines generated: %d\nLongest line: %d\n\n",
	    line_count, longest_line);

}
