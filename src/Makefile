CC=/usr/bin/gcc
# The symbol NO_THREADS avoids the compiling of multi-threaded code in QBASHQ.exe.
# At the moment, it should ALWAYS be defined because the current implementation using
# the pthreads library is appallingly slow.  Ten threads runs about ten times
# slower than the the unthreaded version :-(   Surely there must be a bug in
# my logic (the threading model is different to the windows one.)
CFLAGS=-O3 -std=c11 -m64 -Wall -DNO_THREADS # No need for -fPIC... "All code is position-independent"
LDFLAGS=-g -lm

# For an explanation of automatic variables (e.g.$@, $? and $^) see
# https://www.gnu.org/software/make/manual/make.html#Automatic-Variables
#	$@ The file name of the target of a rule
#       $? The names of all the prerequisites which are newer than the target
#       $^ The names of all the prerequisites with spaces between them
#       .......... there are others.
#
# Haven't worked out fully how to make gcc DLLs work.  Not needed anyway, so quickly gave up.

all: QBASHI.exe libpcre2 libQBASHQ-LIB.a QBASH_vocab_lister.exe TFdistribution_from_TSV.exe QBASHQ.exe generate_fuzz_queries.exe


QBASHI.exe: QBASHI/arg_parser.o QBASHI/input_buffer_management.o  QBASHI/QBASHI.o QBASHI/Write_Inverted_File.o utils/dahash.o utils/linked_list.o shared/utility_nodeps.o shared/unicode.o imported/Fowler-Noll-Vo-hash/fnv.o utils/dynamic_arrays.o utils/latlong.o shared/QBASHER_common_definitions.h
	$(CC) $(LDFLAGS) -o $@ $^

QBASHQ_OBJECTS=qbashq-lib/QBASHQ_lib.o qbashq-lib/arg_parser.o qbashq-lib/classification.o qbashq-lib/error_explanations.o qbashq-lib/saat.o qbashq-lib/relaxation.o  qbashq-lib/query_shortening.o shared/utility_nodeps.o shared/unicode.o shared/substitutions.o utils/latlong.o utils/street_addresses.o

libQBASHQ-LIB.a:  $(QBASHQ_OBJECTS) shared/QBASHER_common_definitions.h
	ar -cvr $@  $(QBASHQ_OBJECTS)

libpcre2:
	#$(MAKE) -C pcre2 clean
	$(MAKE) -C imported/pcre2
	/bin/cp imported/pcre2/libpcre2.a .
	/bin/cp imported/pcre2/libpcre2.dll .



QBASH_vocab_lister.exe: vocab_lister/QBASH_vocab_lister.o shared/utility_nodeps.o shared/unicode.o shared/QBASHER_common_definitions.h
	$(CC) $(LDFLAGS) -o $@ $^


TFdistribution_from_TSV.exe : TFdistribution_from_TSV/TFdistribution_from_TSV.o utils/dahash.o shared/utility_nodeps.o shared/unicode.o imported/Fowler-Noll-Vo-hash/fnv.o shared/QBASHER_common_definitions.h
	$(CC) $(LDFLAGS) -o $@ $^

QBASHQ.exe: qbashq/QBASHQ.o libQBASHQ-LIB.a libpcre2
	$(CC) $(LDFLAGS) -o $@ qbashq/QBASHQ.o -L./ -lQBASHQ-LIB -Limported/ -lpcre2

generate_fuzz_queries.exe: generate_fuzz_queries/generate_fuzz_queries.o
	$(CC) $(LDFLAGS) -o $@ $^

dahash_demo.exe:	utils/dahash_demo.o utils/dahash.o imported/Fowler-Noll-Vo-hash/fnv.o shared/unicode.o shared/utility_nodeps.o
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	/bin/rm -f *.a *.exe *.dll *.so


cleaner: clean
	/bin/rm -f [a-oq-zA-OQ-Z]*/*.o  # Everything except PCRE

cleanest: cleaner
	/bin/rm -rf visual_studio/x64 visual_studio/Release visual_studio/Debug visual_studio/ipch visual_studio/visual_studio.VC*
	 /bin/rm -f imported/pcre2/*.o imported/pcre2/*.dll imported/pcre2/*.a  # all the PCRE stuff
	 /bin/rm -rf imported/pcre2/x64  imported/pcre2/Debug imported/pcre2/Release imported/pcre2/ipch imported/pcre2/JIT  # all the VS PCRE stuff

git:
	git add Makefile ../scripts/*.pl */*.c */*.cs */*.txt */*.h */*.sln */*.vcxproj ../*.TXT ../test_data/wikipedia_titles*/QBASH.forward.bz2 ../test_data/[a-vx-zA-VX-Z]*/QBASH.forward ../test_data/*/QBASH.substitution_rules* ../test_queries/*.q ../*/README.txt ../doc/*.tex #*/*.csproj */App.config */Properties/AssemblyInfo.cs
	#
	# Please run "grep 'if (1' */*.c" and check output for unwanted output
	# ...  now run git commit -m \"<commit message>\" and git push"


