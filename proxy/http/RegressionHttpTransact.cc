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

#include "tscore/Regression.h"
#include "HttpTransact.h"
#include "HttpSM.h"

void
forceLinkRegressionHttpTransact()
{
}

static void
init_sm(HttpSM *sm)
{
  sm->init();
  sm->t_state.hdr_info.client_request.create(HTTP_TYPE_REQUEST, nullptr);
}

static void
setup_client_request(HttpSM *sm, const char *scheme, const char *request)
{
  init_sm(sm);

  MIOBuffer *read_buffer        = new_MIOBuffer(HTTP_HEADER_BUFFER_SIZE_INDEX);
  IOBufferReader *buffer_reader = read_buffer->alloc_reader();
  read_buffer->write(request, strlen(request));

  HTTPParser httpParser;
  http_parser_init(&httpParser);
  int bytes_used = 0;
  sm->t_state.hdr_info.client_request.parse_req(&httpParser, buffer_reader, &bytes_used, true /* eos */);
  sm->t_state.hdr_info.client_request.url_get()->scheme_set(scheme, strlen(scheme));
  sm->t_state.method = sm->t_state.hdr_info.client_request.method_get_wksidx();
  free_MIOBuffer(read_buffer);
}

/*
pstatus return values
REGRESSION_TEST_PASSED
REGRESSION_TEST_INPROGRESS
REGRESSION_TEST_FAILED
REGRESSION_TEST_NOT_RUN
 */
REGRESSION_TEST(HttpTransact_is_request_valid)(RegressionTest *t, int /* level */, int *pstatus)
{
  HttpTransact transaction;
  HttpSM sm;
  *pstatus = REGRESSION_TEST_PASSED;

  struct {
    const char *scheme;
    const char *req;
    bool result;
  } requests[] = {// missing host header
                  {"http", "GET / HTTP/1.1\r\n\r\n", false},
                  // good get request
                  {"http", "GET / HTTP/1.1\r\nHost: abc.com\r\n\r\n", true},
                  // good trace request
                  {"http", "TRACE / HTTP/1.1\r\nHost: abc.com\r\n\r\n", true},
                  // content len < 0
                  {"http", "POST / HTTP/1.1\r\nHost: abc.com\r\nContent-Length: -1\r\n\r\n", false},
                  {"http", "PUSH / HTTP/1.1\r\nHost: abc.com\r\nContent-Length: -1\r\n\r\n", false},
                  {"http", "PUT / HTTP/1.1\r\nHost: abc.com\r\nContent-Length: -1\r\n\r\n", false},
                  // valid content len
                  {"http", "POST / HTTP/1.1\r\nHost: abc.com\r\nContent-Length: 10\r\n\r\n", true},
                  {"http", "PUSH / HTTP/1.1\r\nHost: abc.com\r\nContent-Length: 10\r\n\r\n", true},
                  {"http", "PUT / HTTP/1.1\r\nHost: abc.com\r\nContent-Length: 10\r\n\r\n", true},
                  // Content Length missing
                  {"http", "POST / HTTP/1.1\r\nHost: abc.com\r\n\r\n", false},
                  {"http", "PUSH / HTTP/1.1\r\nHost: abc.com\r\n\r\n", false},
                  {"http", "PUT / HTTP/1.1\r\nHost: abc.com\r\n\r\n", false},
                  {nullptr, nullptr, false}};
  for (int i = 0; requests[i].req; i++) {
    setup_client_request(&sm, requests[i].scheme, requests[i].req);

    if (requests[i].result != transaction.is_request_valid(&sm.t_state, &sm.t_state.hdr_info.client_request)) {
      rprintf(t, "HttpTransact::is_request_valid - failed for request = '%s'.  Expected result was %s request\n", requests[i].req,
              (requests[i].result ? "valid" : "invalid"));
      *pstatus = REGRESSION_TEST_FAILED;
    }
  }
}

REGRESSION_TEST(HttpTransact_handle_trace_and_options_requests)(RegressionTest *t, int /* level */, int *pstatus)
{
  HttpTransact transaction;
  HttpSM sm;
  *pstatus = REGRESSION_TEST_PASSED;

  struct {
    const char *scheme;
    const char *req;
    bool result;
  } requests[] = {// good trace request
                  {"http", "TRACE www.abc.com/ HTTP/1.1\r\nHost: abc.com\r\nMax-Forwards: 0\r\n\r\n", true},
                  {nullptr, nullptr, false}};
  for (int i = 0; requests[i].req; i++) {
    setup_client_request(&sm, requests[i].scheme, requests[i].req);

    if (requests[i].result != transaction.is_request_valid(&sm.t_state, &sm.t_state.hdr_info.client_request)) {
      rprintf(t, "HttpTransact::is_request_valid - failed for request = '%s'.  Expected result was %s request\n", requests[i].req,
              (requests[i].result ? "valid" : "invalid"));
      *pstatus = REGRESSION_TEST_FAILED;
    }
    if (requests[i].result != transaction.handle_trace_and_options_requests(&sm.t_state, &sm.t_state.hdr_info.client_request)) {
      rprintf(t, "HttpTransact::handle_trace_and_options - failed for request = '%s'.  Expected result was %s request\n",
              requests[i].req, (requests[i].result ? "true" : "false"));
      *pstatus = REGRESSION_TEST_FAILED;
    }
  }
}

REGRESSION_TEST(HttpTransact_handle_request)(RegressionTest * /* t */, int /* level */, int *pstatus)
{
  // To be added..
  *pstatus = REGRESSION_TEST_PASSED;
}
