#! /usr/bin/perl -w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

# Read in a set of queries and expand it to include all the variants as the
# user typed characters after the completion of the first word.
# E.g. "donald trump" -> "donald trump", "donald trum", "donald tru",
# "donald tr", "donald t", "donald "

while (<>) {
    chomp;
    if (/(.*? )(.*)/) {
	# split on first space
	$w1 = $1; $rest = $2;
	while ($rest) {
	    print $w1, $rest, "\n";
	    $rest =~ s/.$//;
	}
    } else {
	# Only one one word
	print $_, "\n";
    }
}

#print STDERR "Don't forget to run these queries with -auto_partials=TRUE\n";

exit(0);

