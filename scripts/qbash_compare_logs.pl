#! /usr/bin/perl -w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

# Compare two different QBQ output files for the same index / queryfile
# combination and report any significant differences.  It is not expected
# that the queries in the logfiles will be in the same order
#
# Intended usage is to compare single-threaded versus multi-threaded
# QBQ runs.

die "Usage: $0 <log1> <log2> [-warn]\n"
    unless $#ARGV >= 1  && -f $ARGV[0] && -f $ARGV[1];

$abort = 1;
$abort = 0 if ($#ARGV > 1 && $ARGV[2] =~ /-warn/i);
$warnings = 0;

die "Can't open $ARGV[0]\n" unless open F, $ARGV[0];

# Build up a (query, results) hash from the first file.
$qcnt1 = 0; 
while (<F>) {
    next if /^\s*$/; 
    last if /^Inputs processed: [0-9]+/;
    $lyn = $_;
    if (defined($query)) {
	if (/^(Query|Milestone): /) {
	    $hash{$query} = $results;
	    undef $query;
	    $qcnt1++;
	    if (/^Query: \{(.*?)\}/) {
		$query = $1;
		$results = "";
	    }
	} else {
	    $results .= $lyn;
	}
    } elsif (/^Query: \{(.*?)\}/) {
	$query = $1;
	$results = "";
    }
}

close F;

#print "Found results for $qcnt1 queries\n";

die "Can't open $ARGV[1]\n" unless open F, $ARGV[1];

# Check the second file against the hash.
undef $query;
undef $results;
$qcnt = 0;
while (<F>) {
    next if /^\s*$/; 
    last if /^Inputs processed: [0-9]+/;
    $lyn = $_;
    if (defined($query)) {
	if (/^(Query|Milestone): /) {
	    # Checking.
	    #print "$results\n\n";
	    $qcnt++;
	    #print "Comparing\n\n$results\n\nagainst\n\n$hash{$query}\n\n for\n\n {$query}\n\n";
	    if (!defined($hash{$query})) {
		if ($abort) {
		    die "Error: Query $query in $ARGV[1] is not in hash\n"; 
		} else {
		    warn "Warning: Query $query in $ARGV[1] is not in hash\n";
		    $warnings++;
		}
	    } elsif ($hash{$query} ne $results) {
		if ($abort) {
		    die "Error: Results for $query are different.
$ARGV[0]: $hash{$query}

$ARGV[1]: $results

";
		} else {
		    warn "Warning: Results for $query are different.
$ARGV[0]: $hash{$query}

$ARGV[1]: $results

";
		    $warnings++;
		}
	    }
	    $hash{$query} = $results;
	    undef $query;
	    $results = "";
	    if (/^Query: \{(.*?)\}/) {
		$query = $1;
	    }
	} else {
	    $results .= $lyn;
	}
    } elsif (/^Query: \{(.*?)\}/) {
	$query = $1;
	$results = "";
    }
}

if ($abort) {
    print "Success:  $qcnt queries checked.\n";
} else {
    print "$qcnt queries checked, $warnings differences found\n";
}
close F;
exit($warnings);
