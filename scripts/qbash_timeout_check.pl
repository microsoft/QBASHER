#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Check the operation and robustness of the various timeout options


# Assumes run in a directory with the following subdirectories:

$|++;

$idxdir = "../test_data";
$tqdir = "../test_queries";

$base_opts = "-classifier_mode=2 -query_streams=1";  

$outfile = "tmp_timeout.out";


$ix = "$idxdir/wikipedia_titles";

@qsets = (
    "$tqdir/emulated_log_10k.q"
);

 
die "Usage: $0 <QBASHQ_binary> 
   Note: This script expects current index in $ix and test queries in each of @qsets.\n" 
	unless ($#ARGV >= 0);

$qp = $ARGV[0];

$qp = "../src/visual_studio/x64/Release/QBASHQ.exe" 
    if ($qp eq "default");


die "$qp is not executable\n" unless -x $qp;
$fail_fast = 0;
$fail_fast = 1 if ($#ARGV > 0 && $ARGV[1] eq "-fail_fast");
$err_cnt = 0;


#Run a warm-up to make things a bit more meaningful

$cmd = "$qp index_dir=$ix $base_opts -file_query_batch=$qsets[0] -file_output=$outfile -x_batch_testing=TRUE -warm_indexes=true";
print $cmd, "\n\n";
$code = system($cmd);
die "Warm up command failed with code $code\n"
    if $?;


# --------------------------------------------------------------------------------------
#      Run the same query batch with different timeout options.
# --------------------------------------------------------------------------------------
print "
 *** Note: Because of the inherent variability in elapsed times, the number of timeouts from   ***
 *** this cause varies from machine to machine and run to run.   Tests involving elapsed times ***
 *** check only whether there are timeouts or not.  Tests on kops only check exact numbers.    ***
    ";
print "\n#                                              TOkops \tTOmsec\t#TOed\tQPS\tMax t\t50th%\t90th%\t95th%\t99th%\t99.9th%\n";

$err_cnt += extract_timings($qsets[0], "-timeout_msec=0 -timeout_kops=0", 0);
$err_cnt += extract_timings($qsets[0], "-timeout_msec=0 -timeout_kops=999999", 0);
$err_cnt += extract_timings($qsets[0], "-timeout_msec=999999 -timeout_kops=0", 0);
$err_cnt += extract_timings($qsets[0], "-timeout_msec=1 -timeout_kops=0", 1);
$err_cnt += extract_timings($qsets[0], "-timeout_msec=10 -timeout_kops=0", 0);
$err_cnt += extract_timings($qsets[0], "-timeout_msec=100 -timeout_kops=0", 0);
$err_cnt += extract_timings($qsets[0], "-timeout_msec=1000 -timeout_kops=0", 0);
$err_cnt += extract_timings($qsets[0], "-timeout_msec=0 -timeout_kops=10", 457);
$err_cnt += extract_timings($qsets[0], "-timeout_msec=0 -timeout_kops=100", 30);
$err_cnt += extract_timings($qsets[0], "-timeout_msec=0 -timeout_kops=1000", 0);
$err_cnt += extract_timings($qsets[0], "-timeout_msec=0 -timeout_kops=10000", 0);
$err_cnt += extract_timings($qsets[0], "-timeout_msec=1 -timeout_kops=100", 1);



if ($err_cnt) {
    print "\n\nQuel dommage!  $0 failed with $err_cnt errors\n";
    print "\n *** However, please note that the elapsed time timeouts are CPU-speed dependent.  ***
 *** If you are running on a very fast machine you may need to modify the -timeout_msec values. ***

\n";
} else {
    print "\n\n       Tout est bien passé!  $0 passed!!\n";
}
exit($err_cnt);

# -------------------------------------------------------------------------------------------


sub extract_timings {
    my $querybatch = shift;
    my $options = shift;
    my $expect_timeouts = shift;
    my $errs = 0;
    
    #print "\n  -------------  $options --------------\n";
    my $cmd = "$qp index_dir=$ix $base_opts -file_query_batch=$querybatch -file_output=$outfile -x_batch_testing=TRUE $options";
    #print $cmd, "\n";
    my $code = system($cmd);
    die "\n\nCommand '$cmd' failed with code $code\n"
	if ($code);

    $tcmd = "tail -30 $outfile";
    my $rslts = `$tcmd`;
    die "\n\nCommand '$tcmd' failed with code $?\n"
	if ($?);

    $rslts =~ /-- ([0-9.]+) QPS.*?([0-9]+) kilo-cost-units.*?set at: ([0-9]+) msec.*?\(from either cause\): ([0-9]+).*?Maximum elapsed msec per query: ([0-9.]+).*?50th -\s+([0-9]+).*?90th -\s+([0-9]+).*?95th -\s+([0-9]+).*?99th -\s+([0-9]+).*?99.9th -\s+([0-9]+)/s;
    $qps = $1;
    $tokops = $2;
    $toms = $3;
    $timeouts = $4;
    $maxt = $5;
    $t50 = $6;
    $t90 = $7;
    $t95 = $8;
    $t99 = $9;
    $t999 = $10;

    if (!defined($qps)) {
	$errs = 1;
	print "Error: Can't match patterns in tail of log:\n\n$rslts\n";
	if ($fail_fast) {
	    exit(1);
	}
	return $errs
    }

    print sprintf("%-47s", $options), "$tokops\t$toms\t$timeouts\t$qps\t$maxt\t$t50\t$t90\t$t95\t$t99\t$t999   ";
    #print $rslts;

    
    if ($expect_timeouts) {
	# If timeout_kops > 0 and timeout_msec == 0 the number of timed out queries
	# should be deterministic.  In that case we check the exact number of timed out
	# queries, otherwise we just check whether there are some.
	#
	if ($tokops > 0 && $toms == 0) {
	    my $diff = $timeouts - $expect_timeouts;
	    if ($diff != 0) {
		$errs = 1;
		$explan = "\nExpected $expect_timeouts timeouts, got $timeouts\n";
	    }
	} else {
	    if ($timeouts == 0)  {
		$errs = 1;
		$explan = "\nExpected timeouts, got none\n";
	    }
	}
	
	if ($errs) {
	    print " [FAIL]\n";
	    print $explan;
	    if ($fail_fast) {
		print $rslts;
		print "\nCommand was $cmd\nFull output is in $outfile\n\n";
		exit(1);
	    }
	} else {
	    print " [OK]\n";
	}
    } else {
	print " [OK]\n";
    }
    return $errs;
}


