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
#include <atscppapi/TransactionPlugin.h>
#include <atscppapi/Logger.h>
#include <atscppapi/Async.h>
#include <atscppapi/AsyncHttpFetch.h>
#include <atscppapi/AsyncTimer.h>
#include <atscppapi/PluginInit.h>
#include <cstring>
#include <cassert>

using namespace atscppapi;
using std::string;

// This is for the -T tag debugging
// To view the debug messages ./traffic_server -T "async_http_fetch_example.*"
#define TAG "async_http_fetch_example"

class AsyncHttpFetch2 : public AsyncHttpFetch
{
public:
  AsyncHttpFetch2(string request) : AsyncHttpFetch(request){};
};

class AsyncHttpFetch3 : public AsyncHttpFetch
{
public:
  AsyncHttpFetch3(string request, HttpMethod method) : AsyncHttpFetch(request, method){};
};

class DelayedAsyncHttpFetch : public AsyncHttpFetch, public AsyncReceiver<AsyncTimer>
{
public:
  DelayedAsyncHttpFetch(string request, HttpMethod method, shared_ptr<Mutex> mutex)
    : AsyncHttpFetch(request, method), mutex_(mutex), timer_(NULL){};
  void
  run()
  {
    timer_ = new AsyncTimer(AsyncTimer::TYPE_ONE_OFF, 1000 /* 1s */);
    Async::execute(this, timer_, mutex_);
  }
  void
  handleAsyncComplete(AsyncTimer & /*timer ATS_UNUSED */)
  {
    TS_DEBUG(TAG, "Receiver should not be reachable");
    assert(!getDispatchController()->dispatch());
    delete this;
  }
  bool
  isAlive()
  {
    return getDispatchController()->isEnabled();
  }
  ~DelayedAsyncHttpFetch() { delete timer_; }
private:
  shared_ptr<Mutex> mutex_;
  AsyncTimer *timer_;
};

