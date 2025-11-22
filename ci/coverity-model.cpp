/**
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

// This is for PCRE2, where the offsets vector is considered uninitialized, but it's an
//  output vector only (input doesn't matter).
extern "C" {
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

int
pcre2_match(const pcre2_code *code, PCRE2_SPTR subject, PCRE2_SIZE length, PCRE2_SIZE startoffset, uint32_t options,
            pcre2_match_data *match_data, pcre2_match_context *mcontext)
{
  __coverity_panic__();
}


// Indicate that our abort function really is that ...
void
ink_abort(const char *message_format, ...)
{
  __coverity_panic__();
}

// Teach Coverity that this aborts in all compilation modes.
void _TSReleaseAssert(const char* txt, const char* f, int l)
{
  __coverity_panic__();
}

} /* extern "C" */

// Teach Coverity that the my_exit() in logstats.cc exits ...
struct ExitStatus {
};

void
my_exit(const ExitStatus &status)
{
  __coverity_panic__();
}
