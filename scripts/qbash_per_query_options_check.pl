#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Checks for correct operation of the per_query_options mechanism.
# By default, tabs in a query should be replaced by single spaces.
# If -allow_per_query_options=TRUE then any options following the first
# TAB in the query string are enforced for just this query. 


# Assumes run in a directory with the following subdirectories:

$idxdir = "../test_data";


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

$fail_fast = 0;
$fail_fast = 1 if ($#ARGV > 0 && $ARGV[1] eq "-fail_fast");


$dexdir = "$idxdir/wikipedia_titles_500k";
die "Can't find $dexdir/QBASH.if\n" 
	unless -r "$dexdir/QBASH.if";

$err_cnt = 0;

#####################################################################################
#  Testing via batch.
#####################################################################################

$qbatch= "hello\nhello\t-display_col=2\nhello\n";
die "Can't write to per_query_overrides.q\n"
    unless open Q, ">per_query_overrides.q";
print Q $qbatch;
close(Q);

$cmd = "$qp index_dir=$dexdir -display_col=4 -file_query_batch=per_query_overrides.q -query_streams=1";

# Run the basic command twice, once forbidding per-query options and once
# allowing them.   One of  the queries in the batch has a per-query override which
# causes one of the 'hello nasty' results to be suppressed.  Check that the total
# number of 'hello nasty's in the concatenated results lists is correct in both
# cases.
for ($i = 0; $i <= 1; $i++) {
    $rslt = `$cmd`;
    die "Command $cmd failed\n"
	if $?;
    # Count how many occurrences of 'hello nasty' there are.  
    $match_count = 0;
    while ($rslt =~ /hello nasty/ig) { $match_count++;}
    $expected = 3 - $i;
    if ($match_count != $expected) {
	print "Error: Should be $expected occurrences of 'hello nasty' but found $match_count
Command: $cmd
Results: $rslt

";
	$err_cnt++;
	die "file_query_batch tests:          [FAILED]\n" if $fail_fast;
    }
    $cmd .= " -allow_per_query_options=TRUE";
}

print "file_query_batch tests:                       [OK]\n"
    unless $err_cnt;


#####################################################################################
# Can we crash QBASHQ by enabling allow_per_query_options and putting junk after tabs? 
#####################################################################################

print "Seeing if we can crash qbashq.exe:\n";

$qbatch= "hello\nhello\t-rubbish -junk= rhubarb\nhello\trats dogs cats el'phants\nhello\n";
die "Can't write to per_query_overrides.q\n"
    unless open Q, ">per_query_overrides.q";
print Q $qbatch;
close(Q);

$cmd = "$qp index_dir=$dexdir -display_col=4 -file_query_batch=per_query_overrides.q  -allow_per_query_options=FALSE -query_streams=1 -debug=10";
$rslt = `$cmd`;

if ($?) {
    print "Error: Command $cmd failed\nRSLTS: $rslt\n";
    $err_cnt++;
    die "\n" if $fail_fast;
}

print "Crash test:                       [OK]\n"
    unless $err_cnt;



#####################################################################################
#  Testing using -pq.  Doesn't support over-riding. Make sure tabs don't cause harm
#####################################################################################

print "-pq tests:\n";

$err_cnt += check_results_for_query_pq(
    "Basic query",
    "Academy Award for Best Production Design", 
    "",
    "^Academy Award for Best Production Design");

$err_cnt += check_results_for_query_pq(
    "Basic query with inserted tabs",
    "Academy\tAward\tfor\tBest\tProduction\tDesign", 
    "",
    "^Academy Award for Best Production Design");

$err_cnt += check_results_for_query_pq(
    "Simple query with at least 8 results",
    "hello", 
    "",
    "Hello Nasty");

$err_cnt += check_results_for_query_pq(
    "Simple query with override which should be treated as part of query",
    "hello\t-max_to_show=3", 
    "",
    "!hello nasty");  # Won't match because of max_to_show in query

#####################################################################################




	    
       
print "\n\nErrors: $err_cnt";
if ($err_cnt) {
        print "  [OVERALL FAIL]\n";
} else {
        print "                       \"Ripper, mate!\"\n";
}

print "\n";

exit($err_cnt);

# -------------------------------------------------------------------

sub check_results_for_query_pq {
    my $label = shift;
    my $query = shift;
    my $opts = shift; 
    my $should_match = shift;  # If this starts with '!' it means should not match.

    $cmd = "$qp index_dir=$dexdir -display_col=4 $opts -pq='$query'\n";
    $rslts = `$cmd`;
    die "\n$label: {$query}: QBASHQ.exe crashed with code $?.  Options were $opts\n\n"
	if $?;

    print "$label: {$query}: "; 

    if ($should_match =~ /!(.*)/) {
	$negpat = $1;
	if (! ($rslts =~ /${negpat}/s)) {
	    print "[OK]\n";
	    return 0;
	} else {
	    print "[FAIL]:
Command: $cmd
Results: $rslts
";
	    die "\n" if $fail_fast;  
	    return 1;
	}

    } else {
	if ($rslts =~ /${should_match}/s) {
	    print "[OK]\n";
	    return 0;
	} else {
	    print "[FAIL]:
Command: $cmd
Results: $rslts
";
	    die "\n" if $fail_fast;	    
	    return 1;
	}
    }
}

