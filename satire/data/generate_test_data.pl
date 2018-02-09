#! /usr/bin/perl -w

use List::Util qw(shuffle);

die "Usage: $0 number_of_terms number_of_documents max_run_length\n"
    unless $#ARGV == 2;

die "Can't write to test_data.tsv\n"
    unless open T, ">test_data.tsv";

for ($t = 0; $t < $ARGV[0]; $t++) {
    @doclist = shuffle 0..($ARGV[1] - 1);
    $docs_not_output = $ARGV[1];
    $max_score = int (2.5 * $ARGV[1] / $ARGV[2]);
    $max_score = 10 if $max_score < 10;
    $score = $max_score;

    # Output the term-id
    print T sprintf("%08d\t", $t);
		      
    while ($docs_not_output > 0 ) {
	$run_length = 1 + int(rand($ARGV[2]));  # Choose a random run length in  1..max_run_length
	$run_length = $docs_not_output if ($run_length > $docs_not_output);
	# Output the run header
	$i = 0;  # index into shuffled doc list
	print T " $score $run_length*";
	#output the run
	for ($r = 0; $r < $run_length; $r++) {
	    print T " ", $doclist[$i++];
	    $docs_not_output--;
	}
	print T "#";   # Mark the end of the run.
	$score--;
    }
    print T "\n";
}

close T;

print "@ARGV", "\ntest_data.tsv written.\n";

exit(0);
