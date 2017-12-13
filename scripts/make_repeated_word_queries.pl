#! /usr/bin/perl -w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

die "Can't open repeated_words_with_context.txt\n"
    unless open R, "repeated_words_with_context.txt";

while (<R>) {
    if (m@.*\((.*) [0-9]+\)@) {
	print $1, "\n";
    } else {
	print "Error: ", $_;
    }
}
close(R);

exit(0);
