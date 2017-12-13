// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

//
//

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>

#include "latlong.h"
#include "../shared/utility_nodeps.h"

static int geoDebug = 0;
static double earthRadius = 6371.0;  // Km
static double pi = 3.14159265358979323;



int generate_latlong_words(double lat, double lon, double width_in_km,
			   char *special_words, int max_wd_len, int
			   print_tile_sizes) {
  // The Earth's latitude and longitude ranges are divided up into
  // strips in each dimension.  The number of horizontal strips is
  // easily determined by dividing the half earth circumference by the
  // target width in km. In the horizontal dimension the width of a
  // strip is constant when measured in degrees of longitude but
  // varies in kilometres according to distance from the equator.
  //
  // The purpose of the latlong words is to allow us to filter candidate
  // results to a manageable subset but we need to avoid excluding results
  // which are within the specified distance.  In this model the latter
  // is unachievable toward the poles.  However, 
  // because virtually all major cities are at latitudes less than 60
  // degrees, (Helsinki is at 60) we choose the number of tiles such that
  // the width of a longitude strip is the width requested.  This means
  // that the strips are twice as wide as requested at the equator.
  //
  // In this sceme, the location represented by (lon, lat) in
  // decimal degrees generates 6 special words, three indicating
  // adjacent horizontal strips (the one containing lat, lon and both of its neighbors)
  // and three vertical strips.  If each document is indexed by all of the six words,
  // then we can guarantee that any query origin with latitude less than 60 degrees
  // will find all documents within W km.
  //
  // Each of the tile numbers is converted into a special word of the
  // form 'x$<tile_num>' (lon) or 'y$<tile_num> (lat) and these words
  // are stored in the special_words array, the three lon (X) words first,
  // then the three lats.  Return value is 6 except in case of error,
  // when it is zero.  Primary strip words are in positions 0 and 3

  double lathtdeg, lonwiddeg;
  int numXTiles, numYTiles, stripnum, rightNeighb, leftNeighb;
  double halfEarthCircumf = earthRadius * pi;
  size_t swoff = 0;

  if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return 0;

  //  ---------------- Deal with longitude ----------------------------
  numXTiles = (int)floor(earthRadius *pi * 2.0 / width_in_km * cos(60 * pi /180.0));  
  // Because cos(60) = 0.50000, numXTiles should == numYTiles
  if (0) printf("For %.3fkm, NumXTiles, NumYTiles = %d, %d\n", width_in_km, numXTiles, numYTiles);
  lonwiddeg = 360.0 / numXTiles;

  
  lon += 180.0; // Get into range 0 - 360
  if (lon >= 360.0) lon = 359.999;

  // Generate lon words
  stripnum = (int)floor(lon / lonwiddeg);  // If lon = 359.999, and ntiles = 10, then stripnum will
                                        // be 9 as it should be.
  sprintf(special_words + swoff, "x$%d", stripnum);  
  swoff += (max_wd_len + 1);
  rightNeighb = stripnum + 1;
  if (rightNeighb >= numXTiles) rightNeighb = 0;
  sprintf(special_words + swoff, "x$%d", rightNeighb);  
  swoff += (max_wd_len + 1);
  leftNeighb = stripnum - 1;
  if (leftNeighb < 0) leftNeighb = numXTiles - 1;
  sprintf(special_words + swoff, "x$%d", leftNeighb);  
  swoff += (max_wd_len + 1);

  //  ---------------- Deal with latitude ----------------------------
  numYTiles = (int)floor(halfEarthCircumf / width_in_km);
  lathtdeg = 180.0 / numYTiles;
  
  lat += 90.0;  // Get into range 0 - 180
  if (lat >= 180.0) lat = 179.999;

  // Generate lat words
  stripnum = (int)floor(lat / lathtdeg);  // If lat = 179.999, and ntiles = 10, then stripnum will
                                       // be 9 as it should be.
  sprintf(special_words + swoff, "y$%d", stripnum);  // This is the primary lat strip
  swoff += (max_wd_len + 1);
  rightNeighb = stripnum + 1;
  if (rightNeighb >= numYTiles) rightNeighb = 0;
  sprintf(special_words + swoff, "y$%d", rightNeighb);  
  swoff += (max_wd_len + 1);
  leftNeighb = stripnum - 1;
  if (leftNeighb < 0) leftNeighb = numYTiles - 1;
  sprintf(special_words + swoff, "y$%d", leftNeighb);  

  
  if (print_tile_sizes) {
    double lat, tile_wid, tile_ht;
    tile_ht = earthRadius * lathtdeg * pi / 180.0;  // It's a constant
    printf("Tile size and shape varies with latitude:\n"
	   "=========================================\n"
	   "Latitude           tile_size\n"
	   "--------           ---------\n");
    for (lat = 0.0; lat <= 90.0; lat += 10.0) {
      tile_wid = (earthRadius * 2.0 * pi * cos(lat * pi / 180.0)) / numXTiles;
      printf("+/-%4.0f    %5.1fkm X %5.1fkm\n", lat, tile_wid, tile_ht);
    }
  }
  

  return 6;
}



