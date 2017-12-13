#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Tests that QBASHI avoids crashing or looping and generates an index
# which gives the correct results for a bunch of different queries,
# across a wide range of indexing option combinations.

# Relies on the Top500M collection, chosen as one which:
#  1. is already sorted by descending score, and so should produce 
#     the same results whether sorted by QBASHI or not
#  2. is short enough to be indexed in reasonable time
#  3. is big enough to generate skip blocks and high frequency terms

# Not that two artificial lines have been appended to the Top5M dataset
# one with a zero score, to check for regressions on zero score docs.
#     bogusbogusbogus 13
#     uniquelastline  0


# Assumes run in a directory with the following subdirectories:

$idxdir = "../test_data";

$|++;


die "Usage: $0 <QBASHQ binary>\n"
		unless ($#ARGV >= 0);

$qp = $ARGV[0];
$qp = "../src/visual_studio/x64/Release/QBASHQ.exe"
    if $qp eq "default";

die "$qp is not executable\n" unless -e $qp;


$fail_fast = 0;
$fail_fast = 1 if ($#ARGV > 0 && $ARGV[1] eq "-fail_fast");

$dexer = $qp;
$dexer =~ s/QBASHQ/QBASHI/;
$dexer =~ s/qbashq/qbashi/;

$ix = "$idxdir/wikipedia_titles_5M";
$fwd = "$ix/QBASH.forward";

die "Can't find $fwd\n" 
	unless -r $fwd;

@option_sets = (
    "-sort_records_by_weight=false -sb_trigger=0 -x_chunk_func=0",
    "-sort_records_by_weight=false -sb_trigger=0 -x_chunk_func=2007",
    "-sort_records_by_weight=false -sb_trigger=0 -x_chunk_func=2007 -x_use_vbyte_in_chunks=true",
    "-sort_records_by_weight=false -sb_trigger=0",
    "-sort_records_by_weight=false -sb_trigger=0 -x_use_vbyte_in_chunks=true",
    "-sort_records_by_weight=false -sb_trigger=0 -x_2postings_in_vocab=true",
    "-sort_records_by_weight=false -sb_trigger=0 -x_2postings_in_vocab=true -x_use_vbyte_in_chunks=true",
    "-sort_records_by_weight=false -x_chunk_func=0",
    "-sort_records_by_weight=false -x_chunk_func=2007",
    "-sort_records_by_weight=false",
    "-sort_records_by_weight=false -x_chunk_func=0 -x_use_vbyte_in_chunks=true",
    "-sort_records_by_weight=false -x_chunk_func=2007 -x_use_vbyte_in_chunks=true",
    "-sort_records_by_weight=false -x_use_vbyte_in_chunks=true",
    "-sort_records_by_weight=false -x_2postings_in_vocab=true",
    "-sort_records_by_weight=false -x_2postings_in_vocab=true -x_use_vbyte_in_chunks=true",
    "-sort_records_by_weight=false -x_hashprobe=1",
    "-sb_trigger=0",
    "-sb_trigger=0 -x_use_vbyte_in_chunks=true",
    "",
    "-x_use_vbyte_in_chunks=true",
    "-max_line_prefix=15 -x_use_vbyte_in_chunks=true",
    );

$err_cnt = 0;
    
foreach $opts (@option_sets) {   
    print "$opts:\t";
    $cmd = "$dexer index_dir=$ix $opts > $ix/index.log; tail -n 1 $ix/index.log";
    $tail1 = `$cmd`;
    die "Command '$cmd' failed with code $?\n"
	if ($?);

    if ($tail1 =~ /elapsed time ([0-9.]+).*?to index ([0-9.]+)/) {
	print "$1 sec.  $2 docs";
    } else {
	die "Mangled output.  $tail1\n\nCommand was: $cmd\n\n";
    }

    # Now check some queries against the index.

    $errs = 0;

    $errs += check_results_contain("chile at the fifa world cup", "", 0, 1,
				"Chile at the FIFA World Cup");

    if ($opts =~ /-sort_records_by_weight=false/) {  # ranking depends upon index order
	$errs += check_results_contain("water", "", 0, 5,
				    "Water \\(classical element\\)",
				    "Nuclear salt-water rocket",
				    "North Atlantic Deep Water",		    
				    "Water",
				    "Brackish water"
	    );
	
	$errs += check_results_contain("\\\"hey jude\\\"", "", 0, 4,
				    "Hey jude",
				    "Hey Jude \\(album\\)",
				    "Hey Jude",
				    "Hey, Jude"
	    );
	
	$errs += check_results_contain("hey /j", "", 0, 4,
				    "Hey Jude",
				    "Hey Joe",
				    "Hey Jude \\(album\\)",
				    "Hey Jealousy"
	    );
     } else {
	$errs += check_results_contain("Water", "", 0, 5,
				    "Water",
				    "Creedence Clear Water Revival",
				    "Water vascular system",
				    "Distilled water",		    				    
				    "Distilled Water"
	    );
	
	$errs += check_results_contain("\\\"hey jude\\\"", "", 0, 4,
				    "Hey jude",
				    "Hey Jude",
				    "Hey Jude \\(album\\)",
				    "Hey, Jude"
	    );
	$errs += check_results_contain("hey /j", "", 0, 4,
				    "Hey jude",
				    "Hey Jude",
				    "Hey Joe",
				    "Hey Jude \\(album\\)"
	    );
    }

	
    $errs += check_results_contain("antimony pentachloride", "", 0, 1,
				"Antimony pentachloride"
	);

    if ($errs) { print "\t[FAIL]\n";}
    else { print "\t[OK]\n";}
    
    if ($opts =~ /max_line_prefix/) {
	print "\nExtra tests of line prefixes ...\n";
	$errs += check_result_count("antim", "", 0, 0);
	
 	$errs += check_results_contain(">antim", "", 0, 3,
				    "Antimatter",
				    "Antimony",
				    "Antimicrobial resistance"
	    );

	# The use of beta, gamma, delta options is to check for a regression
	# which caused segfaults due to an error in document length counts
	# caused by line_prefix indexing
	$errs += check_results_contain("antim", "-auto_line_prefix=on -beta=1", 0, 3,
				    "Antimatter",
				    "Antimony",
				    "Antimicrobial resistance"
	    );
	$errs += check_result_count("\"antim\"", "-auto_line_prefix=on -gamma=1", 0, 0);
	$errs += check_result_count("[antim]\"", "-auto_line_prefix=on -delta=0.5", 0, 0);
    }
    $err_cnt += $errs;

}

die "\nSin and corruption! $err_cnt failures.\n"
    if ($err_cnt);

print "\nAnother day, another dollar.  :-)\n";
exit(0);


#----------------------------------------------------------------


sub check_results_contain {
	my $pq = shift;
	my $options = shift;
	my $show_rslts = shift;
	my $k = shift;
	my @patterns;
	for (my $a = 0; $a < $k; $a++) {
		$patterns[$a] = shift;
	}	

	my $cmd = "$qp index_dir=$ix -pq=\"$pq\" $options -chatty=off";
	#print $cmd, "\n";
	my $rslts = `$cmd`;
	die "Command '$cmd' failed with code $?\n"
		if ($?);

	# Strip shard statistics out of $rslts
	$rslts =~ s/\s+Shard\s+[0-9]+[^\n]+\n//sg;
	print $rslts if $show_rslts;

	#Check that all the patterns are in the results
	for ($a = 0; $a < $k; $a++) {
	    if (! ($rslts =~m@$patterns[$a]@s)) {
		print "\nPattern '$patterns[$a]' not matched in results.\n";
		print "\n$cmd [FAIL]\n";
		if ($fail_fast) {
			print $rslts;
			exit(1);
		}
	    }
	}

	# Now check the results for duplicates and for the count.
	
	my $count = 0;
	my @rank;
	my %dup_check;
	while ($rslts =~ /([^\t]+)\s+[0-9]+\.[0-9]+\s*\n/sg) {
		die "\n\n$rslts\nResults for '$cmd' contain duplicate suggestions\n"
			if defined($dup_check{$1});
		$dup_check{$1} = 1;
		$count++;
		last if ($count >= $k);
	}
	if ($count < $k) {
		print "\n$cmd [FAIL]  $count < $k\n";
		if ($fail_fast) {
			my $rslts = `$cmd`;
			print $rslts;
			exit(1);
		}
		return 1;
	}
	return 0;
}


sub check_result_count {
    my $pq = shift;
    my $options = shift;
    my $expected = shift;
    my $show_rslts = shift;

    my $qfile = "index_modes_check.q";
    die "Can't open >$qfile" unless open Q, ">$qfile";
    print Q "$pq\n";
    close Q;

    my $cmd = "$qp index_dir=$ix $options -file_query_batch=$qfile -max_to_show=100 -chatty=off";
    print "$pq {$options}";
    my $rslts = `$cmd`;
    die "Command '$cmd' failed with code $?\n"
        if ($?);
    print $rslts if $show_rslts;
    my $count = 0;
    my %dup_check;
    while ($rslts =~ /Query:\s+[^\t]+\s+[0-9]+\s+([^\t]+)\s+[01]\.[0-9]+\s*\n/sg) {
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
            print $rslts;
            print "\nQuery file retained: cat $qfile\n";
            exit(1);
        }
        return 1;
    }
    return 0;
}


