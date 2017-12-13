#! /usr/bin/perl -w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

# This script tests whether the results returned by two different versions
# of QBASHQ.exe on the same index and query set are identical.  This could 
# be useful in the following scenarios:
#
#   1. A change has been made to QBASHER algorithms which should not change
#      results
#   2. Two EXEs have been compiled by different compilers or with different
#      compiler options.
#

die "Usage: $0 <index_dir> <query_set> <base_EXE> <other_EXE> [<options>]\n"
    unless $#ARGV >= 3;

if ($#ARGV == 4) {$options = $ARGV[4];}
else {$options = "";}

$base_options = "-index_dir=$ARGV[0] -file_query_batch=$ARGV[1] -x_batch_testing=TRUE -query_streams=1 $options";

die "Can't write to diffs.txt\n" unless open D, ">diffs.txt";
$thresh = 200;  # Limit on number of diffs of each type to be recorded.

$cmd1 = "$ARGV[2] $base_options\n";
print "\n\nRun1: ", $cmd1;
$run1 = `$cmd1`;
die "Command failed with  code $?\n" if $?;

if ($run1 =~ /Average elapsed msec per query: ([0-9.]+).*?Maximum elapsed msec per query: ([0-9.]+.*?)\n/s) {
    $ave_run1 = $1;  
    $max_run1 = $2;
    $max_run1 =~ s/\*//g;   # Old versions wrongfully inserted an asterisk after the query.
    print "   Average latency: $ave_run1\n    Maximum latency: $max_run1\n";
} else {
    die "    Tail not matched!\n";
}

$cmd2 = "$ARGV[3] $base_options\n";
print "\n\nRun2: ", $cmd2;
$run2 = `$cmd2`;
die "Command failed with code $?\n" if $?;

if ($run2 =~ /Average elapsed msec per query: ([0-9.]+).*?Maximum elapsed msec per query: ([0-9.]+.*?)\n/s) {
    $ave_run2 = $1;  
    $max_run2 = $2;
    $max_run2 =~ s/\*//g;   # Old versions wrongfully inserted an asterisk after the query.
    print "   Average latency: $ave_run2\n    Maximum latency: $max_run2\n";
} else {
    die "    Tail not matched!\n";
}

print "\n\nChecking results ...\n";
$diffsA = 0; 
$diffsB = 0;
$run1_lines = 0;

#insert all the run1 result lines into a run1 hash
while ($run1 =~ /(Query:.*?)\r?\n/sg) {
    $run1_lines++;
    $line1 = $1;
    $line1 =~ s/\*//g;  # Old versions wrongfully inserted an asterisk after the query.
    $run1{$line1} = 0;
}
    
while ($run2 =~ /(Query:.*?)\r?\n/sg) {
    $line2 = $1;
    $line2 =~ s/\*//g;  # Old versions wrongfully inserted an asterisk after the query.
    if (defined($run1{$line2})) {
	$run1{$line2}++;
    } else {
	print D "R2notinR1: $line2\n";
	$diffsA++;
	if ($diffsA >= $thresh) {
	    print "Too many ($diffsA) R2notinR1 diffs found.  No further reporting\n";
	    last;
	}
    }
}

foreach $k (keys %run1) {
    if ($run1{$k} == 0) {
	print D "R1notinR2: $k\n";
	$diffsB++;
	if ($diffsB >= $thresh) {
	    print "Too many ($diffsB) R2notinR1 diffs found.  No further reporting\n";
	    last;
	}
    }
}

close(D);


print "$run1_lines result lines checked,  $diffsA + $diffsB diffs found. (Listed in diffs.txt)\n";

exit(0);



