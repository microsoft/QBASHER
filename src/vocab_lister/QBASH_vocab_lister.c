// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// QBASH_vocab_lister started life as a simple tool to convert the binary
// QBASH.vocab file into a printable vocab.tsv file sorted either alphabetically
// or by descending frequency.
//
// First functionality extension was to allow it to handle QBASH.bigrams, QBASH.cooccurs
// QBASH.repetitions (which are in similar format to QBASH.vocab), creating
// bigrams.tsv, cooccurs.tsv or repetitions.tsv as appropriate.
//
// Second, it was realised that the .vocab etc. files contain enough information to
// allow modeling of the term frequency distribution (TFD).   That is now done, resulting
// in three output files:
// .plot   - sampled log(freq) v. log(rank) data for plotting with gnuplot
// .segdat - another gnuplot datafile showing the piecewise segments fitting the TFD
// .tfd    - QBASHER options which could be used to emulate the TFD of this corpus.
//
// Third, the QBASH.vocab file also contains enough info to plot the distribution of
// lengths of words, both distinct words and word occurrences.  This is now done, resulting
// in another output file:
// .wdlens - length distinct_prob occurrence_prob, suitable for gnuplotting



#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#ifdef WIN64
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#else
#include <errno.h>
#endif

#include "../shared/unicode.h"
#include "../shared/QBASHER_common_definitions.h"
#include "../shared/utility_nodeps.h"

#define OBUF_SIZE (50 * 1048576)   // X * 1MB

//---------------------------------------------------------------------
// We now provide a means of changing the following items
int HEAD_TERMS = 10, PIECEWISE_SEGMENTS = 10;
#define EPSILON 0.02  // The minimum distance in log(rank) between successive points for plotting.
//---------------------------------------------------------------------



typedef enum {  // Make sure that the filetype[] array below has an element for each of these.
	VOCAB,
	BIGRAMS,
	NGRAMS,
	COOCCURS,
	TERM_REPS
} filetype_t;

char *filetype[] = {
  "vocab",
  "bigrams",
  "ngrams",
  "cooccurs",
  "term_reps",};



#if 0   // no longer used
static int cmp_freq(const void *ip, const void *jp) {
  byte *ib = (byte *)ip, *jb = (byte *)jp;
  int *freqip = (int *)(ib + MAX_WD_LEN + 1), *freqjp = (int *)(jb + MAX_WD_LEN + 1);
  if (*freqip < *freqjp) return 1;
  if (*freqip > *freqjp) return -1;
  return (int)(ib - jb);  // Just a tie breaker
}
qsort(fileinmem, distinct_wds, entry_len, cmp_freq);   // qsort turned out to be O(n^2)
#endif


static double get_freq(size_t r, byte *fileinmem, int *permute, int distinct_wds, size_t entry_len, filetype_t whichtype) {
  // Return the occurrence frequency of the term at rank r in the vocab (numbering from 1), assumed sorted via permute.

  byte *vp, qidf;
  u_ll payload, freq, *llfreqp;
  int p;
  if (r > distinct_wds || fileinmem == NULL || permute == NULL) {
    printf("Calamity!\n");
    exit(1);
  }
  p = permute[r - 1];    // -1 to convert numbering from 1
  vp = fileinmem + p * entry_len; 
  if (whichtype == VOCAB) {
    vocabfile_entry_unpacker(vp, MAX_WD_LEN + 1, &freq, &qidf, &payload);
  }
  else if (whichtype == TERM_REPS) {
    llfreqp = (u_ll *)(vp + MAX_REP_LEN + 1);
    freq = *llfreqp;
  }
  else if (whichtype == NGRAMS) {
    llfreqp = (u_ll *)(vp + MAX_NGRAM_LEN + 1);
    freq = *llfreqp;
  }
  else {
    llfreqp = (u_ll *)(vp + MAX_BIGRAM_LEN + 1);
    freq = *llfreqp;
  }

  return (double)freq;
}


