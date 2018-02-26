#! /usr/bin/perl -w

# First load the judgments into %hj.  hj is indexed by query number
# and contains semicolon-separated "<document> TAB <score>" pairs

die "Usage: $0 <judgment_file> <results_file> [<judging_depth>]\n"
    unless $#ARGV >= 1;

$jfile = $ARGV[0];
$rsltsfile = $ARGV[1];
$depth = 1000;   # Maximum rank used in calculating NDCG score.
$depth = $ARGV[2] if ($#ARGV >= 2);

print "Reading judgments from $jfile and results from $rsltsfile ....\n\n";

die "Can't load judgments file $jfile"
    unless open J, $jfile;

while (<J>) {
    chomp;
    if (/^([0-9]+)\t(.*[0-9])/) {
	$qnum = $1;
	$judgment = $2;
	if (defined($hj{$qnum})) {
	    $hj{$qnum} .= ";$judgment";
	} else {
	    $hj{$qnum} = "$judgment";
	}
    } else {
	die "format error: '$_'\n";
    }
}

close(J);


# Now read the results (In either TREC submission format or Bhaskar's format)

die "Can't open $rsltsfile\n"
    unless open R, $rsltsfile;
$current_qid = -1;
$ave_dcg = 0;

print 
"Query  #Rel_found  DCG   #Unjudged
=====  ==========  ====  =========\n";

while (<R>) {
    chomp;
    s /^\s+//;  # remove leading spaces
    @f = split /\s+/;
    if ($#f == 5) {
	# Assume TREC submission format
	$qid = $f[0];
	$did = $f[2];
	$rank = $f[3];
    } elsif ($#f == 3) {
	# Assume Bhaskar's submission format
	$qid = $f[0];
	$did = $f[1];
	$rank = $f[2];
    } elsif ($#f == 0) {
	next;
    } else {
	$fields = $#f + 1;
	die "Error: Non-empty lines in $rsltsfile must have either 6 fields (TREC) or 3 (Bhaskar)
Offending line was $_
It's field count was $fields\n";
    }
	    
    next if $rank > $depth;

    if ($qid != $current_qid) {
	if ($current_qid >= 0) {
	    # Report results for old query
	    print "$qid\t$relfound\t", sprintf("%6.4f", $cdg), "\t$unjudged\n";
	    if ($relfound > 0) {
		$successes++;
	    } else {
		$failures++;
	    }
	}
	$ave_dcg += $cdg;
	$cdg = 0.0;
	$relfound = 0;
	$unjudged = 0;
	$current_qid = $qid;
    }
    
    $gain = lookup_gain($qid, $did);
    if ($gain < 0) {
	$unjudged++;
	$gain = 0;
    }
    $dg = $gain /log2($rank + 1);
    $cdg += $dg;
    $relfound++ if ($gain > 0);    
}

if ($qid != $current_qid) {
    if ($current_qid >= 0) {
	# Report results for old query
	print "$qid\t$relfound\t", sprintf("%6.4f", $cdg), "\t$unjudged\n";
	if ($relfound > 0) {
	    $successes++;
	} else {
	    $failures++;
	}
    }
}

$ave_dcg /= ($successes + $failures);

print "

Successes: $successes
Failures: $failures

Average DCG: $ave_dcg
    ";

exit(0);

# ---------------------------------------------------------

sub log2 {
    my $arg = shift;
    return log($arg)/log(2);
}

sub lookup_gain {
    my $qid = shift;
    my $did = shift;
    die "lookup_gain:  arg undefined\n"
	unless defined($qid) && defined($did);
    die "No hash entry for $qid\n"
	unless defined($hj{$qid});

    my @rels = split /;/, $hj{$qid};
    foreach $rel (@rels) {
	if ($rel =~ /([0-9]+)\t([0-9]+)/) {
	    return $2 if $1 eq $did;
	}
    }
    return -1;   # Signal unjudged.
}
