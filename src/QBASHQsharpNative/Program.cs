using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Runtime.InteropServices;   // Seems to be needed for Platform Invoke  (pinvoke)

// This is a version of QBASHQsharp which uses the same subset of API calls as the OneBox version
//  -- The motivation for creating this is to make it easier to debug the DLL and the C# mechanism
//     for calling the API.  Now buildable for Release

// ********** No longer single threaded ************************


using Griff = System.Int64;   // We treat 64 bit pointers returned from the QBASHQ API as Int64s but we give them their own typename
// to indicate what's going on.  Because they're C pointers, it's unlikely to make sense to manipulate
// them within C#.  Normally they're just received from one API call and passed on to another.


namespace QBASHQsharpNative
{
    class Program
    {


        static int queryStreams = 10;
        static Task[] workers = new Task[40];
        static Boolean[] busy = new Boolean[40];
        static IntPtr QPEnv;
        static TimeSpan zeroms = TimeSpan.FromMilliseconds(0);

        /*  Not sure why these are all defined but not used.
        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern int test_sb_macros();

        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern int test_isprefixmatch();

        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern int test_isduplicate(int x);

        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern int test_substitute();

        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern int run_bagsim_tests();


        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern void print_errors();

        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern void show_mode_settings(
            Griff qoenv
            );

        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern void free_options_memory(
            Griff qoenv
            );

        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern void warm_up_indexes(
            Griff qoenv, Griff ixenv
            );

        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern int pinvoke_test(
            int i, Boolean negate, ref int r2
            );

        [DllImport(@"QBASHQ-LIB.dll", EntryPoint = "load_query_processing_environment")]
        [return: MarshalAs(UnmanagedType.I8)]
        public static extern Griff lqpe();

        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern int finalize_query_processing_environment(Griff qoenv, Boolean verbose);

        [DllImport(@"QBASHQ-LIB.dll", EntryPoint = "assign_one_arg")]
        public static extern int assign_one_arg(Griff qoenv, byte[] arg_equals_val, Boolean initialising, Boolean enforce_limits);

        [DllImport(@"QBASHQ-LIB.dll")]
        [return: MarshalAs(UnmanagedType.I8)]
        public static extern Griff load_indexes(Griff qoenv, Boolean verbose, Boolean run_tests, ref int error_code);

        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern int handle_one_query(Griff ixenv, Griff qoenv, byte[] query_string, byte[] option_string,
           out Griff returned_results, out Griff corresponding_scores);

        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern IntPtr extract_result_at_rank(Griff returned_results, Griff corresponding_scores, int rank, out int length, out double score);

        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern void free_results_memory(ref Griff returned_results, ref Griff corresponding_scores, int how_many_results);

        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern void present_results(Griff qoenv, byte[] query_string, byte[] option_string, byte[] label,
           Griff returned_results, Griff corresponding_scores, int how_many_results, Griff start);
        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern void unload_indexes(ref Griff ixenv);

        [DllImport(@"QBASHQ-LIB.dll")]
        public static extern void unload_query_processing_environment(ref Griff qoenv, Boolean report_memory);
        */

        // ---------------------------- These are the "new" ObjectStore calls ----------------------------------------

        [DllImport(@"QBASHQ-lib.dll", CallingConvention = CallingConvention.Cdecl)]
        public static extern void NativeInitialize();

        [DllImport(@"QBASHQ-lib.dll", CallingConvention = CallingConvention.Cdecl)]
        public static extern void NativeDeinitialize(IntPtr QpEnv);

        // Keep signatures simple. Don't pass bond for now (QBasher team can add that if desired)
        [DllImport(@"QBASHQ-lib.dll", CallingConvention = CallingConvention.Cdecl)]
        public static extern int NativeInitializeSharedFiles([MarshalAs(UnmanagedType.LPStr)] string filenames, out IntPtr QpEnv);

        [System.Security.SuppressUnmanagedCodeSecurityAttribute()]
        public delegate void IssueResponse([MarshalAs(UnmanagedType.LPWStr)] string response);