class TransactionHookPlugin : public TransactionPlugin,
                              public AsyncReceiver<AsyncHttpFetch>,
                              public AsyncReceiver<AsyncHttpFetch2>,
                              public AsyncReceiver<AsyncHttpFetch3>,
                              public AsyncReceiver<DelayedAsyncHttpFetch>
{
public:
  TransactionHookPlugin(Transaction &transaction)
    : TransactionPlugin(transaction), transaction_(transaction), num_fetches_pending_(0)
  {
    TS_DEBUG(TAG, "Constructed TransactionHookPlugin, saved a reference to this transaction.");
    registerHook(HOOK_SEND_REQUEST_HEADERS);
  }

  void
  handleSendRequestHeaders(Transaction & /*transaction ATS_UNUSED */)
  {
    Async::execute<AsyncHttpFetch>(this, new AsyncHttpFetch("http://127.0.0.1/"), getMutex());
    ++num_fetches_pending_;
    AsyncHttpFetch *post_request = new AsyncHttpFetch("http://127.0.0.1/post", "data");

    (void)post_request;

    Async::execute<AsyncHttpFetch>(this, new AsyncHttpFetch("http://127.0.0.1/post", "data"), getMutex());
    ++num_fetches_pending_;

    // we'll add some custom headers for this request
    AsyncHttpFetch2 *provider2 = new AsyncHttpFetch2("http://127.0.0.1/");
    Headers &request_headers   = provider2->getRequestHeaders();
    request_headers.set("Header1", "Value1");
    request_headers.set("Header2", "Value2");
    Async::execute<AsyncHttpFetch2>(this, provider2, getMutex());
    ++num_fetches_pending_;

    DelayedAsyncHttpFetch *delayed_provider = new DelayedAsyncHttpFetch("url", HTTP_METHOD_GET, getMutex());
    Async::execute<DelayedAsyncHttpFetch>(this, delayed_provider, getMutex());

    // canceling right after starting in this case, but cancel() can be called any time
    TS_DEBUG(TAG, "Will cancel delayed fetch");
    assert(delayed_provider->isAlive());
    delayed_provider->cancel();
    assert(!delayed_provider->isAlive());
  }

  void
  handleAsyncComplete(AsyncHttpFetch &async_http_fetch)
  {
    // This will be called when our async event is complete.
    TS_DEBUG(TAG, "AsyncHttpFetch completed");
    handleAnyAsyncComplete(async_http_fetch);
  }

  void
  handleAsyncComplete(AsyncHttpFetch2 &async_http_fetch)
  {
    // This will be called when our async event is complete.
    TS_DEBUG(TAG, "AsyncHttpFetch2 completed");
    handleAnyAsyncComplete(async_http_fetch);
  }

  virtual ~TransactionHookPlugin()
  {
    TS_DEBUG(TAG, "Destroyed TransactionHookPlugin!");
    // since we die right away, we should not receive the callback for this (using POST request this time)
    Async::execute<AsyncHttpFetch3>(this, new AsyncHttpFetch3("http://127.0.0.1/", HTTP_METHOD_POST), getMutex());
  }

  void
  handleAsyncComplete(AsyncHttpFetch3 & /* async_http_fetch ATS_UNUSED */)
  {
    assert(!"AsyncHttpFetch3 shouldn't have completed!");
  }

  void
  handleAsyncComplete(DelayedAsyncHttpFetch & /*async_http_fetch ATS_UNUSED */)
  {
    assert(!"Should've been canceled!");
  }

private:
  Transaction &transaction_;
  int num_fetches_pending_;

  void
  handleAnyAsyncComplete(AsyncHttpFetch &async_http_fetch)
  {
    // This will be called when our async event is complete.
    TS_DEBUG(TAG, "Fetch completed for URL [%s]", async_http_fetch.getRequestUrl().getUrlString().c_str());
    const Response &response = async_http_fetch.getResponse();
    if (async_http_fetch.getResult() == AsyncHttpFetch::RESULT_SUCCESS) {
      TS_DEBUG(TAG, "Response version is [%s], status code %d, reason phrase [%s]",
               HTTP_VERSION_STRINGS[response.getVersion()].c_str(), response.getStatusCode(), response.getReasonPhrase().c_str());

      TS_DEBUG(TAG, "Reponse Headers: \n%s\n", response.getHeaders().str().c_str());

      const void *body;
      size_t body_size;
      async_http_fetch.getResponseBody(body, body_size);
      TS_DEBUG(TAG, "Response body is %zu bytes long and is [%.*s]", body_size, static_cast<int>(body_size),
               static_cast<const char *>(body));
    } else {
      TS_ERROR(TAG, "Fetch did not complete successfully; Result %d", static_cast<int>(async_http_fetch.getResult()));
    }
    if (--num_fetches_pending_ == 0) {
      TS_DEBUG(TAG, "Reenabling transaction");
      transaction_.resume();
    }
  }
};

class GlobalHookPlugin : public GlobalPlugin
{
public:
  GlobalHookPlugin()
  {
    TS_DEBUG(TAG, "Registering a global hook HOOK_READ_REQUEST_HEADERS_POST_REMAP");
    registerHook(HOOK_READ_REQUEST_HEADERS_POST_REMAP);
  }

  virtual void
  handleReadRequestHeadersPostRemap(Transaction &transaction)
  {
    TS_DEBUG(TAG, "Received a request in handleReadRequestHeadersPostRemap.");

    // If we don't make sure to check if it's an internal request we can get ourselves into an infinite loop!
    if (!transaction.isInternalRequest()) {
      transaction.addPlugin(new TransactionHookPlugin(transaction));
    } else {
      TS_DEBUG(TAG, "Ignoring internal transaction");
    }
    transaction.resume();
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  TS_DEBUG(TAG, "Loaded async_http_fetch_example plugin");
  RegisterGlobalPlugin("CPP_Example_AsyncHttpFetch", "apache", "dev@trafficserver.apache.org");
  GlobalPlugin *instance = new GlobalHookPlugin();

  (void)instance;
}
