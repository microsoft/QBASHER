#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Check the operation of the mechanism to label results with a query-specific label.
# This mechanism is useful when running a batch of queries and trying to match up
# answers returned with expected.  Format (for three query variants) is
#
#      <query variant> RS <query variant> RS<query variant> GS <query_label>
#
# (where each query variant optionally includes per-query-options, weight and fall through conditions.)




# Assumes run in a directory with the following subdirectory:
$idxdir = "../test_data";


$|++;

$err_cnt = 0;

$tmpqfile = "tmp_batch_labels.q";
$tmpout = "tmp_batch_labels.out";

$ix = "$idxdir/wikipedia_titles";


 
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
print "\n";


die "Can't write to $tmpqfile\n"
    unless open Q, ">$tmpqfile";

print Q
    "rhodium\035LABEL: simple query(one variant, no extra fields)
rhodium\036palladium\036platinum\036white gold\035LABEL: multi-query, no extra fields
rhodium\t\t1.0\tN<100\036platinum\t\t1.0\036palladium\t\t0.5\036white gold\t\t0.25\035LABEL: multi-query, with fields
    ";
close Q;

$base_cmd = "$qp index_dir=$ix -allow_per_query_options=TRUE -file_query_batch=$tmpqfile -max_to_show=10 -file_output=$tmpout";

$err_cnt += check_for_labels("-x_batch_testing=true -chatty=true", 6, 30);
$err_cnt += check_for_labels("-x_batch_testing=true", 6, 30);

$err_cnt += check_for_labels("-chatty=true", 2, 3);
$err_cnt += check_for_labels("", 2, 3);


if ($err_cnt) {
    print "\n\nCurses!  $0 failed with $err_cnt errors\n";
} else {
    print "\n\n     Delight and delectation:  $0 PASSED.\n";
}
exit($err_cnt);



# -------------------------------------------------------------------------------


sub check_for_labels {
    # Used for checking output format when -x_batch_testing is FALSE
    my $opts = shift;
    my $expect_fields = shift;
    my $expect_qlines = shift;
    
    my $cmd = "$base_cmd $opts";
    my $errs = 0;
    my $oline = sprintf("%40s      ", "\"$opts\"");

    $code = system($cmd);
    die "Command $cmd failed with code $code\n"
	if $code;

    # Now scan the output and make sure all is in order

    die "Can't read $tmpout\n"
	unless open O, $tmpout;
    $tot_rez =0;
    while (<O>) {
	next unless /Query:/;
	chomp;
	$tot_rez++;
	@fields = split /\t/;
	$fc = $#fields + 1;
	if ($fc != $expect_fields) {
	    print "Error: should be $expect_fields tab-sepped fields in result line but there were $fc\n   '$_'\n";
	    $errs++;
	} elsif (! ($fields[$#fields] =~ /LABEL:/)) {
	    print "Error: Didn't find LABEL: in last field in result line\n   '$_'\n";
	    $errs++;
	}
    }
    close(O);

    if ($tot_rez != $expect_qlines) {
	print "Error: Number of results is $tot_rez but it should be $expect_qlines\n";
	$errs++;
    }

    if ($errs) {
	print $oline, "  [FAIL]\n";
    } else {
	print $oline, "  [PASS]\n"
    }    
    return $errs;
}
