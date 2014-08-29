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
 * @file Transaction.cc
 */

#include "atscppapi/Transaction.h"
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <ts/ts.h>
#include "atscppapi/shared_ptr.h"
#include "logging_internal.h"
#include "utils_internal.h"
#include "atscppapi/noncopyable.h"

using std::map;
using std::string;
using namespace atscppapi;

/**
 * @private
 */
struct atscppapi::TransactionState: noncopyable {
  TSHttpTxn txn_;
  std::list<TransactionPlugin *> plugins_;
  TSMBuffer client_request_hdr_buf_;
  TSMLoc client_request_hdr_loc_;
  ClientRequest client_request_;
  TSMBuffer server_request_hdr_buf_;
  TSMLoc server_request_hdr_loc_;
  Request server_request_;
  TSMBuffer server_response_hdr_buf_;
  TSMLoc server_response_hdr_loc_;
  Response server_response_;
  TSMBuffer client_response_hdr_buf_;
  TSMLoc client_response_hdr_loc_;
  Response client_response_;
  map<string, shared_ptr<Transaction::ContextValue> > context_values_;

  TransactionState(TSHttpTxn txn, TSMBuffer client_request_hdr_buf, TSMLoc client_request_hdr_loc)
    : txn_(txn), client_request_hdr_buf_(client_request_hdr_buf), client_request_hdr_loc_(client_request_hdr_loc),
      client_request_(txn, client_request_hdr_buf, client_request_hdr_loc),
      server_request_hdr_buf_(NULL), server_request_hdr_loc_(NULL),
      server_response_hdr_buf_(NULL), server_response_hdr_loc_(NULL),
      client_response_hdr_buf_(NULL), client_response_hdr_loc_(NULL)
  { };
};

Transaction::Transaction(void *raw_txn) {
  TSHttpTxn txn = static_cast<TSHttpTxn>(raw_txn);
  TSMBuffer hdr_buf;
  TSMLoc hdr_loc;
  TSHttpTxnClientReqGet(txn, &hdr_buf, &hdr_loc);
  if (!hdr_buf || !hdr_loc) {
    LOG_ERROR("TSHttpTxnClientReqGet tshttptxn=%p returned a null hdr_buf=%p or hdr_loc=%p.", txn, hdr_buf, hdr_loc);
  }

  state_ = new TransactionState(txn, hdr_buf, hdr_loc);
  LOG_DEBUG("Transaction tshttptxn=%p constructing Transaction object %p, client req hdr_buf=%p, client req hdr_loc=%p",
      txn, this, hdr_buf, hdr_loc);
}

Transaction::~Transaction() {
  LOG_DEBUG("Transaction tshttptxn=%p destroying Transaction object %p", state_->txn_, this);
  static const TSMLoc NULL_PARENT_LOC = NULL;
  TSHandleMLocRelease(state_->client_request_hdr_buf_, NULL_PARENT_LOC, state_->client_request_hdr_loc_);
  if (state_->server_request_hdr_buf_ && state_->server_request_hdr_loc_) {
    LOG_DEBUG("Releasing server request");
    TSHandleMLocRelease(state_->server_request_hdr_buf_, NULL_PARENT_LOC, state_->server_request_hdr_loc_);
  }
  if (state_->server_response_hdr_buf_ && state_->server_response_hdr_loc_) {
    LOG_DEBUG("Releasing server response");
    TSHandleMLocRelease(state_->server_response_hdr_buf_, NULL_PARENT_LOC, state_->server_response_hdr_loc_);
  }
  if (state_->client_response_hdr_buf_ && state_->client_response_hdr_loc_) {
    LOG_DEBUG("Releasing client response");
    TSHandleMLocRelease(state_->client_response_hdr_buf_, NULL_PARENT_LOC, state_->client_response_hdr_loc_);
  }
  delete state_;
}

void Transaction::resume() {
  TSHttpTxnReenable(state_->txn_, static_cast<TSEvent>(TS_EVENT_HTTP_CONTINUE));
}

