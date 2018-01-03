CC=/usr/bin/gcc
CFLAGS=-O3 -std=c11 -DPCRE2_CODE_UNIT_WIDTH=8 -DHAVE_CONFIG_H -DSUPPORT_UNICODE
LDFLAGS=-g

pcre2objects := pcre2_auto_possess.o pcre2_chartables.o pcre2_compile.o pcre2_config.o pcre2_context.o pcre2_dfa_match.o pcre2_error.o pcre2_jit_compile.o pcre2_maketables.o pcre2_match.o pcre2_match_data.o pcre2_newline.o pcre2_ord2utf.o pcre2_pattern_info.o pcre2_serialize.o pcre2_string_utils.o pcre2_study.o pcre2_substitute.o pcre2_substring.o pcre2_tables.o pcre2_ucd.o pcre2_valid_utf.o pcre2_xclass.o

ifdef fPIC
	CFLAGS+=-fPIC
endif

all: libpcre2.a libpcre2.dll

libpcre2.a: $(pcre2objects)
	ar -cvq $@  $(pcre2objects)

libpcre2.dll: $(pcre2objects)
	$(CC) -shared -o $@ $(pcre2objects)

clean:
	/bin/rm -f *.o *.dll *.a
