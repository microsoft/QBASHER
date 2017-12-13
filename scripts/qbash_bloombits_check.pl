#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Checks that the setting of Bloom filter bits in the indexer is consistent 
# with that in the query processor, by checking that matching of prefixes
# works as it should.

# Assumes run in a directory with the following subdirectories:

$idxdir = "../test_data";


$|++;

die "Usage: $0 <QBASHQ binary> [-fail_fast]\n"
		unless ($#ARGV >= 0);
$qp = $ARGV[0];
$qp = "../src/visual_studio/x64/Release/QBASHQ.exe"
    if $qp eq "default";

$fail_fast = 0;
$fail_fast = 1 if ($#ARGV > 0 && $ARGV[1] eq "-fail_fast");

die "$qp is not executable\n" unless -e $qp;
$dexer = $qp;
$dexer =~ s/QBASHQ/QBASHI/;
$dexer =~ s/qbashq/qbashi/;

$dexdir = "$idxdir/bloombits_test";
die "Can't find $dexdir/QBASH.forward\n" 
	unless -r "$dexdir/QBASH.forward";



print "Indexing ...\n";
$cmd = "$dexer -index_dir=$dexdir > $dexdir/index.log";
`$cmd`;
die "Command '$cmd' failed with code $?\n"
	if ($?);

# The data consists of "Bono and Willie Nelson to team up on a film"
$err_cnt = 0;
foreach $q ("Bono and Willie",
	    "Bono and Willie /n",
	    "Bono and Willie /N",
	    "bono and willie /nelson",
	    "bono and willie /Nelson",
	    "and /b",
	    "and /b",
	    "AND /f",
	    "AND /film",
	    "and /TEA",
	    "and /tEa",
	    "and /b /w /n /t /u /o /f") {

    $cmd = "$qp index_dir=$dexdir -pq='$q' \n";
    $rslts = `$cmd`;
    print "{$q}: "; 
    if ($rslts =~ /Bono and Willie Nelson/) {
	print "YES\n";
    } else {
	$err_cnt++;
	print "FAIL\n";
	if ($fail_fast) {
	    print "\nCommand '$cmd' failed.  Results were:\n\n$rslts\n\n";
	    exit(1);
	}
    }
}
	    
	   
print "-----------------------------------------------------------\n";


print "\n\nErrors: $err_cnt";
if ($err_cnt) {
        print "  [OVERALL FAIL]\n";
} else {
        print "                       \"Och 'tis nae bad at all.\"\n";
}


exit($err_cnt);

