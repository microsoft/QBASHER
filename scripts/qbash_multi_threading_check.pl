#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Run a query set against an index in single-threaded mode, saving the
# output to a file.  Then run with a range of different degrees of parallelism
# and check that the results are the same (apart from being reordered.)

# Assumes run in a directory with the following subdirectories:

$idxdir = "../test_data";
$tqdir = "../test_queries";

$|++;

$ix = "$idxdir/wikipedia_titles";
@qsets = ("$tqdir/emulated_log_10k.q");
$comparator = "./qbash_compare_logs.pl";

die "Usage: $0 <QBASHQ_binary> 
   Note: This script expects a current index in $ix and
         10,000 test queries in each of @qsets.\n" 
	unless ($#ARGV >= 0);

$qp = $ARGV[0];
$qp = "../src/visual_studio/x64/Release/QBASHQ.exe"
    if $qp eq "default";

die "$qp is not executable\n" unless -e $qp;

die "Can't find QBASHER indexes in $ix\n" 
	unless (-r "$ix/QBASH.if");

die "Comparison script $comparator is not there or not executable\n"
    unless -x $comparator;



$reffile = "/tmp/A";
$qsfile = "/tmp/B";

foreach $qset (@qsets) {
    print " ------- $qset --------\n";
    $cmd = "$qp -warm_indexes=TRUE index_dir=$ix -query_streams=1 <$qset > $reffile";
    $code = system($cmd);
    die "Error getting Ref File $reffile\n" if $code;
    print "Single-stream reference set generated.\n";
    foreach $QS (2, 3, 4, 7, 8, 10, 15, 20) {
	$cmd = "$qp -warm_indexes=TRUE index_dir=$ix -query_streams=$QS <$qset > $qsfile";
	$code = system($cmd);
	die "Error getting QS File $qsfile\n" if $code;
	# Now do the comparison
	print "$QS query streams: ";
	$cmd = "$^X $comparator $reffile $qsfile";
	$code = system($cmd);
	die "Error comparison failed\n" if $code;
    }
    print "\n\n";
}
	
print "      Wonderful!!\n\n";
exit(0);
