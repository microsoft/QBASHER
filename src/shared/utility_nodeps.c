// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#ifdef WIN64
#include <tchar.h>
#include <strsafe.h>
#include <windows.h>
#include <Psapi.h>  
#else
#include <sys/mman.h>
#include <unistd.h>
#include <sys/time.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>

#include "unicode.h"
#include "utility_nodeps.h"

#ifndef DEBUG
#define DEBUG 0
#endif

#ifndef ASCII_RS
#define ASCII_RS 0x1E // ASCII Record Separator (RS)
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                                              //
// This module contains utility functions that don't depend upon anything else in QBASHER (except unicode.c)    //
//                                                                                                              //
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timing functions
////////////////////////////////////////////////////////////////////////////////////////////////////////


double what_time_is_it() {
  // Returns current time-of-day in fractional seconds in a portable way
  // To calculate elapsed times, just subtract the results of two of these calls
#ifdef WIN64
  // gettimeofday is not available on Windows.  Use https://msdn.microsoft.com/en-us/library/windows/desktop/ms644904(v=vs.85).aspx
  LARGE_INTEGER now;
  static double QPC_frequency = -1.0;
  if (QPC_frequency < 0.0) {
    LARGE_INTEGER t;
    QueryPerformanceFrequency(&t);
    QPC_frequency = (double)t.QuadPart;
  }
  // We now have the elapsed number of ticks, along with the
  // number of ticks-per-second. We use these values
  // to convert to the fractional number of seconds since "the epoch", whatever that might be.

  QueryPerformanceCounter(&now);
  return (double)now.QuadPart / QPC_frequency;

#else
  struct timeval now;
  gettimeofday(&now, NULL);
  return (double)now.tv_sec + (double)(now.tv_usec) / 1000000.0;
#endif

}


#ifdef WIN64
void set_cpu_affinity(u_int cpu) {
  // Set affinity to cpu or if cpu is not in the process mask, to the lowest bit in the mask
  // whose number is above cpu
  static HANDLE this_process;
  DWORD_PTR process_affinity_mask, system_affinity_mask, cpu_mask = 1ULL << (cpu - 1);
  BOOL ok;

  this_process = GetCurrentProcess();
  ok = GetProcessAffinityMask(this_process, &process_affinity_mask, &system_affinity_mask);
  if (ok)
    printf("set_cpu_affinity: AffinityMask=%llX\n", process_affinity_mask);
  else {
    printf("set_cpu_affinity: GetProcessAffinityMask() call failed.\n");
    return;
  }

  while (cpu_mask & ((cpu_mask & process_affinity_mask) == 0)) {
    cpu_mask <<= 1;
  }
  if (cpu_mask) {
    ok = SetProcessAffinityMask(this_process, cpu_mask);
    if (ok)
      printf("set_cpu_affinity: AffinityMask successfully set to %llX\n", cpu_mask);
    else
      printf("set_cpu_affinity: SetProcessAffinityMask(%llX) call failed.\n", cpu_mask);
  }
  else
    printf("set_cpu_affinity: Unable to find a cpu number >= %u and in the process mask %llX\n", cpu, process_affinity_mask);

}


static BOOL rlc_initialized = FALSE;   // RMU = Report Memory Usage
static double rmu_last_clock;

void report_memory_usage(FILE *printto, u_char *msg, DWORD *pagefaultcount) {
  // If pagefaultcount isn't NULL, return the current pagefaultcount.
  PROCESS_MEMORY_COUNTERS m;
  BOOL ok;
  double now;

  if (!rlc_initialized) {
    rlc_initialized = TRUE;
    rmu_last_clock = what_time_is_it();
  }
  now = what_time_is_it();
  ok = GetProcessMemoryInfo(GetCurrentProcess(), &m, (DWORD)sizeof(m));
  if (pagefaultcount != NULL) *pagefaultcount = m.PageFaultCount;
  fprintf(printto, "----------- Memory Usage Summary (%s %.3f sec. since previous summary) -----------\n"
	  "   Page fault count: %u\n"
	  "   Working set size: current %I64d, peak %I64d  -- %.1fMB\n"
	  "   Quota paged pool usage: current %I64d, peak %I64d\n"
	  "   Quota nonpaged pool usage: current %I64d, peak %I64d\n"
	  "   Pagefile usage: current %I64d, peak %I64d  -- %.1fMB\n"
	  "-------------------------------------------------------\n\n",
	  msg,
	  now - rmu_last_clock,
	  m.PageFaultCount,
	  (long long)m.WorkingSetSize,
	  (long long)m.PeakWorkingSetSize,
	  (double)m.WorkingSetSize / 1048576.0,
	  (long long)m.QuotaPagedPoolUsage,
	  (long long)m.QuotaPeakPagedPoolUsage,
	  (long long)m.QuotaNonPagedPoolUsage,
	  (long long)m.QuotaPeakNonPagedPoolUsage,
	  (long long)m.PagefileUsage,
	  (long long)m.PeakPagefileUsage,
	  (double)m.PagefileUsage / 1048576.0
	  );
  rmu_last_clock = now;
}

#endif



////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//  Functions dealing with file i/o and memory mapping
////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BOOL is_a_directory(char *arg) {
  // Is arg the path to a directory?
#ifdef WIN64
  struct __stat64 fileStat;
  if (_stat64((char *)arg, &fileStat) == 0) {
	  if (fileStat.st_mode & _S_IFDIR) return TRUE;
	  else return FALSE;
  }
  else return FALSE;   // It doesn't even exist
#else
  struct stat statbuf;
  if (stat(arg, &statbuf) == 0) {
	  if (S_ISDIR(statbuf.st_mode)) return TRUE;
	  else return FALSE;
  }
  else return FALSE;   // It doesn't even exist
#endif
}


BOOL exists(char *fstem, char *suffix) {
  // Test for the existence of a file whose name is the concatenation of fstem and suffix
#ifdef WIN64
  struct __stat64 fileStat;
#else
  struct stat statbuf;
#endif
  char *fname;
  size_t l1 = strlen(fstem);

  fname = (char *)malloc(l1 + strlen(suffix) + 2);  // MAL2000
  if (fname == NULL) {
    fprintf(stderr, "Warning: Malloc failed in exists(%s, %s)\n", fstem, suffix);
    return FALSE;
  }
  strcpy(fname, fstem);
  strcpy(fname + l1, suffix);
#ifdef WIN64
  if (_stat64((char *)fname, &fileStat) == 0) {
    free(fname);								 //FRE2000
    return TRUE;
  }
#else
  if (stat(fname, &statbuf) == 0) {
    free(fname);								 //FRE2000
    return TRUE;
  }       
#endif
  free(fname);								     //FRE2000
  return FALSE;
}



size_t get_filesize(u_char *fname, BOOL verbose, int *error_code) {
#ifdef WIN64
  struct __stat64 fileStat;
#else
  struct stat statbuf;
#endif
  *error_code = 0;
#ifdef WIN64
  if (_stat64((char *)fname, &fileStat) != 0) {
    long long ser = GetLastError();
    // Error codes are listed via https://msdn.microsoft.com/en-us/library/windows/desktop/ms681381%28v=vs.85%29.aspx
    if (verbose) printf("Error %lld while statting %s\n", ser, fname);
    *error_code = -210007;
    return -1;
  }
  return fileStat.st_size;
#else 
  if (stat((char *)fname, &statbuf) != 0) {
    if (verbose) printf("Error %d while statting %s\n", errno, fname);
    *error_code = -210007;
    return -1;
  }
  return statbuf.st_size;
#endif

}


