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

#include "HttpNet.h"
#include <iostream.h>
#include "ts/ink_assert.h"
#include "DebugStream.h"
#include "IOBuffer.h"
#include "Main.h"
#include "Event.h"
#include "ProtectedQueue.h"
#include "Cluster.h"
#include "HttpConfig.h"
#include "HttpTransact.h"

HttpNetProcessor httpNetProcessor;
HttpConfigParams httpConfigParams;
DebugStream debug_out("debug.txt", 1, 1, "DebugStreamLevels.txt", "http.stops");

typedef struct {
  char *accept;
  char *field;
} AcceptPair;

void
test()
{
  float q;

  ///// Accept /////

  static AcceptPair a1[] = {{"*", "text/html"}, {"image/gif, *; q=.9, text/*; q=.2", "text/html"}, {NULL, NULL}};

  fprintf(stderr, "\n*** Testing Accept matching ***\n");
  for (int i = 0; a1[i].accept; i++) {
    q = HttpTransact::CalcQualityOfAcceptMatch(a1[i].accept, a1[i].field);
    fprintf(stderr, "Accept(\"%s\",\"%s\") ==> %g\n", a1[i].accept, a1[i].field, q);
  }

  ///// Accept-Charset /////

  static AcceptPair a2[] = {{"*", "us-ascii"}, {NULL, NULL}};

  fprintf(stderr, "\n*** Testing Accept-Charset matching ***\n");
  for (int i = 0; a2[i].accept; i++) {
    q = HttpTransact::CalcQualityOfAcceptCharsetMatch(a2[i].accept, a2[i].field);
    fprintf(stderr, "Accept-Charset(\"%s\",\"%s\") ==> %g\n", a2[i].accept, a2[i].field, q);
  }

  ///// Accept-Encoding /////

  static AcceptPair a3[] = {{"*", "gzip"}, {NULL, NULL}};

  fprintf(stderr, "\n*** Testing Accept-Encoding matching ***\n");
  for (int i = 0; a3[i].accept; i++) {
    q = HttpTransact::CalcQualityOfAcceptEncodingMatch(a3[i].accept, a3[i].field);
    fprintf(stderr, "Accept-Encoding(\"%s\",\"%s\") ==> %g\n", a3[i].accept, a3[i].field, q);
  }

  ///// Accept-Language /////

  static AcceptPair a4[] = {{"*", "en"},
                            {"*", ""},
                            {"fr, en", "en-ebonics"},
                            {"fr, en-ebonics", "en-ebonics"},
                            {"fr, *;q=.314, en-ebonics", "en-boston"},
                            {"fr, *;q=.314, en-ebonics", "en-ebonics-oakland"},
                            {NULL, NULL}};

  fprintf(stderr, "\n*** Testing Accept-Language matching ***\n");
  for (int i = 0; a4[i].accept; i++) {
    q = HttpTransact::CalcQualityOfAcceptLanguageMatch(a4[i].accept, a4[i].field);
    fprintf(stderr, "Accept-Language(\"%s\",\"%s\") ==> %g\n", a4[i].accept, a4[i].field, q);
  }
}
