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

/**
 * @file AsyncHttpFetch.cc
 */

#include <memory>
#include <arpa/inet.h>
#include "tscpp/api/AsyncHttpFetch.h"
#include "ts/ts.h"
#include "ts/experimental.h"
#include "logging_internal.h"
#include "utils_internal.h"

#include <cstdio>
#include <cstring>
#include <utility>

using namespace atscppapi;
using std::string;

/**
 * @private
 */
struct atscppapi::AsyncHttpFetchState : noncopyable {
  std::shared_ptr<Request> request_;
  Response response_;
  string request_body_;
  AsyncHttpFetch::Result result_;
  const void *body_;
  size_t body_size_;
  TSMBuffer hdr_buf_;
  TSMLoc hdr_loc_;
  std::shared_ptr<AsyncDispatchControllerBase> dispatch_controller_;
  AsyncHttpFetch::StreamingFlag streaming_flag_;
  TSFetchSM fetch_sm_;
  static const size_t BODY_BUFFER_SIZE = 32 * 1024;
  char body_buffer_[BODY_BUFFER_SIZE];

  AsyncHttpFetchState(const string &url_str, HttpMethod http_method, string request_body,
                      AsyncHttpFetch::StreamingFlag streaming_flag)
    : request_body_(std::move(request_body)),
      result_(AsyncHttpFetch::RESULT_FAILURE),
      body_(nullptr),
      body_size_(0),
      hdr_buf_(nullptr),
      hdr_loc_(nullptr),
      streaming_flag_(streaming_flag),
      fetch_sm_(nullptr)
  {
    request_.reset(new Request(url_str, http_method,
                               (streaming_flag_ == AsyncHttpFetch::STREAMING_DISABLED) ? HTTP_VERSION_1_0 : HTTP_VERSION_1_1));
    if (streaming_flag_ == AsyncHttpFetch::STREAMING_ENABLED) {
      body_ = body_buffer_;
    }
  }

  ~AsyncHttpFetchState()
  {
    if (hdr_loc_) {
      TSMLoc null_parent_loc = nullptr;
      TSHandleMLocRelease(hdr_buf_, null_parent_loc, hdr_loc_);
    }
    if (hdr_buf_) {
      TSMBufferDestroy(hdr_buf_);
    }
    if (fetch_sm_) {
      TSFetchDestroy(fetch_sm_);
    }
  }
};

namespace
{
const unsigned int LOCAL_IP_ADDRESS = 0x0100007f;
const int LOCAL_PORT                = 8080;

static int
handleFetchEvents(TSCont cont, TSEvent event, void *edata)
{
  LOG_DEBUG("Received fetch event = %d, edata = %p", event, edata);
  AsyncHttpFetch *fetch_provider = static_cast<AsyncHttpFetch *>(TSContDataGet(cont));
  AsyncHttpFetchState *state     = utils::internal::getAsyncHttpFetchState(*fetch_provider);

  if (state->streaming_flag_ == AsyncHttpFetch::STREAMING_DISABLED) {
    if (event == static_cast<int>(AsyncHttpFetch::RESULT_SUCCESS)) {
      TSHttpTxn txn = static_cast<TSHttpTxn>(edata);
      int data_len;
      const char *data_start = TSFetchRespGet(txn, &data_len);
      if (data_start && (data_len > 0)) {
        const char *data_end = data_start + data_len;
        TSHttpParser parser  = TSHttpParserCreate();
        state->hdr_buf_      = TSMBufferCreate();
        state->hdr_loc_      = TSHttpHdrCreate(state->hdr_buf_);
        TSHttpHdrTypeSet(state->hdr_buf_, state->hdr_loc_, TS_HTTP_TYPE_RESPONSE);
        if (TSHttpHdrParseResp(parser, state->hdr_buf_, state->hdr_loc_, &data_start, data_end) == TS_PARSE_DONE) {
          TSHttpStatus status = TSHttpHdrStatusGet(state->hdr_buf_, state->hdr_loc_);
          state->body_        = data_start; // data_start will now be pointing to body
          state->body_size_   = data_end - data_start;
          utils::internal::initResponse(state->response_, state->hdr_buf_, state->hdr_loc_);
          LOG_DEBUG("Fetch result had a status code of %d with a body length of %ld", status, state->body_size_);
        } else {
          LOG_ERROR("Unable to parse response; Request URL [%s]; transaction %p", state->request_->getUrl().getUrlString().c_str(),
                    txn);
          event = static_cast<TSEvent>(AsyncHttpFetch::RESULT_FAILURE);
        }
        TSHttpParserDestroy(parser);
      } else {
        LOG_ERROR("Successful fetch did not result in any content. Assuming failure");
        event = static_cast<TSEvent>(AsyncHttpFetch::RESULT_FAILURE);
      }
      state->result_ = static_cast<AsyncHttpFetch::Result>(event);
    }
  } else {
    LOG_DEBUG("Handling streaming event %d", event);
    if (event == static_cast<TSEvent>(TS_FETCH_EVENT_EXT_HEAD_DONE)) {
      utils::internal::initResponse(state->response_, TSFetchRespHdrMBufGet(state->fetch_sm_),
                                    TSFetchRespHdrMLocGet(state->fetch_sm_));
      LOG_DEBUG("Response header initialized");
      state->result_ = AsyncHttpFetch::RESULT_HEADER_COMPLETE;
    } else {
      state->body_size_ = TSFetchReadData(state->fetch_sm_, state->body_buffer_, sizeof(state->body_buffer_));
      LOG_DEBUG("Read %zu bytes", state->body_size_);
      state->result_ = (event == static_cast<TSEvent>(TS_FETCH_EVENT_EXT_BODY_READY)) ? AsyncHttpFetch::RESULT_PARTIAL_BODY :
                                                                                        AsyncHttpFetch::RESULT_BODY_COMPLETE;
    }
  }
  if (!state->dispatch_controller_->dispatch()) {
    LOG_DEBUG("Unable to dispatch result from AsyncFetch because promise has died.");
  }

  if ((state->streaming_flag_ == AsyncHttpFetch::STREAMING_DISABLED) || (state->result_ == AsyncHttpFetch::RESULT_BODY_COMPLETE)) {
    LOG_DEBUG("Shutting down");
    utils::internal::deleteAsyncHttpFetch(fetch_provider); // we must always cleans up when we're done.
    TSContDestroy(cont);
  }
  return 0;
}
} // namespace

