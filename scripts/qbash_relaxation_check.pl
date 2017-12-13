#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Assumes run in a directory with the following subdirectories:

$idxdir = "../test_data";


$ix = "$idxdir/wikipedia_titles";

die "Usage: $0 <QBASHQ_binary> 
   Note: This script expects a current index in $ix\n" 
	unless ($#ARGV >= 0);

$qp = $ARGV[0];
$qp = "../src/visual_studio/x64/Release/QBASHQ.exe"
    if $qp eq "default";

die "$qp is not executable\n" unless -e $qp;

$fail_fast = 0;
$fail_fast = 1 if ($#ARGV > 0 && $ARGV[1] eq "-fail_fast");


die "Can't find QBASHER indexes in $ix\n" 
	unless (-r "$ix/QBASH.if");


$base_cmd = "$qp -index_dir=$ix -max_candidates=200 -max_to_show=200";

foreach $query ("St Anne Catholic Church Greensboro North Carolina",
    "100 photographs that changed the world",
    "tanzanian high commission in Canada") {
    # Make all of the relaxation_level=1 sub-queries
    print "Query is {$query}.  It's results are:\n";
    $rslts = `$base_cmd -pq="$query"`;
    print $rslts, "\n";

    @qwds = split /\s+/, $query;
    for ($missout = 0; $missout <= $#qwds; $missout++) {
	$first = 1;
	$sq = "";
	for ($w = 0; $w <= $#qwds; $w++) {
	    if ($w != $missout) {
		$sq .= " " if (!$first);
		$first = 0;
		$sq .= $qwds[$w];
	    }
	}
	$rslts[$missout] = `$base_cmd -pq="$sq"`;
	print "\n\n{$sq}\n$rslts[$missout]\n";
    }

    $relaxed = `$base_cmd -pq="$query" -relaxation_level=1`;

    print "\n\nRelaxation results:\n", $relaxed, "\n";


    $errs = check_a_in_b($rslts, $relaxed);
}

if ($errs) {print "\n\nGaak!  $errs encountered by $0\n";}
else {print "\n\nMost pleasing!  All tests passed.\n";}

exit($errs);

# ------------------------------------------------------------

sub check_a_in_b {
    my $a = shift;
    my $b = shift;

    # Escape all the metacharacters in the pattern $a
    $a =~ s/([.{}()*+])/\\$1/g;

    while ($a =~ /(.*?\t[0-9.]+)/sg) {
	my $r = $1;
	print $r;
	if ($b =~ /$a/) {
	    print "\t [OK]\n";
	} else {
	    print "\t [NOT FOUND]\n";
	    die "\n" if $fail_fast;
	}
    }
}
