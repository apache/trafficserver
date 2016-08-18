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

#include <stdlib.h>
#include <string.h>

#include "ts/Arena.h"
#include "HTTP.h"
#include "MIME.h"
#include "ts/Regex.h"
#include "URL.h"
#include "HttpCompat.h"

static void
test_url()
{
  static const char *strs[] = {"http://npdev:19080/1.6664000000/4000", "http://npdev:19080/1.8666000000/4000"};
  static int nstrs          = sizeof(strs) / sizeof(strs[0]);

  int err, failed;
  URL url;
  const char *start;
  const char *end;
  int i, old_length, new_length;

  failed = 0;
  for (i = 0; i < nstrs; i++) {
    old_length = strlen(strs[i]);
    start      = strs[i];
    end        = start + old_length;

    url.create(NULL);
    err = url.parse(&start, end);
    if (err < 0) {
      failed = 1;
      break;
    }

    INK_MD5 md5;
    url.MD5_get(&md5);
    // url_MD5_get(url.m_url_impl, &md5);
    unsigned int *h = (unsigned int *)&md5;
    printf("(%s)\n", strs[i]);
    printf("%X %X %X %X\n", h[0], h[1], h[2], h[3]);

    url.destroy();
  }

  printf("*** %s ***\n", (failed ? "FAILED" : "PASSED"));
}

int
main(int argc, char *argv[])
{
  hdrtoken_init();
  url_init();
  mime_init();
  http_init();

  test_url();

  return 0;
}
