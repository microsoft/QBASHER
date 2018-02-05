#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Run increasing batches of test queries in batch mode against an index 
# using the C-sharp front-end and report QPS rates.

# Currently the paths to the index are hard-coded into the EXE.  They 
# reference ../QBASHER/indexes/Top100M

use Time::HiRes qw (gettimeofday);

$long_run = 0;
if ($long_run) {
    @runs = (1000, 2000, 5000, 10000, 
	     15000, 20000, 25000, 30000, 35000, 40000,
	     50000, 100000, 1000000);
} else {
    @runs = (1000, 2000, 5000, 10000);
}


# Assumes run in a directory with the following relative directories:
$tqdir = "../test_queries";
$ix = "../test_data/wikipedia_titles";

$base_qset = "$tqdir/emulated_log.q";
$qset = "/tmp/qset_c-sharp";
$tfile = "/tmp/c-sharp_timing.out";
$|++;

die "Usage: $0 <QBASHQ_binary> 
   Note: This script expects current indexes in $ix and
         test queries in $base_qset.
"
	unless ($#ARGV >= 0);

$qp = $ARGV[0];
$qp = "../src/visual_studio/x64/Release/QBASHQ.exe" if ($qp eq "default");

$qp =~ s/QBASHQ/QBASHQsharpNative/;
$qp =~ s/qbashq/QBASHQsharpNative/;

die "$qp is not executable\n" unless -x $qp;

$obstorfiles = "-object_store_files=$ix/QBASH.forward,$ix/QBASH.vocab,$ix/QBASH.if,$ix/QBASH.doctable,c-sharp.config";
$obstorfiles_w = $obstorfiles;
$obstorfiles_w =~ s/c-sharp/c-sharp_warmup/;
print "Query processor:  $qp
File list: $obstorfiles\n"; 

$errs = 0;

die "Can't find QBASHER indexes in $ix\n" 
	unless (-r "$ix/QBASH.if");

$hort = "tail";   # Head or Tail?


generate_problem_qset();  # A few mongrel queries including one which
                          # caused David Maxwell problems
$cmd = "$qp $obstorfiles_w < $qset";
$code = system($cmd);
die "Command failed: $cmd\n" if $code;



# Determine how long it takes to start up the exe, after warming up a little
$cmd = "$hort -1 $base_qset > $qset";
$code = system($cmd);
die "Can't take $hort -1 of $base_qset\n" if $code;
$cmd = "$qp $obstorfiles_w < $qset";
$code = system($cmd);
die "Command failed: $cmd\n" if $code;

print "Warmed up...\n";


# Calculate the start-up time by timing the running of a single query
# -- multiple times and averaging.
$min_startup = 9999999999;

for ($k = 0; $k < 5; $k++) {
    $start = Time::HiRes::time();
    $cmd = "$qp $obstorfiles < $qset > $tfile";
    $code = system($cmd);
    die "Command failed: $cmd\n" if $code;
    $startup = Time::HiRes::time() - $start;
    if ($startup < $min_startup) { $min_startup = $startup;}
}

$startup = $min_startup;

print "Seconds: Subtracted for startup / shutdown of $qp: ", 
    sprintf("%.3f\n", $startup); 

print "
===================================================================
";

$last_qps = 1; 

for $batchsize (@runs) {
    print "  --- Query Batch Size: $batchsize ---\n";
    $cmd = "$hort -$batchsize $base_qset > $qset";
    $code = system($cmd);
    die "Can't take $hort -$batchsize of $base_qset\n" if $code;

    $start = Time::HiRes::time();
    $cmd = "$qp $obstorfiles < $qset > $tfile";
    #print $cmd, "\n";
    $code = system($cmd);
    $elapsed = Time::HiRes::time() - $start - $startup;
    $qps = $batchsize / $elapsed;
    print "    Elapsed time: ", sprintf("%.3f", $elapsed), " sec.  QPS: ", 
    sprintf("%.1f\n", $qps);
    #print "Output of batch run in: $tfile\n\n";
    die "Batch run killed by signal\n" if ($code & 255);
    $code >>= 8;
    die "Batch run crashed with code $code\n" if $code;

    #unlink $tfile;
    if ($qps < $last_qps / 4) {
	$errs++;
	last;
    }
    $last_qps = $qps;



}

if ($errs) {
	print "\n\nQuery processing has bogged down (QPS dropped by > 75%), quitting\n";
} else {
    print "\n\n       Top hole, what!!\n";
}
exit($errs);

# -----------------------------------------------------------


sub generate_problem_qset {
    die "Can't open >$qset\n"
	unless open Q, ">$qset";
    print Q "\candy
 \candy
>candy
*#!)(*#()!*#)(!**())
";
    close(Q);
}
