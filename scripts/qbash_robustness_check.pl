#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Run a files of test queries of increasing size in batch mode against the test index with a few
# different option combinations and check:
#	A. that QBASHQ.exe doesn't crash or infinitely loop, 
#	B. that there are no substantial memory leaks, and
#       C. that the response time distribut2ion is OK.  (Not sure how to make this machine independent)

# Assumes run in a directory with the following subdirectories:

$idxdir = "../test_data";
$tqdir = "../test_queries";

$|++;
$ix = "$idxdir/wikipedia_titles_5M/";   # Default to using this index.

die "Usage: $0 <QBASHQ_binary> [<Index_directory>] [-fail_fast]
   Note: This script expects a series of query testfiles to be in $tqdir.
         If no Index_directory is given it defaults to $ix.\n" 
	unless ($#ARGV >= 0);
$qp = $ARGV[0];
$qp = "../src/visual_studio/x64/Release/QBASHQ.exe"
    if $qp eq "default";

die "$qp is not executable\n" unless -e $qp;

$fail_fast = 0;

for ($a = 1; $a <= $#ARGV; $a++) {
    if ($ARGV[$a] eq "-fail_fast") {
	$fail_fast = 1;
    } else {
	$ix = $ARGV[$a];
    }
}


$keystroke_qfile = "$tqdir/emulated_log_100k_autopartials.q";
die "Can't read query input file $keystroke_qfile\n" 
    unless -r $keystroke_qfile;

die "Can't find QBASHER indexes in $ix\n" 
	unless (-r "$ix/0/QBASH.if") || (-r "$ix/QBASH.if");

$errs = 0;

$first = 1;
$tfile = "tmp_autosuggest_keystrokes.log";
print "First a special case with autopartials and simulated keystrokes\n";
$opts = "-auto_partials=on";
$qfile = $keystroke_qfile;
one_run();


# Next loop over all the other combinations of qfiles and options.
for $qfile ("$tqdir/emulated_log_1k.q", "$tqdir/emulated_log.q") {
    die "Can't read query input file $qfile\n" unless -r $qfile;
    print "Running tests on query file $qfile ...............\n";
    $tfile = $qfile;
    $tfile =~ s@.*/@tmp_@;   # Write temporary logs in script directory
    $tfile =~ s/\.q/.log/; # If this file exists it will be analysed and nothing run.
    $first = 1;
    for $opts ("", "-auto_partials=on", "-auto_partials=on -timeout_kops=1", "-relaxation_level=1") {
	one_run();
    }
    last if $errs;
}

if ($errs) {
    print "\n\n    FAIL: Errors encountered\n";
} else {
    print "\n\n     Brilliant!!\n";
}
exit($errs);

# -----------------------------------------------------------

sub one_run {
    
    if ($first) { $first = 0;}
    else {$tfile .= "+" ;}
    $cmd = "$qp -warm_indexes=TRUE index_dir=$ix $opts -file_query_batch=$qfile >$tfile";
    $code = system($cmd);

    print "Output of batch run in: $tfile\n\n";
    die "Batch run killed by signal\n" if ($code & 255);
    $code >>= 8;
    die "Batch run crashed with code $code\n" if $code;

    print "Successfully completed $qfile for options: $opts.\n";
    $errs += analyse_log(4, 10);
    if ($interactive) {
	print "\n\nOK to delete the log of the batch run now? (Y/N): ";
	$ans = <STDIN>;
	unlink $tfile if ($ans =~/^y/i);
    }
    if ($errs) {
	print "Last command was '$cmd'\n";
	last;
    }

}


sub analyse_log {
	my $target_ave = shift;
	my $target_99 = shift;

	die "Can't open $tfile\n" unless open T, $tfile;
	$initial = 1;
	undef %perc;
	undef $ave_msec;
	undef $max_msec;
	print "Analysing logfile $tfile ...\n";
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
			print $_;
		} elsif (/^Maximum elapsed msec per query: ([0-9]+) +\(.*\)/) {
			$max_msec = $1;
			print $_, "Elapsed time percentiles:\n";
		} elsif (/^ +([0-9.]+)th - +([0-9]+)/) {
			print $_;
			$perc{$1} = $2;
		} elsif (/^Query: /) {
			$qcnt++;
			print "  --- $qcnt ---\n" if $qcnt %100000 == 0;
		}
	}

	$grewby = $currentpfu - $initialpfu;
	$MB = sprintf("%.1f", $grewby /1024/1024);
	$bytes_per_query = sprintf("%.1f", $grewby / $qcnt);
	print "\n\nQueries run: $qcnt
Initial pagefile usage: $initialpfu
Final pagefile usage: $currentpfu
Peak pagefile usage: $peakpfu
Total pagefile growth: $MB
Average pagefile growth per query: $bytes_per_query bytes\n\n";

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
	if ($ave_msec > 1.1 * $target_ave) {
		print "ERROR: Average response time is more than 10% above target $target_ave.\n";
		$err_cnt++;
	}
	if ($perc{99} > 1.1 * $target_99) {
		print "ERROR: 99th percentile response is more than 10% above target $target_99.\n";
		$err_cnt++;
	}

	return $err_cnt;
}