void Transaction::error() {
  LOG_DEBUG("Transaction tshttptxn=%p reenabling to error state", state_->txn_);
  TSHttpTxnReenable(state_->txn_, static_cast<TSEvent>(TS_EVENT_HTTP_ERROR));
}

void Transaction::error(const std::string &page) {
  setErrorBody(page);
  error(); // finally, reenable with HTTP_ERROR
}

void Transaction::setErrorBody(const std::string &page) {
  LOG_DEBUG("Transaction tshttptxn=%p setting error body page: %s", state_->txn_, page.c_str());
  TSHttpTxnErrorBodySet(state_->txn_, TSstrdup(page.c_str()), page.length(), NULL); // Default to text/html
}

bool Transaction::isInternalRequest() const {
  return TSHttpIsInternalRequest(state_->txn_) == TS_SUCCESS;
}

void *Transaction::getAtsHandle() const {
  return static_cast<void *>(state_->txn_);
}

const std::list<atscppapi::TransactionPlugin *> &Transaction::getPlugins() const {
  return state_->plugins_;
}

void Transaction::addPlugin(TransactionPlugin *plugin) {
  LOG_DEBUG("Transaction tshttptxn=%p registering new TransactionPlugin %p.", state_->txn_, plugin);
  state_->plugins_.push_back(plugin);
}

shared_ptr<Transaction::ContextValue> Transaction::getContextValue(const std::string &key) {
  shared_ptr<Transaction::ContextValue> return_context_value;
  map<string, shared_ptr<Transaction::ContextValue> >::iterator iter = state_->context_values_.find(key);
  if (iter != state_->context_values_.end()) {
    return_context_value = iter->second;
  }

  return return_context_value;
}

void Transaction::setContextValue(const std::string &key, shared_ptr<Transaction::ContextValue> value) {
  state_->context_values_[key] = value;
}

ClientRequest &Transaction::getClientRequest() {
  return state_->client_request_;
}

Request &Transaction::getServerRequest() {
  return state_->server_request_;
}

Response &Transaction::getServerResponse() {
  return state_->server_response_;
}

Response &Transaction::getClientResponse() {
  return state_->client_response_;
}

string Transaction::getEffectiveUrl() {
	string ret_val;
	int length = 0;
	char *buf = TSHttpTxnEffectiveUrlStringGet(state_->txn_, &length);
	if (buf && length) {
		ret_val.assign(buf, length);
	}

	if (buf)
		TSfree(buf);

	return ret_val;
}

bool Transaction::setCacheUrl(const string &cache_url) {
	TSReturnCode res = TSCacheUrlSet(state_->txn_, cache_url.c_str(), cache_url.length());
    return (res == TS_SUCCESS);
}

const sockaddr *Transaction::getIncomingAddress() const {
  return TSHttpTxnIncomingAddrGet(state_->txn_);
}

const sockaddr *Transaction::getClientAddress() const {
  return TSHttpTxnClientAddrGet(state_->txn_);
}

const sockaddr *Transaction::getNextHopAddress() const {
  return TSHttpTxnNextHopAddrGet(state_->txn_);
}

const sockaddr *Transaction::getServerAddress() const {
  return TSHttpTxnServerAddrGet(state_->txn_);
}

bool Transaction::setServerAddress(const sockaddr *sockaddress) {
  return TSHttpTxnServerAddrSet(state_->txn_,sockaddress) == TS_SUCCESS;
}

bool Transaction::setIncomingPort(uint16_t port) {
  TSHttpTxnClientIncomingPortSet(state_->txn_, port);
  return true; // In reality TSHttpTxnClientIncomingPortSet should return SUCCESS or ERROR.
}

/*
 * Note: The following methods cannot be attached to a Response
 * object because that would require the Response object to
 * know that it's a server or client response because of the
 * TS C api which is TSHttpTxnServerRespBodyBytesGet.
 */
