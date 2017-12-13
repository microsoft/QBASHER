Notes on building QBASHER executables and libraries
---------------------------------------------------

12 Dec 2017

QBASHER can be built using either Visual Studio 2015 (or later) or
using gcc.  Unfortunately, multi-threaded query processing is not yet
supported in the gcc version.

QBASHER makes use of two third-party libraries:  PCRE2 (Perl
compatible regular expressions) and FNV (Fowler-Noll-Vo) hashing.
These are included with QBASHER (under the terms of the relevant
licenses) to make this project more stand-alone.

After building QBASHER, please make sure to run the QBASHER test-suite
in ../scripts to make sure you haven't broken anything.   Instructions
are in README.txt in that directory.

Full details of building and using QBASHER executables and libraries
are in ../doc/introduction_to_QBASHER.pdf.

