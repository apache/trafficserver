/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/*************************** -*- Mod: C++ -*- *********************

  Hist.cc
******************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "Hist.h"

/* someday convert this file to real C++ */

void
histogram_new(histogram * h, char *units, char *prefix, int nintervals, double minbin, double maxbin)
{
  int j;

  h->prefix = strdup(prefix);
  h->units = strdup(units);
  h->nintervals = nintervals;
  h->minbin = minbin;
  h->maxbin = maxbin;
  h->minval = maxbin + 1.0;
  h->maxval = minbin - 1.0;
  h->npoints = 0;
  h->total = 0.0;
  if ((h->bins = (int *) malloc((nintervals + 2) * sizeof(int))) == NULL) {
    fprintf(stderr, "Error: could not allocate memory for histogram.");
    exit(-1);
  }
  for (j = 0; j < nintervals + 2; j++) {
    h->bins[j] = 0;
  }
}

void
histogram_point(histogram * h, double val)
{
  int index;
  if (h->npoints == 0) {
    h->minval = h->maxval = val;
  } else {
    if (val < h->minval)
      h->minval = val;
    if (val > h->maxval)
      h->maxval = val;
  }
  h->npoints++;
  h->total += val;
  index = (int) ((double) (h->nintervals * (val - h->minbin))
                 / (h->maxbin - h->minbin));
  index++;
  if (index > h->nintervals + 1)
    index = h->nintervals + 1;
  if (index < 0)
    index = 0;
  h->bins[index]++;
}

void
histogram_display(histogram * h)
{
  int percentile = 90, found_percentile = (-1);
  int screen = 40;
  int j, k, sum, maxcount, bar, last_nonzero_interval = 0;
  char c = '*';
  double low, high;
  maxcount = 0;

  for (j = 0; j < h->nintervals + 2; j++) {
    if (h->bins[j] > maxcount)
      maxcount = h->bins[j];
    if (h->bins[j] > 0)
      last_nonzero_interval = j;
  }
  sum = 0;
  for (j = 0; j < h->nintervals + 2; j++) {
    sum += h->bins[j];
    low = h->minbin + (j - 1) * (h->maxbin - h->minbin) / (double) h->nintervals;
    high = h->minbin + (j) * (h->maxbin - h->minbin) / (double) h->nintervals;
    if (maxcount == 0) {
      bar = 0;
    } else {
      bar = (screen * h->bins[j]) / maxcount;
    }
    if ((bar == 0) && (h->bins[j] > 0))
      bar = 1;
    if (j <= last_nonzero_interval + 1) {
      if (j == 0) {
        printf("%s %4d [ %7s : %7.2f ]: %8d ", h->prefix, j, "_", high, h->bins[j]);
      } else if ((j == last_nonzero_interval + 1) || (j == h->nintervals + 2 - 1)) {
        printf("%s %4d [ %7.2f : %7s ]: %8d ", h->prefix, j, low, "_", h->bins[j]);
      } else {
        printf("%s %4d [ %7.2f : %7.2f ]: %8d ", h->prefix, j, low, high, h->bins[j]);
      }
      for (k = 0; k < bar; k++) {
        printf("%c", c);
      }
      printf("\n");
    }
    if ((found_percentile<0) && (sum>= (0.01 * percentile) * h->npoints)) {
      found_percentile = j;
#ifdef different_mark_above_percentile
      c = '+';
#endif
    }
  }
  printf("------------------------------\n");
  printf("#points = %d\n", sum);
  if (h->npoints > 0) {
    printf("minimum value = %g\n", h->minval);
    printf("maximum value = %g\n", h->maxval);
    printf("average value = %g\n", h->total / (double) h->npoints);
    j = found_percentile;
    low = h->minbin + (j - 1) * (h->maxbin - h->minbin) / (double) h->nintervals;
    high = h->minbin + (j) * (h->maxbin - h->minbin) / (double) h->nintervals;
    if (high > h->maxbin) {
      printf("%d percentile mark greater than %g %s\n", percentile, low, h->units);
    } else {
      printf("%d percentile mark between %g and %g %s\n", percentile, low, high, h->units);
    }
    printf("\n");
  }
}

#ifdef MAIN

main(int argc, char *argv[])
{
  int npoints = 1000, nintervals = 15;
  histogram H;
  int offset = 0;
  int j;
  double val;

  srandom(getpid());
  if (argc > 1)
    npoints = atoi(argv[1]);
  if (argc > 2)
    nintervals = atoi(argv[2]);
  printf("Testing with %d points and %d intervals\n", npoints, nintervals);

  histogram_new(&H, nintervals, 0.0, 1000.0 * nintervals);
  /* histogram_display(&H); */
  for (j = 0; j < npoints; j++) {
    val = random() % (1000 * nintervals);
#if 0
    if ((random() % 5) == 0)
      val = 0.0;
    if ((random() % 5) == 0)
      val = 1000.0 * nintervals;
#endif
    histogram_point(&H, val);
  }
  histogram_display(&H);
}

#endif
