#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# First check that various queries produce the right answers against the 
# classifierPaper index.  Also check that the modes whch use IDFs are working
# to some basic extent.

# Run a batch (or batches) of test queries against a suitable index in each of the 
# four classification modes to check robustness. Speed is now tested in a separate
# build test script.


# Assumes run in a directory with the following subdirectories:

$|++;

$idxdir = "../test_data";
$tqdir = "../test_queries";



@ix = (
    "$idxdir/wikipedia_titles"
);

@qsets = (
    "$tqdir/emulated_log_100k.q"
);


die "Usage: $0 <QBASHQ_binary> 
   Note: This script expects current indexes in each of @ix and
         test queries in each of @qsets.\n" 
	unless ($#ARGV >= 0);

$qp = $ARGV[0];

$qp = "../src/visual_studio/x64/Release/QBASHQ.exe" 
    if ($qp eq "default");

die "$qp is not executable\n" unless -x $qp;
$fail_fast = 0;
$fail_fast = 1 if ($#ARGV > 0 && $ARGV[1] eq "-fail_fast");

# This script assumes that the classifcationPaper index has been indexed 
# to be sorted in weight order.  If it's not we must re-index.

$grep = `/usr/bin/grep sort_records_by_weight=FALSE $ix[0]/QBASH.if\n 2>&1`;
print $grep;

if ($grep =~ m@matches@) {
    print "Re-indexing in weight order\n";
    $ixer = $qp;
    $ixer =~ s/qbashq/qbashi/ig;
    $cmd = "$ixer index_dir=$ix[0] > $ix[0]/index.log";
    $code = system($cmd);
    if ($code) {
	die "$cmd exited with code $code\n";
    }
}


$errs = 0;



$errs += check_results_format();

$errs += check_early_termination();

# ----------------------------------------------------------------------------------------------
print "Testing that there's a difference between count and IDF based classifier modes....\n";

$errs += check_answers("enigma japanese power", 
		       "The Enigma of Japanese Power", "-classifier_mode=1 -classifier_threshold=0.2");

$errs += check_answers("enigma japanese power", 
		       "", "-classifier_mode=1 -classifier_threshold=0.7");

$errs += check_answers("enigma japanese power", 
		       "The Enigma of Japanese Power", "-classifier_mode=2 -classifier_threshold=0.7");

$errs += check_answers("the enigma of", 
		       "", "-classifier_mode=2 -classifier_threshold=0.7");


# Jaccard and weighted Jaccard

$errs += check_answers("enigma japanese power", 
		       "The Enigma of Japanese Power", "-classifier_mode=3 -classifier_threshold=0.2");

$errs += check_answers("enigma japanese power", 
		       "", "-classifier_mode=3 -classifier_threshold=0.6");

$errs += check_answers("enigma japanese power", 
		       "The Enigma of Japanese Power", "-classifier_mode=4 -classifier_threshold=0.7");

$errs += check_answers("the enigma of", 
		       "", "-classifier_mode=4 -classifier_threshold=0.7");



# ----------------------------------------------------------------------------------------------
print "\nTesting that the right classification decisions are made with different forms of query...\n";

# simple words
$errs += check_answers("The Tale of Jemima Puddle-Duck", 
		       "The Tale of Jemima Puddle-Duck", "");

$errs += check_answers("Jemima Puddle-Duck", 
		       "The Tale of Jemima Puddle-Duck", "-classifier_threshold=0.3");

$errs += check_answers("duck puddle jemima of tale the", 
		       "The Tale of Jemima Puddle-Duck", "-classifier_threshold=0.6");


if (0) {  # -- These don't make sense now that punctuation is stripped
# full phrase
$errs += check_answers("\\\"growing up like the women from the exorcist\\\"", 
		       "growing up like the women from the exorcist", "");

# phrase at the start
$errs += check_answers("\\\"growing up like the women from\\\"", 
		       "growing up like the women from the exorcist", "-classifier_threshold=0.7");


# phrase in the middle
$errs += check_answers("\\\"like the women from\\\"", 
		       "growing up like the women from the exorcist", "-classifier_threshold=0.4");

# phrase at the end
$errs += check_answers("growing up like the women \\\"from the exorcist\\\"", 
		       "growing up like the women from the exorcist", "");


# disjunctions
$errs += check_answers("growing up like the [men women] from the exorcist", 
		       "growing up like the women from the exorcist", "");

$errs += check_answers("growing up like the [men women man woman] [form from] the exorcist", 
		       "growing up like the women from the exorcist", "");

#disjunction at end
$errs += check_answers("growing up like the women from the [exorcist priest wizard]", 
		       "growing up like the women from the exorcist", "");

#disjunction at start
$errs += check_answers("[growing increasing] up like the women from the exorcist", 
		       "growing up like the women from the exorcist", "");

#disjunction with phrases
$errs += check_answers("[maturing \\\"growing up\\\"] like the women from the exorcist", 
		       "growing up like the women from the exorcist", "");

$errs += check_answers("[maturing \\\"growing up\\\" \\\"getting older\\\"] like the women from the exorcist", 
		       "growing up like the women from the exorcist", "");

$errs += check_answers("[maturing \\\"getting older\\\" \\\"growing up\\\"] like the women from the exorcist", 
		       "growing up like the women from the exorcist", "");

#disjunction with phrases at the end
$errs += check_answers("growing up like the women from [\\\"the priest\\\" \\\"the fraud\\\" \\\"the exorcist\\\"]", 
		       "growing up like the women from the exorcist", "");

#disjunction within a phrase 

$errs += check_answers("growing up like \\\"the [man woman women men] from the exorcist\\\"", 
		       "growing up like the women from the exorcist", "");

}

# ----------------------------------------------------------------------------------------------


@addiopts = (
#    "-max_length_diff=900",
#    "-max_length_diff=500",
#    "-max_length_diff=400",
#    "-max_length_diff=401",
#    "-max_length_diff=401 -relaxation_level=1",
    "",
    "-relaxation_level=1",
#   "-max_length_diff=1000",   # Should be very slow!
    );


$tfile = "/tmp/classifier_modes_timing.out";

@classimodes = (
    "",
    "DOLM_counts", 
    "DOLM_idfs",
    "Jaccard_counts",
    "Jaccard_idfs",
    );


for $qset (@qsets) {
	die "Can't find query set $qset\n"
	unless -r $qset;
}

$base_options = "-warm_indexes=TRUE -display_col=1 -classifier_mode=XXX";   


for $ix (@ix) {
    die "Can't find QBASHER indexes in $ix\n" 
	unless (-r "$ix/QBASH.if");

    print "Query processing timings against $ix:

Options              Ave_resp_msec   95%_resp   99%_resp        QPS
===================================================================
";

    for $qset (@qsets) {
	print "  --- Query Set: $qset;  Index: $ix ---\n";
	for ($mode = 1; $mode <= $#classimodes; $mode++)  {
	    foreach $addiopts (@addiopts) {
		$opts = $base_options;
		$opts =~ s/XXX/$mode/;
		$opts .= " $addiopts";
		# Must use file_query_batch otherwise ctrl-Z is EOF
		$cmd = "$qp index_dir=$ix $opts -file_query_batch=$qset > $tfile";
		#print $cmd, "\n";
		$code = system($cmd);

		#print "Output of batch run in: $tfile\n\n";
		die "Batch run killed by signal\n" if ($code & 255);
		$code >>= 8;
		die "Batch run crashed with code $code\n" if $code;

		#print "Successfully completed $qset for options: $opts.\n";
		$errs += analyse_log($opts);
		unlink $tfile;
	    }
	}
    }
print "===================================================================
";
}


if ($errs) {
    print "\n\nBlast!  $0 failed with $errs errors\n";
} else {
    print "\n\n       Splendid!!\n";
}
exit($errs);

# -----------------------------------------------------------
sub analyse_log {
    my $opts = shift;
    my $err_cnt = 0;
	die "Can't open $tfile\n" unless open T, $tfile;
	$initial = 1;
	undef %perc;
	undef $ave_msec;
	undef $max_msec;
	$qcnt = 0;
	while(<T>) {
		#Pagefile usage: current 5287936, peak 5296128  -- 5.0MB
		if (/Pagefile usage: current ([0-9]+), peak ([0-9]+)/) {
			if ($initial) {
				$initial = 0;
				$initialpfu = $1;
				$peakpfu = $2;
			} else {
				$currentpfu = $1;
				$peakpfu = $2;
			}
		} elsif (/^Average elapsed msec per query: ([0-9]+)/) {
			$ave_msec = $1;
			#print $_;
		} elsif (/^Maximum elapsed msec per query: ([0-9]+) +\(.*\)/) {
			$max_msec = $1;
			#print $_, "Elapsed time percentiles:\n";
		} elsif (/^ +([0-9.]+)th - +([0-9]+)/) {
			#print $_;
			$perc{$1} = $2;
		} elsif (/    Elapsed time [0-9]+ msec/) {
			$qcnt++;
			#print "  --- $qcnt ---\n" if $qcnt %100000 == 0;
		} elsif (/Total elapsed time: .*? -- ([0-9.]+) QPS/) {
		    $QPS = $1;
			#print "  --- $qcnt ---\n" if $qcnt %100000 == 0;
		}
	}

    $p95 = $perc{"95"};
    $p99 = $perc{"99"};
    print "$opts\t$ave_msec\t$p95\t$p99\t$QPS\n";

    if (defined($currentpfu) && defined($initialpfu)) {
	$grewby = $currentpfu - $initialpfu;
	$MB = sprintf("%.1f", $grewby /1024/1024);

	$err_cnt = 0;
	if ($MB > 50 && $currentpfu > 1.5 * $initialpfu) {
		print "ERROR: Memory grew too much (by ${MB}MB), probable leak.\n";
		$err_cnt++;
		exit(1) if $fail_fast;
	}
    } else {
	printf("  -- pagefile usage data unavailable --\n");
    }

    return $err_cnt;
}


sub count_results {
    my $rslts = shift;
    my $cnt = 0;
    while ($rslts =~ m@(.*?\t\s*[0-9.]+)\s*\n@sg) {
	$cnt++;
    }
    return $cnt;
}


sub results_are_in_descending_score_order {
    my $rslts = shift;
    my $last_score = 1.0;
    while ($rslts =~ m@.*?\t\s*([0-9.]+)\s*\n@sg) {
	if ($1 > $last_score) {
	    return 0;
	}
	$last_score = $1;
    }
    return 1;
}


sub check_results_format {
    my $err_cnt = 0;
    my $opts = "-classifier_mode=1 -classifier_threshold=0.9 -display_col=3 -extra_col=4 -duplicate_handling=0";
    my $base_cmd = "$qp index_dir=$ix[0] $opts";
    print "\n\nChecking results format\n";
    
    my $cmd = "$base_cmd  -pq=\"the effect of gamma rays on man in the moon marigolds\"";
    print $cmd, "\n";
    my $rslts = `$cmd`;
    print $rslts;
    if ($rslts =~ m@The Effect of Gamma Rays on Man-in-the-Moon Marigolds\s*\t\s*\s*\t\s*EXACT\s*\t\s*the, effect, of, gamma, rays, on, man, in, the, moon, marigolds,\s*\t11\t\s*0.99354@) {
	print "\n                                                               [PASS]\n\n";
    } else {
	print "                                                               [FAIL]\n\n";
	$err_cnt++;
	exit(1) if $fail_fast;
    }

    $cmd = "$base_cmd  -pq=\"the effect of gamma rays on man in the moon marigolds\" -include_result_details=FALSE";
    print $cmd, "\n";
    $rslts = `$cmd`;
    print $rslts;
    if ($rslts =~ m@The Effect of Gamma Rays on Man-in-the-Moon Marigolds\s*\t\s*0.99354@) {
	print "\n                                                               [PASS]\n\n";
    } else {
	print "                                                               [FAIL]\n\n";
	$err_cnt++;
	exit(1) if $fail_fast;
    }

    $cmd = "$base_cmd  -pq=\"the effect of gamma rays on man in the moon marigolds\" -extra_col=2";
    print $cmd, "\n";
    my $rslts = `$cmd`;
    print $rslts;
    if ($rslts =~ m@The Effect of Gamma Rays on Man-in-the-Moon Marigolds\s*\t\s*80\s*\t\s*EXACT\s*\t\s*the, effect, of, gamma, rays, on, man, in, the, moon, marigolds,\s*\t11\t\s*0.99354@) {
	print "\n                                                               [PASS]\n\n";
    } else {
	print "                                                               [FAIL]\n\n";
	$err_cnt++;
	exit(1) if $fail_fast;
    }


    $cmd = "$base_cmd  -pq=\"Never in the field of human conflict was so much owed by so many to so few\" -extra_col=1";
    print $cmd, "\n";
    $rslts = `$cmd`;
    print $rslts;
    if ($rslts =~ m@Never in the field of human conflict was so much owed by so many to so few\s*\t.*?\t\s*EXACT\s*\t.*?\t17\t@) {
	print "\n                                                               [PASS]\n\n";
    } else {
	print "                                                               [FAIL]\n\n";
	$err_cnt++;
	exit(1) if $fail_fast;
    }



    # Check output of extra features

    $opts = "-classifier_mode=1 -classifier_threshold=0.9 -display_col=3 -extra_col=4 -include_extra_features=true -duplicate_handling=0";
    $base_cmd = "$qp index_dir=$ix[0] $opts";
    print "\n\nChecking results format with extra features\n";
    $cmd = "$base_cmd  -pq=\"the effect of gamma rays on man in the moon marigolds\"";
    print $cmd, "\n";
    $rslts = `$cmd`;
    print $rslts;

    if ($rslts =~ m@The Effect of Gamma Rays on Man-in-the-Moon Marigolds\s*\t\s*\s*\t\s*EXACT\s*\t\s*the, effect, of, gamma, rays, on, man, in, the, moon, marigolds,\s*\t11\t11.00000\t11.00000\t0.00000\t0.00000\t0.00000\t0.00000\t0.35421\t1.00000\t1.00000\t\s*0.99354@) {
	print "\n                                                               [PASS]\n\n";
    } else {
	print "                                                               [FAIL]\n\n";
	$err_cnt++;
	exit(1) if $fail_fast;
    }


    # Fiddle with omega, chi, psi etc.

    # Note that in this case, without explicitly setting weights on these params,
    # the score is in two components.  The first 2 digits represent the degree of
    # lexical match truncated to two digits.  The rest of the digits represent the
    # static score shifted right by two places.
    $opts = "-classifier_mode=1 -classifier_threshold=0.1 -display_col=3 -extra_col=4  -include_result_details=FALSE -max_to_show=1 -duplicate_handling=0";
    $base_cmd = "$qp index_dir=$ix[0] $opts";
    print "\n\nChecking effect of omega\n";
    $cmd = "$base_cmd  -pq=\"ballad a thin man\"";
    print $cmd, "\n";
    $rslts = `$cmd`;
    print $rslts;
    # score should be 4 / (5 + 1) = 0.66667 -> 0.66 + 0.112/100  (static score in less sig digs)
    if ($rslts =~ m@Ballad of a Thin Man\s*\t\s*0.66112@) {
	print "\n                                                               [PASS]\n\n";
    } else {
	print "                                                               [FAIL]\n\n";
	$err_cnt++;
	exit(1) if $fail_fast;
    }

    # Note that when weights are set for omega, chi, psi the score is computed as a weighted
    # linear combination of the scores. 
    # chi is the weight of the classifier score, omega is the weight of the static score
    # Answer should be 0.666667 * 0.7 + 0.112 * 0.3 = 0.500 (approx)
    $cmd = "$base_cmd -chi=0.7 -omega=0.3 -pq=\"ballad a thin man\"";
    print $cmd, "\n";
    $rslts = `$cmd`;
    print $rslts;
    if ($rslts =~ m@Ballad of a Thin Man\s*\t\s*0.500@) {
	print "\n                                                               [PASS]\n\n";
    } else {
	print "                                                               [FAIL]\n\n";
	$err_cnt++;
	exit(1) if $fail_fast;
    }

    return $err_cnt;
}


sub check_early_termination {
    my $err_cnt = 0;
    my $opts = "-classifier_mode=1 -classifier_threshold=0.70 -display_col=1 -duplicate_handling=0";
    my $base_cmd = "$qp index_dir=$ix[0] $opts -pq=\"united states elections\"";
    my $cmd = "$base_cmd -max_to_show=1000";
    print "\n\nChecking early termination mechanisms ...\n";
    print "\n      ------------------- With no_relax and good query -------------------------\n";
    #print $cmd, "\n";
    my $rslts = `$cmd`;
    my @base_results;
    #print "\n\n$rslts\n\n";
    die "Command '$cmd' failed with code $?\n"
	if ($?);
    while ($rslts =~ m@(.*?\t\s*[0-9.]+)\s*\n@sg) {
	#print $1,"\n";
	push @base_results, $1;
    }
    my $l = $#base_results;

    if ($l != 137) {
	print "Wrong number ($l) of results from C_E_T base.  Should be 137\n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }

    if (!results_are_in_descending_score_order($rslts)) {
	print "C_E_T base results are not in descending score order.\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
	
	
    # Now run with thresh1.  Stop when the highest ranked candidate exceeds a threshold.
    # Note that we might have already encountered candidates which exceed the classifier_threshold
    # but which don't exceed classifier_stop_thresh1
    $cmd = "$base_cmd -max_to_show=1000 -classifier_stop_thresh1=0.90";
    #print $cmd, "\n";
    $rslts = `$cmd`;
    #print "\nThresh1 = 0.7\n$rslts\n\n";
    if (($l = count_results($rslts)) != 10) {
	print "Wrong number ($l) of results from C_E_T no_relax thresh1.  Should be 10\n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    if (! ($rslts =~ m@^united states elections.*0.99266@i)) {
	print "Wrong first result from C_E_T no_relax thresh1. \n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }

    print "Test of -classifier_stop_thresh1:                    [PASS]\n";

    # Now run with thresh2. Terminate early if the lowest-ranked of max_to_show candidates exceeds this value.
    $cmd = "$base_cmd -max_to_show=5 -classifier_stop_thresh2=0.2";
    #print $cmd, "\n";
    $rslts = `$cmd`;
    #print "\nThresh2 = 0.2\n$rslts\n\n";
     if (count_results($rslts) != 5) {
	print "Wrong number of results from C_E_T no_relax thresh2.  Should be 5\n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    if (! ($rslts =~ m@^united states elections.*0.746@i)) {
	print "Wrong first result from C_E_T no_relax thresh2. \n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }

    print "Test of -classifier_stop_thresh2:                    [PASS]\n";


    # Now run with thresh1 and thresh2
    $cmd = "$base_cmd -max_to_show=5  -classifier_stop_thresh1=0.7 -classifier_stop_thresh2=0.2$cmd\n";
    #print $cmd, "\n";
    $rslts = `$cmd`;
    #print "\nThresh1 = 0.7 Thresh2 = 0.2\n$rslts\n\n";
    if (($l = count_results($rslts)) != 1) {
	print "Wrong number ($l) of results from C_E_T no_relax thresh1&2.  Should be 1\n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    if (! ($rslts =~ m@^united states elections.*0.746@i)) {
	print "Wrong first result from C_E_T no_relax thresh1&2. \n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }

    print "Test of both -classifier_stop_threshholds:           [PASS]\n";


    print "\n      ------------------- With relaxation and good query -----------------------\n";
    $base_cmd = "$qp index_dir=$ix[0] $opts -relaxation_level=1 -pq=\"united states elections\"";
    $cmd = "$base_cmd -max_to_show=100";
    #print $cmd, "\n";
    $rslts = `$cmd`;
    #print "\n\n$rslts\n\n";
    if (count_results($rslts) != 100) {
	print "Wrong number of results from C_E_T good query relax.  Should be 100\n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    if (! ($rslts =~ m@^united states elections.*0.99266@i)) {
	print "Wrong first result from C_E_T good query, relax. \n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }

    if (!results_are_in_descending_score_order($rslts)) {
	print "C_E_T good query relax results are not in descending score order.\n$cmd\n";
	$err_cnt++;
	return $err_cnt;
    }

    print "Running with -relaxation_level=1:                     [PASS]\n";
    
    # Now run with thresh1
    $cmd = "$base_cmd -max_to_show=5 -classifier_stop_thresh1=0.7";
    #print $cmd, "\n";
    $rslts = `$cmd`;
    #print "\nRelax1 Thresh1 = 0.7\n$rslts\n\n";
    if (($l = count_results($rslts)) != 1) {
	print "Wrong number ($l) of results from C_E_T good query relax thresh1.  Should be 1\n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    if (! ($rslts =~ m@^united states elections.*0.746@i)) {
	print "Wrong first result from C_E_T good query, relax thresh1. \n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    
    print "Running with -relaxation_level=1 thresh1:             [PASS]\n";

    # Now run with thresh2
    $cmd = "$base_cmd -max_to_show=5 -classifier_stop_thresh2=0.2";
    #print $cmd, "\n";
    $rslts = `$cmd`;
    #print "\nRelax1 Thresh2 = 0.2\n$rslts\n\n";
    if (($l = count_results($rslts)) != 5) {
	print "Wrong number of results ($l)from C_E_T good query relax thresh2.  Should be 5\n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	return $err_cnt;
    }
    if (! ($rslts =~ m@^united states elections.*0.992@i)) {
	print "Wrong first result from C_E_T good query, relax thresh2. \n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }

    print "Running with -relaxation_level=1 thresh2:             [PASS]\n";

    
    # Now run with thresh1 and thresh2
    $cmd = "$base_cmd -max_to_show=5  -classifier_stop_thresh1=0.7 -classifier_stop_thresh2=0.2";
    #print $cmd, "\n";
    $rslts = `$cmd`;
    #print "\nRelax1 Thresh1 = 0.7 Thresh2 = 0.2\n$rslts\n\n";
    if (($l = count_results($rslts)) != 1) {
	print "Wrong number of results ($l) from C_E_T good query relax thresh1&2.  Should be 1\n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    if (! ($rslts =~ m@^united states elections.*0.746@i)) {
	print "Wrong first result from C_E_T good query, relax thresh1&2. \n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }

    print "Running with -relaxation_level=1 thresh1&2:           [PASS]\n";
 




    print "\n      ------------------- With relaxation and dud query ------------------------\n";
    $opts = "-classifier_mode=1 -classifier_threshold=0.4224 -display_col=1 -duplicate_handling=0";
    $base_cmd = "$qp index_dir=$ix[0] $opts -relaxation_level=1 -pq=\"xxxxxx united states elections\"";
    $cmd = "$base_cmd -max_to_show=100";
    #print $cmd, "\n";
    $rslts = `$cmd`;
    #print "\n\n$rslts\n\n";
    if (($l = count_results($rslts)) != 16) {
	print "Wrong number ($l) of results from C_E_T dud query relax.  Should be 16\n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    if (! ($rslts =~ m@^united states elections.*0.492@i)) {
	print "Wrong first result from C_E_T dud query, relax. \n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }

    if (!results_are_in_descending_score_order($rslts)) {
	print "C_E_T dud query relax results are not in descending score order.\n\n$rslts\n\n$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    print "Running dud with -relaxation_level=1        :         [PASS]\n";


    # Now run with thresh1
    $cmd = "$base_cmd -max_to_show=5 -classifier_stop_thresh1=0.7";
    #print $cmd, "\n";
    $rslts = `$cmd`;
    #print "\nRelax1 Thresh1 = 0.7\n$rslts\n\n";
    if (($l =count_results($rslts)) != 5) {
	print "Wrong number ($l)of results from C_E_T dud query relax thresh1.  Should be 5\n\n$rslts\n\nCMD=$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    if (! ($rslts =~ m@^united states elections.*0.492@i)) {
	print "Wrong first result from C_E_T dud query, relax thresh1. \n\n$rslts\n\nCMD=$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    print "Running dud with -relaxation_level=1 thresh1:         [PASS]\n";

    
    # Now run with thresh2
    $cmd = "$base_cmd -max_to_show=5 -classifier_stop_thresh2=0.2";
    #print $cmd, "\n";
    $rslts = `$cmd`;
    #print "\nRelax1 Thresh2 = 0.2\n$rslts\n\n";
    if (($l = count_results($rslts)) != 5) {
	print "Wrong number ($l) of results from C_E_T dud query relax thresh2.  Should be 5\n\n$rslts\n\nCMD=$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    if (! ($rslts =~ m@^united states elections.*0.492@i)) {
	print "Wrong first result from C_E_T dud query, relax thresh2. \n\n$rslts\n\nCMD=$cmd\nCMD=$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    print "Running dud with -relaxation_level=1 thresh2:         [PASS]\n";

    # Now run with thresh1 and thresh2
    $cmd = "$base_cmd -max_to_show=5  -classifier_stop_thresh1=0.7 -classifier_stop_thresh2=0.2";
    #print $cmd, "\n";
    $rslts = `$cmd`;
    #print "\nRelax1 Thresh1 = 0.7 Thresh2 = 0.2\n$rslts\n\n";
    if (($l = count_results($rslts)) != 5) {
	print "Wrong number ($l) of results from C_E_T dud query relax thresh1&2.  Should be 5\n\n$rslts\n\nCMD=$cmd\n";
	
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    if (! ($rslts =~ m@^united states elections.*0.492@i)) {
	print "Wrong first result from C_E_T dud query, relax thresh1&2. \n\n$rslts\n\nCMD=$cmd\n";
	$err_cnt++;
	exit(1) if $fail_fast;
	return $err_cnt;
    }
    
    print "Running dud with -relaxation_level=1 thresh1&2:       [PASS]\n";

    print "\n                                          ... all good.\n\n" unless $err_cnt;
    return $err_cnt;
}


sub check_answers {
    my $pq = shift;
    my $expected = shift;
    my $addiopts = shift;
    my $outcome = 0;
    my $showq = $pq;
    $showq =~ s/\\//g;  # strip backslashes out of query for display
    $showq = sprintf("%-80s", $showq);

    my $cmd = "$qp index_dir=$ix[0] -display_col=1 -classifier_mode=1 $addiopts -pq=\"$pq\"\n";
    #print $cmd;
    my $rslts = `$cmd`;
    #print "\n\n$rslts\n\n";
    die "Command '$cmd' failed with code $?\n"
	if ($?);

    if ($expected eq "") {
	if ($rslts ne "") {$outcome = 1;}
    } else {
	if (!($rslts =~ /$expected/)) {$outcome = 1;}
    }

    if ($outcome) {
	print "$showq [FAIL]\n   $cmd\n\n   $rslts\n";
	exit(1) if $fail_fast;
	return 1;
    } else {
	print "$showq [OK]\n";
	return 0;
    }
}
