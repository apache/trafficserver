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

#include "ts/ink_hrtime.h"
#include <sys/time.h>

void
test()
{
  ink_hrtime t = ink_get_hrtime();
  int i        = 1000000;
  timespec ts;
  while (i--) {
    clock_gettime(CLOCK_REALTIME, &ts);
  }
  ink_hrtime t2 = ink_get_hrtime();
  printf("time for clock_gettime %" PRId64 " nsecs\n", (t2 - t) / 1000);

  t = ink_get_hrtime();
  i = 1000000;
  while (i--) {
    ink_get_hrtime();
  }
  t2 = ink_get_hrtime();
  printf("time for clock_gettime %" PRId64 " nsecs\n", (t2 - t) / 1000);
}
