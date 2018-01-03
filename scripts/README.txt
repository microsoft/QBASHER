Notes on QBASHER test-suite (scripts directory)
-----------------------------------------------

03 Jan 2018

After making any changes to the QBASHER code, please make sure to run
the test suite to ensure that you haven't: caused collateral damage to
functionality, introduced memory leaks, introduced memory addressing
errors (segfaults), or caused significant deterioration in query
latency or throughput.



A. RUNNING THE TEST SUITE FOR THE FIRST TIME (OR AFTER CHANGING THE INDEXER)
----------------------------------------------------------------------------
   perl ./qbash_run_tests.pl RI       # if you have built QBASHER using 
                                      # Visual Studio 2015, or
   perl ./qbash_run_tests.pl GCC RI   # if you have built QBASHER
                                      # using gcc/make

The RI option causes the data sets in ../test_data to be rebuilt, and
then runs the basic set of tests. The 'perl' should be the path to your
perl5 interpreter, or just perl if it's in your path. If the actual
data is bzipped (as is the case when first cloned from the github
repository) then the RI option causes the QBASH.forward.bz2
files to be uncompressed as QBASH.forward.   It is  assumed that
there is a \texttt{bunzip2} executable in your path.


B. SUBSEQUENT RUNS OF THE TEST SUITE
------------------------------------

If the indexer hasn't changed, you can leave off the RI option in the
commands shown in Section A above.  You can also choose which test
scripts are run and how they are run using the following options:


#Usage: qbash_run_tests.pl [GCC] [RI | LITE | BASIC | FULL] [ithreads=<int>] [qthreads=<int>]
#
#    'GCC', meaning test with the GCC-built executables (no multi-threading, no C#),
#    'LITE', meaning run a reduced set of tests (a quick check)
#    'BASIC', meaning run the LITE tests plus some longer ones,
#    'FULL', meaning run all tests including ones with very large query sets, or
#    'RI', meaning re-index the collections and run the BASIC set of tests.
#          (If the .forward files are bzipped, they will be unbzipped first.)
#    'ithreads=<int>', meaning run indexing in <int> parallel threads.
#    'qthreads=<int>', meaning run tests in <int> parallel threads.
#
#
#With no argument, all tests except the very long running ones will be run,
#and collections will not be re-indexed.

Note that the ithreads and qthreads options are implemented using
perl's fork() function, and are compatible with the gcc-compiled
version of QBASHER.   If you have a multi-core machine with a large
RAM configuration, you can run the build tests quite a bit more
quickly with these options.


C. RUNNING INDIVIDUAL TESTS
---------------------------

All the scripts in the test suite are named
'qbash_<testname>_check.pl'.  You can run any of them individually
using:

	perl ./qbash_<testname>_check.pl <QBASHQ binary> [-fail_fast]

To use the normal VS-built QBASHQ binary you can either use 'default' or
'visual_studio/x64/Release/QBASHQ.exe'.  To use the gcc-built one you
need '../src/QBASHQ.exe'.

If you specify -fail_fast, then the script will exit as soon as a test
fails and give you the command string which failed and the results
from running it.


D. NOTES ON USING GCC.
----------------------
Unfortunately, the GCC-built version does not yet support
multi-threaded operation.  Some conditionally compiled pthreads
code is present but hasn't been debugged. (12 Dec 2017).  This means
that processing large query batches with the GCC version takes very
much longer. Tests which compare output from single and multi-threaded
running are not run, and nor is the test using a C# front-end.



E. MONITORING LATENCY AND THROUGHPUT
------------------------------------
Latency and QPS figures vary considerably from one hardware
configuration to another, and to a lesser extent from one run to
another on the same hardware. If you are making frequent changes to
the code, and running the test suite each time (as you should) then
you will get a feel for what sort of numbers to expect from the timing
scripts. 

For information, below is some output obtained using the Visual Studio
2015 compiled version of QBASHER running on a high-end quad-core
laptop, with 32GB RAM and running Windows 10.  To give you an idea of
the expected time to run the standard tests, below that is the
final output from running qbash_run_tests.pl with no options, on that
laptop. 


#   -------------- ./qbash_timing_check.pl ----------------
#Query processing timings against ../test_data/wikipedia_titles:
#
#Options              Ave_resp_msec   95%_resp   99%_resp        QPS
#===================================================================
#  --- Query Set: ../test_queries/emulated_log_10k.q ---
#                                        0       0       0       94203.4
#-alpha=1 -beta=1 -gamma=1 -delta=1 -epsilon=10  0       0       97082.7
#-alpha=1 -beta=1 -gamma=1 -delta=1 -epsilon=1 -max_candidates=1000      0       0       103779.5
#Warning: Memory (PFU) grew by 3.9MB.
#                       -auto_partials=on0       1       5       10940.9
#     -auto_partials=on -timeout_kops=1000       1       4       37521.7
#  --- Query Set: ../test_queries/emulated_log_four_full_words_10k.q ---
#                                        0       0       0       135909.3
#-alpha=1 -beta=1 -gamma=1 -delta=1 -epsilon=10  0       0       136839.8
#-alpha=1 -beta=1 -gamma=1 -delta=1 -epsilon=1 -max_candidates=1000      0       0       129114.0
#Warning: Memory (PFU) grew by 3.6MB.
#                       -auto_partials=on0       0       1       75101.0
#     -auto_partials=on -timeout_kops=1000       0       1       83904.3
#  --- Query Set: ../test_queries/emulated_log_four_words_with_operators.q ---
#                                        0       1       2       34460.0
#-alpha=1 -beta=1 -gamma=1 -delta=1 -epsilon=10  1       2       34583.0
#-alpha=1 -beta=1 -gamma=1 -delta=1 -epsilon=1 -max_candidates=1000      1       3       31091.5
#Warning: Memory (PFU) grew by 3.4MB.
#                       -auto_partials=on1       2       22      6339.3
#     -auto_partials=on -timeout_kops=1000       1       6       18601.0
#
#....
#
# --------  It's our lucky day.  All tests passed. ---------
#
#Easter-Egg: QBASHER 1.5.121-OS - 11054480 documents in wikipedia_titles corpus
#Elapsed time for all the tests: 540.9 sec.

As you can see, the default test-suite run without parallelism took
540.9 seconds (about 9 minutes) to run on that laptop.  For
information, when run with FULL under the same conditions the run time
was 1701.0 sec (about 29 minutes). 

F. A NOTE ON LINUX USAGE
------------------------
If you are using Linux, the test case may fail if the perl files are not
set as executable. That is, run `chmod 755 *.pl` before executing any test
scripts.

