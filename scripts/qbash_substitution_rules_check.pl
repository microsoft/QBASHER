#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Test out the mechanism by which queries can be modified by regex rules in
# a substitution_rules file.


$idxdir = "../test_data";
$fail_fast = 0;

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

$ix = "$idxdir/wikipedia_titles_500k";
$err_cnt = 0;
$base_opts = "-display_parsed_query=true";

die "Can't find QBASHER index in $ix\n"
        unless (-r "$ix/QBASH.if");

print "\nChecking single substitutions....\n";
$err_cnt += check_topk_results("log in", "-use_substitutions=TRUE", 0, 6,
			      "Login spoofing"
        );

print "Note that this next test will fail if rules are applied to input with '[' ...\n";
$err_cnt += check_topk_results("pet breeds", "-use_substitutions=TRUE", 0, 4,
			      "List of cat breeds",
			      "Dog breeds",
			      "Cat breeds",
			      "List of Dog Breeds",
        );

$err_cnt += check_topk_results("clean", "-use_substitutions=TRUE", 0, 3,
			      "Bill Tidy",
			      "Spotless starling",
			      "Eternal Sunshine of the Spotless Mind"
        );

print "Checking that a substitution rule with both LHS and RHS in uppercase works\n";
$err_cnt += check_topk_results("UCASETEST", "-use_substitutions=TRUE", 0, 1,
			      "Zippergate",
        );

print "Checking multiple substitutions from one pattern ....\n";
$err_cnt += check_topk_results("jump jump", "-use_substitutions=TRUE", 0, 2,
			      "Dance dance revolution",
			      "Dance Dance Revolution"
        );


print "Checking substitution by multiple rules ....\n";
#lloyd webber -> superstar -> australian actress -> cate blanchett -> oscars
$err_cnt += check_topk_results("lloyd webber", "-use_substitutions=TRUE", 0, 2,
			      "Oscars",
			      "The Oscars"
        );


print "Checking use of a French substitutions file ....\n";


$err_cnt += check_topk_results("tour eiffel", "-use_substitutions=TRUE -language=FR", 0, 2,
			      "Eiffel tower",
			      "Eiffel Tower"
        );


print "Checking combination of substitutions and classifier_mode=1 ....\n";

$err_cnt += check_topk_results("AFAF", "-use_substitutions=TRUE -classifier_mode=1", 0, 1,
			      "ablation for atrial fibrillation"
        );

$err_cnt += check_topk_results("pet eat pet", "-use_substitutions=TRUE -classifier_mode=1", 0, 2,
			       "Dog Eat Dog",
			       "Dog-eat-dog"
        );

$err_cnt += check_topk_results("unpleasant 13", "-use_substitutions=TRUE -classifier_mode=1", 0, 1,
			      "13 days in hell"
        );


print "Checking removal of possessives ... \n";

$err_cnt += check_topk_results("macdonald's", "-use_substitutions=TRUE -display_col=1", 0, 3,
			      "John A. MacDonald",
			      "John A. Macdonald",
			      "Ramsay MacDonald"
        );

# QBASHQ avoids substitutions within phrases.
$err_cnt += check_count("\\\"macdonald s\\\"", "-use_substitutions=TRUE -display_col=1", 0, 0);


#Note that we changed classifier_mode in late August 2016 to strip all punctuation.  That dooms to failure
# the following tests			       
#$err_cnt += check_topk_results("macdonald's", "-use_substitutions=TRUE -classifier_mode=1 -display_col=1", 0, 1,
#			      "macdonalds"
#        );

#$err_cnt += check_topk_results("\\\"macdonald s\\\"", "-use_substitutions=TRUE -display_col=1 -classifier_mode=1", 0, 1,

#\\"  The escaped quotes in the prev line bugger up emacs pretty printing :-(  This fixes it

#			      "macdonalds"
#        );


print "Checking that there's no crash or bad behaviour from an undefined language\n";

