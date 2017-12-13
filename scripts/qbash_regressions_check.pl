#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Checks that various bugs which have been encountered in the past, do not
# reappear.  Bugs tested for include:
#
# 1. Corrupted results due to error in doctable offset calculation
#    when records are ignored.
# 2. Division by zero when QBASHQ is run with relaxation_level > 0 and
#    a candidate document is one word shorter than the query.

# Assumes run in a directory with the following subdirectories:

$idxdir = "../test_data";


$|++;

die "Usage: $0 <QBASHQ binary>\n"
		unless ($#ARGV >= 0);
$qp = $ARGV[0];
$qp = "../src/visual_studio/x64/Release/QBASHQ.exe"
    if $qp eq "default";

die "$qp is not executable\n" unless -e $qp;
$dexer = $qp;
$dexer =~ s/QBASHQ/QBASHI/;
$dexer =~ s/qbashq/qbashi/;

$dexdir = "$idxdir/regressions";
die "Can't find $dexdir/QBASH.forward\n" 
	unless -r "$dexdir/QBASH.forward";



print "Indexing ...\n";
$cmd = "$dexer -index_dir=$dexdir > $dexdir/index.log";
`$cmd`;
die "Command '$cmd' failed with code $?\n"
	if ($?);
print "\n";

$err_cnt += check_results_for_query(
    "Test for corrupted doctable offsets",
    "barrier reef", 
    "",
    "^barrier reef marine park authority");

$err_cnt += check_results_for_query(
    "Test for division by zero in length score calc.",
    "great barrier reef marine park authority", 
    "-relaxation_level=1 -epsilon=0.5",
    "^barrier reef marine park authority\t0.083");

	    
       
print "\n\nErrors: $err_cnt";
if ($err_cnt) {
        print "  [OVERALL FAIL]\n";
} else {
        print "                       \"I say!  Absolutely spiffing, what!\"\n";
}

print "\n";

exit($err_cnt);

# -------------------------------------------------------------------

sub check_results_for_query {
    my $label = shift;
    my $query = shift;
    my $opts = shift; 
    my $should_match = shift;

    $cmd = "$qp index_dir=$dexdir -display_col=4 $opts -pq='$query'\n";
    $rslts = `$cmd`;
    die "\n$label: {$query}: QBASHQ.exe crashed with code $?.  Options were $opts\n\n"
	if $?;

    print "$label: {$query}: "; 
    if ($rslts =~ /${should_match}/) {
	print "OK\n";
	return 0;
    } else {
	print "FAIL:
Command: $cmd
Results: $rslts
";
	
	return 1;
    }
}