static double get_freq_for_range(size_t f, size_t l, byte *fileinmem, int *permute, int distinct_wds, size_t entry_len, filetype_t whichtype) {
  // Return the total of the occurrence frequencies of terms at ranks f .. l in the vocab (numbering from 1), assumed sorted via permute.

  byte *vp, qidf;
  u_ll payload, freq, *llfreqp, totfreq = 0;
  size_t p, r;

  if (l > distinct_wds || l < f || fileinmem == NULL || permute == NULL) {
    printf("Double calamity!\n");
    exit(1);
  }
  for (r = f; r <= l; r++) {
    p = permute[r - 1];    // -1 to convert numbering from 1
    vp = fileinmem + p * entry_len; 
    if (whichtype == VOCAB) {
      vocabfile_entry_unpacker(vp, MAX_WD_LEN + 1, &freq, &qidf, &payload);
    }
    else if (whichtype == TERM_REPS) {
      llfreqp = (u_ll *)(vp + MAX_REP_LEN + 1);
      freq = *llfreqp;
    }
    else if (whichtype == NGRAMS) {
      llfreqp = (u_ll *)(vp + MAX_NGRAM_LEN + 1);
      freq = *llfreqp;
    }
    else {
      llfreqp = (u_ll *)(vp + MAX_BIGRAM_LEN + 1);
      freq = *llfreqp;
    }
    totfreq += freq;
  }

  return (double)totfreq;
}


static void write_tfd_file(FILE *TFD, FILE *SDF, byte *fileinmem, int *permute,
			   int distinct_wds, size_t singletons, size_t entry_len,
			   u_ll totfreq, filetype_t whichtype) {
  // Using the information passed in, generated the derived values and write to the tfdfile
  int h, s, f, l, middle_highest;  // f and l are the first and last ranks of a linear segment
  double dtotfreq = (double)totfreq, dl, probf, probl, logprobf, logprobl, alpha, 
    domain, domain_step, probrange, cumprob, cumprob_head = 0.0; 

  fprintf(TFD, "#Type of file from which this was derived: %s\n", filetype[whichtype]);
  fprintf(TFD, "#Option names correspond to generate_a_corpus_plus.exe\n"); 
  fprintf(TFD, "#Note:  zipf_alpha shown below is for the line connecting the extreme points of the middle segment - not for best fit.\n");
  fprintf(TFD, "#Head_terms: %d\n#Piecewise_segments: %d\n", HEAD_TERMS, PIECEWISE_SEGMENTS);
  fprintf(TFD, "-synth_postings=%llu  # Total of all the frequencies in %s.tsv\n", totfreq, filetype[whichtype]);
  fprintf(TFD, "-synth_vocab_size=%d  # Number of lines in %s.tsv\n",  distinct_wds, filetype[whichtype]);
  fprintf(TFD, "-zipf_tail_perc=%.6f  # Number of lines with freq. 1 in %s.tsv\n", 
	  (double)singletons * 100.0 / (double)distinct_wds, filetype[whichtype]);

  // Only try to do this if there are more than HEAD_TERMS terms
  if (distinct_wds > HEAD_TERMS) {
    fprintf(TFD, "-head_term_percentages=");

    for (h = 1; h <= HEAD_TERMS; h++) {
      probf = get_freq((size_t)h, fileinmem, permute, distinct_wds, entry_len, whichtype) / dtotfreq;
      fprintf(TFD, "%.6f", probf * 100.0);
      cumprob_head += probf;
      if (h == HEAD_TERMS) fputc('\n', TFD);
      else fputc(',', TFD);
    }

    fprintf(TFD, "#Combined_head_term_probability: %.10f\n", cumprob_head);

    // Calculate an overall Zipf Alpha for the middle part.
    f = (int)(HEAD_TERMS + 1);
    l = (int)(distinct_wds - singletons);
    middle_highest = l; 

    // Only try to do this if l is quite a bit larger than f
    if (l - f > 10) {
      probf = get_freq(f, fileinmem, permute, distinct_wds, entry_len, whichtype) / dtotfreq;
      logprobf = log(probf);
      probl = get_freq(l, fileinmem, permute, distinct_wds, entry_len, whichtype) / dtotfreq;
      logprobl = log(probl);
      domain = log((double)l) - log((double)f);
      alpha = (logprobl - logprobf) / domain;
      fprintf(TFD, "-zipf_alpha=%.4f\n", alpha);

      // Only try to do this if there are a lot of terms in the middle
      if (l - f > 1000) {
	// Now the middle segments.  Only try to do this if 
	// Each piecewise segment has five comma-separated values.   Segments are separated by a '%'
	// The five values are:
	//      alpha - the slope of the segment (in log-log space)
	//          f - the first rank in the segment
	//          l - the last rank in the segment
	//  probrange - the sum of all the term probabilities for ranks f..l (inclusive)
	//    cumprob - the sum of all the term probabilities for ranks 1..l (inclusive)
	fprintf(TFD, "-zipf_middle_pieces=");

	cumprob = cumprob_head;
	// Divide the domain f - l into equal segments in log space.
	domain_step = domain / (double)PIECEWISE_SEGMENTS;
	dl = log((double)f);  // just to get started.  dl is maintained in fractional form.
	for (s = 0; s < PIECEWISE_SEGMENTS; s++) {
	  dl = dl + domain_step;  // dl positions the end of this segment in log space
	  l = (int)trunc(exp(dl) + 0.5);  // use exp to convert dl to a rank in linear space
	  if (l > middle_highest) l = middle_highest;
	  probf = get_freq(f, fileinmem, permute, distinct_wds, entry_len, whichtype) / dtotfreq;
	  logprobf = log(probf);
	  probl = get_freq(l, fileinmem, permute, distinct_wds, entry_len, whichtype) / dtotfreq;
	  logprobl = log(probl);
	  domain = log((double)l) - log((double)f);
	  alpha = (logprobl - logprobf) / domain;  

	  probrange = get_freq_for_range(f, l, fileinmem, permute, distinct_wds, entry_len, whichtype) / dtotfreq;
	  cumprob += probrange;
					
	  fprintf(TFD, "%.4f,%d,%d,%.10f,%.10f%%", alpha, f, l, probrange, cumprob);

	  fprintf(SDF, "%.10f %.10f\n%.10f %.10f\n\n", log((double)f), logprobf, log((double)l), logprobl);
	  f = l + 1;  // Don't want f to be the same as the l for the last segment.
	}
	fputc('\n', TFD);
      }
    }
  }
}