$err_cnt += check_count("log in", "-use_substitutions=FALSE -display_col=1", 2, 0);
$err_cnt += check_count("log in", "-language=XX -use_substitutions=TRUE -display_col=1", 2, 0);



print "Checking multiple substitutions with operators ....\n";


$err_cnt += check_results_match_pattern("pet", "-use_substitutions=TRUE -display_parsed_query=TRUE", "en substitutions is \\{[cat dog rat hamster]");

$err_cnt += check_results_match_pattern("dog", "-use_substitutions=TRUE -display_parsed_query=TRUE", "en substitutions is \\{[wolf dingo \"domestic dog\"]");

$err_cnt += check_results_match_pattern("wonderful", "-use_substitutions=TRUE -display_parsed_query=TRUE", "en substitutions is \\{[wonderful fantastic magnificent]");

$err_cnt += check_results_match_pattern("magnificent", "-use_substitutions=TRUE -display_parsed_query=TRUE", "en substitutions is \\{[wonderful fantastic magnificent]");


$err_cnt += check_results_match_pattern("magnificent", "-use_substitutions=TRUE -display_parsed_query=TRUE", "en substitutions is \\{[wonderful fantastic magnificent]");

$err_cnt += check_results_match_pattern("what a magnificent clean young pet it is", "-use_substitutions=TRUE -display_parsed_query=TRUE", "en substitutions is \\{what a [wonderful fantastic magnificent] [tidy spotless] young [cat dog rat hamster] it is");





if ($err_cnt == 0) {
    print "

             Formidable!   Tous les tests de règles de substitution ont passés.
";
} else {
    print "

Errors: $err_cnt\n";
}

exit($err_cnt);

# --------------------------------------------------------------------

sub check_results_match_pattern {
        my $pq = shift;
        my $options = shift;
        my $pattern = shift;

	# Escape any square brackets in pattern
	$pattern =~ s@(\[|\])@\\$1@g;

        my $cmd = "$qp index_dir=$ix display_col=4 -pq=\"$pq\" $options";
        my $rslts = `$cmd`;
        die "Command '$cmd' failed with code $?\n"
                if ($?);

	if ($rslts =~ m@$pattern@) {
	    print "         $cmd  [OK]\n";
	    return 0;# What we're returning is an error count
	} else {
	    print "         $cmd  [FAIL]\n";
	    print $rslts;
	    if ($fail_fast) {
		exit(1);
	    }
	    return 1;
	}
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
	while ($rslts =~ /^([^\t]+)\t.*?\n/sg) {
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


sub check_count {
        my $pq = shift;
        my $options = shift;
        my $expected = shift;
        my $show_rslts = shift;

        # VS does terrible things to non-ASCII command line arguments, so
        # we need to put the query in a file and read that as a batch.
        die "Can't open temporary query file\n"
            unless open QF, ">utf8.q";
        print QF "$pq\n";
        close QF;
        
        #my $cmd = "$qp index_dir=$ix $base_opts -pq=\"$pq\" $options";
        my $cmd = "$qp index_dir=$ix $base_opts -file_query_batch=utf8.q -x_batch_testing=TRUE $options";
        print $cmd, "{$pq}";
        my $rslts = `$cmd`;
        die "Command '$cmd' failed with code $?\n"
                if ($?);
        print $rslts if $show_rslts;
        my $count = 0;
        my %dup_check;
        # The pattern on the next line has to match output (if any) from experimental_show()
        # in QBASHQ_lib.c -- maybe somewhere else in classifier mode???
        while ($rslts =~ /Query:\t[^\t]*\t[^\t]*\t([^\t]+).*?\t([01]\.[0-9]+)\s*\n/sg) {
                 die "\n\n$rslts\nResults for '$cmd' contain duplicate suggestions\n"
                        if defined($dup_check{$1});
                 $dup_check{$1} = 1;
                 # print "\n\n  ----> Result matched was '$1'.  Score was $2\n";
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


