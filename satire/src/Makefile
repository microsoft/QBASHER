CC=/usr/bin/gcc

# -MD automatically makes a .d dependency file for each .c
# -MP allows that stuff to be used in the Makefile

# For an explanation of automatic variables (e.g.$@, $? and $^) see
# https://www.gnu.org/software/make/manual/make.html#Automatic-Variables
#       $@ The file name of the target of a rule
#       $? The names of all the prerequisites which are newer than the target
#       $^ The names of all the prerequisites with spaces between them
#       .......... there are others.

CFLAGS=-O3 -std=c11 -m64 -Wall -MP -MD -DNO_THREADS # No need for -fPIC... "All code is position-independent"
LDLIBS=-lm

# On Linux there apparently is a need for fPIC
ifdef fPIC
        export fPIC=1
endif

all:	i.exe q.exe

i.exe:	i/i.o i/iargTable.o u/utility_nodeps.o u/arg_parser.o u/unicode.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

q.exe:	q/q.o q/qargTable.o u/utility_nodeps.o u/arg_parser.o u/unicode.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f */*.o
	rm -f *.exe


git:
	git add `find . -iname \*.c`
	git add `find . -iname \*.h`
	git add `find . -iname \*.sln`
	git add `find . -iname \*.vcxproj`
	git add `find .. -iname \*.pl`
	git add */*.txt
	git add `find .. -iname \*README\*`
	git add `find .. -name Makefile\*`

