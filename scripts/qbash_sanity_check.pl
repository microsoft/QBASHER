#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Given a QBASHQ exe, runs a bunch of test queries against a standard two-shard index 
# with various combinations of options:

# For many combinationso of queries and options, check that the number of suggestions made 
#is as expected.  Expectations are usually based on past runs.

# For combinations of Greek-letter options, check that the order of suggestions is
# as expected.

# In every case, check result sets for duplicate suggestions.

# Assumes run in a directory with the following subdirectories:

$idxdir = "../test_data";

$base_opts = "-display_col=1 -duplicate_handling=2";  # Legacy

$|++;

die "Usage: $0 <QBASHQ binary> [-fail_fast]
	If -fail_fast is given the script will stop at the first failure after rerunning
	the failing command with -debug=1\n" 
		unless ($#ARGV >= 0);
$qp = $ARGV[0];
$qp = "../src/visual_studio/x64/Release/QBASHQ.exe"
    if $qp eq "default";

die "$qp is not executable\n" unless -e $qp;
$fail_fast = 0;
$fail_fast = 1 if ($#ARGV > 0 && $ARGV[1] eq "-fail_fast");
$ix = "$idxdir/wikipedia_titles_500k/";
$err_cnt = 0;

die "Can't find QBASHER index in $ix\n" 
	unless (-r "$ix/QBASH.if");


print "\nExpect Got Cmd\n------ --- -----------------------------\n";

$err_cnt += check_count("water", "", 8, 0);
$err_cnt += check_count("water", "-max_to_show=4", 4, 0);
$err_cnt += check_count("water", "-max_candidates=4", 4, 0);
$err_cnt += check_count("united states congress", "max_length_diff=0", 3, 0);  # There are many case/punct variants
$err_cnt += check_count("water xxnoexistxxx", "", 0, 0);


$err_cnt += check_count("[aol google facebook]", "", 8, 0);


$err_cnt += check_count("be be or not to to", "", 4, 0);   
$err_cnt += check_count("\\\"to be or not to be\\\"", "", 4, 0);

$err_cnt += check_count("Ada Byron's notes on the analytical engine", "", 1, 0);
$err_cnt += check_count("\\\"Ada Byron's notes on the analytical engine\\\"", "", 1, 0);
$err_cnt += check_count("Americans with Disabilities Act of 1990/Findings and Purposes", "", 1, 0);
$err_cnt += check_count("\\\"Americans with Disabilities Act of 1990/Findings and Purposes\\\"", "", 1, 0);
$err_cnt += check_count("list of cyclists in and the /f", "", 2, 0);
$err_cnt += check_count("list of cyclists in and the f", "", 0, 0);
$err_cnt += check_count("list of cyclists in and the f", "-auto_partials=on", 2, 0);

$err_cnt += check_count("/b self long /f", "", 2, 0);  # Multiple partials
$err_cnt += check_count("/b long self /sim /f", "", 2, 0);  # Multiple partials
$err_cnt += check_count("of self long /sim /f /b", "", 2, 0);  # Multiple partials


# Timeouts should be consistent
$err_cnt += check_count("of self long /sim /f /b", "--timeout_kops=20", 2, 0);
$err_cnt += check_count("of self long /sim /f /b", "--timeout_kops=20", 2, 0);
$err_cnt += check_count("of self long /sim /f /b", "--timeout_kops=20", 2, 0);
$err_cnt += check_count("of self long /sim /f /b", "--timeout_kops=20", 2, 0);
$err_cnt += check_count("of self long /sim /f /b", "--timeout_kops=20", 2, 0);
$err_cnt += check_count("of self long /sim /f /b", "--timeout_kops=20", 2, 0);
$err_cnt += check_count("of self long /sim /f /b", "--timeout_kops=20", 2, 0);
$err_cnt += check_count("of self long /sim /f /b", "--timeout_kops=20", 2, 0);
$err_cnt += check_count("of self long /sim /f /b", "--timeout_kops=20", 2, 0);

# Check query cleanup.  Note that the following includes smart quotes
$err_cnt += check_count("\@MeMbErS---the\@united’’states&congress&from&^%", "-display_col=1", 1, 0);

# Check epsilon  -- excess length feature

$err_cnt += check_topk_results("water", "-max_candidates=100", 0, 5,
			       "Water\t0.720",
			       "Creedence Clear Water Revival\t0.626",
			       "Water vascular system\t0.579",   
			       "Distilled water\t0.571",
			       "Carbonated water\t0.565"
    );

$err_cnt += check_topk_results("water", "-max_candidates=100 -alpha=0.2 -epsilon=0.8", 0, 5,
			       "Water\t0.944",
			       "Distilled water\t0.514",
			       "Carbonated water\t0.513",
			       "Tonic water\t0.506",
			       "Brackish water\t0.503",	       
    );

# Check delta   -- primacy feature

$err_cnt += check_topk_results("eagle", "-max_candidates=100", 0, 3,
			       "Eagle\t0.616",
			       "McDonnell Douglas F-15E Strike Eagle\t0.577",
			       "American Eagle Outfitters\t0.502"
    );


$err_cnt += check_topk_results("eagle", "-max_candidates=100 -alpha=0.2 -delta=0.8", 0, 5,
			       "Eagle\t0.923",
			       "Eagle Nebula\t0.891",   # Order of these two is fragile unfortunately
			       "Eagle nebula\t0.891",
			       "Eagle-Eye Cherry\t0.889",
			       "Eagle Talon\t0.885"
    );


#Check gamma    -- words-in-sequence feature

$err_cnt += check_topk_results("barrier Israel", "-max_candidates=100", 0, 2,
	"Israel–Gaza barrier\t0.340",
	"Wall / Protective Barrier between Israel and Palestina\t0.113"
	);


$err_cnt += check_topk_results("barrier Israel", "-max_candidates=100 -alpha=0.2 -gamma=0.8", 0, 2,
	"Wall / Protective Barrier between Israel and Palestina\t0.822",
	"Israel–Gaza barrier\t0.068"
	);

#check beta  -- phrase feature

$err_cnt += check_topk_results("climate change convention", "-max_candidates=100 ", 0, 2,
	"United Nations Framework Convention on Climate Change\t0.565",
	"Climate Change Convention\t0.219"
	);


$err_cnt += check_topk_results("climate change convention", "-max_candidates=100  -alpha=0.2 -beta=0.8", 0, 4,
	"Climate Change Convention\t0.843",
	"United Nations Framework Convention on Climate Change\t0.113"
	);

print "-----------------------------------------------------------\n\n
-------------- Checking Phrases -------------------------------\n";

check_phrase_sanity("\"Western Reef\"");
check_phrase_sanity("\"australian national\"");
check_phrase_sanity("\"Ashmore and Cartier\"");
check_phrase_sanity("\"publications in mathematics\"");
check_phrase_sanity("\"savings bank\"");
check_phrase_sanity("\"software engineering\"");
check_phrase_sanity("\"to be or not to be\"");

print "-----------------------------------------------------------\n\n
-------------- Checking Disjunctions -------------------------------\n";

$err_cnt +=check_disjunction_sanity("[rat mouse] trap", "rat trap|mouse trap");
$err_cnt +=check_disjunction_sanity("[pure applied vedic] mathematics", 
				    "pure mathematics|applied mathematics|vedic mathematics");
$err_cnt +=check_disjunction_sanity("[ballet brewing motorcycle] company", 
				    "ballet company|brewing company|motorcycle company");
$err_cnt +=check_disjunction_sanity("[australian british austrian] [politician leader]", 
	"australian politician|british politician|austrian politician|australian leader|british leader|austrian leader");
# Phrase within disjunction
$err_cnt +=check_disjunction_sanity("[\"south africa\" \"motor car\"] [\"progressive federal party\" \"pierce arrow\"]", 
	"\"south africa\" \"progressive federal party\"|\"south africa\" \"pierce arrow\"|\"motor car\" \"progressive federal party\"|\"motor car\" \"pierce arrow\"");
$err_cnt +=check_disjunction_sanity("[rimfire \"armour piercing\"] ammunition", 
	"rimfire ammunition|\"armour piercing\" ammunition");
$err_cnt +=check_disjunction_sanity("[\"manic depressive\" \"Fitz-Hugh–Curtis\"] [illness syndrome]", 
	"\"manic depressive\" illness|\"manic depressive\" syndrome|\"Fitz-Hugh–Curtis\" illness|\"Fitz-Hugh–Curtis\" syndrome");




print "-----------------------------------------------------------\n";

# Disjunction within phrase
$err_cnt += check_count("\\\"[kabuki lolita sanfilippo] syndrome\\\"", "", 3, 0);
$err_cnt += check_count("\\\"[ohio georgia california] [state federal] politician\\\"", "", 1, 0);
$err_cnt += check_count("\\\"university of [Pennsylvania Hawaii Nottingham] [law medical] school\\\"", "", 3, 0);

print "-----------------------------------------------------------\n\n
-------------- Checking Relaxation Options -------------------------------\n";

$err_cnt += check_topk_results("california state university Fullerton", "", 0, 1,
	"California State University, Fullerton"
    );

$err_cnt += check_topk_results("california state university Fullerton", "-relaxation_level=1", 0, 4,
	"California State University, Fullerton",
	"California Polytechnic State University",
	"California State University, Chico",
	"California State University San Marcos");


print "-----------------------------------------------------------\n\n
-------------- Checking Rank-Only Operator -------------------------------\n";


$err_cnt += check_topk_results("reactor", "-max_candidates=100 -max_to_show=100", 0, 4,
    "CANDU Reactor",
    "CANDU reactor",
    "Natural nuclear fission reactor",  
    "Juno Reactor");

$err_cnt += check_topk_results("reactor ~S6G ~A4W", "-max_candidates=200 -max_to_show=10", 0,4,
    "A4W reactor",   
    "S6G reactor",
    "CANDU Reactor",
    "CANDU reactor");

$err_cnt += check_topk_results("reactor ~\\\"experimental breeder\\\" ~\\\"pressurized water\\\"", "-max_candidates=200 -max_to_show=10", 0,4,
    "Experimental Breeder Reactor I",   
    "Pressurized Water Reactor",
    "Pressurized water reactor",
    "CANDU reactor");


# The following don't work with 1.5.114 -- Why not?
#$err_cnt += check_topk_results("reactor ~pebble", "-max_candidates=200 -max_to_show=10", 0,4,
#    "candy crush saga on facebook",   
#    "jackpot party casino facebook",
#    "facebook candy crush",
#    "candy crush on facebook");

#$err_cnt += check_topk_results("reactor ~\\\"light water\\\" ~\\\"pebble bed\\\"", "-max_candidates=200 -max_to_show=10", 0,4,
#    "candy crush saga on facebook",   
#    "jackpot party casino facebook",
#    "facebook candy crush",
#    "candy crush on facebook");




print "-----------------------------------------------------------\n";


print "\n\nErrors: $err_cnt";
if ($err_cnt) {
	print " -- $0: [OVERALL FAIL]\n";
} else {
	print "                       \"Simply marvellous!\"\n";
}


exit($err_cnt);


# -----------------------------------------------------------------------------

sub check_count {
    my $pq = shift;
    my $options = shift;
    my $expected = shift;
    my $show_rslts = shift;
    my $cmd = "$qp index_dir=$ix $base_opts -pq=\"$pq\" $options";
    print $cmd;
    my $rslts = `$cmd`;
    die "Command '$cmd' failed with code $?\n"
	if ($?);
    print $rslts if $show_rslts;
    my $count = 0;
    my %dup_check;
    while ($rslts =~ /([^\t]+)\s+[01]\.[0-9]+\s*\n/sg) {
	die "\n\n$rslts\nResults for '$cmd' contain duplicate suggestions\n"
	    if defined($dup_check{$1});
	$dup_check{$1} = 1;
	$count++;
    }
    print sprintf(" - %4d %4d - ", $expected, $count);
    if ($count == $expected) {print " [OK]\n";}
    else {
	print " [FAIL]\n";
	if ($fail_fast) {
	    my $rslts = `$cmd`;
	    print $rslts;
	    exit(1);
	}
	return 1;
    }
    return 0;
}


sub check_topk_results {
    # This used to test ranks, but that's prone to errors when two items have
    # identical scores.  Now changed to check presence and (optionally) scores instead.
    my $pq = shift;
    my $options = shift;
    my $show_rslts = shift;
    my $k = shift;
    my @items_with_scores;
    for (my $a = 0; $a < $k; $a++) {
	$items_with_scores[$a] = shift;
    }	
    my $missings = 0;
    my $cmd = "$qp index_dir=$ix $base_opts -pq=\"$pq\" $options";
    my $rslts = `$cmd`;
    die "Command '$cmd' failed with code $?\n"
	if ($?);

    print $rslts if $show_rslts;
    # Check for duplicates.
    my %dup_check;
    while ($rslts =~ /([^\t]+)\s+([0-9]+\.[0-9]+)\s*\n/sg) {
	die "\n\n$rslts\nResults for '$cmd' contain duplicate suggestions\n"
	    if defined($dup_check{$1});
	$dup_check{$1} = 1;
    }

    # Check that all the listed items are present
    for (my $a = 0; $a < $k; $a++) {
	if (!($rslts =~ /$items_with_scores[$a]/s)) {
	    print "  Item '$items_with_scores[$a]' not found in results.\n";
	    $missings++;
	}
    }
    print $cmd;
    if ($missings == 0) {print " [OK]\n";}
    else {
	print " [FAIL]\n";
	if ($fail_fast) {
	    my $rslts = `$cmd`;
	    print $rslts;
	    exit(1);
	}
	return 1;
    }
    return 0;
}



sub check_phrase_sanity {
    # Given a phrase (in double quotes) run it with and without quotes and make 
    # sure that all the results in the without-quotes set which actually match 
    # the phrase are present in the with-quotes set.
    # Turn off  display of spelling correction
    my $with_quotes = shift;

    my $errs = 0;

    print "check_phrase_sanity($with_quotes) - ";
    my $without_quotes = $with_quotes;
    $without_quotes =~ s/\"//g;
    my $cmd = "$qp index_dir=$ix $base_opts  -max_candidates=1000 -max_to_show=1000 -display_col=1 -pq='$without_quotes'";
    my $without_rslts = `$cmd`;
    die "[FAIL]\n'$cmd' failed with $?\n" if $?;
    my $cmd_with = "$qp index_dir=$ix  -max_candidates=1000 -max_to_show=1000 -display_col=1 -pq='$with_quotes'";
    my $with_rslts = `$cmd_with`;
    die "[FAIL]\n'$cmd_with' failed with $?\n" if $?;

    # Strip shard statistics out of $with_rslts
    $with_rslts =~ s/\s+Shard\s+[0-9]+[^\n]+\n//isg;
    # Strip shard statistics out of $without_rslts
    $without_rslts =~ s/\s+Shard\s+[0-9]+[^\n]+\n//isg;

    # Make a hash of all the with results
    my %withs;
    my $phrase_matches = 0;
    while ($with_rslts =~ /([^\t]+)\s+[01]\.[0-9]+\s*\n/isg) {
	my $sug = $1;
	#print "SUG: $sug\n";
	$withs{$sug} = 1;
	$phrase_matches++;
    }
    
    print "Matches for $with_quotes: $phrase_matches\n" if ($fail_fast);

    # Now do the checking.  Get each result from the without set which 
    # contains a phrase match and check that it's in the withs hash.

    $without_quotes =~ s/ /\\W+/g;   # Allow for broad set of delimiters

    while ($without_rslts =~ /([^\t]+)\s+[01]\.[0-9]+\s*\n/isg) {
	my $sug = $1;
	next unless $sug =~ /$without_quotes/i;
	if (! defined($withs{$sug})) {
	    print "FAIL: '$sug' expected in phrase results but not found.\n" if ($fail_fast);
	    $errs++;
	} else {
	    $withs{$sug} = 0;
	}
    }

    # Now check that the phrase operation hasn't returned extra (possibly spurious) results.
    # Is there anything in the withs_set?
    foreach my $k (keys %withs) {
	if ($withs{$k} >= 1) {
	    print "FAIL: '$k' in phrase results was unexpected.\n" if ($fail_fast);
	    $errs++;
	}
    }

    
    if ($errs) {
	print "[FAIL] $errs\n";
	if ($fail_fast) {
	    my $rslts = `$cmd`;
	    print $rslts, "\n\n";

	    print "Phrase command was: $cmd_with\n";
	    
	    exit(1);
	}
    } else {
	print "[OK]\n";	
    }
    return $errs;
}


sub check_disjunction_sanity {
    # The logic of this function is that a query with disjunctions should 
    # produce the union of results from a set of other queries.  E.g. that
    # '[rat mouse] trap' should produce the union of results for 'rat trap'
    # and mouse trap.  
    # Pass in a disjunction query and an equivalent set of queries separated by '|'
    my $dizzo = shift;
    my $qset = shift;
    
    print "check_disjunction_sanity($dizzo) - ";
    my $errs = 0;
    # Get the set of results for the disjunction query
    my $cmd = "$qp index_dir=$ix $base_opts -max_candidates=1000 -max_to_show=1000 -pq='$dizzo'";
    my $dizzo_rslts = `$cmd`;
    die "[FAIL]\n'$cmd' failed with $?\n" if $?;
    #print "\n -------- $dizzo_rslts\n";
    # Strip shard statistics out of $dizzo_rslts
    $dizzo_rslts =~ s/\s+Shard\s+[0-9]+[^\n]+\n//isg;

    # And stick them in a hash
    my %dizzo_hash;
    while ($dizzo_rslts =~ /([^\t]+)\s+[01]\.[0-9]+\s*\n/isg) {
	$dizzo_hash{$1} = 1;
    }
    
    my @non_dizzo = split /\|/, $qset;
    foreach my $q (@non_dizzo) {
	#print "RUNNING '$q'\n";
	$cmd = "$qp index_dir=$ix $base_opts -max_candidates=1000 -max_to_show=1000 -pq='$q'";
	my $q_rslts = `$cmd`;
	# Strip shard statistics out of $q_rslts
	$q_rslts =~ s/\s+Shard\s+[0-9]+[^\n]+\n//isg;

	die "[FAIL]\n'$cmd' failed with $?\n" if $?;
	while ($q_rslts =~ /([^\t]+)\s+[01]\.[0-9]+\s*\n/isg) {
	    my $sug = $1;
	    if (defined ($dizzo_hash{$sug})) {
		$dizzo_hash{$sug} = 0;
	    } else {
		print "FAIL:  Expected '$sug' in disjunction results.\n" if ($fail_fast);
		$errs++;
	    }
	}
    }

    # Is there anything in the dizzo_set?
    foreach my $k (keys %dizzo_hash) {
	if ($dizzo_hash{$k} >= 1) {
	    print "FAIL: '$k' in disjunction results was unexpected.\n" if ($fail_fast);
	    $errs++;
	}
    }
    
    if ($errs) {
	print "[FAIL] $errs\n";
	if ($fail_fast) {
	    my $rslts = `$cmd`;
	    print $rslts;
	    exit(1);
	}
    } else {
	print "[OK]\n";	
    }
    return $errs;
}


1;
