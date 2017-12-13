#! /usr/bin/perl -w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

# While finding QBASHER test queries for a new data set, this script can
# help by finding words or word-pairs which are repeated within the same
# document, and also for finding words and bigrams which occur frequently
# enough but not too frequently.

$r1 = "repeated_words_with_context.txt";
die "Can't open $r1\n" unless open W, ">$r1";
$r2 = "repeated_bigrams_with_context.txt";
die "Can't open $r1\n" unless open B, ">$r2";

$lions = 0;
while (<>) {
    s/[\r\n]+//;
    s/&\w+;//g;
    s/\W+/ /g;
    s/\s+/ /g;
    if (/\b(\w{4,})\b.*\b\g{1}\b/) {
	print W "$1 ($_)\n";
	$repeated_words{lc $1}++;
    }
    if ((/\b(\w+ \w+)\b.*\b\g{1}\b/)&& ($1 ne "of the")) {
	print B "\"$1\" ($_)\n" ;
	$repeated_bigrams{lc $1}++;
    }
    @words = split / +/;
    foreach $w (@words) {$all_words{lc $w}++;}
    print "Lines: $lions\n" if (++$lions % 100000 == 0);
}

close(W);
close(B);

$o1 = "repeated_bigrams_by_freq.tsv";
die "Can't open $o1\n" unless open W, ">$o1";
foreach $k (sort {$repeated_bigrams{$b} <=> $repeated_bigrams{$a}} keys %repeated_bigrams) {
    print W "$k\t$repeated_bigrams{$k}\n";
}
close(W);

$o2 = "repeated_words_by_freq.tsv";
die "Can't open $o2\n" unless open W, ">$o2";
foreach $k (sort {$repeated_words{$b} <=> $repeated_words{$a}} keys %repeated_words) {
    print W "$k\t$repeated_words{$k}\n";
}
close(W);

$o3 = "medfreq_words_by_freq.tsv";
die "Can't open $o3\n" unless open W, ">$o3";
foreach $k (sort {$all_words{$b} <=> $all_words{$a}} keys %all_words) {
    print W "$k\t$all_words{$k}\n"
	unless $all_words{$k} > 50 || $all_words{$k} < 2;
}
close(W);

print "Have a look at the following files: $r1, $r2, $o1, $o2, $o3\n";

exit(0);
