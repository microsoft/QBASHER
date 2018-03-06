#! /usr/bin/perl
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

# @ARGV[0]: tds.txt
# @ARGV[1]: qt.txt
# @ARGV[2]: numDocs
# @ARGV[3]: lowScoreCutoff
# @ARGV[4]: num_results
# @ARGV[5]: out.txt

$cmd = "vs_i.exe inputFileName=@ARGV[0] outputStem=./satire numDocs=@ARGV[2] -lowScoreCutoff=@ARGV[3]";
die "Indexing command $cmd failed \n"
    if system($cmd);

$cmd = "vs_q.exe indexStem=./satire numDocs=@ARGV[2] k=@ARGV[4] < @ARGV[1] > @ARGV[5]";
die "Query processing command $cmd failed \n"
    if system($cmd);
