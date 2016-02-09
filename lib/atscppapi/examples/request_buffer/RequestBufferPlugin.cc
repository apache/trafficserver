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


#include <iostream>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/TransactionPlugin.h>
#include <atscppapi/PluginInit.h>

using namespace atscppapi;
static const int MIN_BYTE_PER_SEC = 1000;

class TimeRecord
{
public:
  TimeRecord() { clock_gettime(CLOCK_MONOTONIC, &start_time); }
  const timespec
  getStartTime() const
  {
    return start_time;
  }

private:
  timespec start_time;
};
class RequestBufferPlugin : public atscppapi::TransactionPlugin
{
public:
  RequestBufferPlugin(Transaction &transaction) : TransactionPlugin(transaction)
  {
    // enable the request body buffering
    transaction.configIntSet(TS_CONFIG_HTTP_REQUEST_BUFFER_ENABLED, 1);
    // save the start time for calculating the data rate
    timeRecord = new TimeRecord();

    TransactionPlugin::registerHook(HOOK_HTTP_REQUEST_BUFFER_READ);
    TransactionPlugin::registerHook(HOOK_HTTP_REQUEST_BUFFER_READ_COMPLETE);
    std::cout << "Constructed!" << std::endl;
  }
  virtual ~RequestBufferPlugin()
  {
    delete timeRecord; // cleanup
    std::cout << "Destroyed!" << std::endl;
  }
  void
  handleHttpRequestBufferRead(Transaction &transaction)
  {
    std::cout << "request buffer read" << transaction.getClientRequestBody().size() << std::endl;
    reached_min_speed(transaction) ? transaction.resume() : transaction.error();
  }
  void
  handleHttpRequestBufferReadComplete(Transaction &transaction)
  {
    std::cout << "request buffer complete!" << transaction.getClientRequestBody().size() << std::endl;
    transaction.resume();
  }

private:
  TimeRecord *timeRecord;
  bool
  reached_min_speed(Transaction &transaction)
  {
    int64_t body_len = transaction.getClientRequestBodySize();
    timespec now_time;
    clock_gettime(CLOCK_MONOTONIC, &now_time);
    double time_diff_in_sec =
      (now_time.tv_sec - timeRecord->getStartTime().tv_sec) + 1e-9 * (now_time.tv_nsec - timeRecord->getStartTime().tv_nsec);
    std::cout << "time_diff_in_sec = " << time_diff_in_sec << ", body_len = " << body_len
              << ", date_rate = " << body_len / time_diff_in_sec << std::endl;
    return body_len / time_diff_in_sec >= MIN_BYTE_PER_SEC;
  }
};

class GlobalHookPlugin : public atscppapi::GlobalPlugin
{
public:
  GlobalHookPlugin() { GlobalPlugin::registerHook(HOOK_READ_REQUEST_HEADERS); }
  virtual void
  handleReadRequestHeaders(Transaction &transaction)
  {
    std::cout << "Hello from handleReadRequestHeaders!" << std::endl;
    if (transaction.getClientRequest().getMethod() == HTTP_METHOD_POST) {
      transaction.addPlugin(new RequestBufferPlugin(transaction));
    }
    transaction.resume();
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  RegisterGlobalPlugin("CPP_Example_RequestBuffer", "apache", "dev@trafficserver.apache.org");
  new GlobalHookPlugin();
}