double greatCircleDistance(double lat0, double long0, double lat1, double long1) {

  // Calculate the GCD in kilometres between two points, assuming the earth
  // is a sphere
  double x0, y0, z0, x1, y1, z1, xdiff, ydiff, zdiff, dist, theta, gcd, deg2rad,
    halfDist, sinTheta;
  deg2rad = pi / 180.0;
  // Convert the two points into x,y,z space
  x0 = earthRadius * sin(long0 * deg2rad);
  y0 = earthRadius * cos(long0 * deg2rad);
  z0 = earthRadius * sin(lat0 * deg2rad);
  x1 = earthRadius * sin(long1 * deg2rad);
  y1 = earthRadius * cos(long1 * deg2rad);
  z1 = earthRadius * sin(lat1 * deg2rad);

  if (geoDebug) printf("greatCircleDistance(%.3f, %.3f, %.3f, %.3f)\n",
		       lat0, long0, lat1, long1);

  // Calculate the straight line distance between the two points
  xdiff = x1 - x0;
  ydiff = y1 - y0;
  zdiff = z1 - z0;
  if (geoDebug) printf("greatCircleDistance(). Diffs: %.3f, %.3f, %.3f\n",
		       xdiff, ydiff, zdiff);

  dist = sqrt(xdiff * xdiff + ydiff * ydiff + zdiff * zdiff);

  halfDist = dist / 2.0;
  sinTheta = halfDist /earthRadius;

  // It seems that sinTheta sometimes comes out > 1.0 which is very bizarre
  // That leads to NaNs.  An unprincipled fix:
  if (sinTheta > 1.0) sinTheta = 2.0 - sinTheta;
  
  if (geoDebug) printf("greatCircleDistance().  Dist = %.3f, earthRadius = %.3f, sinTheta = %.3f\n",
		       dist, earthRadius, sinTheta);
  // Calculate the angle subtended at the centre of the earth between the two points
  theta = 2.0 * asin(sinTheta);
  if (geoDebug) printf("greatCircleDistance().  theta = %.3f\n",
		       theta);

  // And finally the gcd
  gcd = earthRadius * theta;
  return gcd;
}


double distance_between(char *doc, double latit, double longit) {
  // Extract lat and long from column 4 of doc and compute gcd from
  // it to (latit, longit)
  char *col4, *nxt;
  double doclat, doclong;
  size_t col4_len;
  col4 = (char *)extract_field_from_record((u_char *)doc, 4,  &col4_len);
  errno = 0;
  doclat = strtod(col4, &nxt);
  if (errno) {
    printf("Error: reading document lat\n");
    return 0.0;
  }	   
  doclong = strtod(nxt, NULL);
  if (errno) {
    printf("Error: reading document long\n");
    return 0.0;
  }	   

  return greatCircleDistance(latit, longit, doclat, doclong);
}


double geoScore(double lat0, double long0, double lat1, double long1) {
  //
  double halfCircumference = earthRadius * pi;
  double gcd = greatCircleDistance(lat0, long0, lat1, long1);  // units of 100M
  double score, distFromInfinity =  halfCircumference - gcd;
  if (distFromInfinity < 0.0) distFromInfinity = 0;  // Just in case
  
  score = distFromInfinity / halfCircumference;
  score *= score * score * score;  // Make the score fall away faster
  score *= score * score * score;  // Make the score fall away faster
  return score;
}



void testGCD() {
  double gcd, score;
  geoDebug = 1; // This function is only called in the indexer.  Extra
                // output shouldn't matter
  gcd = greatCircleDistance(-37.819124, 144.968200, 37.691, -108.032);
  printf("gcd(Bodo's example) = %.3fkm\n", gcd);
  score = geoScore(-37.819124, 144.968200, 37.691, -108.032);
  printf("geoScore(Bodo's example) = %.3f\n", score);
  gcd = greatCircleDistance(-35.26768, 149.12061, -35.307, 149.134);
  printf("gcd(Home, MSCanberra) = %.3fkm\n", gcd);  
  gcd = greatCircleDistance(-35.307, 149.134, -37.822, 144.962);
  printf("gcd(MSCanberra, MSMelbourne) = %.3fkm\n", gcd);
  gcd = greatCircleDistance(-37.822, 144.962, -35.307, 149.134);
  printf("gcd(MSMelbourne, MSCanberra) = %.3fkm\n", gcd);
  gcd = greatCircleDistance(0.0, 0.0, 0.0, 180.0);
  printf("gcd: should be about 20,000km = %.3fkm\n", gcd);
  gcd = greatCircleDistance(90.0, 0.0, -90.0, 0.0);
  printf("gcd: should be about 20,000km = %.3fkm\n", gcd);
  gcd = greatCircleDistance(-35.307, 149.134, 47.615, -122.196);
  printf("gcd(MSCanberra, MSBellevue) = %.3fkm\n", gcd);
  gcd = greatCircleDistance(47.615, -122.196, -35.307, 149.134);
  printf("gcd(MSBellevue, MSCanberra) = %.3fkm\n", gcd);
}
