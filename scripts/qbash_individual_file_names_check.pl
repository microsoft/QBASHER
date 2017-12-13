#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Checks that the usage modes developed for Aether (specifying all the 
# index files individually rather than an index directory) work correctly

# Also test the use of -file_query_stream and -file_output

# Assumes run in a directory with the following subdirectories:

#$bindir = "bin";
$idxdir = "../test_data";
$tqdir = "../test_queries";

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

$ixd = "$idxdir/wikipedia_titles_500k";
$fwd = "$ixd/QBASH.forward";
$ifntd = "$idxdir/individual_file_names_test";
$stem = "$ifntd/different";
$QFILE = "$tqdir/emulated_log_10k.q";
die "Can't read $QFILE\n" unless -r $QFILE;
$OFILE = "./individual.out";

die "Can't find $fwd\n" 
	unless -r $fwd;

die "$ifntd is not a directory\n"
	unless -d $ifntd;

print "Indexing using index_dir ...\n";
$cmd = "$dexer index_dir=$ixd > $ixd/index.log";
`$cmd`;
die "Command '$cmd' failed with code $?\n"
	if ($?);


print "Indexing using explicit file specs ...\n";
$cmd = "$dexer -file_forward=$fwd -file_if=$stem.if -file_vocab=$stem.vocab -file_doctable=$stem.doctable > $ifntd/index.log";
`$cmd`;
die "Command '$cmd' failed with code $?\n"
	if ($?);

for $query ("soap", "\"water heater\"", "\"tom cruise height\"") {

    print "Querying for $query with the old style index ...\n";
    $cmd = "$qp -index_dir=$ixd -pq=$query";
    $rslts_old = `$cmd`;
    die "Command '$cmd' failed with code $?\n"
	if ($?);


    print "Querying for $query with the Individual style index ...\n";
    $cmd = "$qp -file_forward=$fwd -file_if=$stem.if -file_vocab=$stem.vocab -file_doctable=$stem.doctable -pq=$query";
    $rslts_individual = `$cmd`;
    die "Command '$cmd' failed with code $?\n"
	if ($?);
    die "Results from Individual mode are different to the old mode.

Individual: $rslts_individual


Old: $rslts_old

" unless $rslts_individual eq $rslts_old;
}




print "\nNow running $QFILE against index and putting results in $OFILE\n";

$cmd = "$qp -file_forward=$fwd -file_if=$stem.if -file_vocab=$stem.vocab -file_doctable=$stem.doctable -file_query_batch=$QFILE -file_output=$OFILE\n";

$rslts = `$cmd`;
die "Error code $? from '$cmd'\n" if $?;
die "Output appearing on stdout for '$cmd'\n\n$rslts\n\n" if ($rslts =~ /\S/);
die "Can't open $OFILE\n" unless open OF, $OFILE;
$qcnt = 0;
while (<OF>) {
   $qcnt++ if /Query:/;
}
close(OF); 
die "Number of occurrences of 'Query:' in $OFILE should be 10,000 but is $qcnt\nCommand was $cmd\n"
    unless $qcnt == 10000;

print "Confirmed that:
   0. The input file of queries was read;
   1. No non-whitespace output appeared in outfile; and
   2. The correct number of 'Query: ' lines appeared in output.

";


print "\nAll's well that ends well.\n";
exit(0);
