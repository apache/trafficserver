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

#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/InterceptPlugin.h>
#include <atscppapi/Logger.h>
#include <atscppapi/Async.h>
#include <atscppapi/AsyncHttpFetch.h>
#include <atscppapi/AsyncTimer.h>
#include <atscppapi/PluginInit.h>
#include <cstring>
#include <cassert>
#include <sstream>

using namespace atscppapi;
using std::string;

// This is for the -T tag debugging
// To view the debug messages ./traffic_server -T "async_http_fetch_example.*"
#define TAG "async_http_fetch_example"

class Intercept : public InterceptPlugin, public AsyncReceiver<AsyncHttpFetch>
{
public:
  Intercept(Transaction &transaction)
    : InterceptPlugin(transaction, InterceptPlugin::SERVER_INTERCEPT), transaction_(transaction), num_fetches_(0)
  {
    main_url_ = transaction.getClientRequest().getUrl().getUrlString();
  }
  void consume(const string &data, InterceptPlugin::RequestDataType type);
  void handleInputComplete();
  void handleAsyncComplete(AsyncHttpFetch &async_http_fetch);
  ~Intercept();

private:
  Transaction &transaction_;
  string request_body_;
  string main_url_;
  string dependent_url_;
  int num_fetches_;
};

class InterceptInstaller : public GlobalPlugin
{
public:
  InterceptInstaller() : GlobalPlugin(true /* ignore internal transactions */)
  {
    GlobalPlugin::registerHook(Plugin::HOOK_READ_REQUEST_HEADERS_PRE_REMAP);
  }
  void
  handleReadRequestHeadersPreRemap(Transaction &transaction)
  {
    transaction.addPlugin(new Intercept(transaction));
    TS_DEBUG(TAG, "Added intercept");
    transaction.resume();
  }
};

void
TSPluginInit(int /* argc ATS_UNUSED */, const char * /* argv ATS_UNUSED */ [])
{
  RegisterGlobalPlugin("CPP_Example_AsyncHttpFetchStreaming", "apache", "dev@trafficserver.apache.org");
  new InterceptInstaller();
}

void
Intercept::consume(const string &data, InterceptPlugin::RequestDataType type)
{
  if (type == InterceptPlugin::REQUEST_BODY) {
    request_body_ += data;
  }
}

void
Intercept::handleInputComplete()
{
  TS_DEBUG(TAG, "Request data complete");
  AsyncHttpFetch *async_http_fetch =
    request_body_.empty() ?
      new AsyncHttpFetch(main_url_, AsyncHttpFetch::STREAMING_ENABLED, transaction_.getClientRequest().getMethod()) :
      new AsyncHttpFetch(main_url_, AsyncHttpFetch::STREAMING_ENABLED, request_body_);
  Async::execute<AsyncHttpFetch>(this, async_http_fetch, getMutex());
  ++num_fetches_;
  size_t dependent_url_param_pos = main_url_.find("dependent_url=");
  if (dependent_url_param_pos != string::npos) {
    dependent_url_ = main_url_.substr(dependent_url_param_pos + 14);
    Async::execute<AsyncHttpFetch>(this, new AsyncHttpFetch(dependent_url_, AsyncHttpFetch::STREAMING_ENABLED), getMutex());
    ++num_fetches_;
    TS_DEBUG(TAG, "Started fetch for dependent URL [%s]", dependent_url_.c_str());
  }
}

void
Intercept::handleAsyncComplete(AsyncHttpFetch &async_http_fetch)
{
  AsyncHttpFetch::Result result = async_http_fetch.getResult();
  string url                    = async_http_fetch.getRequestUrl().getUrlString();
  if (result == AsyncHttpFetch::RESULT_HEADER_COMPLETE) {
    TS_DEBUG(TAG, "Header completed for URL [%s]", url.c_str());
    const Response &response = async_http_fetch.getResponse();
    std::ostringstream oss;
    oss << HTTP_VERSION_STRINGS[response.getVersion()] << ' ' << response.getStatusCode() << ' ' << response.getReasonPhrase()
        << "\r\n";
    Headers &response_headers = response.getHeaders();
    for (Headers::iterator iter = response_headers.begin(), end = response_headers.end(); iter != end; ++iter) {
      HeaderFieldName header_name = (*iter).name();
      if (header_name != "Transfer-Encoding") {
        oss << header_name.str() << ": " << (*iter).values() << "\r\n";
      }
    }
    oss << "\r\n";
    if (url == main_url_) {
      Intercept::produce(oss.str());
    } else {
      TS_DEBUG(TAG, "Response header for dependent URL\n%s", oss.str().c_str());
    }
  } else if (result == AsyncHttpFetch::RESULT_PARTIAL_BODY || result == AsyncHttpFetch::RESULT_BODY_COMPLETE) {
    const void *body;
    size_t body_size;
    async_http_fetch.getResponseBody(body, body_size);
    if (url == main_url_) {
      Intercept::produce(string(static_cast<const char *>(body), body_size));
    } else {
      TS_DEBUG(TAG, "Got dependent body bit; has %zu bytes and is [%.*s]", body_size, static_cast<int>(body_size),
               static_cast<const char *>(body));
    }
    if (result == AsyncHttpFetch::RESULT_BODY_COMPLETE) {
      TS_DEBUG(TAG, "response body complete");
    }
  } else {
    TS_ERROR(TAG, "Fetch did not complete successfully; Result %d", static_cast<int>(result));
    if (url == main_url_) {
      InterceptPlugin::produce("HTTP/1.1 500 Internal Server Error\r\n\r\n");
    }
  }
  if (result == AsyncHttpFetch::RESULT_TIMEOUT || result == AsyncHttpFetch::RESULT_FAILURE ||
      result == AsyncHttpFetch::RESULT_BODY_COMPLETE) {
    if (--num_fetches_ == 0) {
      TS_DEBUG(TAG, "Marking output as complete");
      InterceptPlugin::setOutputComplete();
    }
  }
}

Intercept::~Intercept()
{
  if (num_fetches_) {
    TS_DEBUG(TAG, "Fetch still pending, but transaction closing");
  }
  TS_DEBUG(TAG, "Shutting down");
}