void print_usage(char *progname){
    printf("Usage: %s <.vocab, .bigrams, .ngrams, .cooccurs or .repetitions file> [sort=alpha] [head_terms=<int>] [piecewise_segments=<int>]\n"
	   "       Output goes to vocab.tsv, bigrams.tsv, ngrams.tsv, cooccurs.tsv, or repetitions.tsv in same directory as first arg.\n"
	   "       Unless sort=alpha, extra files are written:\n"
	   "          *.tfd - a summary of term freq distribution, in the form of generate_a_corpus options.\n"
	   "	      *.plot - a subset of the logfreq v. logrank data points for plotting.\n"
	   "          *.segdat - data for plotting the piecewise segments in GNUPLOT format.\n",
	   progname);
    exit(1);
 
}


int main(int argc, char **argv) {
  u_ll sum = 0, count = 0, freq, payload, *llfreqp, *score_histo = NULL;
  double wdlength_count[MAX_WD_LEN + 1] = {0},
         freq_weighted_wdlength_count[MAX_WD_LEN + 1] = {0},
         wdlen_mean_freq[MAX_WD_LEN + 1] = {0},
         wdlen_stdev_freq[MAX_WD_LEN + 1] = {0};
  long long h, max_freq = 0;
  size_t l, entry_len = VOCABFILE_REC_LEN;
  int a, distinct_wds, w, pw, *permute = NULL, error_code, wdlen;
  byte *fileinmem = NULL, *vp = NULL, qidf;
  char *obuf, *p, *outfilename, *suffix;
  double start_time, very_start;
  CROSS_PLATFORM_FILE_HANDLE FH;
  FILE *tsvfile, *tfdfile = NULL, *plotfile = NULL, *segdatfile = NULL,
       *wdlensfile = NULL, *wdfreqsfile = NULL;
  HANDLE FMH;
  filetype_t whichtype = VOCAB;
  BOOL sort_by_freq = TRUE, debug = FALSE;
int i;

  // Variables needed to accumulate the data for writing the .tfd file.
  u_ll totfreq = 0;
  size_t vsz, singletons = 0;

  double lastlogrank = -1.0, logrank, logfreq;  

  if (sizeof(size_t) != 8) error_exit("Error:  program must be compiled for 64 bit!\n");
  setvbuf(stdout, NULL, _IONBF, 0);

  if (argc < 2) print_usage(argv[0]);

  l = strlen(argv[1]);
  outfilename = (char *)malloc(l + 50);
  if (outfilename == NULL) {
    printf("Malloc of outfilename failed\n");
    exit(1);
  }

  initialize_unicode_conversion_arrays(FALSE);

  test_utf8_functions();

  // Make a copy of the input filepath, strip off the filename part and append either blahblah.tsv
  strcpy(outfilename, argv[1]);
  p = outfilename + l - 1;
  while (p > outfilename && *p != '\\' && *p != '/') p--;  // p ends up at last slash of whatever persuasion or at start of buffer
  if (p != outfilename) p++;  // Advance past the slash


  if (strstr(argv[1], ".bigrams") != NULL) {
    // The format of .bigrams records is different.  There's a longer string
    whichtype = BIGRAMS;
    entry_len = MAX_BIGRAM_LEN + 1 + sizeof(long long);
    strcpy(p, "bigrams.tsv");
    suffix = p + 8;
  }
  else if (strstr(argv[1], ".ngrams") != NULL) {
    // The format of .ngrams records is different.  There's an even longer string
    whichtype = NGRAMS;
    entry_len = MAX_NGRAM_LEN + 1 + sizeof(long long);
    strcpy(p, "ngrams.tsv");
    suffix = p + 7;
  }
  else if (strstr(argv[1], ".cooccurs") != NULL) {
    // The format of .coocurs is the same as for .bigrams
    whichtype = COOCCURS;
    entry_len = MAX_BIGRAM_LEN + 1 + sizeof(long long);
    strcpy(p, "cooccurs.tsv");
    suffix = p + 9;
  }
  else if (strstr(argv[1], ".repetitions") != NULL) {
    // The format of .coocurs is the same as for .bigrams
    whichtype = TERM_REPS;
    entry_len = MAX_REP_LEN + 1 + sizeof(long long);
    strcpy(p, "repetitions.tsv");
    suffix = p + 12;
  }
  else {
    strcpy(p, "vocab.tsv");
    suffix = p + 6;
  }

  for (a = 2; a < argc; a++){
    p = argv[a];
    while (*p == '-') p++;  // Skip over leading hyphens
    if (!strcmp(p, "sort=alpha")) sort_by_freq = FALSE;
    else if (!strncmp(p, "head_terms=", 11)) {
      HEAD_TERMS = strtol(p + 11, NULL, 10);
      printf("HEAD_TERMS = %d\n", HEAD_TERMS);
    } else if (!strncmp(p, "piecewise_segments=", 19)) {
      PIECEWISE_SEGMENTS = strtol(p + 19, NULL, 10);
      printf("PIECEWISE_SEGMENTS = %d\n", PIECEWISE_SEGMENTS);
    } else {
      printf("Unrecognized argument '%s'.\n", argv[a]);
      print_usage(argv[0]);
    }
  }

  // Open the TSV file
#ifdef WIN64
  _set_errno(0);
#else
  errno = 0;
#endif
  tsvfile = fopen(outfilename, "wb");  // Forward slashes are OK
  if (tsvfile == NULL) {
    printf("Curses! Can't open %s. code is %d\n", outfilename, errno);
    exit(1);
  }


  printf("TSV output will go to %s\n", outfilename);
  if (sort_by_freq) {
    // We are going to write  .tfd, .plot, .segdat, .wdlens and .wdfreqs files.
    strcpy(suffix, "tfd");   // Overwrite the suffix of the outfilename
#ifdef WIN64
    _set_errno(0);
#else
    errno = 0;
#endif
    tfdfile = fopen(outfilename, "wb");  // Forward slashes are OK
    if (tfdfile == NULL) {
      printf("Double curses! code is %d\n", errno);
      exit(1);
    }
    printf("Characteristics of the term frequency distribution will be to saved in %s\n", outfilename);

    strcpy(suffix, "plot");   // Overwrite the suffix of the outfilename
#ifdef WIN64
    _set_errno(0);
#else
    errno = 0;
#endif
    plotfile = fopen(outfilename, "wb");  // Forward slashes are OK
    if (plotfile == NULL) {
      printf("Triple curses! code is %d\n", errno);
      exit(1);
    }
    printf("Logfreq v. logrank data will be saved in %s for plotting.\n", outfilename);

    strcpy(suffix, "segdat");   // Overwrite the suffix of the outfilename
#ifdef WIN64
    _set_errno(0);
#else
    errno = 0;
#endif
    segdatfile = fopen(outfilename, "wb");  // Forward slashes are OK
    if (segdatfile == NULL) {
      printf("Quadruple curses! code is %d\n", errno);
      exit(1);
    }
    printf("Data to allow plotting of the piecewise segments will be saved in %s\n", outfilename);

    if (whichtype == VOCAB) {
      strcpy(suffix, "wdlens");   // Overwrite the suffix of the outfilename
#ifdef WIN64
      _set_errno(0);
#else
      errno = 0;
#endif
      wdlensfile = fopen(outfilename, "wb");  // Forward slashes are OK
      if (wdlensfile == NULL) {
	printf("Quintuple curses! code is %d\n", errno);
	exit(1);
      }
      printf("Data to allow plotting of word length distributions will be saved in %s\n", outfilename);
      
      strcpy(suffix, "wdfreqs");   // Overwrite the suffix of the outfilename
#ifdef WIN64
      _set_errno(0);
#else
      errno = 0;
#endif
      wdfreqsfile = fopen(outfilename, "wb");  // Forward slashes are OK
      if (wdfreqsfile == NULL) {
	printf("Sextuple curses! code is %d\n", errno);
	exit(1);
      }
      printf("Data to allow plotting of relationship between word length\n"
	     "and word frequency will be saved in %s\n", outfilename);
    }

 
    // Get the name of the index and include it in comments at the head of
    // .plot, .segdat etc files
    *(suffix - 6) = 0;   // Zap out the "vocab.plot" bit.
    fprintf(plotfile, "#Log(freq) v. Log(rank) data for index %s.\n#Log(rank)  Log(freq).\n",
	    outfilename);
    fprintf(segdatfile, "#Segments for fitting the data for index %s.\n"
	    "# Consists of x0 y0NLx1 y1 pairs of lines interspersed with blank lines\n"
	    "# gnuplot interprets blank lines as meaning the end of a discrete line seg.\n",
	    outfilename);
    if (whichtype == VOCAB) {
      fprintf(wdlensfile, "#Word length probability for index %s.\n",
	      outfilename);

      fprintf(wdfreqsfile, "#Word frequency distributions for different word lengths for index %s.\n",
	      outfilename);
    }
  }

  start_time = what_time_is_it();
  very_start = start_time;


  fileinmem = (byte *)mmap_all_of((u_char *)argv[1], &vsz, FALSE, &FH, &FMH, &error_code);
  if (vsz % entry_len != 0) {
    printf("Error: Size of file %s should be a multiple of %zu but it isn't\n",
	   argv[1], entry_len);
  }
  distinct_wds = (int)(vsz / entry_len);
  printf("Vocab_lister: %s mmapped.  %zu / %zu = %d entries.  Type is %d.  Time taken: %.2f sec\n", 
	 argv[1], vsz, entry_len, distinct_wds, whichtype,
	 what_time_is_it() - start_time);


  // qsort(fileinmem, distinct_wds, entry_len, cmp_freq);   // qsort turned out to be O(n^2)
  //printf("Elapsed time for qsort: %8.1f sec.\n", (double)(clock() - start) / CLOCKS_PER_SEC);

  // Use counting sort instead.

  // 1. Find out what is the maximum frequency in order to size the histogram array
  vp = fileinmem;
  if (0) printf("Starting the counting sort\n");

  if (sort_by_freq) {
    start_time = what_time_is_it();
    for (w = 0; w < distinct_wds; w++) {
      if (whichtype == VOCAB) {
	vocabfile_entry_unpacker(vp, MAX_WD_LEN + 1, &freq, &qidf, &payload);
	if ((long long)freq > max_freq) {
	  if (debug) {
	    printf("New Max: %llu for word w = %d\n", freq, w);
	    //printf("Word %d - freq %I64d, entry_len = %d\n", w, freq, entry_len);
	  }
	  max_freq = freq;
	}
      }
      else if (whichtype == TERM_REPS) {
	llfreqp = (u_ll *)(vp + MAX_REP_LEN + 1);
	freq = (int)*llfreqp;
	if ((long long)freq > max_freq) {
	  if (debug) printf("New Max: %llu for (%s)\n", freq, vp);
	  max_freq = freq;
	}
      }
      else if (whichtype == NGRAMS) {
	llfreqp = (u_ll *)(vp + MAX_NGRAM_LEN + 1);
	freq = (int)*llfreqp;
	if ((long long)freq > max_freq) {
	  if (debug) printf("New Max: %llu for (%s)\n", freq, vp);
	  max_freq = freq;
	}
      }
      else {
	llfreqp = (u_ll *)(vp + MAX_BIGRAM_LEN + 1);
	freq = (int)*llfreqp;
	if ((long long)freq > max_freq) {
	  if (debug) printf("New Max: %llu for (%s)\n", freq, vp);
	  max_freq = freq;
	}
      }
      if (freq == 1) singletons++;
      vp += entry_len;
    }

    printf("Highest freq: %llu. Time taken: %.2f sec\n", max_freq,
	   what_time_is_it() - start_time);


    // 2. Set up the score_histo
    start_time = what_time_is_it();
    score_histo = (u_ll *)malloc((max_freq + 1) * sizeof(u_ll));
    for (h = 0; h <= max_freq; h++) { score_histo[h] = 0; }

    vp = fileinmem;
    for (w = 0; w < distinct_wds; w++) {
      if (whichtype == VOCAB) {
	vocabfile_entry_unpacker(vp, MAX_WD_LEN + 1, &freq, &qidf, &payload);
      }
      else if (whichtype == TERM_REPS) {
	llfreqp = (u_ll *)(vp + MAX_REP_LEN + 1);
	freq = *llfreqp;
      }
      else if (whichtype == NGRAMS) {
	llfreqp = (u_ll *)(vp + MAX_NGRAM_LEN + 1);
	freq = *llfreqp;
      }
      else {
	llfreqp = (u_ll *)(vp + MAX_BIGRAM_LEN + 1);
	freq = *llfreqp;
      }
      if (0) printf("%8d: Freq: %lld\n", w, freq);
      score_histo[freq]++;
      vp += entry_len;
    }
    if (debug) printf("Histo set up: %llu\n", max_freq);



    // 3. Turn the score histo into a cumulative one - Descending freq
    sum = 0;
    for (h = max_freq; h >= 0; h--) {
      count = score_histo[h];
      score_histo[h] = sum;
      sum += count;
    }

    printf("Cumulative score histogram set up. Time taken: %.2f sec\n",
	   what_time_is_it() - start_time);


    // 4. Create and set up a permutation array
    start_time = what_time_is_it( );
    permute = (int *)malloc(distinct_wds * sizeof(int));
    if (permute == NULL) { error_exit("Malloc of permute failed"); }
    for (w = 0; w < distinct_wds; w++) {
      permute[w] = -1;  // To enable detection of errors.
    }

    vp = fileinmem;
    for (w = 0; w < distinct_wds; w++) {
      u_ll pos;
      if (whichtype == VOCAB) {
	vocabfile_entry_unpacker(vp, MAX_WD_LEN + 1, &freq, &qidf, &payload);
	// Check for spaces in vp.  There shouldn't be any.
	u_char *p = vp;
	while (*p) {
	  if (*p <= ' ') {
	    printf("Gaak!  Vocab_lister found a space in '%s'\n",
		   vp);
	    break;
	  }
	  p++;
	}	
      }
      else if (whichtype == TERM_REPS) {
	llfreqp = (u_ll *)(vp + MAX_REP_LEN + 1);
	freq = *llfreqp;
      }
      else if (whichtype == NGRAMS) {
	llfreqp = (u_ll *)(vp + MAX_NGRAM_LEN + 1);
	freq = *llfreqp;
      }
      else {
	llfreqp = (u_ll *)(vp + MAX_BIGRAM_LEN + 1);
	freq = *llfreqp;
      }
      totfreq += freq;
      pos = score_histo[freq];
      permute[pos] = w;
      score_histo[freq]++;
      wdlen = utf8_count_characters(vp);
      wdlen_mean_freq[wdlen] += freq;
      wdlen_stdev_freq[wdlen] += (freq * freq);
      freq_weighted_wdlength_count[wdlen] += freq;
      wdlength_count[wdlen]++;
      vp += entry_len;
    }

    printf("Permutation array set up for %d entries. Time taken: %.2f sec\n", 
	   distinct_wds, what_time_is_it() - start_time);
    
  }  // End of if (sort_by_freq)


  // 5. Finally print the results of all this.
  start_time = what_time_is_it();
  obuf = (char *)malloc(OBUF_SIZE);
  setvbuf(tsvfile, obuf, _IOFBF, (size_t)(OBUF_SIZE));
  if (0) printf(" .. about to write TSV file.\n");
  for (w = 0; w < distinct_wds; w++) {
    if (sort_by_freq) pw = permute[w];
    else pw = w;

    vp = fileinmem + pw * entry_len;
    if (whichtype == VOCAB) {
      vocabfile_entry_unpacker(vp, MAX_WD_LEN + 1, &freq, &qidf, &payload);
      fprintf(tsvfile, "%s\t%llu\t%u\n", vp, freq, qidf);
    }
    else if (whichtype == TERM_REPS) {
      llfreqp = (u_ll *)(vp + MAX_REP_LEN + 1);
      freq = *llfreqp;
      fprintf(tsvfile, "%s\t%llu\n", vp, freq);
    }
    else if (whichtype == NGRAMS) {
      llfreqp = (u_ll *)(vp + MAX_NGRAM_LEN + 1);
      freq = *llfreqp;
      fprintf(tsvfile, "%s\t%llu\n", vp, freq);
    }
    else {
      llfreqp = (u_ll *)(vp + MAX_BIGRAM_LEN + 1);
      freq = *llfreqp;
      fprintf(tsvfile, "%s\t%llu\n", vp, freq);
    }

    // Now write the data to the plotfile.
    if (sort_by_freq) {
      logrank = log10((double)(w + 1));
      if (logrank - lastlogrank > EPSILON) {
	logfreq = log10((double)freq );  // Used to be logprob: divided by (double)totfreq
	fprintf(plotfile, "%.10f %.10f\n", logrank, logfreq);
	lastlogrank = logrank;
      }
    }
  }
  fclose(tsvfile);
  if (0) printf(" .. TSV file successfully written.\n");


  // 6. [Sometimes] output the .tfd, .plot, .segdat, .wdlens, and .wdfreqs files
  if (sort_by_freq) {
  if (tfdfile != NULL) {
    write_tfd_file(tfdfile, segdatfile, fileinmem, permute, distinct_wds, singletons, entry_len, totfreq, whichtype);
    fclose(tfdfile);
    if (whichtype == BIGRAMS) printf("  bigrams.tfd written.\n");
    else if (whichtype == NGRAMS) printf("  ngrams.tfd written.\n");
    else if (whichtype == COOCCURS) printf("  cooccurs.tfd written.\n");
    else if (whichtype == TERM_REPS) printf("  repetitions.tfd written.\n");
    else printf("  vocab.tfd written.\n");
  }

  if (plotfile != NULL) {
    fclose(plotfile);
    if (whichtype == BIGRAMS) printf("  bigrams.plot written.\n");
    else if (whichtype == NGRAMS) printf("  ngrams.plot written.\n");
    else if (whichtype == COOCCURS) printf("  cooccurs.plot written.\n");
    else if (whichtype == TERM_REPS) printf("  repetitions.plot written.\n");
    else printf("  vocab.plot written.\n");
  }

  if (segdatfile != NULL) {
    fclose(segdatfile);
    if (whichtype == BIGRAMS) printf("  bigrams.segdat written.\n");
    else if (whichtype == NGRAMS) printf("  ngrams.segdat written.\n");
    else if (whichtype == COOCCURS) printf("  cooccurs.segdat written.\n");
    else if (whichtype == TERM_REPS) printf("  repetitions.segdat written.\n");
    else printf("  vocab.segdat written.\n");
  }

  if (whichtype == VOCAB) {
     // 7. Generate the .wdlens file after converting histograms to probabilities
     //    The .wdlens file gives the probability (A. in the vocab, and B in the
     //    corpus) that a word is of length L.
double ave_len  = 0, freq_weighted_ave_len = 0,
ave_freq = 0, stdev_freq = 0.0, count;
  for (i = 1; i <= MAX_WD_LEN; i++) {
    ave_len += (double)i * wdlength_count[i];
    freq_weighted_ave_len += (double)i * freq_weighted_wdlength_count[i];
    count = wdlength_count[i];
    wdlength_count[i] /= (double)distinct_wds;
    freq_weighted_wdlength_count[i] /= (double)totfreq;

    // Accumulating overall mean and standard deviations of word frequencies.
    ave_freq += wdlen_mean_freq[i];
    stdev_freq += wdlen_stdev_freq[i];

    // Calculating mean and standard deviation for words of length i.
    // St.dev calculation uses old method, prone to numerical accuracy issues
    // .. hopefully OK
    // Var = (SumSq − (Sum × Sum) / n) / (n − 1)
    wdlen_stdev_freq[i] = sqrt((wdlen_stdev_freq[i]
				- (wdlen_mean_freq[i] * wdlen_mean_freq[i])
				/ count) / (count -1));
    wdlen_mean_freq[i] /= count;  
  }

  stdev_freq = sqrt((stdev_freq
		     - (ave_freq * ave_freq)
		     / (double)distinct_wds) / ((double)distinct_wds - 1));
			
  ave_freq /= (double)distinct_wds;


  fprintf(wdlensfile, "# - lengths are measured in Unicode characters, not bytes.\n"
	  "#Average of distinct word lengths: %.3f\n",
	  ave_len /= (double)distinct_wds);
  fprintf(wdlensfile, "#Average of word occurrence lengths in Unicode characters: %.3f\n",
	  freq_weighted_ave_len /= (double)totfreq);
  fprintf(wdlensfile, "#Length prob._for_distinct_wds  prob_for_wd_occurrences\n");


  fprintf(wdfreqsfile, "# Overall word frequency: Mean %.3f; St. Dev %.3f\n#\n"
	  "# Mean and st.dev of frequencies by word length.\n",
	  ave_freq, stdev_freq);
  fprintf(wdfreqsfile, "#Length Mean-freq.  St.dev\n");


  for (i = 1; i <= MAX_WD_LEN; i++) {
    fprintf(wdlensfile, "%d\t%.6f\t%.6f\n",  i, wdlength_count[i],  freq_weighted_wdlength_count[i]);
    fprintf(wdfreqsfile, "%d\t%.6f\t%.6f\n",  i, wdlen_mean_freq[i],  wdlen_stdev_freq[i]);
  }
fclose(wdlensfile);
printf("  vocab.wdlens written.\n");
fclose(wdfreqsfile);
printf("  vocab.wdfreqs written.\n");

  }
  }  // End of if sort_by_freq()


  printf("All files written. Time taken: %.2f sec\n", what_time_is_it() - start_time);


  unmmap_all_of(fileinmem, FH, FMH, vsz);

  if (permute != NULL) free(permute);
  if (score_histo != NULL) free(score_histo);

  printf("Total elapsed time %.2f sec.\n", what_time_is_it() - very_start);

  return 0;
}

