#! /usr/bin/perl

chdir "../data";
#die "convert_Bhaskar_file failed\n"
#    if system("./convert_file_from_Bhaskar.pl DINNER/tds.txt bhaskar.tsv ../test/bhaskar.termids");
die "convert_Bhaskar queries failed\n"
    if system("./convert_queries_from_Bhaskar.pl DINNER/qt.txt  ../test/bhaskar.q  ../test/bhaskar.termids");

$cmd = "../src/i.exe inputFileName=bhaskar.tsv outputStem=../test/bhaskar numDocs=1842879 numTerms=931";
die "Indexing command $cmd failed \n"
    if system($cmd);

$cmd = "../src/q.exe indexStem=../test/bhaskar numDocs=1842879 numTerms=931 <../test/bhaskar.q";
die "Query processing command $cmd failed \n"
    if system($cmd);

