#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Tests that, when QBASHI is given the -x_bigger_trigger=TRUE option, it correctly 
# indexes very long documents.


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
$term_ratios = $qp;
$term_ratios =~ s/qbashq/TFdistribution_from_TSV/ig;

die "$dexer not executable\n" unless -e $dexer;
die "$term_ratios not executable\n" unless -e $term_ratios;

 
$s1 = "The quick brown fox jumps over the lazy dog again but much more rapidly this time. ";  # 16 words
$s2 = "Ask what you can do for your country not what your country can do for you. ";  #16 words
$s3 = "She was married when we first met, soon to be divorced. I helped her out of a jam I guess. ";  #20 words
$s4 = "Yesterday upon the stair I spied a man who wasn't there. He wasn't there again today.  How I wish he'd go away. ";  #25
$s5 = "It is a truth universally acknowledged, that a single man in possession of a good fortune must be in want of a wife. "; #23

$combo = $s1.$s2.$s3.$s4.$s5;  # 100 words

#Create the data in a teporary area to reduce risk of catastrophe.
mkdir "Long_Documents_Tempdata"
    unless -d "Long_Documents_Tempdata";

die "Can't create Long_Documents_Tempdata/QBASH.forward file\n"
    unless open F, ">Long_Documents_Tempdata/QBASH.forward";

$oreps = 20;  # outer reps -- Also the number of documents
$ireps = 10;  # inner reps
$combolen = 100;  # indexable word occurrences in $combo
$distincts = 73; # Distinct words in $combo
$patlen = $ireps * $combolen;
$errs = 0;
$words = 0;

for ($i = 1; $i <= $oreps; $i++) {
    # Each iteration writes a record with $i repetitions of the basic pattern
    for ($j = 1; $j <= $i; $j++) {
	# $k loop generates the pattern which is 10 repetitions of the 100 word combo.
	for ($k = 1; $k <= $ireps; $k++) {
	    print F $combo;
	    $words += $combolen;
	}
    }
    print F "\t$i\n";
    $docs++;
}

close(F);

print "Long_Documents_Tempdata/QBASH.forward written with $docs documents and $words word occurrences.\n";

# Now index the corpus thus created.
$cmd = "$dexer index_dir=Long_Documents_Tempdata -x_bigger_trigger=TRUE -x_doc_length_histo=TRUE > Long_Documents_Tempdata/index.log\n";
$code = int((system($cmd) / 256));
die "Indexing failed with code $code.  Command was $cmd\n"
    if $code;

$errs += check_index_log($oreps, $words);

$errs += check_doclenhist($oreps, $patlen);

$errs += check_term_ratios($oreps, $patlen);

die "Blast and botheration!  $errs errors!\n"
    if ($errs);

print "\nMay the blue bird of happiness NOT rupture your rubber duck!  :-)\n\n";
exit(0);


#----------------------------------------------------------------


sub check_index_log {
    $numdox = shift;
    $totpostings = shift;
    my $err_count = 0;

    $rslt = `grep "Total postings: " Long_Documents_Tempdata/index.log`;
    die "First grep failed\n" if $?;
    $rslt =~ /Total postings: (\d+)/;
    if ($1 != $totpostings) {
	print "Error: total postings should be $totpostings, is $1\n";
	$err_count++;
    }

    $rslt = `grep "sec. to index " Long_Documents_Tempdata/index.log`;
    die "2nd grep failed\n" if $?;
    $rslt =~ /sec. to index (\d+) docs/;
    if ($1 != $numdox) {
	print "Error: no. documents should be $numdox, is $1\n";
	$err_count++;
    }
    print "   Check of index.log                                [OK]\n"
	unless $err_count;
    return $err_count;
}


sub check_doclenhist {
    my $numdox = shift;
    my $patlen = shift;

    my $explen = $patlen;  # Expected length of first doc
    my $err_count = 0;
    my $rec = 0;

    die "Can't open Long_Documents_Tempdata/QBASH.doclenhist\n"
	unless open DLH, "Long_Documents_Tempdata/QBASH.doclenhist";

    while (<DLH>) {
	next if (/^\s*#/);  # Ignore comment lines
	next unless (/\s*(\d+)\t(\d+)/);
	next if ($2 == 0);
	$len = $1;
	$cnt = $2;
	$rec++;
	if ($rec > $numdox) {
	    print "Error: number of non-zero counts in doclenhist exceeds $numdox\n";
	    $err_count++;
	    return $err_count;
	}

	if ($cnt != 1) {
	    print "Error: all counts in the doclenhist should be one. Count at line $rec is $cnt\n";
	    $err_count++;
	    return $err_count;
	}

 	if ($len != $explen) {
	    print "Error: Length of line $rec should be $explen, is $len.\n";
	    $err_count++;
	    return $err_count;
	}
	$explen += $patlen;
    }  
    close DLH;

    print "   Check of QBASH.doclenhist                         [OK]\n"
	unless $err_count;
    return $err_count;

}


sub check_term_ratios {
    my $numdox = shift;
    my $patlen = shift;

    my $explen = $patlen;  # Expected length of first doc
    my $cnt = 0;
    my $err_count = 0;

    $cmd = "$term_ratios Long_Documents_Tempdata/QBASH.forward -singletons_too\n";
    $rslt = `$cmd`;

    die "$cmd failed.\n" if $?;
    die "Can't open Long_Documents_Tempdata/term_ratios.tsv\n"
	unless open TR, "Long_Documents_Tempdata/term_ratios.tsv";
    while (<TR>) {
	next if (/^\s*#/);  # Ignore comment lines
	if (/\s*(\d+)\t(\d+)\t(\d+)\t/) {
	    $line = $1; $len = $2; $dists = $3; 

	    if ($cnt > $numdox) {
		print "Error: number of docs in term_ratios.tsv exceeds $numdox\n";
		$err_count++;
		return $err_count;
	    }

	    if (++$cnt != $line) {
		print "Error: Record number $line is not the $cnt-th non-comment record.\n";
		$err_count++;
		return $err_count;
	    }

	    if ($dists != $distincts) {
		print "Error: Number of distinct words should be $distincts, is $dists.\n";
		$err_count++;
		return $err_count;
	    }
	} else {
	    die "Unmatched line $_\n";
	}
    }
    close TR;
    print "   Check of term_ratios.tsv                          [OK]\n"
	unless $err_count;
    return $err_count;

}