AsyncHttpFetch::AsyncHttpFetch(const string &url_str, const string &request_body)
{
  init(url_str, HTTP_METHOD_POST, request_body, STREAMING_DISABLED);
}

AsyncHttpFetch::AsyncHttpFetch(const string &url_str, HttpMethod http_method)
{
  init(url_str, http_method, "", STREAMING_DISABLED);
}

AsyncHttpFetch::AsyncHttpFetch(const string &url_str, StreamingFlag streaming_flag, const string &request_body)
{
  init(url_str, HTTP_METHOD_POST, request_body, streaming_flag);
}

AsyncHttpFetch::AsyncHttpFetch(const string &url_str, StreamingFlag streaming_flag, HttpMethod http_method)
{
  init(url_str, http_method, "", streaming_flag);
}

void
AsyncHttpFetch::init(const string &url_str, HttpMethod http_method, const string &request_body, StreamingFlag streaming_flag)
{
  LOG_DEBUG("Created new AsyncHttpFetch object %p", this);
  state_ = new AsyncHttpFetchState(url_str, http_method, request_body, streaming_flag);
}

void
AsyncHttpFetch::run()
{
  state_->dispatch_controller_ = getDispatchController(); // keep a copy in state so that cont handler can use it

  TSCont fetchCont = TSContCreate(handleFetchEvents, TSMutexCreate());
  TSContDataSet(fetchCont, static_cast<void *>(this)); // Providers have to clean themselves up when they are done.

  struct sockaddr_in addr;
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = LOCAL_IP_ADDRESS;
  addr.sin_port        = LOCAL_PORT;

  Headers &headers = state_->request_->getHeaders();
  if (headers.size()) {
    // remove the possibility of keep-alive
    headers.erase("Connection");
    headers.erase("Proxy-Connection");
  }
  if (!state_->request_body_.empty()) {
    char size_buf[128];
    snprintf(size_buf, sizeof(size_buf), "%zu", state_->request_body_.size());
    headers.set("Content-Length", size_buf);
  }

  if (state_->streaming_flag_ == STREAMING_DISABLED) {
    TSFetchEvent event_ids;
    event_ids.success_event_id = RESULT_SUCCESS;
    event_ids.failure_event_id = RESULT_FAILURE;
    event_ids.timeout_event_id = RESULT_TIMEOUT;

    string request_str(HTTP_METHOD_STRINGS[state_->request_->getMethod()]);
    request_str += ' ';
    request_str += state_->request_->getUrl().getUrlString();
    request_str += ' ';
    request_str += HTTP_VERSION_STRINGS[state_->request_->getVersion()];
    request_str += "\r\n";
    request_str += headers.wireStr();
    request_str += "\r\n";
    request_str += state_->request_body_;

    LOG_DEBUG("Issuing (non-streaming) TSFetchUrl with request\n[%s]", request_str.c_str());
    TSFetchUrl(request_str.c_str(), request_str.size(), reinterpret_cast<struct sockaddr const *>(&addr), fetchCont, AFTER_BODY,
               event_ids);
  } else {
    state_->fetch_sm_ =
      TSFetchCreate(fetchCont, HTTP_METHOD_STRINGS[state_->request_->getMethod()].c_str(),
                    state_->request_->getUrl().getUrlString().c_str(), HTTP_VERSION_STRINGS[state_->request_->getVersion()].c_str(),
                    reinterpret_cast<struct sockaddr const *>(&addr), TS_FETCH_FLAGS_STREAM | TS_FETCH_FLAGS_DECHUNK);
    string header_value;
    for (auto &&header : headers) {
      HeaderFieldName header_name = header.name();
      header_value                = header.values();
      TSFetchHeaderAdd(state_->fetch_sm_, header_name.c_str(), header_name.length(), header_value.data(), header_value.size());
    }
    LOG_DEBUG("Launching streaming fetch");
    TSFetchLaunch(state_->fetch_sm_);
    if (state_->request_body_.size()) {
      TSFetchWriteData(state_->fetch_sm_, state_->request_body_.data(), state_->request_body_.size());
      LOG_DEBUG("Wrote %zu bytes of data to fetch", state_->request_body_.size());
    }
  }
}

Headers &
AsyncHttpFetch::getRequestHeaders()
{
  return state_->request_->getHeaders();
}

AsyncHttpFetch::Result
AsyncHttpFetch::getResult() const
{
  return state_->result_;
}

const Url &
AsyncHttpFetch::getRequestUrl() const
{
  return state_->request_->getUrl();
}

const string &
AsyncHttpFetch::getRequestBody() const
{
  return state_->request_body_;
}

const Response &
AsyncHttpFetch::getResponse() const
{
  return state_->response_;
}

void
AsyncHttpFetch::getResponseBody(const void *&body, size_t &body_size) const
{
  body      = state_->body_;
  body_size = state_->body_size_;
}

AsyncHttpFetch::~AsyncHttpFetch()
{
  delete state_;
}
