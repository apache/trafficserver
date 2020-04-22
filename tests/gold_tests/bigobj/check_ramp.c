/*
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

// Program to read standard input and verify it is ramping pattern.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

int
main(int n_arg, char const *const *arg)
{
  int64_t data_count;
  int c = 0;

  if ((n_arg != 2) || ((data_count = atoi(arg[1])) < 0)) {
    fprintf(stderr, "usage: check_ramp number-of-kilobytes\n");
    return 1;
  }

  data_count *= 1024;

  while (data_count--) {
    if (getchar() != c) {
      fprintf(stderr, "error in standard input\n");
      return 1;
    }
    if (c == 255) {
      c = 0;
    } else {
      ++c;
    }
  }

  if (getchar() != EOF) {
    fprintf(stderr, "error in standard input (too long)\n");
    return 1;
  }

  return 0;
}
