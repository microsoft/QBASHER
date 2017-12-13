Notes on the Use and Origin of the query sets in this directory.
----------------------------------------------------------------

12 Dec 2017

Use:  The query sets in this directory are relied upon by the
build-test scripts in ../scripts.  By running large query sets
with different QBASHER options we are able to achieve reasonable
confidence that changes to the code base have not seriously slowed
down latency and throughput, and have not introduced segfaults or
memory leaks.

Fuzz Origin:  The Fuzz query set was generated using the
generate_fuzz_queries program in ../src/generate_fuzz_queries.  It
consists of randomly generated ASCII bytes, including control
characters and punctuation.  The aim is to reduce the chance that
QBASHER will crash when highly garbled input is received.

Emulated Log Origin:  The emulated_log.q file was emulated using the
queryLogEmulator program distributed as part of the SynthaCorpus
project using a de-identified real query log.  The -obfuscate option
was used, and the target corpus was ../test_data/wikipedia_titles.
The other query sets were derived from emulated_log.q using
../scripts/make_derivative_query_logs.pl.  Note that
../scripts/qbash_classifier_timing_check.pl concatenates 100 copies of
emulated_log.q in order to create a large batch.


