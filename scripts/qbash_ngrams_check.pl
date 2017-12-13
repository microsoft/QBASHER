#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Tests basic functionality of Ngrams_from_TSV.exe


$|++;
$errs = 0;

die "Usage: $0 <QBASHQ binary>\n"
		unless ($#ARGV >= 0);

$qp = $ARGV[0];
$qp = "../src/visual_studio/x64/Release/QBASHQ.exe"
    if $qp eq "default";

die "$qp is not executable\n" unless -e $qp;

die "Can't find the Ngrams data directory or QBASH.forward in it.\n"
    unless -r "Ngrams/QBASH.forward";


$dexer = $qp;
$dexer =~ s/QBASHQ/QBASHI/;
$dexer =~ s/qbashq/qbashi/;
$Ngrammer = $qp;
$Ngrammer =~ s/qbashq/Ngrams_from_TSV/ig;

die "$dexer not executable\n" unless -e $dexer;
die "$Ngrammer not executable\n" unless -e $Ngrammer;


# Index the corpus.
$cmd = "$dexer index_dir=Ngrams -x_doc_length_histo=TRUE > Ngrams/index.log\n";
$code = int((system($cmd) / 256));
die "Indexing failed with code $code.  Command was $cmd\n"
    if $code;

#Run the Ngrams extractor with default settings
$cmd = "$Ngrammer index=Ngrams > Ngrams.log1 2>&1";
$code = int((system($cmd) / 256));
die "Ngram extraction failed with code $code.  Command was $cmd\n"
    if $code;

$termidsfile = "Ngrams/ngrams.termids";
print "Checking $termidsfile for run with default options: ";

die "Can't open $termidsfile\n"
    unless open N, $termidsfile;
read N, $read1, 100000;
close(N);

$errs1 = 0;
foreach $p ("\\\"bohemian rhapsody\\\"", "\\\"in the\\\"", "\\\"of hearts\\\"", "\\\"queen of\\\"",
	 "\\\"queen of hearts\\\"", "\\\"the queen\\\"", "\\\"the queen of\\\"", "\\\"the queen of hearts\\\"") {
    $errs1++ unless contains($read1, $p);
}
if ($errs1) {
    print "[FAIL]\n";
    print $read1;
    $errs++;
} else {
    print "[OK]\n";
}

#Run the Ngrams extractor with English filtering
$cmd = "$Ngrammer index=Ngrams -apply_english_filters=TRUE > Ngrams.log2 2>&1";
$code = int((system($cmd) / 256));
die "Ngram extraction failed with code $code.  Command was $cmd\n"
    if $code;

$termidsfile = "Ngrams/ngrams.termids";
print "Checking $termidsfile for run with apply_english_filters: ";


die "Can't open $termidsfile\n"
    unless open N, $termidsfile;
read N, $read2, 100000;
close (N);

$errs2 = 0;
foreach $p ("\\\"bohemian rhapsody\\\"", "\\\"in the\\\"", "\\\"queen of hearts\\\"", "\\\"the queen\\\"", 
	    "\\\"the queen of hearts\\\"") {
    $errs2++ unless contains($read2, $p);
}
foreach $p ("\\\"of hearts\\\"", "\\\"queen of\\\"", "\\\"the queen of\\\"") {
    $errs2++ if contains($read2, $p);  # Make sure these are not there!
}

if ($errs2) {
    print "[FAIL]\n";
    print $read2;
    $errs++;
} else {
    print "[OK]\n";
}

#Run the Ngrams extractor in bigrams mode
unlink "Ngrams/QBASH.bigrams";

$cmd = "$Ngrammer index=Ngrams -max_ngram_words=2 > Ngrams.log3 2>&1";
$code = int((system($cmd) / 256));
die "Ngram extraction failed with code $code.  Command was $cmd\n"
    if $code;

$errs3 = 0;
if (!(-e "Ngrams/QBASH.bigrams")) {
    print "Error: Ngrams/QBASH.bigrams should have been produced but wasn't\n";
    $errs3++;
}


$termidsfile = "Ngrams/ngrams.termids";
print "Checking $termidsfile for run with -max_ngram_words=2 : ";


die "Can't open $termidsfile\n"
    unless open N, $termidsfile;
read N, $read3, 100000;
close (N);

foreach $p ("\\\"bohemian rhapsody\\\"", "\\\"in the\\\"", "\\\"of hearts\\\"", "\\\"queen of\\\"",
	 "\\\"the queen\\\"") {
    $errs3++ unless contains($read3, $p);
}
foreach $p ("\\\"queen of hearts\\\"", "\\\"the queen of\\\"", "\\\"the queen of hearts\\\"") {
    $errs3++ if contains($read3, $p);  # Make sure these are not there!
}

if ($errs3) {
    print "[FAIL]\n";
    print $read2;
    $errs++;
} else {
    print "[OK]\n";
}




die "Sin and corruption!  $errs errors!\n"
    if ($errs);

print "\nFantastic!  :-)\n\n";
exit(0);


#----------------------------------------------------------------

sub contains {
    my $string = shift;
    my $pat = shift;
    if ($string =~ /$pat/) {return 1;}
    return 0;
}
