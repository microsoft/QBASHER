#! /usr/bin/perl -w

use List::Util qw(shuffle);

die "Usage: $0 number_of_queries max_query_length vocab_words\n"
    unless $#ARGV == 2;

die "Can't write to test_queries.q\n"
    unless open T, ">test_queries.q";

for ($q = 0; $q < $ARGV[0]; $q++) { # Loop through the queries
    $qlength = int(rand($ARGV[1])) + 1;
    for ($w = 0; $w < $qlength; $w++) {
	print T " " if $w > 0;
	$t = int(rand($ARGV[2]));
	print T $t;
    }
    print T "\n";
}

close T;

print "@ARGV", "\ntest_queries.q written.\n";

exit(0);
