#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Check the operation and robustness of the QBASHQ street address processing capability

die "Usage: $0 <QBASHQ binary>\n"
    unless ($#ARGV >= 0);

$idxdir = "../test_data/";

$qp = $ARGV[0];
$qp = "../src/visual_studio/x64/Release/QBASHQ.exe"
    if $qp eq "default";

die "$qp is not executable\n" unless -e $qp;

$fail_fast = 0;
$fail_fast = 1 if ($#ARGV > 0 && $ARGV[1] eq "-fail_fast");

$dexer = $qp;
$dexer =~ s/QBASHQ/QBASHI/;
$dexer =~ s/qbashq/qbashi/;

$ix = "$idxdir/street_addresses";
$fwd = "$ix/QBASH.forward";

die "Can't find $fwd\n" 
    unless -r $fwd;

$errs = 0;

print "Check that the .forward file entries each have three columns\n";
$cmd = "awk -F \"\\t\" 'NF != 3 {print \"Error\", \$0}' $fwd";
print $cmd;
$rslts = `$cmd`;
die "\n\nError: $fwd must have 3 columns in every record.\n"
    if $rslts =~ /Error/s;
print "  [OK]\n";

$cmd = "$dexer index_dir=$ix > $ix/index.log";

`$cmd`;
if ($?) {
    print "Indexing $ix                                                 [FAIL]\n";
    $errs++;
} else {
    print "Indexing $ix                                                 [OK]\n";
}

$base_opts = "-display_col=1 -street_address_processing=2 -street_specs_col=3 -use_substitutions=true";
$tmpqfile = "tmp_street_addresses.q";


$errs += check_count("60 Ormond St", "ormond st", "", 1, 0);
$errs += check_count("60 Ormond . St", "ormond st", "", 1, 0);
$errs += check_count("156 Ormond St", "ormond st", "", 0, 0);
$errs += check_count("5/52 Ormond St", "ormond st", "", 1, 0);
$errs += check_count("5/526 Ormond St", "ormond st", "", 0, 0);
$errs += check_count("60 Ormond Street ACT Turner 2602", "ormond street act turner 2602", "", 1, 0);
$errs += check_count("57 Ormond Street 2602 ACT Turner", "ormond street 2602 act turner", "", 1, 0);
$errs += check_count("700 51st st", "51st st", "", 1, 0);
$errs += check_count("710 51st st", "51st st", "", 1, 0);
$errs += check_count("711 51st st", "51st st", "", 0, 0);
$errs += check_count("712 51st st", "51st st", "", 1, 0);
$errs += check_count("1090 51st st", "51st st", "", 1, 0);
$errs += check_count("242 creighton siding", "creighton siding", "", 1, 0);
$errs += check_count("243 creighton siding", "creighton siding", "", 0, 0);

print "Trying  a few more variants on 60 Ormond St ...\n";
$errs += check_count("60A Ormond St", "ormond st", "", 1, 0);
$errs += check_count("60bis Ormond St", "ormond st", "", 1, 0);
$errs += check_count("#60 Ormond St", "ormond st", "", 1, 0);
$errs += check_count("n60 Ormond St", "ormond st", "", 1, 0);   # 1.5.111
$errs += check_count("#60B Ormond St", "ormond st", "", 1, 0);
$errs += check_count("n60B Ormond St", "ormond st", "", 1, 0);  # 1.5.111
$errs += check_count("12/60 Ormond St", "ormond st", "", 1, 0);
$errs += check_count("58-60 Ormond St", "ormond st", "", 1, 0);
$errs += check_count("apt 30, 60 Ormond St", "ormond st", "", 1, 0);
$errs += check_count("60A Ormond St, apartment 30", "ormond st", "", 1, 0);
$errs += check_count("unit 15, 60A Ormond St", "ormond st", "", 1, 0);
$errs += check_count("60A Ormond St, suite 213", "ormond st", "", 1, 0);


print "Let's try a few street-number-only queries.  Use 'Australia' to generate the candidate set\n";

$errs += check_count("Australia 52", "australia", "", 1, 0);
$errs += check_count("52 Australia", "australia", "", 1, 0);
$errs += check_count("59 Australia", "australia", "", 0, 0);
$errs += check_count("7 Australia", "australia", "", 0, 0);
$errs += check_count("380 Australia", "australia", "", 1, 0);

print "Now see if street-number-only queries work with auto_partials=on.\n";

$errs += check_count("Australia 52", "australia", "-auto_partials=on", 1, 0);
$errs += check_count("Australia 59", "australia", "-auto_partials=on", 0, 0);
$errs += check_count("Australia 7", "australia", "-auto_partials=on", 0, 0);
$errs += check_count("Australia 999", "australia", "-auto_partials=on", 0, 0);
$errs += check_count("Australia 380", "australia", "-auto_partials=on", 1, 0);

print "And a few more auto_partials=on tests .....\n";
$errs += check_count("60 Ormond Street ACT Turner 2602", "ormond street act turner 2602", 
		     "-auto_partials=on", 1, 0);
$errs += check_count("60 Ormond Street ACT Turner 260", "ormond street act turner 260", 
		     "-auto_partials=on", 1, 0);
$errs += check_count("60 Ormond Street ACT Turner 2", "ormond street act turner 2", 
		     "-auto_partials=on", 1, 0);
$errs += check_count("60 Ormond Street ACT Tur", "ormond street act tur", 
		     "-auto_partials=on", 1, 0);
$errs += check_count("60 Ormond Street A", "ormond street a", 
		     "-auto_partials=on", 1, 0);
$errs += check_count("60 Ormond St", "ormond st", 
		     "-auto_partials=on", 1, 0);
$errs += check_count("australia 60 Orm", "australia orm", 
		     "-auto_partials=on", 1, 0);

if ($errs) {
    print "\n\nHorrendous!  $0 failed with $errs errors\n";
} else {
    print "\n\n       Zip-a-dee-doo-dah!  $0 passed!!\n";
}
exit($errs);



# -------------------------------------------------------------------------------------------

sub check_count {
    my $query = shift;
    my $expected_rewrite = shift;
    my $options = shift;
    my $expected = shift;
    my $show_rslts = shift;

    die "Can't open >$tmpqfile\n"
	unless open QF, ">$tmpqfile";
    print QF "$query\n";
    close(QF);
    
    my $cmd = "$qp index_dir=$ix $base_opts -file_query_batch=$tmpqfile -x_batch_testing=TRUE -display_parsed_query=TRUE $options";
    print $query;
    my $rslts = `$cmd`;
    die "\n\nCommand '$cmd' failed with code $?\n"
	if ($?);
    print "\n\nOutput from above command: \n", $rslts if $show_rslts;

    $rslts =~ m@Query after street address processing is \{(.*?)\}@s;
    my $rewritten = $1;
    if ($rewritten ne $expected_rewrite) {
	print " [REWRITING FAILED $rewritten] ";
    }
    
    my $count = 0;
    my %dup_check;
    while ($rslts =~ /Query:\s+?[^\t]+\t[0-9]+\t([^\t]+)\t/sg) {
	die "\n\n$rslts\nResults for '$cmd' contain duplicate suggestions\n"
	    if defined($dup_check{$1});
	$dup_check{$1} = 1;
	$count++;
    }
    print sprintf(" - %4d %4d - ", $expected, $count);
    if ($count == $expected && $rewritten eq $expected_rewrite) {
        print " [OK]\n";
    } else {
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


