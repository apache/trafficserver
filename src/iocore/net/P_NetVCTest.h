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

  P_NetVCTest.h

   Description:
       Unit test for infrastructure for VConnections implementing the
         NetVConnection interface




 ****************************************************************************/

#pragma once

#include "tscore/ink_platform.h"
#include "tscore/Regression.h"

class VIO;
class MIOBuffer;
class IOBufferReader;

enum NetVcTestType_t {
  NET_VC_TEST_ACTIVE,
  NET_VC_TEST_PASSIVE,
};

struct NVC_test_def {
  const char *test_name;

  int bytes_to_send;
  int nbytes_write;

  int bytes_to_read;
  int nbytes_read;

  int write_bytes_per;
  int timeout;

  int expected_read_term;
  int expected_write_term;
};

extern NVC_test_def netvc_tests_def[];
extern const unsigned num_netvc_tests;

class NetTestDriver : public Continuation
{
public:
  NetTestDriver();
  ~NetTestDriver() override;

  int errors = 0;

protected:
  RegressionTest *r = nullptr;
  int *pstatus      = nullptr;
};

class NetVCTest : public Continuation
{
public:
  NetVCTest();
  ~NetVCTest() override;
  NetVcTestType_t test_cont_type = NET_VC_TEST_ACTIVE;

  int main_handler(int event, void *data);
  void read_handler(int event);
  void write_handler(int event);
  void cleanup();

  void init_test(NetVcTestType_t n_type, NetTestDriver *driver, NetVConnection *nvc, RegressionTest *robj, NVC_test_def *my_def,
                 const char *module_name_arg, const char *debug_tag_arg);
  void start_test();
  int fill_buffer(MIOBuffer *buf, uint8_t *seed, int bytes);
  int consume_and_check_bytes(IOBufferReader *r, uint8_t *seed);

  void write_finished();
  void read_finished();
  void finished();
  void record_error(const char *msg);

  NetVConnection *test_vc = nullptr;
  RegressionTest *regress = nullptr;
  NetTestDriver *driver   = nullptr;

  VIO *read_vio  = nullptr;
  VIO *write_vio = nullptr;

  MIOBuffer *read_buffer  = nullptr;
  MIOBuffer *write_buffer = nullptr;

  IOBufferReader *reader_for_rbuf = nullptr;
  IOBufferReader *reader_for_wbuf = nullptr;

  int write_bytes_to_add_per = 0;
  int timeout                = 0;

  int actual_bytes_read = 0;
  int actual_bytes_sent = 0;

  bool write_done = false;
  bool read_done  = false;

  uint8_t read_seed  = 0;
  uint8_t write_seed = 0;

  int bytes_to_send = 0;
  int bytes_to_read = 0;

  int nbytes_read  = 0;
  int nbytes_write = 0;

  int expected_read_term  = 0;
  int expected_write_term = 0;

  const char *test_name   = nullptr;
  const char *module_name = nullptr;
  const char *debug_tag   = nullptr;
};
