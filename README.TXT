QBASHER (Query Building, Auto-Suggest and Entity Recognition) is a
specialized information retrieval system optimized for indexing
billions of short texts such as academic paper titles and web search
query logs.  It can do ranked retrieval but was built to support two
specific tasks:

1. Query suggestion allowing for word order permutation.
   E.g. Recognizing that the candidate 'race to the moon' may be
   a reasonable suggestion when the user types 'moon r...'
2. A particular type of query classification.  E.g. recognizing
   that the query 'newspaper taxis appear on the shore' might
   have lyrics intent despite the absence of intent words such
   as 'lyrics'

QBASHER indexes are normally sorted (internally) by static score, and
include word positions (up to 254) within a document. The assumption
of short texts allows use of the document text itself instead of a
per-document-index when handling word prefixes, dealing with
rank-only query terms, and when scoring lexical degree of match
between a query and a candidate document.  The use of UTF-8 is
assumed.  The QBASHER query language supports words, word prefixes,
phrases, disjunctions, disjunctions within phrases, phrases within
disjunctions, and rank-only operators.

Please see the PDF files in the doc sub-directory for more details of
these and other functions, and for instructions on how to run the test
scripts designed to verify correct operation.

QBASHER is written in C11, and test scripts in perl5. A Makefile is provided
for building the C tools using GCC.  Solution/project files are also
provided for building with Visual Studio 2015 (64-bit architecture,
Release version).

The two main tools are the indexer QBASHI.exe and the query processor
QBASHQ.exe.  However, most of the functionality of QBASHQ.exe is embodied in
a dynamically linked library (DLL) -- qbashq-lib.dll which includes an
API capable of being p-invoked from a C# main program.  By this means,
QBASHER query processing can be run as a Microsoft web service.  An
example C# main program is included to illustrate the use of this API.

NOTE: the included distribution of pcre2 has been cut down to remove
components which are unnecessary in the QBASHER context.


Microsoft
17 November 2017
