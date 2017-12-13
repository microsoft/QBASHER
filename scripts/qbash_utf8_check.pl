#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Checks a few cases involving non-ASCII punctuation

# Assumes run in a directory with the following subdirectories:

$|++;

$idxdir = "../test_data";


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

$base_opts = "";

$ix = "$idxdir/UTF8";
die "Can't find $ix/QBASH.forward\n" 
	unless -r "$ix/QBASH.forward";



print "Indexing ...  [$dexer] [$ix]\n";
$cmd = "$dexer -index_dir=$ix > $ix/index.log";
`$cmd`;
die "Command '$cmd' failed with code $?\n"
	if ($?);

print "... indexed\n";

# One line of the data consists of "Maybe I’m just paranoid"  with the smart quote represented in UTF-8 (3 bytes)
$err_cnt = 0;
foreach $q ("Maybe I'm just paranoid",  # ASCII
	    "Maybe I\x92m just paranoid", #CP 1252
	    "Maybe I\xE2\x80\x99m just paranoid"  # UTF-8
) {

    $cmd = "$qp index_dir=$ix -display_col=1 -pq=\"$q\" \n";
    $rslts = `$cmd`;
    print "{$q}: "; 
    if ($rslts =~ /Maybe I.*?m just paranoid/) {
	print "[OK]\n";
    } else {
	$err_cnt++;
	print "[FAIL]\n - $rslts\n";
	print "Cmd was: $cmd";
	exit if ($fail_fast);
    }
}

# Check UTF-8 lower casing
$err_cnt += check_count("23ÈMES JOURNÉES BASES DE DONNÉES AVANCÉES", "", 1, 0);
$err_cnt += check_count("BEYONCÉ", "", 1, 0);
$err_cnt += check_count("NEUCHÂTEL", "", 1, 0);
$err_cnt += check_count("MÜNCHEN", "", 1, 0);
# Next lot shouldn't match anything
$err_cnt += check_count("23EMES JOURNEES BASES DE DONNEES AVANCEES", "", 0, 0);


$err_cnt += check_count("BEYONCE", "", 0, 0);
$err_cnt += check_count("NEUCHATEL", "", 0, 0);
$err_cnt += check_count("MUNCHEN", "", 0, 0);

# Check UTF-8 accent removal
$err_cnt += check_count("Saarbrücken und Köln", "-x_conflate_accents=on", 1, 0);
$err_cnt += check_count("Ces élèves sont très grands", "-x_conflate_accents=on", 1, 0);


# Check accent removal in indexing.
print "Indexing ...  [$dexer] [$ix]\n";
$cmd = "$dexer -index_dir=$ix -conflate_accents=true > $ix/index.log2";
`$cmd`;
die "Command '$cmd' failed with code $?\n"
    if ($?);

$err_cnt += check_count("\"23ÈMES JOURNÉES BASES DE DONNÉES AVANCÉES\"", "", 1, 0);  #phrase
$err_cnt += check_count("BEYONCÉ", "", 1, 0);
$err_cnt += check_count("NEUCHÂTEL", "", 1, 0);
$err_cnt += check_count("MÜNCHEN", "", 1, 0);

$err_cnt += check_count("\"23EMES JOURNEES BASES DE DONNEES AVANCEES\"", "", 1, 0);  #phrase
$err_cnt += check_count("BEYONCE", "", 1, 0);
$err_cnt += check_count("NEUCHATEL", "", 1, 0);
$err_cnt += check_count("MUNCHEN", "", 1, 0);
	 
# Check classifier mode - we need -x_conflate_accents=on to allow word by word matching to succeed.
 $err_cnt += check_count("\"23ÈMES JOURNÉES BASES DE DONNÉES AVANCÉES\"", "-classifier_mode=1 -x_conflate_accents=on", 1, 0);  #phrase
$err_cnt += check_count("BEYONCÉ knowles", "-classifier_mode=1 -x_conflate_accents=on", 1, 0);
$err_cnt += check_count("NEUCHÂTEL", "-classifier_mode=1 -x_conflate_accents=on", 1, 0);
$err_cnt += check_count("MÜNCHEN", "-classifier_mode=1 -x_conflate_accents=on", 1, 0);

$err_cnt += check_count("\"23EMES JOURNEES BASES DE DONNEES AVANCEES\"", "-classifier_mode=1 -x_conflate_accents=on", 1, 0);  #phrase
$err_cnt += check_count("BEYONCE knowles", "-classifier_mode=1 -x_conflate_accents=on", 1, 0);
$err_cnt += check_count("NEUCHATEL", "-classifier_mode=1 -x_conflate_accents=on", 1, 0);
$err_cnt += check_count("MUNCHEN", "-classifier_mode=1 -x_conflate_accents=on", 1, 0);
  
	   
print "-----------------------------------------------------------\n";


print "\n\nErrors: $err_cnt";
if ($err_cnt) {
        print "  [OVERALL FAIL]\n";
} else {
        print "                       \"Ah havnae seen ut us guid as this.\"\n";
}


exit($err_cnt);

# -----------------------------------------------------------------------------

sub check_count {
	my $pq = shift;
	my $options = shift;
	my $expected = shift;
	my $show_rslts = shift;

	# VS does terrible things to non-ASCII command line arguments, so
	# we need to put the query in a file and read that as a batch.
	die "Can't open temporary query file\n"
	    unless open QF, ">utf8.q";
 	print QF "$pq\n";
	close QF;
	
	#my $cmd = "$qp index_dir=$ix $base_opts -pq=\"$pq\" $options";
	my $cmd = "$qp index_dir=$ix $base_opts -file_query_batch=utf8.q -x_batch_testing=TRUE $options";
	print $cmd, "{$pq}";
	my $rslts = `$cmd`;
	die "Command '$cmd' failed with code $?\n"
		if ($?);
	print $rslts if $show_rslts;
	my $count = 0;
	my %dup_check;
	# The pattern on the next line has to match output (if any) from experimental_show()
	# in QBASHQ_lib.c -- maybe somewhere else in classifier mode???
	while ($rslts =~ /Query:\t[^\t]*\t[^\t]*\t([^\t]+).*?\t([01]\.[0-9]+)\s*\n/sg) {
		 die "\n\n$rslts\nResults for '$cmd' contain duplicate suggestions\n"
			if defined($dup_check{$1});
		 $dup_check{$1} = 1;
		 print "\n\n  ----> Result matched was '$1'.  Score was $2\n";
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
	my $pq = shift;
	my $options = shift;
	my $show_rslts = shift;
	my $k = shift;
	my @items_with_scores;
	for (my $a = 0; $a < $k; $a++) {
	    $items_with_scores[$a] = shift;
	}	
	my $missings = 0;
	my $cmd = "$qp index_dir=$ix $base_opts -pq=\"$pq\" $options";
	my $rslts = `$cmd`;
	die "Command '$cmd' failed with code $?\n"
		if ($?);

	print $rslts if $show_rslts;
	# Check for duplicates.
	my %dup_check;
	while ($rslts =~ /([^\t]+)\s+([0-9]+\.[0-9]+)\s*\n/sg) {
		die "\n\n$rslts\nResults for '$cmd' contain duplicate suggestions\n"
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