        public delegate void IssueResponseB(ref byte[] response, Int32 len);

        [System.Security.SuppressUnmanagedCodeSecurityAttribute()]
        [DllImport(@"QBASHQ-lib.dll", CallingConvention = CallingConvention.Cdecl)]
        //public static extern int NativeExecuteQueryAsync(byte [] query, byte [] options, IntPtr QpEnv, IssueResponse issueResponse);
        public static extern int NativeExecuteQueryAsync([MarshalAs(UnmanagedType.LPWStr)] string pQuery, IntPtr QpEnv, IssueResponse issueResponse);
        //public static extern int NativeExecuteQueryAsync(IntPtr QpEnv, IntPtr QpEnv2, IssueResponse issueResponse);

        private static readonly object writeLock = new object();

        static void issueResponse(string QBQoutput)
        {
            bool lockAcquired = false;
            while (! lockAcquired)
            {
                Monitor.TryEnter(writeLock, ref lockAcquired);
                if (lockAcquired)
                {
                    Console.WriteLine("{0}", QBQoutput);
                    Monitor.Exit(writeLock);
                }
                else {
                    Thread.Sleep(1);
                }
            }
        }

        static void issueResponseB(ref byte[] boater, Int32 len)
        {
            System.Text.Encoding utf8_encoding = System.Text.Encoding.UTF8;
            int elts = boater.Length;
            Console.WriteLine("Boater length: {0}", elts);
            Console.Out.Flush();
            string s = new string(utf8_encoding.GetChars(boater, 0, len));
            Console.WriteLine("{0}", s);
        }




        static void processOneInputLine(string line)
        {
            Boolean queryLaunched = false, verbose = false;
            int q;
 
            while (!queryLaunched)
            {
                for (q = 0; q < queryStreams; q++)
                {
                    if (!busy[q])
                    {
                        // Start the thread.  Making the local copy of line is necessary because the lambda expression 
                        // captures by reference rather than by value.  That means the value of line may have changed
                        // between the time of capture and the time the thread runs.
                        string lline = line;
                        if (verbose) Console.WriteLine("Starting thread {0}  {1}", q, lline);
                        workers[q] = Task.Run(() =>
                        {
                            NativeExecuteQueryAsync(lline, QPEnv, issueResponse);
                        });
                        queryLaunched = true;
                        busy[q] = true;
                        break;
                    }

                }  // End of starting loop

                if (verbose) Console.WriteLine("Checking ..........");

                // Now check whether any threads have finished
                //int queriesFinished = 0;
                for (q = 0; q < queryStreams; q++)
                {
                    if (busy[q])
                    {
                        // Console.WriteLine("Thread {0} is busy", q);
                        if (workers[q].Wait(zeroms))  // Returns true if task completes within zeroms
                        {
                            if (verbose) Console.WriteLine("Thread {0} finished -----------------------------  Yippeee!", q);
                            busy[q] = false;
                            //queriesFinished++;
                        }
                    }
                }  // end of queries finished check loop
                   //if (queriesFinished == 0) Thread.Sleep(1);


            }  // end of while (!queryLaunched)

        }



