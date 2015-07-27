/*

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

#include "ts/ink_assert.h"
#include "ts/ink_defs.h"
#include "ts/Regex.h"

typedef struct {
  char subject[100];
  bool match;
} subject_match_t;

typedef struct {
  char regex[100];
  subject_match_t tests[4];
} test_t;

static const test_t test_data[] = {
  {"^foo", {{"foo", true}, {"bar", false}, {"foobar", true}, {"foobarbaz", true}}},
  {"foo$", {{"foo", true}, {"bar", false}, {"foobar", false}, {"foobarbaz", false}}},
};

static void
test_basic()
{
  for (unsigned int i = 0; i < countof(test_data); i++) {
    Regex r;

    printf("Regex: %s\n", test_data[i].regex);
    r.compile(test_data[i].regex);
    for (unsigned int j = 0; j < countof(test_data[i].tests); j++) {
      printf("Subject: %s Result: %s\n", test_data[i].tests[j].subject, test_data[i].tests[j].match ? "true" : "false");
      ink_assert(r.exec(test_data[i].tests[j].subject) == test_data[i].tests[j].match);
    }
  }
}

int
main(int /* argc ATS_UNUSED */, char ** /* argv ATS_UNUSED */)
{
  test_basic();
  printf("test_Regex PASSED\n");
}
