// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

//

int generate_latlong_words(double lat, double lon, double width_in_km,
			   char *special_words, int max_wd_len, int print_tile_sizes);

int generate_latlong_words2(double lat, double lon, double width_in_km,
			   char *special_words, int max_wd_len, int
			    print_tile_sizes);

double greatCircleDistance(double lat0, double long0, double lat1, double long1);

double distance_between(char *doc, double latit, double longit);

double geoScore(double lat0, double long0, double lat1, double long1);

void testGCD();

