#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Run a file of randomly generated garbage queries against an index in a whole lot
# of different modes and make sure QBASHER doesn't crash.


$|++;

$idxdir = "../test_data";
$tqfile = "../test_queries/Fuzz_7_100000_7200_0.q";


@ix = (
    "$idxdir/wikipedia_titles_500k"
);

@qsets = (
    "$tqfile"
);


die "Usage: $0 <QBASHQ_binary> 
   Note: This script expects current indexes in each of @ix and
         test queries in each of @qsets.\n" 
	unless ($#ARGV >= 0);

$qp = $ARGV[0];

$qp = "../src/visual_studio/x64/Release/QBASHQ.exe" 
    if ($qp eq "default");

die "$qp is not executable\n" unless -x $qp;
$fail_fast = 0;
$fail_fast = 1 if ($#ARGV > 0 && $ARGV[1] eq "-fail_fast");


# ----------------------------------------------------------------------------------------------


@relaxopts = (
    "",
    "-relaxation_level=1",
     "-relaxation_level=2",
     "-relaxation_level=3",
     "-relaxation_level=4",
 );


$tfile = "/tmp/fuzz.out";

@classimodes = (
    "",
    "DOLM_counts", 
    "DOLM_idfs",
    "Jaccard_counts",
    "Jaccard_idfs",
    );


for $qset (@qsets) {
	die "Can't find query set $qset\n"
	unless -r $qset;
}

$base_options = "-warm_indexes=TRUE -display_col=1 -classifier_mode=XXX";

for $ix (@ix) {
    die "Can't find QBASHER indexes in $ix\n" 
	unless (-r "$ix/QBASH.if");

    for $qset (@qsets) {
	print " --- Query Set: $qset;  Index: $ix ---\n";

	print "\n      - First run with street number options turned on - ";
	# Must use file_query_batch otherwise ctrl-Z is EOF
	$cmd = "$qp index_dir=$ix -warm_indexes=true -street_address_processing=2 -file_query_batch=$qset > $tfile";
	$code = system($cmd);

	#print "Output of batch run in: $tfile\n\n";
	die "\nBatch run killed by signal\nCommand was $cmd\nResults in $tfile\n" if ($code & 255);
	$code >>= 8;
	die "\nBatch run crashed with code $code\nCommand was $cmd\nResults in $tfile\n" if $code;

	print "  [OK]\n\n";
	unlink $tfile;

	print "\n      - Now do all the combinations of classifier modes and relax levels  - \n";

	for ($mode = 0; $mode <= $#classimodes; $mode++)  {
	    foreach $relaxopt (@relaxopts) {
		$opts = $base_options;
		$opts =~ s/XXX/$mode/;
		$opts .= " $relaxopt";
		print "          $opts ";
		# Must use file_query_batch otherwise ctrl-Z is EOF
		$cmd = "$qp index_dir=$ix $opts -file_query_batch=$qset > $tfile";
		$code = system($cmd);

		#print "Output of batch run in: $tfile\n\n";
		die "\nBatch run killed by signal\nCommand was $cmd\nResults in $tfile\n" if ($code & 255);
		$code >>= 8;
		die "\nBatch run crashed with code $code\nCommand was $cmd\nResults in $tfile\n" if $code;

		print "  [OK]\n";
		unlink $tfile;
	    }
	}

	
    }

    
print "===================================================================\n";
}



print "\n\n       Whacko!   Fuzz tests completed without a crash!\n";
exit(0);

