#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# More thorough exploration of the number of results returned by queries
# involving disjunctions.

# Relies on the AcademicID collection, chosen because it is fairly large.


$|++;


die "Usage: $0 <QBASHQ binary>\n"
		unless ($#ARGV >= 0);

$qp = $ARGV[0];
$qp = "../src/visual_studio/x64/Release/QBASHQ.exe"
    if $qp eq "default";

die "$qp is not executable\n" unless -e $qp;

my $qfile = "disjunctions_check.q";


$fail_fast = 0;
$fail_fast = 1 if ($#ARGV > 0 && $ARGV[1] eq "-fail_fast");

$dexer = $qp;
$dexer =~ s/QBASHQ/QBASHI/;
$dexer =~ s/qbashq/qbashi/;

$vlister = $qp;
$vlister =~ s/QBASHQ/QBASH_vocab_lister/;
$vlister =~ s/qbashq/vocab_lister/;

$ix = "../test_data/wikipedia_titles";
$base_opts = "-max_to_show=1000000 -max_candidates=1000000 -x_batch_testing=TRUE -display_col=1 -duplicate_handling=2";

$errs = 0;

foreach $qwd ("eidgenoessische"
	      , "effigies", "elektrogorsk", "jäätynyt", "djurdjević", "of"
    ) {
    $errs += are_the_counts_the_same("$qwd", "$qwd", "");
    $errs += are_the_counts_the_same("$qwd", "[$qwd]", "");
    $errs += are_the_counts_the_same("$qwd", "[$qwd $qwd $qwd]", "");
    $errs += are_the_counts_the_same("$qwd results", "[$qwd $qwd] results", "");
    $errs += are_the_counts_the_same("unusual $qwd results", "unusual [$qwd $qwd] results", "");
}

$errs += are_the_counts_the_same("[mytosis photosynthesis electrophoresis]", 
				 "[electrophoresis photosynthesis mytosis]", "");

## Note:  If there's ever a need to make this test work against a different data set,
## you may find the ./find_repeated_words_phrases_in_docs.pl script handy for finding
## example words and bigrams.

$errs += validate_count("nathália", "erythropoetic", "");
$errs += validate_count("erythropoetic", "Supino", "");
$errs += validate_count("clupeiformes", "stolephorus", "");
$errs += validate_count("cymol", "harzwasserwerke", "");

# Testing for regression on an error encountered by Bodo
$errs += check_count("galaxies clusters galaxies", "", 7, 0);
$errs += check_count("galaxies clusters [galaxies universe]", "", 7, 0);  # Should get >= the same results as the previous one
$errs += check_count("galaxies clusters [universe galaxies]", "", 7, 0);  # Should get >= the same results as the previous one
$errs += check_count("[universe galaxies] galaxies clusters", "", 7, 0);  # Should get >= the same results as the previous one
$errs += check_count("iraq invasion iraq", "", 1, 0);
$errs += check_count("iraq invasion [iraq babylon]", "", 1, 0);  # Should get >= the same results as the previous one
$errs += check_count("social actors social", "", 2, 0);
$errs += check_count("social actors [social effective]", "", 2, 0);  # Should get >= the same results as the previous one
$errs += check_count("[social effective] social actors", "", 2, 0);  # 


# Testing for the same problem in phrase-within disjunction
$errs += check_count('"central african" affairs ["central african" CAR]', "", 1, 0);  # 
$errs += check_count('"central african" affairs [CAR "central african"]', "", 1, 0);  # 
$errs += check_count('[CAR "central african"] "central african" affairs', "", 1, 0);  # 
$errs += check_count('["llewellyn george" "lloyd george"] "lloyd george" frances', "", 2, 0);  # 
$errs += check_count('"central african" affairs ["central african" kelvin]', "", 1, 0);  # 
# 


# Now what about repeated phrases without disjunctions?
$errs += check_count('"lloyd george" owen "lloyd george"', "", 1, 0);  # 
$errs += check_count('"central african" affairs "central african"', "", 1, 0);  # 

# How about repeated phrases containing disjunctions?
$errs += check_count('"central [african australian]" affairs ""central [african australian]"', "", 1, 0);  # 
$errs += check_count('"lloyd [george titmus]" earl "lloyd [george titmus]"', "", 4, 0);  # 

$errs += check_count('"central [african australian region]" affairs ""central [african australian region]"', "", 1, 0);  # 
$errs += check_count('"lloyd [george titmus alfred]" earl "lloyd [george titmus alfred]"', "", 4, 0);  # 


die "\nScandalous! Errors encountered in $0: $errs\n"
    if ($errs);

print "\n     You had to be there!  (We were.)\n";
exit(0);

# --------------------------------------------------------------------

sub set_test_query {
    my $query = shift;
    die "Can't open >$qfile" unless open Q, ">$qfile";
    print Q "$query\n";
    close Q;
}

sub check_count {
    my $pq = shift;
    my $options = shift;
    my $expected = shift;
    my $show_rslts = shift;
    
    set_test_query($pq); 
    my $cmd = "$qp index_dir=$ix $base_opts $options -file_query_batch=$qfile -max_to_show=100";
    print "$pq {$options}";
    my $rslts = `$cmd`;
    die "Command '$cmd' failed with code $?\n"
	if ($?);
    print $rslts if $show_rslts;
    my $count = 0;
    my %dup_check;
    while ($rslts =~ /Query:\t[^\t]+\t[0-9]+\t([^\t]+)\t[01]\.[0-9]+\s*\n/sg) {
	die "\n\n$rslts\nResults for '$cmd' contain duplicate suggestions\n"
	    if defined($dup_check{$1});
	$dup_check{$1} = 1;
	$count++;
    }
    print sprintf(" - %4d %4d - ", $expected, $count);
    if ($count == $expected) {
	print " [OK]\n";
	unlink $qfile;
    }
    else {
	print " [FAIL]\n";
	if ($fail_fast) {
	    my $rslts = `$cmd`;
	    print "\n", $rslts;
	    print "\nQuery file retained: cat $qfile\nCommand was: $cmd\n";
	    exit(1);
	}
	return 1;
    }
    return 0;
}


sub are_the_counts_the_same {
    my $pq1 = shift;
    my $pq2 = shift;
    my $options = shift;
    
    set_test_query($pq1); 
    my $cmd = "$qp index_dir=$ix $base_opts $options -file_query_batch=$qfile";
    my $rslts = `$cmd`;
    die "Command '$cmd' failed with code $?\n"
	if ($?);
      my $count1 = 0;
    while ($rslts =~ /Query:\t[^\t]+\t[0-9]+\t([^\t]+)\t[01]\.[0-9]+\s*\n/sg) {
	$count1++;
    }

    
    set_test_query($pq2); 
    $cmd = "$qp index_dir=$ix $base_opts $options -file_query_batch=$qfile";
    $rslts = `$cmd`;
    die "Command '$cmd' failed with code $?\n"
	if ($?);
    my $count2 = 0;
    while ($rslts =~ /Query:\t[^\t]+\t[0-9]+\t([^\t]+)\t[01]\.[0-9]+\s*\n/sg) {
	$count2++;
    }

    print "Run with {$options} '$pq1' v '$pq2' -- $count1 v $count2     ";
    if ($count1 == $count2) {
	print "[OK]\n";
	return 0;
    } else {
	print "[FAIL]\n";
	return 1;
    }
}


sub validate_count {
    # Assuming there are no effects due to result list truncation,
    # count([a b]) should equal count(a) + count(b) - count(a b)

    my $a = shift;
    my $b = shift;
    my $options = shift;
    
    set_test_query($a); 
    my $cmd = "$qp index_dir=$ix $base_opts $options -file_query_batch=$qfile";
    my $rslts = `$cmd`;
    die "Command '$cmd' failed with code $?\n"
	if ($?);
    my $count1 = 0;
    while ($rslts =~ /Query:\t[^\t]+\t[0-9]+\t([^\t]+)\t[01]\.[0-9]+\s*\n/sg) {
	$count1++;
    }

    set_test_query($b); 
    $cmd = "$qp index_dir=$ix $base_opts $options -file_query_batch=$qfile";
    $rslts = `$cmd`;
    die "Command '$cmd' failed with code $?\n"
	if ($?);
    my $count2 = 0;
    while ($rslts =~ /Query:\t[^\t]+\t[0-9]+\t([^\t]+)\t[01]\.[0-9]+\s*\n/sg) {
	$count2++;
    }

    set_test_query("$a $b"); 
    $cmd = "$qp index_dir=$ix $base_opts $options -file_query_batch=$qfile";
    $rslts = `$cmd`;
    die "Command '$cmd' failed with code $?\n"
	if ($?);
    my $count3 = 0;
    while ($rslts =~ /Query:\t[^\t]+\t[0-9]+\t([^\t]+)\t[01]\.[0-9]+\s*\n/sg) {
	$count3++;
    }

    set_test_query("[$a $b]"); 
    $cmd = "$qp index_dir=$ix $base_opts $options -file_query_batch=$qfile";
    $rslts = `$cmd`;
    die "Command '$cmd' failed with code $?\n"
	if ($?);
    my $count4 = 0;
    while ($rslts =~ /Query:\t[^\t]+\t[0-9]+\t([^\t]+)\t[01]\.[0-9]+\s*\n/sg) {
	$count4++;
    }

    print "Validating [$a $b]:  $count1 + $count2 - $count3 should equal $count4.  ";
    if (($count1 + $count2 - $count3) == $count4) {
	print "[OK]\n";
	return 0;
    } else {
	print "[FAIL]\n";
	return 1;
    }
}


