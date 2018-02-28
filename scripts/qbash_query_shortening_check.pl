#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Check the operation and robustness of the QBASHQ query_shortening capability


# Assumes run in a directory with the following subdirectories:

$|++;

$idxdir = "../test_data";
$tqdir = "../test_queries";
$base_opts = "-display_col=1 -display_parsed_query=TRUE";  

$tmpqfile = "tmp_qshort.q";


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

# --------------------------------------------------------------------------------------
#      Run some deliberately constructed query shortening queries and check results
# --------------------------------------------------------------------------------------

print "\nRanking: Checking result counts ...\n";
$err_cnt += check_count("Protocol amending the Agreements, Conventions and Protocols on Narcotic Drugs, 1946", "", 1, 0);
$err_cnt += check_count("Protocol amending the Agreements, Conventions and Protocols on Narcotic Drugs, 1946", "-query_shortening_threshold=5", 2, 0);
$err_cnt += check_count("Protocol amending the Agreements, Conventions and Protocols on Narcotic Drugs, 1946", "-query_shortening_threshold=2", 2, 0);
$err_cnt += check_count("water xxnoexistxxx", "", 0, 0);
$err_cnt += check_count("water xxnoexistxxx", "-query_shortening_threshold=1", 8, 0);
$err_cnt += check_count("Dance Dance Revolution EXTREME (North America) song litzs", "", 0, 0);
$err_cnt += check_count("Dance Dance Revolution EXTREME (North America) song litzs", "-query_shortening_threshold=4", 3, 0);
$err_cnt += check_count("Dance Dance Revolution EXTREME (North America) song litzs", 
			"-classifier_mode=1 -classifier_threshold=0.1 -query_shortening_threshold=4", 3, 0);
$err_cnt += check_count("List of colleges and universities in New xxnoexistxxx", "", 0, 0);
$err_cnt += check_count("List of colleges and universities in New xxnoexistxxx", "-query_shortening_threshold=5", 8, 0);

print "\nClassification: Checking that repeated query words are handled OK ...\n";
$err_cnt += check_count("it's a mad mad mad mad", "-classifier_mode=1 -classifier_threshold=.75", 8, 0);
$err_cnt += check_count("it's a mad mad mad mad world", "-query_shortening_threshold=4 -classifier_mode=1 -classifier_threshold=.75", 8, 0);


print "\nRanking: Checking result counts in a multi-query ...\n";
$err_cnt += check_count("it's a mad mad mad mad wurrald\t\t\tN<1\036it's a mad mad mad mad wurrald\t-query_shortening_threshold=4", "-allow_per_query_options=true", 8, 0);


# --------------------------------------------------------------------------------------
#      Run a file of 10k queries with query shortening -- hopefully it doesn't crash
# --------------------------------------------------------------------------------------

print "\nRun a set of 10k queries through QBASHQ with query shortening and make sure it doesn't crash. ...\n";

$cmd = "$qp -index_dir=$ix -file_query_batch=$qsets[0] -query_shortening_threshold=3 -display_parsed_query=true > shortened-queries.out\n";
print $cmd;

$code = system($cmd);
die "$cmd failed.  Output is in shortened-queries.out\n" if $code;



if ($err_cnt) {
    print "\n\nUnspeakable!  $0 failed with $errs errors\n";
} else {
    print "\n\n       Outrageous good fortune.  $0 passed!!\n";
}
exit($err_cnt);

# -------------------------------------------------------------------------------------------


sub check_count {
	my $query = shift;
	my $options = shift;
	my $expected = shift;
	my $show_rslts = shift;

	die "Can't open >$tmpqfile\n"
	    unless open QF, ">$tmpqfile";
	print QF "$query\n";
	close(QF);
	
	my $cmd = "$qp index_dir=$ix $base_opts -file_query_batch=$tmpqfile -x_batch_testing=TRUE $options";
	print $cmd;
	my $rslts = `$cmd`;
	die "\n\nCommand '$cmd' failed with code $?\n"
		if ($?);
	print "\n\nOutput from above command: \n", $rslts if $show_rslts;
	my $count = 0;
	my %dup_check;
	while ($rslts =~ /Query:\s+?[^\t]+\t[0-9]+\t([^\t]+)\t/sg) {
		 die "\n\n$rslts\nResults for '$cmd' contain duplicate suggestions\n"
			if defined($dup_check{$1});
		$dup_check{$1} = 1;
		$count++;
	}
	print sprintf(" - %4d %4d - ", $expected, $count);
	if ($count == $expected) {print " [OK]\n";}
	else {
		print " [FAIL]\n";
		if ($fail_fast) {
			my $rslts = `$cmd`;
			print $rslts;
			exit(1);
		}
		return 1;
	}
	return 0;
}


sub check_topk_results {
    # This used to test ranks, but that's prone to errors when two items have
    # identical scores.  Now changed to check presence and (optionally) scores instead.
    my $query = shift;
    my $options = shift;
    my $show_rslts = shift;
    my $k = shift;
    my @items_with_scores;
    for (my $a = 0; $a < $k; $a++) {
	$items_with_scores[$a] = shift;
    }

    die "Can't open >$tmpqfile\n"
	unless open QF, ">$tmpqfile";
    print QF "$query\n";
    close(QF);

    
    my $missings = 0;
    my $cmd = "$qp index_dir=$ix $base_opts -file_query_batch=$tmpqfile -x_batch_testing=TRUE $options";
    my $rslts = `$cmd`;
    die "\n\nCommand '$cmd' failed with code $?\n"
	if ($?);

    print $rslts if $show_rslts;
    # Check for duplicates.
    my %dup_check;
    while ($rslts =~ /Query:\s+?[^\t]+\t[0-9]+\t([^\t]+)\t/sg) {
	die "\n\n$rslts\nResults for '$cmd' contain duplicate suggestions ($1)\n"
	    if defined($dup_check{$1});
	$dup_check{$1} = 1;
    }

    # Check that all the listed items are present
    for (my $a = 0; $a < $k; $a++) {
	if (!($rslts =~ /$items_with_scores[$a]/s)) {
	    print "  Item '$items_with_scores[$a]' not found in results.\n";
	    $missings++;
	}
    }
    print $cmd;
    if ($missings == 0) {print " [OK]\n";}
    else {
	print " [FAIL]\n";
	if ($fail_fast) {
	    my $rslts = `$cmd`;
	    print $rslts;
	    exit(1);
	}
	return 1;
    }
    return 0;
}


