#! /usr/bin/perl -w
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

$|++;

die "Usage: $0 <infile> <outfile> <termids_file>\n"
    unless $#ARGV == 2;

$infile = $ARGV[0];
$outfile = $ARGV[1];
$tidfile = $ARGV[2];

die "Can't read $infile\n"
    unless open I, "$infile";
die "Can't write to $outfile\n"
    unless open O, ">$outfile";
die "Can't write to $tidfile"
    unless open T, ">$tidfile";


$curterm = -1;
$stillgoing = 1;
$numTerms = 0;
$maxDocNo = -1;

$_ = <I>;
@f = split /\t/;
$count = 1;
while ($stillgoing) {

    if ($f[0] != $curterm) {
	# Output the term-id
	print O "\n" if $curterm >= 0;
	$numTerms++;
	print T "$f[0]\n";
	print O sprintf("%08d\t", $f[0]);
	$curterm = $f[0];
	undef @docids;
	print "Term $numTerms: $curterm\n";
    }
    
    # We're at the start of a run
    $qscore = int($f[2] * 10000);
    $curscore = $qscore;
    $runlen = 0;
    while ($qscore == $curscore) {
	push @docids, $f[1];
	$maxDocNo = $f[1] if $f[1] > $maxDocNo;
	$runlen++;
	if (!defined($_ =  <I>)) {
	    # At end of file
	    $stillgoing = 0;
	    last;
	}
	$count++;
	@f = split /\t/;
	if (/[0-9]/ && $#f != 2) {
	    print STDERR "Error: $#f!  Line was '$_'\n";
	    exit(1);
	}
	$qscore = int($f[2] * 10000);
    }

    # Output the run header and then all the saved up docids.
    print O "$qscore $runlen*";
    for ($i = 0; $i < $runlen; $i++) {
	print O " $docids[$i]";
    }
    print O "#";
}

# Have to now print the last run.
print O "$qscore $runlen*";
for ($i = 0; $i < $runlen; $i++) {
    print O " $docids[$i]";
}
print O "#\n";
close(I);
close(O);
close(T);	    

print "\n@ARGV", "\n$count Bhaskar records read -- $outfile written.\n";
print "numTerms: $numTerms\nmaxDocNo: $maxDocNo\n";
exit(0);
