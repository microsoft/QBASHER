#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Check the operation and robustness of the new (July 2017) multi-query
# capability.


# Assumes run in a directory with the following subdirectories:

$|++;

$idxdir = "../test_data";
$tqdir = "../test_queries";
$base_opts = "-display_col=1 -allow_per_query_options=true";  

$tmpqfile = "tmp_multiq.q";


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
#      Run some deliberately constructed multi queries and check results
# --------------------------------------------------------------------------------------

print "\nRanking: Checking result counts ...\n";
$err_cnt += check_count("water", "", 8, 0);
$err_cnt += check_count("water xxnoexistxxx", "", 0, 0);
$err_cnt += check_count("water xxnoexistxxx", "-relaxation_level=1", 8, 0);
$err_cnt += check_count("water xxnoexistxxx\036water xxnoexistxxx\t-relaxation_level=1", "", 8, 0);
$err_cnt += check_count("salmonella outbreak", "", 3, 0);
$err_cnt += check_count("salmonella outbreak\t\t1.0\tN<4\036salmonella\t\t0.1", "", 8, 0);

print "\n\nRanking: Checking result sets ...\n";
$err_cnt += check_topk_results("salmonella outbreak\t\t1.0\tN<4\036peanut salmonella\t\t0.1", "", 0, 4,
    "2012 salmonella outbreak\t0.217",
    "2008 United States salmonella outbreak\t0.088",
    "2008 salmonella outbreak\t0.054",
    "2009 peanut salmonella\t0.011"
    );

print "\n\nRanking: Make sure that we can get results from both of a pair of queries ...\n";
# The two queries have the same number of words, and almost identical static scores.
$err_cnt += check_topk_results("mickey mouse clubhouse\t\t1.0\tN<1000\036solitaire card game\t\t1.0", "-classifier_mode=1", 0, 2,
			       "Solitaire card game.*0\.991",
			       "Mickey Mouse Clubhouse.*0\.990"
   );
$err_cnt += check_topk_results("solitaire card game\t\t1.0\tN<1000\036mickey mouse clubhouse\t\t1.0", "classifier_mode=1", 0, 4,
			       "Solitaire card game.*0\.991",
			       "Mickey Mouse Clubhouse.*0\.990"
    );



print "\nClassifying: Checking result counts ...\n";
$err_cnt += check_count("bank of america beechworth", "-classifier_mode=1", 0, 0);
$err_cnt += check_count("bank of america", "-classifier_mode=1", 3, 0);
$err_cnt += check_count("alaska usa feddral credit union", "-classifier_mode=1", 0, 0);
$err_cnt += check_count("alaska usa feddral credit union\036alaska usa federal credit union", 
			"-classifier_mode=1", 1, 0);
$err_cnt += check_count("bath and body works coupons 15 dollars off", "-classifier_mode=1", 0, 0);
$err_cnt += check_count("bath and body works coupons 15 dollars off\t\t1.0\tN<1\036bath and body works\t\t0.9", 
			"-classifier_mode=1", 2, 0);

print "\n\nClassifying: Checking result sets ...\n";
$err_cnt += check_topk_results("bath and body works coupons 15 dollars off\t\t1.0\tN<1\036bath and body works\t\t0.9", "-classifier_mode=1", 0, 3,
    "Bath and body works\t.*?0.892",   # Need the .*? because classifier results have extra fields.
    "Bath and Body Works\t.*?0.892"
    );

print "\n\nChecking the use of query_shortening in a variant ...\n";
$err_cnt += (check_absent_from_results("appellate procedure in the unttied states", "", 0, 1,
		       "Appellate procedure in the United States"));  # It shouldn't find anything
$err_cnt += check_topk_results("appellate procedure in the unttied states\t\t1.0\tN<1\036appellate procedure in the unttied states\t-query_shortening_threshold=5\t0.9", "", 0, 1,
    "Appellate procedure in the United States");


# --------------------------------------------------------------------------------------
#      Create a file of 10,000 multi queries and run it -- hopefully it doesn't crash
# --------------------------------------------------------------------------------------
print "\n\nConverting @qsets into multi queries.  For 
each input query, a triple is generated: the input query in quotes, the original query, and (if possible) 
the original query with a word dropped. ...\n";

$cmd = "./make_multi_queries.pl @qsets > multi-queries.mq\n";
$code = system($cmd);
die "$cmd failed\n" if $code;

print "\nNow run the whole lot through QBASHQ in standard and classifier modes to make sure it doesn't crash. ...\n";

$cmd = "$qp -index_dir=$ix -file_query_batch=multi-queries.mq -allow_per_query_options=true > multi-queries.out\n";
print $cmd;

$code = system($cmd);
die "$cmd failed.  Output is in multi-queries.out\n" if $code;

$cmd = "$qp -index_dir=$ix -file_query_batch=multi-queries.mq -allow_per_query_options=true -classifier_mode=2 > multi-queries.out\n";
print $cmd;

$code = system($cmd);
die "$cmd failed.  Output is in multi-queries.out\n" if $code;

if ($err_cnt) {
    print "\n\nChut alors!  $0 failed with $errs errors\n";
} else {
    print "\n\n       Nous avons eu de la chance!  $0 passed!!\n";
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
	print "     - {$query}\n";
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


sub check_absent_from_results {
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

    
    my $presents = 0;
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

    # Check that all the listed items are absent
    for (my $a = 0; $a < $k; $a++) {
	if ($rslts =~ /$items_with_scores[$a]/s) {
	    print "  Item '$items_with_scores[$a]' found in results. Should not have been.\n";
	    $presents++;
	}
    }
    print $cmd;
    if ($presents == 0) {print " [OK]\n";}
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


