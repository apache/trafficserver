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

#include "atscppapi/AsyncHttpFetch.h"
#include <ts/ts.h>
#include <arpa/inet.h>
#include "logging_internal.h"
#include "utils_internal.h"

using namespace atscppapi;
using std::string;

/**
 * @private
 */
struct atscppapi::AsyncHttpFetchState : noncopyable {
  Request request_;
  Response response_;
  AsyncHttpFetch::Result result_;
  const void *body_;
  size_t body_size_;
  TSMBuffer hdr_buf_;
  TSMLoc hdr_loc_;
  shared_ptr<AsyncDispatchControllerBase> dispatch_controller_;

  AsyncHttpFetchState(const string &url_str, HttpMethod http_method)
    : request_(url_str, http_method, HTTP_VERSION_1_0), result_(AsyncHttpFetch::RESULT_FAILURE), body_(NULL),
      body_size_(0), hdr_buf_(NULL), hdr_loc_(NULL) { }
  
  ~AsyncHttpFetchState() {
    if (hdr_loc_) {
      TSMLoc null_parent_loc = NULL;
      TSHandleMLocRelease(hdr_buf_, null_parent_loc, hdr_loc_);
    }
    if (hdr_buf_) {
      TSMBufferDestroy(hdr_buf_);
    }
  }
};

namespace {

const unsigned int LOCAL_IP_ADDRESS = 0x0100007f;
const int LOCAL_PORT = 8080;

static int handleFetchEvents(TSCont cont, TSEvent event, void *edata) {
  LOG_DEBUG("Fetch result returned event = %d, edata = %p", event, edata);
  AsyncHttpFetch *fetch_provider = static_cast<AsyncHttpFetch *>(TSContDataGet(cont));
  AsyncHttpFetchState *state = utils::internal::getAsyncHttpFetchState(*fetch_provider);
  
  if (event == static_cast<int>(AsyncHttpFetch::RESULT_SUCCESS)) {
    TSHttpTxn txn = static_cast<TSHttpTxn>(edata);
    int data_len;
    const char *data_start = TSFetchRespGet(txn, &data_len);
    const char *data_end = data_start + data_len;
    
    TSHttpParser parser = TSHttpParserCreate();
    state->hdr_buf_ = TSMBufferCreate();
    state->hdr_loc_ = TSHttpHdrCreate(state->hdr_buf_);
    TSHttpHdrTypeSet(state->hdr_buf_, state->hdr_loc_, TS_HTTP_TYPE_RESPONSE);
    if (TSHttpHdrParseResp(parser, state->hdr_buf_, state->hdr_loc_, &data_start, data_end) == TS_PARSE_DONE) {
      TSHttpStatus status = TSHttpHdrStatusGet(state->hdr_buf_, state->hdr_loc_);
      state->body_ = data_start; // data_start will now be pointing to body
      state->body_size_ = data_end - data_start;
      utils::internal::initResponse(state->response_, state->hdr_buf_, state->hdr_loc_);
      LOG_DEBUG("Fetch result had a status code of %d with a body length of %ld", status, state->body_size_);
    } else {
      LOG_ERROR("Unable to parse response; Request URL [%s]; transaction %p",
                state->request_.getUrl().getUrlString().c_str(), txn);
      event = static_cast<TSEvent>(AsyncHttpFetch::RESULT_FAILURE);
    }
    TSHttpParserDestroy(parser);
  }
  state->result_ = static_cast<AsyncHttpFetch::Result>(event);
  if (!state->dispatch_controller_->dispatch()) {
    LOG_DEBUG("Unable to dispatch result from AsyncFetch because promise has died.");
  }

  delete fetch_provider; // we must always be sure to clean up the provider when we're done with it.
  TSContDestroy(cont);
  return 0;
}

}

AsyncHttpFetch::AsyncHttpFetch(const std::string &url_str, HttpMethod http_method) {
  LOG_DEBUG("Created new AsyncHttpFetch object %p", this);
  state_ = new AsyncHttpFetchState(url_str, http_method);
}

void AsyncHttpFetch::run(shared_ptr<AsyncDispatchControllerBase> sender) {
  state_->dispatch_controller_ = sender;

  TSCont fetchCont = TSContCreate(handleFetchEvents, TSMutexCreate());
  TSContDataSet(fetchCont, static_cast<void *>(this)); // Providers have to clean themselves up when they are done.

  TSFetchEvent event_ids;
  event_ids.success_event_id = RESULT_SUCCESS;
  event_ids.failure_event_id = RESULT_FAILURE;
  event_ids.timeout_event_id = RESULT_TIMEOUT;

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = LOCAL_IP_ADDRESS;
  addr.sin_port = LOCAL_PORT;

  string request_str(HTTP_METHOD_STRINGS[state_->request_.getMethod()]);
  request_str += ' ';
  request_str += state_->request_.getUrl().getUrlString();
  request_str += ' ';
  request_str += HTTP_VERSION_STRINGS[state_->request_.getVersion()];
  request_str += "\r\n";

 /* for (Headers::const_iterator iter = state_->request_.getHeaders().begin(),
         end = state_->request_.getHeaders().end(); iter != end; ++iter) {
    request_str += iter->first;
    request_str += ": ";
    request_str += Headers::getJoinedValues(iter->second);
    request_str += "\r\n";
  }
*/
  request_str += "\r\n";

  LOG_DEBUG("Issing TSFetchUrl with request\n[%s]", request_str.c_str());
  TSFetchUrl(request_str.c_str(), request_str.size(), reinterpret_cast<struct sockaddr const *>(&addr), fetchCont,
             AFTER_BODY, event_ids);
}

Headers &AsyncHttpFetch::getRequestHeaders() {
  return state_->request_.getHeaders();
}

AsyncHttpFetch::Result AsyncHttpFetch::getResult() const {
  return state_->result_;
}

const Url &AsyncHttpFetch::getRequestUrl() const {
  return state_->request_.getUrl();
}

const Response &AsyncHttpFetch::getResponse() const {
  return state_->response_;
}

void AsyncHttpFetch::getResponseBody(const void *&body, size_t &body_size) const {
  body = state_->body_;
  body_size = state_->body_size_;
}

AsyncHttpFetch::~AsyncHttpFetch() {
  delete state_;
}