size_t Transaction::getServerResponseBodySize() {
  return static_cast<size_t>(TSHttpTxnServerRespBodyBytesGet(state_->txn_));
}

size_t Transaction::getServerResponseHeaderSize() {
  return static_cast<size_t>(TSHttpTxnServerRespHdrBytesGet(state_->txn_));
}

size_t Transaction::getClientResponseBodySize() {
  return static_cast<size_t>(TSHttpTxnClientRespBodyBytesGet(state_->txn_));
}

size_t Transaction::getClientResponseHeaderSize() {
  return static_cast<size_t>(TSHttpTxnClientRespHdrBytesGet(state_->txn_));
}

void Transaction::setTimeout(Transaction::TimeoutType type, int time_ms) {
  switch (type) {
    case TIMEOUT_DNS:
      TSHttpTxnDNSTimeoutSet(state_->txn_, time_ms);
      break;
    case TIMEOUT_CONNECT:
      TSHttpTxnConnectTimeoutSet(state_->txn_, time_ms);
      break;
    case TIMEOUT_NO_ACTIVITY:
      TSHttpTxnNoActivityTimeoutSet(state_->txn_, time_ms);
      break;
    case TIMEOUT_ACTIVE:
      TSHttpTxnActiveTimeoutSet(state_->txn_, time_ms);
      break;
    default:
      break;
  }
}

namespace {

/**
 * initializeHandles is a convinience functor that takes a pointer to a TS Function that
 * will return the TSMBuffer and TSMLoc for a given server request/response or client/request response
 *
 * @param constructor takes a function pointer of type GetterFunction
 * @param txn a TSHttpTxn
 * @param hdr_buf the address where the hdr buf will be stored
 * @param hdr_loc the address where the mem loc will be storeds
 * @param name name of the entity - used for logging
 */
class initializeHandles {
public:
  typedef TSReturnCode (*GetterFunction)(TSHttpTxn, TSMBuffer *, TSMLoc *);
  initializeHandles(GetterFunction getter) : getter_(getter) { }
  bool operator()(TSHttpTxn txn, TSMBuffer &hdr_buf, TSMLoc &hdr_loc, const char *handles_name) {
    if (!hdr_buf && !hdr_loc) {
      if (getter_(txn, &hdr_buf, &hdr_loc) == TS_SUCCESS) {
        return true;
      }
      else {
        LOG_ERROR("Could not get %s", handles_name);
      }
    }
    else {
      LOG_ERROR("%s already initialized", handles_name);
    }
    return false;
  }
private:
  GetterFunction getter_;
};

} // anonymous namespace

void Transaction::initServerRequest() {
  static initializeHandles initializeServerRequestHandles(TSHttpTxnServerReqGet);
  if (initializeServerRequestHandles(state_->txn_, state_->server_request_hdr_buf_,
                                     state_->server_request_hdr_loc_, "server request")) {
    LOG_DEBUG("Initializing server request");
    state_->server_request_.init(state_->server_request_hdr_buf_, state_->server_request_hdr_loc_);
  }
}

void Transaction::initServerResponse() {
  static initializeHandles initializeServerResponseHandles(TSHttpTxnServerRespGet);
  if (initializeServerResponseHandles(state_->txn_, state_->server_response_hdr_buf_,
                                      state_->server_response_hdr_loc_, "server response")) {
    LOG_DEBUG("Initializing server response");
    state_->server_response_.init(state_->server_response_hdr_buf_, state_->server_response_hdr_loc_);
  }
}

void Transaction::initClientResponse() {
  static initializeHandles initializeClientResponseHandles(TSHttpTxnClientRespGet);
  if (initializeClientResponseHandles(state_->txn_, state_->client_response_hdr_buf_,
                                      state_->client_response_hdr_loc_, "client response")) {
    LOG_DEBUG("Initializing client response");
    state_->client_response_.init(state_->client_response_hdr_buf_, state_->client_response_hdr_loc_);
  }
}
