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

   test_results.h

   Description:

   
 ****************************************************************************/

#ifndef _TEST_RESULTS_H_
#define _TEST_RESULTS_H_

#include <time.h>
#include "List.h"

struct TestRunResults;

struct TestResult
{
  TestResult();
  ~TestResult();

  void start(const char *name_arg);
  void finish();
  void build_output_file_name(const char *base, const char *ext);

  char *test_case_name;
  char *output_file;
  const TestRunResults *test_run_results;

  int errors;
  int warnings;

  time_t time_start;
  time_t time_stop;

    Link<TestResult> link;
};

struct TestRunResults
{
  TestRunResults();
  ~TestRunResults();

  void start(const char *testcase_name, const char *username, const char *build_id);
  TestResult *new_result();
  void cleanup_results(bool print);

  void build_tinderbox_message_hdr(const char *status, time_t now, sio_buffer * output);
  int post_tinderbox_message(sio_buffer * hdr, sio_buffer * body);
  void send_final_tinderbox_message();

  void build_summary_html(sio_buffer * output);
  int output_summary_html();

  char *run_id_str;
  char *test_name;
  char *username;
  char *build_id;
  time_t start_time;
  bool cleanup_called;

    DLL<TestResult> results;
};

#endif
