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

// Program to output push request with ramping data.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

int
main(int n_arg, char const *const *arg)
{
  int64_t data_count;
  int c = 0, hdr_count;
  ;
  char buf[200];

  if ((n_arg != 2) || ((data_count = atoi(arg[1])) < 0)) {
    fprintf(stderr, "usage: push_request number-of-kilobytes\n");
    return 1;
  }

  data_count *= 1024;

  hdr_count = snprintf(buf, sizeof(buf),
                       "HTTP/1.1 200 OK\r\n"
                       "Content-Type: text/plain\r\n"
                       "Content-Length: %" PRId64 "\r\n"
                       "\r\n",
                       data_count);

  if (hdr_count <= 0) {
    fprintf(stderr, "INTERNAL ERROR\n");
    return 1;
  }

  if (printf("PUSH http://localhost/bigobj HTTP/1.1\r\n"
             "Content-Length: %" PRId64 "\r\n"
             "\r\n%s",
             hdr_count + data_count, buf) <= 0) {
    fprintf(stderr, "error writing standard output\n");
    return 1;
  }

  while (data_count--) {
    if (putchar(c) != c) {
      fprintf(stderr, "error writing standard output\n");
      return 1;
    }
    if (c == 255) {
      c = 0;
    } else {
      ++c;
    }
  }

  if (fflush(stdout) != 0) {
    fprintf(stderr, "error writing standard output\n");
    return 1;
  }

  return 0;
}
