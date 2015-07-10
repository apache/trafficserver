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

/****************************************************************************

   HdrTest.cc

   Description:
       Unit test code for sanity checking the header system is functioning
         properly


 ****************************************************************************/

#ifndef _HdrTest_H_
#define _HdrTest_H_

#include "ts/Regression.h"

class HdrTest
{
public:
  RegressionTest *rtest;

  HdrTest() : rtest(NULL){};
  ~HdrTest(){};

  int go(RegressionTest *t, int atype);

private:
  int test_error_page_selection();
  int test_http_hdr_print_and_copy();
  int test_parse_date();
  int test_format_date();
  int test_url();
  int test_http_parser_eos_boundary_cases();
  int test_arena();
  int test_regex();
  int test_accept_language_match();
  int test_accept_charset_match();
  int test_comma_vals();
  int test_set_comma_vals();
  int test_delete_comma_vals();
  int test_extend_comma_vals();
  int test_insert_comma_vals();
  int test_parse_comma_list();
  int test_mime();
  int test_http();
  int test_http_mutation();

  int test_http_hdr_print_and_copy_aux(int testnum, const char *req, const char *req_tgt, const char *rsp, const char *rsp_tgt);
  int test_http_hdr_null_char(int testnum, const char *req, const char *req_tgt);
  int test_http_hdr_ctl_char(int testnum, const char *req, const char *req_tgt);
  int test_http_hdr_copy_over_aux(int testnum, const char *request, const char *response);
  int test_http_aux(const char *request, const char *response);
  int test_arena_aux(Arena *arena, int len);
  void bri_box(const char *s);
  int failures_to_status(const char *testname, int nfail);
};

#endif
