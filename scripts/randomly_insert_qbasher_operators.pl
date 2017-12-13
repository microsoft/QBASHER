#! /usr/bin/perl -w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

# Read in a set of queries assumed to be without operators and randomly insert
# phrase or disjunction operators.  If the input has less than two words, just
# pass it straight through

while (<>) {
    chomp;
    @w = split /\s+/;
    $num_words = $#w + 1;
    if ($num_words < 2) {
	print $_, "\n";
	next;
    }
    $first = int(rand($num_words - 1));  # Insert LH operator before word with this index
    $words_left = $num_words - $first;
    $len = 1 + int(rand($words_left - 1));
    $last = $first + $len;  #Insert RH operator after word with this index
    # Choose between phrase and disjunction
    $op = '[';
    $op = '"' if (rand(1) < 0.5);
    for ($i = 0; $i < $num_words; $i++) {
	print " " unless $i == 0;
	if ($i == $first) {
	    print "$op";
	    $op = ']' if $op eq '[';
	}
	print $w[$i];
	print $op if ($i == $last);
    }
    print "\n";
}

print STDERR "\n";

exit(0);

