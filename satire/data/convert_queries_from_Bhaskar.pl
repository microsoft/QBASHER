#! /usr/bin/perl -w
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

$|++;

# Bhaskar's queries comprise a query-id then a space-separated list of term-ids
# As a quick hack, this script converts the term-ids to the corresponding indices in the term-id file,
# which is what is expected by the initial version of q.exe.  Also drops the query-id.

die "Usage: $0 <input_query_file> <output_query_file> <termids_file>\n"
    unless $#ARGV == 2;

$infile = $ARGV[0];
$outfile = $ARGV[1];
$tidfile = $ARGV[2];

die "Can't read $infile\n"
    unless open I, "$infile";
die "Can't read $tidfile"
    unless open T, "$tidfile";
die "Can't write to $outfile\n"
    unless open O, ">$outfile";

$count = 0;
while (<T>) {
    chomp;
    $term{$_} = $count++;
}
close(T);

print "Mappings read for $count terms.\n";

$qcnt = 0;
while (<I>) {
    chomp;
    if (/^([0-9]+)\t(.*)/) {
	$quid = $1;
	$qcnt++;
	$terms = $2;
	print O $quid, "\t";
	@terms = split /\s+/, $terms;
	foreach $t (@terms) {
	    if (defined($term{$t})) {
		print O $term{$t}, " ";
	    } else {
		print "Grrr!  no mapping for term '$t'\n";
		exit(1);
	    }
	}
	print O "\n";
    } else {
	print "Aaargh!\n";
	exit(1);
    }
}

close(I);
close(O);

print "Queries converted: $qcnt.  Ouput in $outfile\n";

exit(0);
    
      