CROSS_PLATFORM_FILE_HANDLE open_ro(const char *fname, int *error_code) {
  // Open fname for read-only access and return an error code if this isn't
  // possible.
  CROSS_PLATFORM_FILE_HANDLE rslt;
  *error_code = 0;
#ifdef WIN64
  rslt = CreateFile((LPCSTR)fname,
		    GENERIC_READ,
		    FILE_SHARE_READ,
		    NULL,
		    OPEN_EXISTING,
		    //FILE_FLAG_SEQUENTIAL_SCAN,
		    FILE_ATTRIBUTE_READONLY,
		    NULL);

  if (rslt == INVALID_HANDLE_VALUE) {
    *error_code = -210006;
  }
#else
  rslt = open(fname, O_RDONLY);
  if (rslt < 0) {
    *error_code = -210006;
  }
#endif
  return(rslt);
}


CROSS_PLATFORM_FILE_HANDLE open_w(const char *fname, int *error_code) {
  // Open fname for write access and return an error code if this isn't
  // possible.
  CROSS_PLATFORM_FILE_HANDLE rslt;
  *error_code = 0;
#ifdef WIN64
  rslt = CreateFile((LPCSTR)fname,
		    GENERIC_WRITE,
		    FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
		    NULL,
		    CREATE_ALWAYS,
		    FILE_FLAG_SEQUENTIAL_SCAN,
		    NULL);

  if (rslt == INVALID_HANDLE_VALUE) {
    *error_code = -210006;
    return NULL;
  }

#else
  // Create the file with mode 0777 so that anyone can re-create the index
  rslt = open(fname, O_CREAT|O_TRUNC|O_WRONLY, S_IRWXU|S_IRWXG|S_IRWXO);
  if (rslt < 0) {
    *error_code = -210006;
    return -1;
  }
#endif
  return(rslt);
}


void close_file(CROSS_PLATFORM_FILE_HANDLE h) {
#ifdef WIN64
  CloseHandle(h);
#else
  close(h);
#endif
      
}

void buffered_flush(CROSS_PLATFORM_FILE_HANDLE wh, byte **buffer, size_t *bytes_in_buffer, char *label, BOOL cleanup)  {
  // Write *bytes_in-buffer bytes to the program-maintained buffer to the file 
  // represented by wh and set bytes_in_buffer to zero.  If an error occurs
  // print label and take an abnormal exit.  If cleanup is set, free the buffer
  // close the handle.

  if (*buffer == NULL) {
    printf("Buffered_flush() - nothing to do: buffer is NULL\n");
    return;
  }
#ifdef WIN64
  BOOL ok;
  DWORD written;
  ok = WriteFile(wh, *buffer, (DWORD)*bytes_in_buffer, (LPDWORD)&written, NULL);
  if (!ok) {
    printf("Error code for %s: %u, trying to write %lld bytes\n", label,
	   GetLastError(), *bytes_in_buffer);
    printf("\n%s: ", label);
    printf("buffered_flush() write error %d\n", GetLastError());
    exit(1);  // Not called by the query processor
  }
#else
  ssize_t written;
  written = write(wh, *buffer, *bytes_in_buffer);
  if (written < 0) {
    printf("Error code for %s: %u, trying to write %zu bytes\n", label,
	   errno, *bytes_in_buffer);
    printf("\n%s: ", label);
    printf("buffered_flush() write error %d\n", errno);
    exit(1); // Not called by the query processor
  }
#endif
  if (written < *bytes_in_buffer) {
    printf("\n%s: ", label);
    printf("buffered_flush() short write.\n");
    exit(1); // Not called by the query processor
  }
  *bytes_in_buffer = 0;
  if (cleanup) {
    free(*buffer);
    *buffer = NULL;
    close_file(wh);
  }
}


void buffered_write(CROSS_PLATFORM_FILE_HANDLE wh, byte **buffer, size_t buffer_size, size_t *bytes_in_buffer,
		    byte *data, size_t bytes2write, char *label) {
  // Attempt to store bytes2write bytes from data in the buffer.  If during the 
  // process, the buffer fills, call buffer_flush to write a buffer full to the file 
  // represented by wh.  If bytes2write is larger than the buffer_size, loop until done.
  // If an error occurs, use label to help identify which file caused the prob.
  // If buffer is NULL, allocate one of the specified buffer_size.
  size_t b, bib;
  byte *buf;

  if (DEBUG) printf("Buffered write(%s) - %zu\n", label, bytes2write);
  if (*buffer == NULL) {
    *buffer = (byte *)cmalloc(buffer_size, (u_char *)"buffered_write()", FALSE);
    *bytes_in_buffer = 0;
    if (DEBUG) printf("Buffered write(%s) - buffer of %zu malloced\n", label, buffer_size);

  }
  buf = *buffer;
  bib = *bytes_in_buffer;
  for (b = 0; b < bytes2write; b++) {
    if (bib >= buffer_size) {
      if (DEBUG) printf("Buffered write(%s) - about to flush %zu bytes\n",
			label, buffer_size);
      buffered_flush(wh, buffer, &bib, label, 0);
      bib = 0;
    }
    if (DEBUG) printf("Buffered write(%s) - b = %zu, bib = %zu\n", label, b, bib);
    buf[bib++] = data[b];
  }
  *bytes_in_buffer = bib;
  if (DEBUG) printf("Buffered write(%s) - write finished\n", label);
}




void *mmap_all_of(u_char *fname, size_t *sighs, BOOL verbose, CROSS_PLATFORM_FILE_HANDLE *H, HANDLE *MH, 
		  int *error_code) {
  // Memory map the entire file named fname and return a pointer to mapped memory,
  // plus handles to the file and to the memory mapping.  We have to return the
  // handles to enable indexes to be properly unloaded.
  // 
  //
  void *mem;
  double MB;
  int ec;
  *error_code = 0;
  BOOL report_errors = TRUE;
  double start;
  start = what_time_is_it();

  if (verbose) fprintf(stderr, "Loading %s\n", fname);
  *sighs = get_filesize(fname, verbose, error_code);

  MB = (double)*sighs / 1048576.0;
  *H = open_ro((char *)fname, &ec);
  if (ec < 0) {
#ifdef WIN64
    long long ser = GetLastError();
    *error_code = ec;
    if (report_errors) printf("\nError %lld while opening %s\n", ser, fname);
#else
    if (report_errors) printf("\nError %d while opening %s\n", errno, fname);
#endif
    return NULL;
  }

  if (verbose) fprintf(stderr, "File %s opened.\n", fname);

#ifdef WIN64
  if ((*MH = CreateFileMapping(*H,
			       NULL,
			       PAGE_READONLY,
			       0,
			       0,
			       NULL)) < 0) {
    long long ser = GetLastError();
    *error_code = -210008;
    if (report_errors) printf("\nError %lld while Creating File Mapping for %s\n", ser, fname);
    return NULL;   // ----------------------------------------->
  }

  if (verbose) printf("File mapping created for %s\n", fname);

  if ((mem = (byte *)MapViewOfFile(*MH,
				   FILE_MAP_READ,
				   0,
				   0,
				   0)) == NULL) {
    long long ser = GetLastError();
    *error_code = -210009;
    if (report_errors) printf("\nError %lld while Mapping View of %s\n", ser, fname);
    return NULL;  // ----------------------------------------->
  }


  // Assertion:  We can close both handles here without losing the mapping.
  //CloseHandle(H);
  //CloseHandle(MH);
  // False.  Subsequent reopening of the file finds a lot of crap.
#else

  /*  Note: 
      MAP_HUGETLB (since Linux 2.6.32)
      Allocate the mapping using "huge pages."  See the Linux kernel
      source file Documentation/vm/hugetlbpage.txt for further
      information, as well as NOTES, below.

      MAP_HUGE_2MB, MAP_HUGE_1GB (since Linux 3.8)
      Used in conjunction with MAP_HUGETLB to select alternative
      hugetlb page sizes (respectively, 2 MB and 1 GB) on systems
      that support multiple hugetlb page sizes.
  */
  mem = mmap(NULL, *sighs, PROT_READ, MAP_PRIVATE, *H, 0);
  if (mem == MAP_FAILED) {
    *error_code = -210007;
    return NULL;
  }

#endif

  if (verbose) fprintf(stderr, "  - %8.1fMB mapped.\n", MB);
  if (verbose) fprintf(stderr, "  - elapsed time: %8.1f sec.\n", what_time_is_it() - start);
  return mem;
}


