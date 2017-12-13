#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Tests QBASHI ability to index lat-long tile words derived from information stored in column
# 4 of the .forward file.  Now extended to include ranking by geospatial distance
# Further extended to include indexing of other special words after LAT, LONG, e.g. LANG

# Relies on the local GeoTiles collection, containing a few geospatial records.
# 07 Jun 2017: Now checks big tiles


$|++;


die "Usage: $0 <QBASHQ binary>\n"
		unless ($#ARGV >= 0);
$idxdir = "../test_data";

$qp = $ARGV[0];
$qp = "../src/visual_studio/x64/Release/QBASHQ.exe"
    if $qp eq "default";

die "$qp is not executable\n" unless -e $qp;

my $qfile = "geo_tile_check.q";


$fail_fast = 0;
$fail_fast = 1 if ($#ARGV > 0 && $ARGV[1] eq "-fail_fast");

$dexer = $qp;
$dexer =~ s/QBASHQ/QBASHI/;
$dexer =~ s/qbashq/qbashi/;

$vlister = $qp;
$vlister =~ s/QBASHQ/QBASH_vocab_lister/;
$vlister =~ s/qbashq/vocab_lister/;

$ix = "$idxdir/geo_tiles";
$fwd = "$ix/QBASH.forward";

die "Can't find $fwd\n" 
    unless -r $fwd;

$errs = 0;

print "Check that the .forward file entries each have four columns\n";
$cmd = "awk -F \"\\t\" 'NF != 4 {print \"Error\", \$0}' $ix/QBASH.forward";
print $cmd;
$rslts = `$cmd`;
die "\n\nError: GeoTiles/QBASH.forward must have 4 columns in every record.\n"
    if $rslts =~ /Error/s;
print "    [OK]\n";

$cmd = "$dexer index_dir=$ix -x_geo_tile_width=100  -x_geo_big_tile_factor=10> $ix/index.log";

$rslt = `$cmd`;
if ($?) {
    print "Indexing $ix                                                 [FAIL]\n";
    $errs++;
} else {
    print "Indexing $ix                                                 [OK]\n";
}

# List vocab and check for geotile words
$cmd = "$vlister $ix/QBASH.vocab";
$rslt = `$cmd`;
if ($?) {
    print "Listing vocab $ix                                            [FAIL]\n";
    $errs++;
} else {
    print "Listing vocab $ix                                            [OK]\n";
}

die "Can't open $ix/vocab.tsv" unless open V, "$ix/vocab.tsv";

$latwords = 0;
$longwords = 0;
$biglatwords = 0;
$biglongwords = 0;
while (<V>) {
    if (/^x\$[0-9]+/) {$longwords++;}
    elsif (/^y\$[0-9]+/) {$latwords++;}
    elsif (/^\d\d\dx\$[0-9]+/) {$biglongwords++;}
    elsif (/^\d\d\dy\$[0-9]+/) {$biglatwords++;}
}
close(V);

if ($latwords == 0 || $longwords == 0 || $biglatwords == 0 || $biglongwords == 0) {
    print "FAIL: vocab counts indicate geo indexing failure:

Lat: $latwords
Long: $longwords
Biglat: $biglatwords
Biglong: $biglongwords

";
    exit(1);
}


$base_opts = "-x_batch_testing=TRUE -display_col=3"; 
$errs += check_count('x$181', "", 4, 0);       # Duplicate entries should be eliminated
$errs += check_count('x$181 y$59', "", 4, 0);   # "
# Now check that radius limiting is working (Set the origin to Beechworth)
$errs += check_count('x$181 y$59', "-lat=-36.3473224 -long=146.6883967 -geo_filter_radius=10", 1, 0);   # Only Beechworth
$errs += check_count('x$181 y$59', "-lat=-36.3473224 -long=146.6883967 -geo_filter_radius=45", 2, 0);   # Add Wangaratta
$errs += check_count('x$181 y$59', "-lat=-36.3473224 -long=146.6883967 -geo_filter_radius=100", 3, 0);   # Add Baddaginnie
$errs += check_count('x$181 y$59', "-lat=-36.3473224 -long=146.6883967 -geo_filter_radius=150", 4, 0);   # Add Euroa

$errs += check_count('"x$181 y$59"', "", 4, 0); # "
$errs += check_count('"x$181 y$59" /8', "", 1, 0); # "
$errs += check_count('"x$181 y$59" /85', "", 1, 0); # "
$errs += check_count('"x$181 y$59" /b', "", 2, 0); # "
$errs += check_count('"x$181 y$59" rd /8', "", 1, 0); # "
$errs += check_count('[x$181 x$101] [y$59 y$154]', "", 5, 0); #Beechworth, Wangaratta, Baddaginnie, Paris 
$errs += check_count('x$181 y$59 rd /8', "", 1, 0); #Beechworth

$errs += check_count('"x$181 y$59" 8', "-auto_partials=ON", 1, 0); # "
$errs += check_count('"x$181 y$59" 85', "-auto_partials=ON", 1, 0); # "
$errs += check_count('"x$181 y$59" b', "-auto_partials=ON", 2, 0); # Beechworth and Baddaginnie
$errs += check_count('"x$181 y$59" rd 8', "-auto_partials=ON", 1, 0); # "
$errs += check_count('[x$181 x$59] [y$59 y$7] rd 8', "-auto_partials=ON", 1, 0); # "


# Now run basic checks on distance ranking. Note that, because of the coarseness 
# of the tiling,  'x$181 y$59' matches all four Victorian addresses. Use this as the
# query in all the tests

set_test_query('x$181 y$59');

$base_cmd = "$qp index_dir=$ix -file_query_batch=geo_tile_check.q -chatty=off";

print "Test D1 - no geo-ranking                                          ";
$rslts = `$base_cmd`;

if ($rslts =~ /Beechworth.*?Wangaratta.*?Baddaginnie.*?Grawking/s) {
    print "[OK]\n";
} else {
    print "[FAIL]\n";
    if ($fail_fast) {
	print $rslts;
	exit(1);
    }
    $errs++;
}

print "Test D2 - set origin at Craigieburn Rlwy Stn but ignore distance  ";
$rslts = `$base_cmd -lat=-37.6016538 -long=144.9410463`;
if ($rslts =~ /Beechworth.*?Wangaratta.*?Baddaginnie.*?Grawking/s) {
    print "[OK]\n";
} else {
    print "[FAIL]\n";
    if ($fail_fast) {
	print $rslts;
	exit(1);
    }
    $errs++;
}

print "Test D3 - set origin at Craigieburn Rlwy Stn and rank by distance ";
$rslts = `$base_cmd -lat=-37.6016538 -long=144.9410463 -alpha=0 -eta=1`;
if ($rslts =~ /Grawking.*?Baddaginnie.*?Wangaratta.*?Beechworth/s) {
    print "[OK]\n";
} else {
    print "[FAIL]\n";
    if ($fail_fast) {
	print $rslts;
	exit(1);
    }
    $errs++;
    print $rslts;
}

print "Test D4 - set origin at Beechworth and rank by distance           ";
$rslts = `$base_cmd -lat=-36.3473224 -long=146.6883967 -alpha=0 -eta=1`;
if ($rslts =~ /Beechworth.*?Wangaratta.*?Baddaginnie.*?Grawking/s) {
    print "[OK]\n";
} else {
    print "[FAIL]\n";
    if ($fail_fast) {
	print $rslts;
	exit(1);
    }
    $errs++;
    print $rslts;
}


# Now check indexing of LANG and other special words.

$base_cmd = "$qp index_dir=$ix -file_query_batch=geo_tile_check.q -chatty=off -duplicate_handling=0";

print "Test E1 - retrieving by language                                          ";
set_test_query('l$FR');
$rslts = `$base_cmd`;
if ($rslts =~ /Eiffel.*?Eiffel/s) {
    print "[OK]\n";
} else {
    print "[FAIL]\n";
    if ($fail_fast) {
	print $rslts;
	exit(1);
    }
    $errs++;
    print $rslts;
}
print "Test E2 - retrieving by country  (FR)                                       ";
set_test_query('c$FR');
$rslts = `$base_cmd`;
if ($rslts =~ /Eiffel.*?Eiffel/s) {
    print "[OK]\n";
} else {
    print "[FAIL]\n";
    if ($fail_fast) {
	print $rslts;
	exit(1);
    }
    $errs++;
    print $rslts;
}

print "Test E3 - retrieving by country (NZ)                                        ";
set_test_query('c$NZ');
$rslts = `$base_cmd`;
if ($rslts =~ /Wellington.*?Wellington/s) {
    print "[OK]\n";
} else {
    print "[FAIL]\n";
    if ($fail_fast) {
	print $rslts;
	exit(1);
    }
    $errs++;
    print $rslts;
}

# Now check the span limit option -x_max_span_length
$base_opts = "-x_batch_testing=TRUE -display_col=3"; 

$errs += check_count("Molesworth N", "-auto_partials=true", 1, 0);
$errs += check_count("Molesworth N", "-auto_partials=true -x_max_span_length=4", 1, 0);
$errs += check_count("Molesworth N", "-auto_partials=true -x_max_span_length=3", 0, 0);
$errs += check_count("Molesworth N", "-auto_partials=true -x_max_span_length=2", 0, 0);
$errs += check_count("Molesworth NZ ", "-auto_partials=true -x_max_span_length=3", 1, 0);  #Only effective when prefixes are being matched.

print "Checking that bigtiles have been indexed and are searchable ...\n";

$errs += check_count('010x$18', "-auto_partials=true", 5, 0);
$errs += check_count('010y$4', "-auto_partials=true", 5, 0);

if ($errs) {
    print "\n\nOutrageous!  $0 failed with $errs errors\n";
} else {
    print "\n\n       Keep right on to the end of the road.  :-)  $0 passed!!\n";
}
exit($err_cnt);

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


