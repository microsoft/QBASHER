#! /usr/bin/perl -w

$num_terms = 1500;
$num_docs = 2500;
$num_queries = 1000;
print "Generate test data for $num_terms terms, $num_docs documents and max_run_length=10\n";
chdir "../data";
die "generate_test_data.pl failed\n"
    if system("./generate_test_data.pl $num_terms $num_docs 10");
die "generate_test_queries.pl failed\n"
    if system("./generate_test_queries.pl $num_queries 10 $num_terms");

chdir "../test";
print "Run the indexer and put the index in the test directory\n";
$cmd = "../src/i.exe inputFileName=../data/test_data.tsv outputStem=test numDocs=$num_docs numTerms=$num_terms";
die "Indexing command ($cmd) failed\n"
    if system($cmd);
print "Run the queries and put the output in the test directory\n\n";
print $cmd, "\n";
$cmd = "../src/q.exe indexStem=test numDocs=$num_docs numTerms=$num_terms debug=0  < ../data/test_queries.q > test_queries.out";
die "Query processing command ($cmd) failed\n"
    if system($cmd);
print "All finished.\n\n";

exit(0);