void unmmap_all_of(void *inmem, CROSS_PLATFORM_FILE_HANDLE H, HANDLE MH, size_t length) {
  // Note MH is only used on Windows and length is only used on Unix-like systems.
#ifdef WIN64
  UnmapViewOfFile(inmem);
  CloseHandle(MH);
  close_file(H);
#else
  munmap(inmem, length);
  close_file(H);
#endif
}


byte **load_all_lines_from_textfile(u_char *fname, int *line_count, CROSS_PLATFORM_FILE_HANDLE *H,
				    HANDLE *MH, byte **file_in_mem, size_t *sighs) {
  // memory map file fname, and return an array of pointers to every line in the in-memory
  // version
  int error_code;
  int cnt = 0;
  byte *p, *lastbyte;
  byte **lions = NULL;
  
  *file_in_mem = mmap_all_of(fname, sighs, TRUE, H, MH, &error_code);
  if (*file_in_mem == NULL) {
    printf("Error: Can't mmap %s.  Error_code %d.\n", fname, error_code);
    exit(1);  // Not called from query processor.
  }
  // Count the number of lines
  p = *file_in_mem;
  lastbyte = p + *sighs - 1;
  if (*lastbyte != '\n') cnt++;  // Last line ends with EOF rather than LF
  while (p <= lastbyte) {
    if (*p++ == '\n') cnt++;
  }
  *line_count = cnt;

  // Allocate storage for the strings
  lions = (byte **)cmalloc(cnt * sizeof(byte *), (u_char *)"load_all_lines_from_textfile", FALSE);

  // set up all the entries in lions[]
  p = *file_in_mem;
  lastbyte = p + *sighs - 1;
  cnt = 0;
  lions[cnt++] = p;
  while (p < lastbyte) {
     if (*p++ == '\n') {
       lions[cnt++] = p;  // Point to the character after each linefeed (except the one at EOF)
     }
  }
  return lions;
}

void unload_all_lines_from_textfile(CROSS_PLATFORM_FILE_HANDLE H, HANDLE MH, byte ***lines,
				    byte **file_in_memory, size_t sighs) {
  free(*lines);
  unmmap_all_of(*file_in_memory, H, MH, sighs);
  *lines = NULL;
  *file_in_memory = NULL;
}

#ifdef WIN64
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Code for setting up to use Virtual Memory Large Pages.  (Can reduce program runtime.)                               //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//Code posted by 'jeremyb1' on https://social.msdn.microsoft.com/forums/windowsdesktop/en-us/09edf7b2-2ddc-44e5-9fd2-5d537b542051/trying-to-use-memlargepages-but-cant-get-selockmemoryprivilege
void Privilege(TCHAR* pszPrivilege, BOOL bEnable, BOOL *x_use_large_pages, size_t *large_page_minimum)
{
  HANDLE      hToken;
  TOKEN_PRIVILEGES tp;
  BOOL       status;
  u_long      error;

  printf("Attempting to set privileges\n");
		
  OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken);
  LookupPrivilegeValue(NULL, pszPrivilege, &tp.Privileges[0].Luid);
  tp.PrivilegeCount = 1;
  tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  status = AdjustTokenPrivileges(hToken, FALSE, &tp, 0, (PTOKEN_PRIVILEGES)NULL, 0);
  error = GetLastError();
  if (error) {
    printf("\n\nAdjustTokenPrivileges error %u.  Giving up on using LARGE PAGES.\n\n", error);
    printf("Note that successful use of x_use_large_pages requires that QBASHI.exe is run with administrator privilege\n"
	   "by a user who has the lock-memory privilege bit set.   Setting this bit can be achieved by running secpol.msc,\n"
	   "navigating to Local Policies, User Rights, and setting the lock-memory right.  I believe you then have to\n"
	   "reboot.\n");
    x_use_large_pages = FALSE;
  }
  else {
    *large_page_minimum = GetLargePageMinimum();
    printf("Proceeding to use LARGE PAGES.  Large Page Minimum: %lld bytes.\n", *large_page_minimum);
  }

  CloseHandle(hToken);
}

#endif

void *lp_malloc(size_t how_many_bytes, BOOL x_use_large_pages, size_t large_page_minimum) {
  // Use either malloc or virtualalloc() (with LARGE PAGES) depending upon the setting of the global
  // x_use_large_pages

  void *rslt;
#ifdef WIN64
  if (x_use_large_pages) {
    // how_many_bytes must be a multiple of large_page_minimum
    if (how_many_bytes % large_page_minimum) {
      // round up
      how_many_bytes = ((how_many_bytes / large_page_minimum) + 1) * large_page_minimum;
    }
    rslt = VirtualAlloc(
			NULL,
			how_many_bytes,
			MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
			PAGE_READWRITE);

    if (rslt == NULL) {
      // Failed
      printf("VirtualAlloc failed: %u\n", GetLastError());
    }
    else {
      if (0) printf("VirtualAlloc succeeded!\n");
    }
  }
  else 
#endif	
    rslt = malloc(how_many_bytes);
  return rslt;
}


void lp_free(void *memory_to_free, BOOL x_use_large_pages) {
#ifdef WIN64
  if (x_use_large_pages) {
    BOOL success = VirtualFree(
			       memory_to_free,
			       (size_t)0,
			       MEM_RELEASE);
    if (!success) {
      printf("VirtualFree failed: %u\n", GetLastError());
    }
  }
  else 
#endif	
    free(memory_to_free);
}


void *cmalloc(size_t s,  u_char *msg, BOOL verbose) {
  // A front end to malloc() which exits on error, zeroes the allocated memory
  // and reports the numbe of MB allocated.
  void *vvv;
  double MB;
  vvv = malloc(s);
  if (vvv == NULL) {
    printf("Error:  CMALLOC(%s) failed for size %zu\n", msg, s);
    exit(1);   // Memory allocation failures are treated as fatal, even in the query processor
  }
  memset(vvv, 0, s);   // Make sure it's all zeroes
  MB = (double)s / (1024.0 * 1024.0);
  if (verbose) printf("CMALLOC(%s):  %.1fMB allocated.\n", msg, MB);
  return vvv;
}



