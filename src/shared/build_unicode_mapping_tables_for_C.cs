using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using System.Globalization;
//From here:
// http://stackoverflow.com/questions/1668571/how-to-generate-all-the-characters-in-the-utf-8-charset-in-net

namespace ConsoleApplication1 {
    class Program {
        // http://stackoverflow.com/questions/249087/how-do-i-remove-diacritics-accents-from-a-string-in-net
        public static string RemoveDiacritics(string text) {
            var normalizedString = text.Normalize(NormalizationForm.FormD);
            var stringBuilder = new StringBuilder();

            foreach (var c in normalizedString) {
                var unicodeCategory = CharUnicodeInfo.GetUnicodeCategory(c);
                if (unicodeCategory != UnicodeCategory.NonSpacingMark) {
                    stringBuilder.Append(c);
                }
            }

            return stringBuilder.ToString().Normalize(NormalizationForm.FormC);
        }

        static void Main(string[] args) {
            System.Net.WebClient client = new System.Net.WebClient();
            string definedCodePoints = client.DownloadString(
                                     "http://unicode.org/Public/UNIDATA/UnicodeData.txt");
            System.IO.StringReader reader = new System.IO.StringReader(definedCodePoints);
            System.Text.UTF8Encoding encoder = new System.Text.UTF8Encoding();
            const int maxCodePoint = 65535;
            StringBuilder diacriticsRemoval = new StringBuilder();

            int toLowerCount = 0;
            int diacriticRemovalCount = 0;
            using (StreamWriter outputFile = new StreamWriter(@"c:/Users/bodob/Desktop/projects/diacriticConversion/toLower.txt")) {
                while (true) {
                    string line = reader.ReadLine();
                    if (line == null) break;
                    int codePoint = Convert.ToInt32(line.Substring(0, line.IndexOf(";")), 16);
                    if (codePoint <= maxCodePoint) {
                        if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
                            //surrogate boundary; not valid codePoint, but listed in the document
                        } else {
                            string utf16 = char.ConvertFromUtf32(codePoint);
                            byte[] utf8 = encoder.GetBytes(utf16);
                            //string utf8string = utf8.ToString();
                            string utf8string = System.Text.Encoding.UTF8.GetString(utf8, 0, utf8.Length);
                            //string lowerCased = utf16.ToLowerInvariant();
                            string lowerCased = utf8string.ToLowerInvariant();
                            //if (utf16 != lowerCased) {
                            if (utf8string != lowerCased) {
                                int lowerCasedCodePoint = char.ConvertToUtf32(lowerCased, 0);
                                if (lowerCasedCodePoint <= maxCodePoint) {
                                    //string outLine = "[" + codePoint.ToString() + "] -> [" + lowerCasedCodePoint.ToString() + "]\t" + utf16 + " -> " + lowerCased;
                                    //string outLine = "map_unicode_to_lower[" + codePoint.ToString() + "] = " + lowerCasedCodePoint.ToString() + ";  // " + utf16 + " -> " + lowerCased;
                                    string outLine = "map_unicode_to_lower[" + codePoint.ToString() + "] = " + lowerCasedCodePoint.ToString() + ";  // " + utf8string + " -> " + lowerCased;
                                    Console.WriteLine(outLine);
                                    outputFile.WriteLine(outLine);
                                    toLowerCount++;
                                }
                            }
                            string diacriticRemoved = RemoveDiacritics(utf8string);
                            if (utf8string != diacriticRemoved) {
                                try {
                                    int diacriticRemovedCodePoint = char.ConvertToUtf32(diacriticRemoved, 0);
                                    if (diacriticRemovedCodePoint <= maxCodePoint) {
                                        string outLine = "map_unicode_to_unaccented[" + codePoint.ToString() + "] = " + diacriticRemovedCodePoint.ToString() + ";  // " + utf8string + " -> " + diacriticRemoved;
                                        Console.WriteLine(outLine);
                                        //outputFile.WriteLine(outLine);
                                        diacriticsRemoval.Append(outLine + "\n");
                                        diacriticRemovalCount++;
                                    }
                                } catch {
                                }
                            }
                        }
                    }
                }
            }

            reader = new System.IO.StringReader(definedCodePoints);
            using (StreamWriter outputFile = new StreamWriter(@"c:/Users/bodob/Desktop/projects/diacriticConversion/diacriticsRemoved.txt")) {
                outputFile.WriteLine(diacriticsRemoval.ToString());
                /*
                while (true) {
                    string line = reader.ReadLine();
                    if (line == null) break;
                    int codePoint = Convert.ToInt32(line.Substring(0, line.IndexOf(";")), 16);
                    if (codePoint <= maxCodePoint) {
                        if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
                            //surrogate boundary; not valid codePoint, but listed in the document
                        } else {
                            string utf16 = char.ConvertFromUtf32(codePoint);
                            byte[] utf8 = encoder.GetBytes(utf16);
                            string utf8string = System.Text.Encoding.UTF8.GetString(utf8, 0, utf8.Length);
                            string diacriticRemoved = RemoveDiacritics(utf8string);
                            if (utf8string != diacriticRemoved) {
                                int diacriticRemovedCodePoint = -1;
                                try {
                                    diacriticRemovedCodePoint = char.ConvertToUtf32(diacriticRemoved, 0);
                                    if (diacriticRemovedCodePoint <= maxCodePoint) {
                                        //string outLine = "[" + codePoint.ToString() + "] -> [" + diacriticRemovedCodePoint.ToString() + "]\t" + utf16 + " -> " + diacriticRemoved;
                                        string outLine = "map_unicode_to_unaccented[" + codePoint.ToString() + "] = " + diacriticRemovedCodePoint.ToString() + ";  // " + utf8string + " -> " + diacriticRemoved;
                                        Console.WriteLine(outLine);
                                        outputFile.WriteLine(outLine);
                                        diacriticRemovalCount++;
                                    }
                                } catch {
                                }
                            }
                        }
                    }
                }
                */
            }

            Console.WriteLine("Lower cased: " + toLowerCount.ToString());
            Console.WriteLine("Diacritics removed: " + diacriticRemovalCount.ToString());
            Console.ReadLine();
        }
    }
}

