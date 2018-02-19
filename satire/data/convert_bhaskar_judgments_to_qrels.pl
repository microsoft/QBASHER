#! /usr/bin/perl -w
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

die "Can't open DINNER/qdr.txt\n"
    unless open I, "DINNER/qdr.txt";

die "Can't write to bhaskar.qrels\n"
    unless open Q, ">bhaskar.qrels";

$lyns = 0;

while (<I>) {
    chomp;
    @f = split /\t/;
    next if ($#f != 2);
    print Q "$f[0] 0 $f[1] $f[2]\n";
    $lyns++;
}

close(Q);
close(I);

print "$0: $lyns lines converted.\n";

exit(0);
