#! /usr/bin/perl
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

chdir "../data";
#die "convert_Bhaskar_file failed\n"
#    if system("./convert_file_from_Bhaskar.pl DINNER/tds.txt bhaskar.tsv ../test/bhaskar.termids");

$rslt = `./convert_bhaskar_judgments_to_qrels.pl`;
die "convert_Bhaskar qrels failed\n"
    if $?;
$rslt =~ /Queries converted: ([0-9]+)./;
$num_queries = $1;
print "Number of queries converted: $num_queries\n";

die "convert_Bhaskar queries failed\n"
    if system("./convert_queries_from_Bhaskar.pl DINNER/qt.txt  ../test/bhaskar.q  ../test/bhaskar.termids");


#Write the T_per_query file
die "Can't write to bhaskar.T_per_query\n"
    unless open T, ">bhaskar.T_per_query";
for ($q = 1; $q <= $num_queries; $q++) {
    print T "$q\t3\n";
}
close(T);

$cmd = "../src/i.exe inputFileName=bhaskar.tsv outputStem=../test/bhaskar numDocs=1842879 numTerms=931";
die "Indexing command $cmd failed \n"
    if system($cmd);

$cmd = "../src/q.exe indexStem=../test/bhaskar numDocs=1842879 numTerms=931 k=1000 <../test/bhaskar.q > ../test/bhaskar.out";
die "Query processing command $cmd failed \n"
    if system($cmd);

$cmd = "../../../inst_eval/inst_eval.py -T 3 -c bhaskar.qrels ../test/bhaskar.out bhaskar.T_per_query";

die "Evaluation command $cmd failed \n"
    if system($cmd);