#define SAMPLE_SIZE 65536 

u_ll estimate_lines_in_mmapped_textfile(u_char *file_in_mem, size_t file_length, int samples) {
  // Scan a specified number of 16kB samples taken at equal intervals in the file and compute the ratio of chars 
  // to linefeeds.  From that average line length can be estimated the number of records in the file.
  // The more samples the greater the accuracy.
  u_ll rslt = 0, chars = 0, linefeeds = 0;
  int s, t;
  size_t off = 0, step = file_length / samples;
  u_char *p, *end_of_file = file_in_mem + (file_length - 1);
  double ave_line_length;
  if (file_length == 0) return 0;

  for (s = 0; s < samples; s++) {
    p = file_in_mem + off;
    for (t = 0; t < SAMPLE_SIZE; t++) {
      if (p > end_of_file) break;
      chars++;
      if (*p++ == '\n') linefeeds++;
    }
    off += step;
  }
  if (linefeeds == 0) return 0;
  ave_line_length = (double)chars / (double)linefeeds;
  rslt = (u_ll)((double)file_length / ave_line_length + 0.999);  // Round up.
  return rslt;
}

u_ll estimate_lines_in_textfile(CROSS_PLATFORM_FILE_HANDLE file, size_t file_length, int samples) {
  // Read a specified number of 16kB samples at equal intervals in the file and compute the ratio of chars 
  // to linefeeds.  From that average line length can be estimated the number of records in the file.
  // The more samples the greater the accuracy.
  //
  // Note that because file has been opened with FILE_FLAG_NO_BUFFERING, reads must start at a multiple of the 
  // file sector size.  
  u_ll rslt = 0, chars = 0, linefeeds = 0, step;
  int s;
  u_char *p, *buffer = NULL;
  double ave_line_length;
  BOOL ok;
  u_int t, bytes_read = 0;
#ifdef WIN64
  LARGE_INTEGER off;
  off.QuadPart = 0;
#else
  off_t off = 0;
#endif


  if (file_length == 0) return 0;

  buffer = (u_char *)cmalloc(SAMPLE_SIZE, (u_char *)"estimate_lines_in_textfile",  FALSE);

  step = file_length / samples;
  step = 4096 * ((step + 4095) / 4096);  // Make sure the seek is to a 4k boundary

  for (s = 0; s < samples; s++) {
#ifdef WIN64
    if ((size_t)(off.QuadPart) > file_length) break;
    ok = SetFilePointerEx(file, off, NULL, FILE_BEGIN);  // FILE_POINTER
    if (!ok) {
      printf("Error code: %u from SetFilePointerEx\n", GetLastError());
      exit(1);  // OK - not QP - probably better to fail if we've only indexed part of the data
    }
    ok = ReadFile(file, buffer, (DWORD)SAMPLE_SIZE, (LPDWORD)&bytes_read, NULL);
    if (!ok) {
      printf("Error code: %u from ReadFIle\n", GetLastError());
      exit(1);  // OK - not QP - probably better to fail if we've only indexed part of the data
    }

    off.QuadPart += step;

#else
    ssize_t red;
    if ((size_t)off > file_length) break;
    ok = (off == lseek(file, off, SEEK_SET));
    if (!ok) {
      printf("Error code: %d from lseek\n", errno);
      exit(1);   // OK - not QP - probably better to fail if we've only indexed part of the data
    }
    red = read(file, buffer, SAMPLE_SIZE);
    if (red < 0) {
      printf("Error code: %d from read\n", errno);
      exit(1);   // OK - not QP - probably better to fail if we've only indexed part of the data
    }
    if (red < SAMPLE_SIZE) {
      printf("Error: short read.\n");
      exit(1);   // OK - not QP - probably better to fail if we've only indexed part of the data
    }

    off += step;
    bytes_read = (u_int) red;
		
#endif		


    p = buffer;
    for (t = 0; t < bytes_read; t++) {
      chars++;
      if (*p++ == '\n') linefeeds++;
    }
  }

  free(buffer);
  buffer = NULL;

  // Before returning put the filepos back to the start of the file.
#ifdef WIN64
  off.QuadPart = 0;
  ok = SetFilePointerEx(file, off, NULL, FILE_BEGIN);
#else
  off = 0;
  lseek(file, off, SEEK_SET);
#endif

  if (linefeeds == 0) return 0;
  ave_line_length = (double)chars / (double)linefeeds;
  rslt = (u_ll)((double)file_length / ave_line_length + 0.999);  // Round up.

  return rslt;
}

