#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Runs all the qbash basic functionality, timing and robustness scripts.
# Takes an error exit whenever one of those scripts fails.

# Assumes run in a directory with the following subdirectories:

$|++;
use Time::HiRes;
use File::Copy;

$start = Time::HiRes::time();


$idxdir = "../test_data";
$tqdir = "../test_queries";

$use_gcc_executables = 0;
$lite_tests = 0;

print "
qbash_run_tests.pl allows for optional arguments which may be 

    'GCC', meaning test with the GCC-built executables (no multi-threading, no C#),
    'LITE', meaning run a reduced set of tests (a quick check)
    'BASIC', meaning run the LITE tests plus some longer ones,
    'FULL', meaning run all tests including ones with very large query sets, or
    'RI', meaning re-index the collections and run the FULL set of tests.
    'ithreads=<int>', meaning run indexing in <int> parallel threads.
    'qthreads=<int>', meaning run tests in <int> parallel threads.


With no argument, all tests except the very long running ones will be run,
and collections will not be re-indexed.

";



$reindex = 0;
$runlongtests = 1;
$runverylongtests = 0;
$ithreads = 1;  # Actually processes since we use fork()
$qthreads = 1;  # Actually processes since we use fork()


foreach $a (@ARGV) {
    # Process the argument
    $lcarg = lc $a;
    $lcarg =~ s/^-+//;  # Strip leading minuses
    if ($lcarg eq "ri") { 
	$reindex = 1;
	$runverylongtests = 1;
    } elsif ($lcarg eq "lite") {
	$runlongtests = 0;
	$lite_tests = 1;
    } elsif ($lcarg eq "basic") {
	$runlongtests = 0;
    } elsif ($lcarg eq "full") {
	$runverylongtests = 1;
    } elsif ($lcarg =~ /ithreads=(\d+)/) {
	$ithreads = $1;
    } elsif ($lcarg =~ /qthreads=(\d+)/) {
	$qthreads = $1;
    } elsif ($lcarg eq "gcc") {
	$use_gcc_executables = 1;
    } else {
	die "Argument $a not recognized.\n";
    }
}


if ($use_gcc_executables) {
    $binDir = "../src";
    $vocab_lister = "../src/QBASH_vocab_lister.exe";
    print "\nUsing GCC executables\n\n";
} else {
    $config = "Release";
    $binDir = "../src/visual_studio/x64/${config}";
    # Can't work out how to get VS2015 to put the C# exe into the same directory as the others :-(
    if (-x "../src/QBASHQsharpNative/x64/Release/QBASHQsharpNative.exe") {
	copy("../src/QBASHQsharpNative/x64/${config}/QBASHQsharpNative.exe", $binDir);
	copy("../src/QBASHQsharpNative/x64/${config}/QBASHQsharpNative.exe.config", $binDir);
	copy("../src/QBASHQsharpNative/x64/${config}/QBASHQsharpNative.pdb", $binDir);
	chmod 0755, "$binDir/QBASHQsharpNative.exe";
	chmod 0755, "$binDir/QBASHQsharpNative.exe.config";
	chmod 0755, "$binDir/x64/${config}/QBASHQsharpNative.exe";
    }	   
    $vocab_lister = "../src/visual_studio/x64/${config}/QBASH_vocab_lister.exe";
}

@indexes = ("wikipedia_titles_500k", "wikipedia_titles_5M", "wikipedia_titles");


if ($lite_tests) {
    @tests = (
	"individual_file_names",
	"bloombits",
	"per_query_options",
	"utf8",
	"geo_tile",
	"regressions",
	"sanity",
	"multi_query",
	"query_shortening",
	"disjunctions",
	"substitution_rules",
	"classifier_modes",    
	"relaxation",
	"street_addresses",
	"index_modes",
	"timeout",
	"fuzz",
	"batch_labels",
	);
} else {
    @tests = (
	"individual_file_names",
	"bloombits",
	"per_query_options",
	"long_documents",
	"utf8",
	"geo_tile",
	"regressions",
	"sanity",
	"multi_query",
	"query_shortening",
	"disjunctions",
	"substitution_rules",
	"c-sharp",
	"classifier_modes",    
	# "option_overrides",   # This test needs work
	"relaxation",
	"street_addresses",
	"multi_threading",
	"index_modes",
	"fuzz",
	"batch_labels",
	"timeout",
	);
}

 if ($runlongtests) {
    foreach $t (	
	"robustness",          
	"timing",
	"substitution_rules_timing",
	"relaxed_timing",
	) {
	push @tests, $t;
    }
}


if ($runverylongtests) {
    foreach $t (	
    "classifier_timing"
    ) {
	push @tests, $t;
    }
}




if ($reindex) {
    print "Re-indexing collections needed to run the tests ...\n\n";
    $global_abort = 0;

    for ($th = 0; $th < $ithreads; $th++) {
	if ($ithreads == 1 || ($pid[$th] = fork()) == 0) {  # No fork if not parallel
	    # ------------------------ Child code -------------------------------
	    for ($nix = $th; $nix <= $#indexes; $nix += $ithreads) {
		if ($global_abort) {last;}
		print "\n$indexes[$nix] ...\n";
		$cmd = "$binDir/QBASHI.exe index_dir=$idxdir/$indexes[$nix] > $idxdir/$indexes[$nix]/index.log";
		$code = system($cmd);
		if ($code) {
		    die "$cmd exited with code $code\n";
		}
		$cmd = "tail -1 $idxdir/$indexes[$nix]/index.log";
		system($cmd);
		$cmd = "$vocab_lister $idxdir/$indexes[$nix]/QBASH.vocab > $idxdir/$indexes[$nix]/vocab.tsv";
		$code = system($cmd);
		if ($code) {
		    $global_abort++;
		    last;
		}
	    }
	    exit(0) unless $ithreads == 1; 
	    # --------------------- End of child code ---------------------------
	}
	print "Indexing thread $th started....\n";
    }

    # Wait for all the children to finish
    if ($ithreads > 1) {
	for ($th = 0; $th < $ithreads; $th++) {
	    waitpid($pid[$th], 0);
	}
    }

    if ($global_abort) {
	print "\n\n --------  Blast! Indexing failed. ---------\n\n";
	exit(1);
    } else {
	print "\n\n --------  Indexing succeeded. ---------\n\n";
    }
}

# Now that we are ready to run the tests, check the Easter Egg Query.

$cmd = "$binDir/QBASHQ.exe index_dir=$idxdir/wikipedia_titles -pq='gonebut notforgotten'";
$eeq_rslts = `$cmd`;
die "Easter egg query failed: $eeq_rslts\n" if ($? || !($eeq_rslts =~ /Easter-Egg:/));

$eeq_rslts =~ s/\t.*/ in wikipedia_titles corpus/;

print $eeq_rslts;




# Run tests in parallel

$global_abort = 0;

for ($th = 0; $th < $qthreads; $th++) {
    print "Test thread $th starting ....\n" unless $qthreads == 1;
    if ($qthreads == 1 || ($pid[$th] = fork()) == 0) {
	# ------------------------ Child code -------------------------------
	for ($test = $th; $test <= $#tests; $test += $qthreads) {
	    my $rezo;
	    if ($global_abort) {last;}
	    $rezo = run_test($tests[$test]) 
		unless $use_gcc_executables && $tests[$test] =~ /c-sharp/;
	    if ($rezo) {
		$global_abort++;
		last;
	    }
	}
	exit(0) unless $qthreads == 1;
	# --------------------- End of child code ---------------------------
    }
}

# Wait for all the children to finish
if ($qthreads > 1) {
    for ($th = 0; $th < $qthreads; $th++) {
	waitpid($pid[$th], 0);
    }
}

if ($global_abort) {
    print "\n\n --------  Blast! At least one test failed. ---------\n\n";
} else {
    print "\n\n --------  It's our lucky day.  All tests passed. ---------\n\n";
}


print $eeq_rslts;  # Reprint the results of running the easter egg query

print sprintf("Elapsed time for all the tests: %.1f sec.\n", Time::HiRes::time() - $start);


exit 0;


# -------------------------------------------------------------------

sub run_test {
    my $test = shift;
    my $script = "./qbash_${test}_check.pl";

    if (! -x $script) {
	print "Catastrophe: Can't execute $script\n";
	return -99;
    }

    print "\n\n\n   -------------- $script ----------------\n";

    $cmd = "$script $binDir/QBASHQ.exe";
    return system($cmd);
}
