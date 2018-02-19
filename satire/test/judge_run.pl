#! /usr/bin/perl -w

# First load the judgments into %hj.  hj is indexed by query number
# and contains semicolon-separated "<document> TAB <score>" pairs

die "Can't load judgments file ../data/DINNER/qdr.txt"
    unless open J, "../data/DINNER/qdr.txt";

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


# Now read the SATIRE results

die "Can't open bhaskar.out\n"
    unless open R, "bhaskar.out";

while (<R>) {
    chomp;
    next if (/^\s*$/);
    if (/^Query\s+([0-9]+):\s/) {
	if (defined($qid)) {
	    print "$qid\t$relfound\t", sprintf("%6.4f", $cdg), "\n";
	    if ($relfound > 0) {
		$successes++;
	    } else {
		$failures++;
	    }
	} 
	$qid = $1;
	$cdg = 0.0;
	$relfound = 0;
    } elsif (/\s+([0-9]+)\s+([0-9]+)\s+/) {
	$rank = $1;
	$did = $2;
	$gain = lookup_gain($qid, $did);
	$dg = $gain /log2($rank + 1);
	$cdg += $dg;
	$relfound++ if ($gain > 0);
    } else {
	die "Unmatched record in bhaskar.out: $_\n";
    }
}

print "

Successes: $successes
Failures: $failures
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
    return 0;
}
