#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Run a few files of test queries in batch mode against an index 
# with a few different option combinations and report response times.

# Assumes run in a directory with the following subdirectories:

$idxdir = "../test_data";
$tqdir = "../test_queries";


@ix = ("$idxdir/wikipedia_titles");
@qsets = (
    "$tqdir/emulated_log_10k.q",
    "$tqdir/emulated_log_100k.q",
    "$tqdir/emulated_log.q",
);
$tfile = "tmp_substitutions_timing.out";
$|++;

die "Usage: $0 <QBASHQ_binary> 
   Note: This script expects current indexes in each of @ix and
         test queries in each of @qsets.  It also expects to find
         a QBASH.substitution_rules_EN file in each index directory.\n" 
	unless ($#ARGV >= 0);

$qp = $ARGV[0];
$qp = "../src/visual_studio/x64/Release/QBASHQ.exe"
    if $qp eq "default";

die "$qp is not executable\n" unless -e $qp;
$fail_fast = 0;
$fail_fast = 1 if ($#ARGV > 0 && $ARGV[1] eq "-fail_fast");


$errs = 0;

@options = (
    "-language=TX -display_col=1 -classifier_mode=0 -use_substitutions=false",
    "-language=TX -display_col=1 -classifier_mode=0 -use_substitutions=true",
    "-language=TX -display_col=1 -classifier_mode=1 -use_substitutions=false",
    "-language=TX -display_col=1 -classifier_mode=1 -use_substitutions=true ",
    );

for $ix (@ix) {
die "Can't find QBASHER indexes in $ix\n" 
	unless (-r "$ix/QBASH.if");
die "Can't find substitution rules file in $ix\n" 
	unless (-r "$ix/QBASH.substitution_rules");

$subrules = count_lines_in_file("$ix/QBASH.substitution_rules");

print "\nNumber of substitution rules for $ix:  $subrules\n\n";


print "Query processing timings against $ix:

Options              Ave_resp_msec   95%_resp   99%_resp        QPS
===================================================================
";

    for $qset (@qsets) {
	print "  --- Query Set: $qset ---\n";
	for $opts (@options) {
	    #print "Running test set with options '$opts'\n";
	    # Must use file_query_batch otherwise ctrl-Z is EOF
	    $cmd = "$qp -warm_indexes=TRUE index_dir=$ix $opts -file_query_batch=$qset > $tfile";
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

print "\n\n       Splendid!!\n" unless $errs;
exit($errs);

# -----------------------------------------------------------

sub count_lines_in_file {
    my $fname = shift;
    die "Can't read $fname"
	unless open CLF, $fname;
    my $lncnt = 0;
    while (<CLF>) {
	$lncnt++;
    }
    close CLF;

    return $lncnt;

}


sub analyse_log {
    my $opts = shift;
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

	$grewby = $currentpfu - $initialpfu;
	$MB = sprintf("%.1f", $grewby /1024/1024);

	$err_cnt = 0;
	if ($MB > 2 && $currentpfu > 1.5 * $initialpfu) {
	    if ($MB < 50) {
		print "Warning: Memory (PFU) grew by ${MB}MB.\n";
	    } else {
		print "ERROR: Memory grew too much (by ${MB}MB), probable leak.\n";
		$err_cnt++;
		exit(1) if $fail_fast;
	    }
	}

	return $err_cnt;
}
