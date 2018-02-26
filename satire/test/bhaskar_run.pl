#! /usr/bin/perl
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

# Top level script to run SAAT/Impact-Ordered experiments for the Mitra, Craswell, Diaz, Yilmaz and 
# Hawking paper.  The data from Bhaskar is expected to be in files in ../data/DINNER:
#    qdr.txt qt.txt tds.txt

chdir "../data";

# ------------------------------ Step 1 Converting judgments to QRELS -----------------------------
$rslt = `./convert_bhaskar_judgments_to_qrels.pl`;
die "convert_Bhaskar qrels failed\n"
    if $?;
$rslt =~ /: ([0-9]+) lines converted./;
$num_queries = $1;
print "Number of queries whose judgments were converted to qrels: $num_queries\n";


# ------------------------------ Step 2 Writing the T_per_query file needed by INST  -----------------------------
#Write the T_per_query file
die "Can't write to bhaskar.T_per_query\n"
    unless open T, ">bhaskar.T_per_query";
for ($q = 1; $q <= $num_queries; $q++) {
    print T "$q\t3\n";
}
close(T);
print "bhaskar.T_per_query written\n";

# ------------------------------ Step 3 Building the index -----------------------------
$cmd = "../src/i.exe inputFileName=DINNER/tds.txt outputStem=../test/bhaskar numDocs=1842879";
die "Indexing command $cmd failed \n"
    if system($cmd);

# ------------------------------ Step 4 Running the queries -----------------------------
$cmd = "../src/q.exe indexStem=../test/bhaskar numDocs=1842879 k=1000 <../data/DINNER/qt.txt > ../test/bhaskar.out";
die "Query processing command $cmd failed \n"
    if system($cmd);

# ------------------------------ Step 5 Evaluating the results using INST  -----------------------------
$cmd = "../../../inst_eval/inst_eval.py -T 3 -c bhaskar.qrels ../test/bhaskar.out bhaskar.T_per_query";

die "Evaluation command $cmd failed \n"
    if system($cmd);


# ------------------------------ Step 6 Evaluating the results using NDCG etc  -----------------------------
$cmd = "../test/judge_run.pl DINNER/qdr.txt ../test/bhaskar.out 10";

die "Evaluation command $cmd failed \n"
    if system($cmd);