void error_exit(char *msg) {
  printf("%s\n\n", msg);
  //print_usage();
  exit(1);
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////
// String functions
////////////////////////////////////////////////////////////////////////////////////////////////////////////


int validate_and_normalise_language_code(u_char *str) {
  // Check that str comprises exactly two ASCII letters (as required for ISO 639:1
  // language codes.  Lower case the two bytes if that's true and return 0.
  // Otherwise return -1 and do nothing.  Don't use tolower() or isalpha()
  // because they are locale-dependent.
  u_char *p = str;

  // Check that str is at least two chars long and that its 3rd byte is a NUL, Space or control.
  if (p == NULL || *p == 0 || *(p + 1) == 0 || *(p + 2) > ' ') return(-1); // --------------------------------------->

  // Process first byte
  if (*p & 0x80) return(-1); // --------------------------------------->
  if (*p < 'A' || *p > 'z' || (*p > 'Z' && *p < 'a')) return(-1); // -------------------->	
  if (*p >= 'A' && *p <= 'Z') *p += 32;
  p++;
  // Process second byte
  if (*p & 0x80) return(-1); // --------------------------------------->
  if (*p < 'A' || *p > 'z' || (*p > 'Z' && *p < 'a'))  return(-1); // -------------------->
  if (*p >= 'A' && *p <= 'Z') *p += 32;
  return 0;
}


size_t map_bytes(u_char *dest, u_char *src, size_t n, u_char *map) {
  // Copy up to n characters of src into dest, while applying a byte-for-byte mapping.
  // Dest must be at least n bytes and will be null terminated only if src is shorter
  // than n bytes.  Map must be an array of 256 elements, set up to map any byte value
  // to any other. E.g. to turn all punctuation into spaces.
  // Returns the number of characters copied, excluding the null if any
  u_char *d = dest;
  while (n > 0 && *src) {
    *d++ = map[*src++];
    n--;
  }
  if (n > 0) *d = 0;  //Only null-terminate if src is shorter than n bytes.
  return (d - dest);
}



#if !defined(strcasecmp) 
int strcasecmp(const char *s, const char *t) {
  // Return -1, 0, +1 as ASCII-case-folded s is less than, equal to, or greater than t (in alphabetic order)

  while (tolower(*s) == tolower(*t)) {
    if (*s++ == 0) return 0;
    t++;
  }
  return (tolower(*s) - tolower(*t));
}
#endif


#if !defined(strncasecpy) 
void strncasecpy(u_char *dest, u_char *src, size_t len) {
  // Just like strncpy but does ascii case folding
  int i;
  for (i = 0; i < len; i++) {
    if (*src == 0) {
      dest[i] = 0;
    }
    else {
      dest[i] = tolower(*src);
      src++;
    }
  }
}
#endif



void map_bytes_in_place(u_char *str, size_t n, u_char *map) {
  // Map all the bytes in str using the mapping in map
  // Map must be an array of 256 elements, set up to map any byte value to any other. E.g. for lower casing.
  while (n > 0 && *str) {
    *str = map[*str];
    str++;
    n--;
  }
}

u_char *tailstr(u_char *str, u_char *s) {
  // Returns NULL iff str doesn't end with a substring s.
  // Otherwise returns a pointer to the matching substring
  u_char *p;
  size_t l = strlen((char *)s);
  BOOL matched = FALSE;
  while ((p = (u_char *)strstr((char *)str, (char *)s)) != NULL) {
    str = p + l;
    matched = TRUE;
  }

  if (matched) {
    // There was at least one match and the last one started at str - l
    if (!strcmp((char *)str - l, (char *)s)) return str - l;
  }
  return NULL;
}


int substitute(u_char *str, u_char *toreplace, u_char *replacement, u_char *map,
	       BOOL check_word_boundaries) {
  // Replace all occurrences of toreplace within null-terminated str with replacement.
  //   1. The length of replacement must not exceed that of toreplace
  //   2. If map is not null, all bytes in str will be byte_mapped before the substitutions are made.  (e.g. ASCII lowercasing)
  //   3. If map is not null, it must have 256 elements
  //   4. None of the string arguments may be null (-1 returned if they are) but replacement may be empty.
  //   
  //	If check_word_boundaries is TRUE, toreplace is treated as though it is surrounded by \b s
  //  Return a count of the number of substitutions or -1 in case of error
  u_char *r, *w = NULL, *s, *ss, *f, *one_beyond = NULL, last;
  int substitutions = 0;
  size_t l1, l2;

  if (str == NULL || toreplace == NULL || replacement == NULL) return -1;
  l1 = strlen((char *)toreplace);
  l2 = strlen((char *)replacement);
  if (l1 == 0) return -1;
  if (l2 > l1) return -1;  // Would increase length.
  if (map != NULL) {
    // Byte mapping of str
    r = str;
    while (*r) { *r = map[*r];  r++; }
  }

  ss = str;
  s = ss;
  if (0) printf("Substitute '%s' for '%s' in '%s'\n", toreplace, replacement, str);
  while ((f = (u_char *)strstr((char *)s, (char *)toreplace)) != NULL) {
    // s is where we started the search, and f is where we found a match.
    if (0) printf("toreplace occurrence found at '%s'\n", f);
    if (check_word_boundaries) {
      // Check that the match of toreplace is surrounded by white space, punctuation or is at start or end of string
      // 1. Check the beginning
      if (f > str) {  // Must be either = or >
	if (!isspace(*(f - 1))) {
	  if (!ispunct(*(f - 1))) {
	    s = f + l1;
	    if (0) printf("Now looking at '%s'\n", s);
	    continue;  // leading \b failed to match
	  }
	}
      }
      // 2. Check the end
      one_beyond = f + l1;
      if (*one_beyond != 0) {
	if (!isspace(*one_beyond)) {
	  if (!ispunct(*one_beyond)) {
	    s = f + l1;
	    if (0) printf("Now looking2 at '%s'\n", s);
	    continue;  // trailing \b failed to match
	  }
	}
      }
    }

    substitutions++;

    if (w != NULL && l1 != l2) {
      // If we've already made a substitution and toreplace and replacement are of different lengths,
      // we must copy down the unsubstituted part of str
      r = ss; // Must start from ss if we have skipped a strstr match because of word boundary checks
      while (r < f) *w++ = *r++;
    }
    else w = f;

    if (l2 > 0) {
      // Do the replacement
      if (0) printf("Replacing\n");
      r = replacement;
      while (*r) *w++ = *r++;
    }
    s = f + l1;  // Start the next search after the end of the string we replaced
    ss = s;  // Note that we don't update ss if we got a strstr match, but didn't pass word-boundary checks
    if (0) printf("string is now '%s'\n", str);
  }


  if (w != NULL && l1 != l2) {
    // If we've made at least one substitution we must copy down the tail of the original string
    // unless the lengths of toreplace and replacement are the same.
    r = ss;  // Must start from ss if we have skipped a strstr match because of word boundary checks
    while (*r) *w++ = *r++;
    *w = 0;
  }

  // Now normalize spaces
  last = 0;  // Last character encountered
  r = str;
  w = str;
  while (*r) {
    if (*r == ' ') {
      if (last == ' ' || last == 0) {
	last = ' ';
	r++;
	continue;
      }
    }
    last = *r;
    *w++ = *r++;
  }
  *w = 0;
  if (*(w - 1) == ' ') *(w - 1) = 0;

  return substitutions;
}



u_char *make_a_copy_of(u_char *in) {
  // Makes and returns a copy of string in in malloced storage
  size_t len;
  u_char *out;
  if (in == NULL) return NULL;
  len = strlen((char *)in);
  out = (u_char *)malloc(len + 1);
  if (out == NULL) return NULL;
  strcpy((char *)out, (char *)in);
  return out;
}


u_char *make_a_copy_of_len_bytes(u_char *in, size_t len) {
  // Makes and returns a null-terminated copy of len byte of in in malloced storage
  u_char *out;
  if (in == NULL) return NULL;
  out = (u_char *)malloc(len + 1);
  if (out == NULL) return NULL;
  strncpy((char *)out, (char *)in, len);
  out[len] = 0;
  return out;
}

void putchars(u_char *str, size_t n) {
  // print the first n chars of str to stdout, stopping if a NUL is encountered
  while (n > 0) {
    if (*str == 0) return;  // ----->
    putchar(*str++);
    n--;
  }
}

void show_string_upto_nator(u_char *str, u_char nator, int indent) {
  // Print str up to the first occurrence of nator or NUL,  preceded by 
  // indent spaces and followed by a linefeed.
  // Like fputs(str, stdio) except that:
  //   - str is terminated by nator or NUL rather than just NUL
  //   - indent spaces are printed before the string.
  int i;
  for (i = 0; i < indent; i++) putchar(' ');
  while (*str  && *str != nator) putchar(*str++);
  putchar('\n');
}


void show_string_upto_nator_nolf(u_char *str, u_char nator, int indent) {
  // Print str up to the first occurrence of nator or NUL,  preceded by 
  // indent spaces and followed by a linefeed.
  // Like fputs(str, stdio) except that:
  //   - str is terminated by nator or NUL rather than just NUL
  //   - indent spaces are printed before the string.
  int i;
  for (i = 0; i < indent; i++) putchar(' ');
  while (*str  && *str != nator) putchar(*str++);
}


int replace_tabs_with_single_spaces_in(u_char *str) {
  int count = 0;
  while (*str) {
    if (*str == '\t') {
      count++;
      *str = ' ';
    } else if (*str == '\n' || *str =='\r') {
      *str = 0;
      return count;
    }
    str++;
  }
  return count;
}

u_char *find_nth_occurrence_in_record(u_char *record, u_char c, int n) {
  // Scan record for the n-th occurrence of ASCII character c and return
  // a pointer to it.  Record is terminated by either RS, CR, LF or NUL.
  // (ASCII_RS is Record Separator 0x1E.)  If we
  // reach the terminator before finding the desired occurrence, return
  // a pointer to the terminator. Return NULL if n is negative or zero,
  // or if c is LF, CR  or NUL.
  // c is typically TAB.  I.e. find the end of the n-th field in a TSV record
  if (n <= 0 || c == '\r' || c == '\n' || c == ASCII_RS || c == 0) return NULL;

  while (n > 0) {
    while (*record != '\r' && *record != '\n' && *record != ASCII_RS
	   && *record != 0 && *record != c) record++;
    if (*record != c) return record;
    n--;
    if (n > 0) record++;
  }
  return record;
}


u_char *extract_field_from_record(u_char *record, int n, size_t *len) {
  // Extract the n-th field from record, assuming that fields are terminated by TAB, 
  // LF, CR, RS or NUL and that the fields are numbered from one.
  //
  // If the request is invalid return an empty string.  Return NULL only if malloc fails
  //
  // Caller's responsibility to free the result returned

  int i;
  size_t l = 0;
  u_char *rslt = NULL, *fs, *r;

  if (n < 1) return make_a_copy_of((u_char *)"");
  r = record;
  for (i = 1; i < n; i++) {		// Find n-1 TABS
    while (*r && *r != '\t' && *r != '\n' && *r != '\r' && *r != ASCII_RS) r++;
    if (*r != '\t') {
      *len = 0;
      return make_a_copy_of((u_char *)"");
    }
    r++;  // Skip over the tab
  }
  fs = r;
  while (*r && *r != '\t' && *r != '\n' && *r != '\r' && *r != ASCII_RS) r++;  // Skip to the end of the field we want;
  l = r - fs;
  rslt = make_a_copy_of_len_bytes(fs, l);
  *len = l;
  return rslt;
}

int split_up_first_3_fields_in_record(u_char *record, u_char **f1, u_char **f2 , u_char **f3) {
  // Identify and null terminate up to the first three fields in a TSV record
  // Return the number of fields so processed.
  // Used e.g. for splitting queryTABoptionsTABlabel
  u_char *p = record;
  if (record == NULL) return 0;
  *f1 = record;  *f2 = NULL;  *f3 = NULL;
  while (*p) {
    if (*p == '\t') {
      *p = 0;  // NUL terminate field 1.
      *f2 = (p + 1);
      break;
    } else if (*p == '\n' || *p == '\r' || *p == ASCII_RS) {
      *p = 0;  // NUL terminate field 1.   There are no other fields
      return 1;
    }
    p++;
  }

  if (*p++ == 0) return 1;
  
  while (*p) {
    if (*p == '\t') {
      *p = 0;  // NUL terminate field 2.
      *f3 = (p + 1);
      break;
    } else if (*p == '\n' || *p == '\r') {
      *p = 0;  // NUL terminate field 1.   There are no other fields
      return 2;
    }
    p++;
  }

  if (*p++ == 0) return 2;
  
  while (*p) {
    if (*p == '\t') {
      *p = 0;  // NUL terminate field 3.
      return 3;
    } else if (*p == '\n' || *p == '\r') {
      *p = 0;  // NUL terminate field 1.   There are no other fields
      return 3;
    }
    p++;
  }

  return 3;
}


size_t get_dirlen_from_path(u_char *file_path) {
  // Given a file path such as /home/dave/files/data.txt, or c:\Users\dave\files\data.txt, return a count 
  // of the number of characters up to and including the last slash or backslash
  size_t l = 0;
  u_char *p = file_path;
  if (file_path == NULL) return 0;
  while (*p) {
    if (*p == '/' || *p == '\\') l = (p - file_path) + 1;
    p++;
  }
  return l;
}


void url_decode(u_char *str) {
  // Inplace replacement of %20 etc with single ASCII character.
  u_char *r = str, *w = str;
  int hexval;
  while (*r) {
    if (*r == '%') {
      hexval = 0;
      r++;
      if (isdigit(*r)) hexval = 16 * (*r - '0');
      else if (*r >= 'a' && *r <= 'f') hexval = 16 * (*r - 'a');
      else if (*r >= 'A' && *r <= 'F') hexval = 16 * (*r - 'A');
      r++;
      if (isdigit(*r)) hexval += (*r - '0');
      else if (*r >= 'a' && *r <= 'f') hexval += (*r - 'a');
      else if (*r >= 'A' && *r <= 'F') hexval += (*r - 'A');
      r++;
      *w++ = (u_char)hexval;
    }
    else *w++ = *r++;
  }
  *w = 0;
}




static int test_for_error_in_substitute(u_char *arg1, u_char *arg2, u_char *arg3, u_char *expected, BOOL lower,
					BOOL check_for_word_boundaries) {
  int c;
  u_char copy[100],  map[256];
  for (c = 0; c < 256; c++) { map[c] = (u_char)tolower(c); }
  strcpy((char *)copy, (char *)arg1);
  if (0) printf(" -----  '%s' -  '%s'\n", arg2, arg3);
  if (lower) c = substitute(copy, arg2, arg3, map, check_for_word_boundaries);
  else c = substitute(copy, arg2, arg3, NULL, check_for_word_boundaries);

  if (strcmp((char *)copy, (char *)expected)) {
    if (check_for_word_boundaries)
      printf("Substitution error for (%s, %s, %s) with word boundary check:  Got '%s', Expected '%s'\n", arg1, arg2, arg3, copy, expected);
    else 
      printf("Substitution error for (%s, %s, %s) with no wb check:  Got '%s', Expected '%s'\n", arg1, arg2, arg3, copy, expected);
    return 1;
  }
  return 0;
}


static int test_tailstr() {
  int errs = 0, verbose = 0;
  u_char *ans;

  ans = tailstr((u_char *)"A", (u_char *)"A");
  if (ans == NULL) errs++;
  ans = tailstr((u_char *)"ASIA", (u_char *)"A");
  if (ans == NULL) errs++;
  ans = tailstr((u_char *)"ASIAD", (u_char *)"A");
  if (ans != NULL) errs++;
  ans = tailstr((u_char *)"Now is the time for all good men to come to the aid of the party.", (u_char *)" party.");
  if (ans == NULL) errs++;
  if (errs) {
    if (verbose) printf("Test_tailstr:  %d errors. Test failed.\n", errs);
    return -73;
  }
  return 0;
}




int test_substitute() {
  u_char s[100];
  int errs = 0, verbose = 0;
  if (verbose) setvbuf(stdout, NULL, _IONBF, 0);

  strcpy((char *)s, "LUNACY");
  errs += test_for_error_in_substitute(s, (u_char *)"Y", (u_char *)"E", (u_char *)"LUNACE", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"CY", (u_char *)"RE", (u_char *)"LUNARE", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"ACY", (u_char *)"DON", (u_char *)"LUNDON", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"NACY", (u_char *)"NDON", (u_char *)"LUNDON", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"UNACY", (u_char *)"ONDON", (u_char *)"LONDON", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"LUNACY", (u_char *)"PARKED", (u_char *)"PARKED", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"LUNACY", (u_char *)"TWO", (u_char *)"TWO", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"LUNACY", (u_char *)"", (u_char *)"", FALSE, FALSE);

  strcpy((char *)s, "Now is the time for all good men to come to the aid of the party.");
  errs += test_for_error_in_substitute(s, (u_char *)"all", (u_char *)"the", (u_char *)"Now is the time for the good men to come to the aid of the party.", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"all ", (u_char *)"my ", (u_char *)"Now is the time for my good men to come to the aid of the party.", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"all ", (u_char *)"", (u_char *)"Now is the time for good men to come to the aid of the party.", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"N", (u_char *)"", (u_char *)"ow is the time for all good men to come to the aid of the party.", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"y.", (u_char *)"y!", (u_char *)"Now is the time for all good men to come to the aid of the party!", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"y.", (u_char *)".", (u_char *)"Now is the time for all good men to come to the aid of the part.", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, s, (u_char *)"my", (u_char *)"my", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"to ", (u_char *)"xx ", (u_char *)"Now is the time for all good men xx come xx the aid of the party.", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"t", (u_char *)"Y", (u_char *)"Now is Yhe Yime for all good men Yo come Yo Yhe aid of Yhe parYy.", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"now is", (u_char *)"it's", (u_char *)"it's the time for all good men to come to the aid of the party.", TRUE, FALSE);

  strcpy((char *)s, "party");
  errs += test_for_error_in_substitute(s, (u_char *)"art", (u_char *)"", (u_char *)"py", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"art", (u_char *)"", (u_char *)"party", FALSE, TRUE);
	
  strcpy((char *)s, "art party");
  errs += test_for_error_in_substitute(s, (u_char *)"art", (u_char *)"", (u_char *)"py", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"art", (u_char *)"", (u_char *)"party", FALSE, TRUE);
  strcpy((char *)s, "art party art");
  errs += test_for_error_in_substitute(s, (u_char *)"art", (u_char *)"", (u_char *)"py", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"art", (u_char *)"", (u_char *)"party", FALSE, TRUE);

  strcpy((char *)s, "tartartart");
  errs += test_for_error_in_substitute(s, (u_char *)"art", (u_char *)"", (u_char *)"t", FALSE, FALSE);
  errs += test_for_error_in_substitute(s, (u_char *)"art", (u_char *)"", (u_char *)"tartartart", FALSE, TRUE);

  if (errs) {
    if (verbose) printf("Test_substitute:  %d errors.  Test failed.\n", errs); 
    return -74;
  }
  return test_tailstr();
}




////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions specific to QBASHER (but whose definition doesn't depend on anything defined elsewhere)
////////////////////////////////////////////////////////////////////////////////////////////////////////


void clean_query(u_char *str) {
  // Remove queries and apostrophes
  // Lower case all uppercase letters.
  // Null-out the newline at the end of str
  // Strip leading, trailing and multiple blanks
  u_char *r = str, *w = str, previous = ' ';
  while (*r >= ' ') {
    if (*r == ',') *w++ = ' ';
    else if (*r == '\'' && (*(r + 1) == 's' || *(r + 1) == 'S')) {
      *w++ = 's';
      r++;
    }
    else if (*r >= 'A' && *r <= 'Z') *w++ = tolower(*r);
    else *w++ = *r;
    r++;
  }
  *w = 0;  // NUll terminate the bit we're interested in.

  r = str;  w = str;
  while (*r) {
    if (*r == ' ') {
      if (previous == ' ') r++;
      else {
	previous = *r;
	*w++ = *r++;
      }
    }
    else {
      previous = *r;
      *w++ = *r++;
    }
  }
  *w = 0;
  while (*(w - 1) == ' ') *--w = 0;
  //printf("CLEANED UP: '%s'", str);
}




u_char *extract_result_at_rank(u_char **returned_results, double *scores, int rank, int *length, double *score) {
  // Just a convenience for C# access.
  // **** Caller's responsibility to ensure that returned-results has an element corresponding to rank
  *score = scores[rank];
  *length = (int)strlen((char *)returned_results[rank]);
  fflush(stdout);
  return returned_results[rank];
}


void vocabfile_entry_packer(byte *entry_start, size_t termflen, byte *term, u_ll occurrence_count, byte qidf, u_ll payload) {
  // Conforms to the 1.5 index format
  // Pack the last four values into termflen + 12 bytes of storage referenced by entry_start:
  //  termlen bytes for the term, including null termination
  //  5 byte for the occurrence count
  //  1 byte for quantized IDF
  //  6 bytes of payload which may be an offset into the .if file, or 5 byte docnum + 1 byte wpos

  u_int *uip;
  u_ll *ullp, occbyte;
  memset(entry_start, 0, termflen);  // Avoid remnants from previous terms.
  strncpy((char *)entry_start, (char *)term, termflen);
  entry_start[termflen - 1] = 0;
  uip = (u_int *)(entry_start + termflen);
  ullp = (u_ll *)(entry_start + termflen + sizeof(u_int));
  occbyte = occurrence_count & 0xFF;   // Save the least significant byte of the occurrence count
  *uip = (u_int)(occurrence_count >> 8);  // Store the four more significant bytes
  *ullp = occbyte;  // Store the saved occurrence count byte
  *ullp <<= 8;
  *ullp |= qidf;    // Or in the quantized IDF
  *ullp <<= 48;      
  *ullp |= (payload & 0xFFFFFFFFFFFF);  // Or in the payload

}


void vocabfile_entry_unpacker(byte *entry_start, size_t termflen, u_ll *occurrence_count, byte *qidf, u_ll *payload) {
  // Conforms to the 1.5 index format
  // Extract the last four values from the termflen + 12 byte .vocab record referenced by entry_start.  It's assumed that
  // the entry starts with a null-terminated term occupying the first termlen bytes.  There's 
  //  5 byte for the occurrence count
  //  1 byte for quantized IDF
  //  6 bytes of payload which may be an offset into the .if file, or 5 byte docnum + 1 byte wpos

  u_int *uip;
  u_ll *ullp, occbyte, ull;
  uip = (u_int *)(entry_start + termflen);
  ullp = (u_ll *)(entry_start + termflen + sizeof(u_int));
  *occurrence_count = *uip;  // Get four more significant bytes of occurrence_count
  *occurrence_count <<= 8;    // and shift to make room for LSB later
  ull = *ullp;    // Have to do this in case entry_start is in read-only memory
  *payload = (ull & 0xFFFFFFFFFFFF);  // 48 bits
  ull >>= 48;
  *qidf = (ull & 0XFF);
  ull >>= 8;
  occbyte = (ull & 0XFF);
  *occurrence_count |= occbyte;
}


void vocabfile_test_pack_unpack(size_t termflen) {
  byte *entry = NULL;
  u_ll occurrence_count, payload;
  byte qidf;
  entry = (byte *) malloc(termflen + 13);
  memset(entry, 0, termflen + 13);
  vocabfile_entry_packer(entry, 16, (byte *)"marquisdesade", 0X1122334455ULL, 0X60, 0X0605040302010ULL);
  if (strcmp((char *)entry, "marquisdesade")) {
    printf("Error in vocabfile_test_pack_unpack() - string wrong (%s)\n", entry);
    exit(1);   // Only when debugging
  }
  if (entry[termflen + 12]) {
    printf("Error in vocabfile_test_pack_unpack() - guard byte overwritten (%d)\n", (int)entry[termflen + 12]);
    exit(1);   // Only when debugging
  }

  vocabfile_entry_unpacker(entry, termflen, &occurrence_count, &qidf, &payload);
  if (occurrence_count != 0X1122334455ULL || qidf != 0X60 || payload != 0X0605040302010ULL) {
    printf("Error in vocabfile_test_pack_unpack() - incorrect values read back: %llX, %X, %llX\n", 
	   occurrence_count, (int)qidf, payload);
    exit(1);   // Only when debugging
  }
  printf("Test of vocab file entry pack and unpack passed.\n");
  free(entry);
}

u_int quantized_idf(double N, double n, u_int bit_mask) {
  // Given the number of documents N and the number of them containing a term,
  // calculate idf = log(N/n) and then quantize it to fit within the bit_mask
  u_int rslt = 0;
  double idf, max_poss_idf, numer = (double)(bit_mask);

  if (n > N || bit_mask < 1 || N <= 1 || n < 1) return 0;
  idf = log(N / n);
  max_poss_idf = log(N);   // Inefficient to re-compute every time.
  rslt = ((u_int)(floor((idf * numer / max_poss_idf) + 0.5))) & bit_mask;

  if (0) {
    printf("     idf = %.4f  mpi = %.4f  numer = %.4f\n", idf, max_poss_idf, numer);
    printf("  %.0f  %.0f  %X -->  %X\n", N, n, bit_mask, rslt);
  }
  return rslt;
}

double get_idf_from_quantized(double N, u_int bit_mask, u_int qidf) {
  double idf, max_poss_idf, dqidf;
  dqidf = (double)(qidf & bit_mask);
  max_poss_idf = log(N);
  idf = dqidf * (max_poss_idf / (double)bit_mask);
  return idf;
}


void test_quantized_idf() {
  u_int rez, errz = 0;
  double idf, max_poss_idf;

  max_poss_idf = log(1000000.0);

  rez = quantized_idf(1000000, 1, 0x1);
  if (rez != 1) errz++;
  rez = quantized_idf(1000000, 1, 0xFF);
  idf = get_idf_from_quantized(1000000.0, 0xFF, rez);
  if (fabs(idf - max_poss_idf) > 0.01) {
    printf("  test_quantized_idf(): inaccuracy:  %.4f v. %.4f\n", idf, max_poss_idf);
    errz++;
  }
  if (rez != 0xFF) errz++;
  rez = quantized_idf(1000000, 1, 0xFFFFFFFF);
  if (rez != 0xFFFFFFFF) errz++;

  rez = quantized_idf(1000000, 1000000, 0x1);
  if (rez != 0) errz++;
  rez = quantized_idf(1000000, 1000000, 0xFF);
  idf = get_idf_from_quantized(1000000.0, 0xFF, rez);
  if (fabs(idf - 0.0) > 0.01) {
    printf("  test_quantized_idf(): inaccuracy:  %.4f v. %.4f\n", idf, 0.0);
    errz++;
  }
  if (rez != 0) errz++;
  rez = quantized_idf(1000000, 1000000, 0xFFFFFFFF);
  if (rez != 0) errz++;

  rez = quantized_idf(1000000, 1000, 0xFF);
  if (rez != 0X7F && rez != 0X80) errz++;
  idf = get_idf_from_quantized(1000000.0, 0xFF, rez);
  if (fabs(idf - 6.907755) > 0.01) {
    printf("  test_quantized_idf(): inaccuracy:  %.4f v. %.4f\n", idf, 6.907755);
  }
  rez = quantized_idf(1000000, 1000, 0xFFFFFFFF);
  if (rez != 0x7FFFFFFF && rez != 0x80000000) errz++;

  printf("Test_quantized_idf:  %d errs\n", errz);
  if (errz) exit(1);   // Only when debugging
}



int count_one_bits_ull(unsigned long long x) {  // Just an adjunct to testing the above
  int cnt = 0;
  while (x) {
    if (x & 1) cnt++;
    x >>= 1;
  }
  return cnt;
}




int count_one_bits_u(unsigned int x) {
  int cnt = 0;
  while (x) {
    if (x & 1) cnt++;
    x >>= 1;
  }
  return cnt;
}



int count_ones_b(byte b) {
  // Return a count of the number of one bits in byte b.
  int count = 0;
  while (b) {
    if (b & 1) count++;
    b >>= 1;
  }
  return count;
}



void test_count_ones_b() {
  byte b = 0;
  if (count_ones_b(b = 0) != 0
      || count_ones_b(b = 1) != 1
      || count_ones_b(b = 0x80) != 1
      || count_ones_b(b = 3) != 2
      || count_ones_b(b = 0xFF) != 8
      || count_ones_b(b = 0x42) != 2) {
    printf("Error in count_ones_b(%X)\n", b);
    exit(1);   // Only when debugging
  }
  printf("Test of count_ones_b() passed.\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////
//             Functions for calculating Bloom filter signatures                             //
///////////////////////////////////////////////////////////////////////////////////////////////


// This function calculates a Bloom signature from the first byte of each word in a string.
unsigned long long calculate_signature_from_first_letters(u_char *str, int bits)  {
  // For the character at the start of each word in str
  // calculate a bit position in the range 0 - bits minus 1,
  // by taking the value of the character modulo bits.
  // Note:  Should use unicode value derived from UTF-8.  For the moment just use bytes
  // Use modulo rather than bit mask so we can easily vary to any number of bits we want.
  // We probably should use something more sophisticated so that the one bits are uniformly
  // distributed. 

  // There is a similar function in the query processor, whose logic must be perfectly
  // compatible with this one.

  unsigned long long signature = 0, bitpat = 0;
  int bit, verbose = 0;
  u_char *p = str, *bafter;
  u_int unicode;
  byte first_byte;

  if (verbose) printf("Calculating %d bit signature for (%s)\n", bits, str);
  if (bits <= 0 || bits > 8 * sizeof(unsigned long long) || p == NULL) return 0ULL;  // Error!  -------------->
  while (*p >= ' ') {   // Terminate scanning on NULL or any control character including tab

    while (*p >= ' ') {
      // Skip over successive non tokens	
      if (*p & 0x80) {
	unicode = utf8_getchar(p, &bafter, TRUE);
	if (!unicode_ispunct(unicode)) break;  // Assume (falsely) that all non-punctuation unicode is indexable
	else p = bafter;
      }
      else if (ascii_non_tokens[*p]) p++;
      else break;  // This is an indexable ASCII character.	
    }


    if (*p < ' ') {
      if (verbose) printf("SIG1 %llx\n", signature);
      return signature;
    }

    first_byte = *p;
    if (!(first_byte & 0x80)) first_byte = tolower(first_byte);  // If ASCII, do ASCII lower-casing.
    bit = (int)(first_byte) % bits;  // It will only be based on the first byte of a UTF-8 sequence
    bitpat = 1ULL << bit;
    if (verbose) printf("Setting bit %d:  %llX\n", bit, bitpat);
    signature |= bitpat;

    while (*p > ' ') {  // Skip over indexable characters
      if (*p & 0x80) {
	unicode = utf8_getchar(p, &bafter, TRUE);
	if (0) printf("Blow me down(%s)!  unicode = %u  skip = %zu\n",
		      p, unicode, bafter - p);
	if (unicode_ispunct(unicode)) break;  // Non-indexable UTF-8 character
	else p = bafter;
      }
      else if (!ascii_non_tokens[*p]) p++;
      else break;  // This is a non-indexable ASCII character.
    }
  }
  if (verbose) printf("SIG2 %llx\n", signature);
  return signature;
}