        static void Main(string[] args)
        {
            Int32 error_code = 0;
            Int64 queries_processed = 0;
            System.Text.Encoding utf8_encoding = System.Text.Encoding.UTF8;
            int q;
            Stopwatch stopWatch = new Stopwatch();
 
            string line,
                ixDir = @"../test_data/wikipedia_titles", listOfPaths = "", partialQuery = "";
            string[] files = {"/QBASH.forward,", "/QBASH.if,", "/QBASH.vocab,", "/QBASH.doctable,", "/QBASH.config,", "/QBASH.query_batch",
                "QBASH.segment_rules", "QBASH.substitution_rules",
                //"/QBASH.output" 
            };
            char[] delims = {'='};

            foreach (string arg in args)
            {
                string[] argval = arg.Split(delims);
                if (argval[0] == "-index_dir") ixDir = argval[1];
                else if (argval[0] == "-object_store_files") listOfPaths = argval[1];
                else if (argval[0] == "-query_streams") queryStreams = Int32.Parse(argval[1]);
                else if (argval[0] == "-pq") partialQuery = argval[1];
                else if (argval[0] == "-help") {
                    Console.WriteLine("\nQBASHQsharpNative.exe is a simple front end to the QBASHER API, designed to help find API bugs");
                    Console.WriteLine("prior to loading the DLL into Object Store.  It supports the following options:\n");
                    Console.WriteLine("  -help                    - show this message.");
                    Console.WriteLine("  -index_dir=<directory>   - A directory potentially containing a single QBASHER index.");
                    Console.WriteLine("  -object_store_files=<comma-separated list of files> - Explicit paths to all the index files. (Instead of index_dir.)");
                    Console.WriteLine("  -query_streams=<integer> - The degree of parallelism used when running queries.");
                    Console.WriteLine("  -pq=<query_string> - A single QBASHER query string.\n");
                    Console.WriteLine("If no -pq option is given, queries are read one per line, either from stdin or from a file called QBASH.query_batch ");
                    Console.WriteLine("in index_dir or explicitly listed in -object_store_files.");
                    Console.WriteLine("\nOutput is in the format expected by the ObjectStore coproc.\n");
                    Environment.Exit(0);
                }
            }
             

            // If we haven't explicitly specified a list of object_store_files, compose the default one.
            if (listOfPaths.Equals("")) {
                foreach (string file in files)
                {
                    listOfPaths += (ixDir + file);
                }
            }

            Console.OutputEncoding = Encoding.UTF8;  // Necessary.  See http://stackoverflow.com/questions/2213541/vietnamese-character-in-net-console-application-utf-8

            Console.WriteLine("Welcome to QBASHQsharpNative.  {0} query streams.  Files are:\n{1}",queryStreams, listOfPaths);


            if (System.IntPtr.Size != 8)
            {
                Console.WriteLine("Pointer Size: {0}.  Must be 8.", System.IntPtr.Size);
                Environment.Exit(1);
            }




            error_code = NativeInitializeSharedFiles(listOfPaths, out QPEnv);
            if (error_code != 0)
            {
                Console.WriteLine("Error {0} while initializing shared files", error_code);
                Environment.Exit(1);
            }





            for (q = 0; q < queryStreams; q++) busy[q] = false;


            // Now read and process the queries, either from a a -pq string, or from a file, or from the Console

            stopWatch.Start();
            if (partialQuery != "") {
                processOneInputLine(partialQuery);
                queries_processed++;
            } else if (listOfPaths.Contains(".query_batch"))
            {
                using (StreamReader reader = File.OpenText(ixDir + "/QBASH.query_batch"))  // from a file.........................
                {
                     while ((line = reader.ReadLine()) != null)
                    {
                        processOneInputLine(line);
                        queries_processed++;
                    }  // end of loop reading queries.

                }
            } else
            {
                // Loop over queries and options read in from console
                Console.WriteLine("Please enter queries (or tab-separated queries + options) one per line.");
                Console.WriteLine();

                while ((line = Console.ReadLine()) != null)                                          // from Stdin .........................
                    {
                        processOneInputLine(line);
                        queries_processed++;
                    }  // end of loop reading queries.
            }



            // Wait for the straggler threads to finish.
            for (q = 0; q < queryStreams; q++)
                {
                    while (busy[q])
                    {
                        if (workers[q].Wait(zeroms))
                        {
                            busy[q] = false;
                            ////workers[q].Dispose();  ?????????????
                        }
                    }
                }
                stopWatch.Stop();

                Console.WriteLine("Done.  {0} queries processed.  Total elapsed milliseconds since initialisation: {1}. QPS  = {2:00}",
                    queries_processed, stopWatch.ElapsedMilliseconds, (1000.0  * (double)queries_processed / (double)stopWatch.ElapsedMilliseconds));
                Environment.Exit(0);
            }
        }
    }
