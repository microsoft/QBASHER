#! /usr/bin/perl -w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

$|++;
# Take a file of queries one per line and output a file of multi queries (MQS)
# in which query variants are separated by RS characters. Single word queries
# are output as is.  For queries with multiple words, the first query
# in an MQS is the input query in quotes, the second is the original query,
# and the third is the original query with the last word dropped.

while (<>) {
    chomp;
    s /^\s+//; # Strip leading whitespace
    s /\s+$//; # Strip trailing whitespace
    # query \t options \t weight \t post_query_test RS
    if (/ /) {
	# multi-word query
	print "\"$_\"\t\t1.0\tN<5\036$_\t\t0.1";
	# Strip off last word
	s/ [^ ]+$//;
	print "\tN<3\036$_\t\t0.01\n";
    } else {
	print "$_\n";  # Just a single word
    }
}

exit(0);
