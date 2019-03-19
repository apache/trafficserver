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
 * @file InterceptPlugin.cc
 */

#include "tscpp/api/InterceptPlugin.h"

#include "ts/ts.h"
#include "logging_internal.h"
#include "tscpp/api/noncopyable.h"
#include "utils_internal.h"

#include <cstdlib>
#include <cerrno>

#ifndef INT64_MAX
#define INT64_MAX (9223372036854775807LL)
#endif

using namespace atscppapi;
using std::string;

/**
 * @private
 */
struct InterceptPlugin::State {
  TSCont cont_;
  TSVConn net_vc_ = nullptr;

  struct IoHandle {
    TSVIO vio_               = nullptr;
    TSIOBuffer buffer_       = nullptr;
    TSIOBufferReader reader_ = nullptr;
    IoHandle(){};
    ~IoHandle()
    {
      if (reader_) {
        TSIOBufferReaderFree(reader_);
      }
      if (buffer_) {
        TSIOBufferDestroy(buffer_);
      }
    };
  };

  IoHandle input_;
  IoHandle output_;

  /** the API doesn't recognize end of input; so we have to explicitly
   * figure out when to continue reading and when to stop */
  TSHttpParser http_parser_;
  int expected_body_size_  = 0;
  int num_body_bytes_read_ = 0;
  bool hdr_parsed_         = false;

  TSMBuffer hdr_buf_     = nullptr;
  TSMLoc hdr_loc_        = nullptr;
  int num_bytes_written_ = 0;
  std::shared_ptr<Mutex> plugin_mutex_;
  InterceptPlugin *plugin_ = nullptr;
  Headers request_headers_;

  /** these two fields to be used by the continuation callback only */
  TSEvent saved_event_ = TS_EVENT_NONE;
  void *saved_edata_   = nullptr;

  TSAction timeout_action_ = nullptr;
  bool plugin_io_done_     = false;

  State(TSCont cont, InterceptPlugin *plugin) : cont_(cont), plugin_(plugin)
  {
    plugin_mutex_ = plugin->getMutex();
    http_parser_  = TSHttpParserCreate();
  }

  ~State()
  {
    TSHttpParserDestroy(http_parser_);
    if (hdr_loc_) {
      TSHandleMLocRelease(hdr_buf_, TS_NULL_MLOC, hdr_loc_);
    }
    if (hdr_buf_) {
      TSMBufferDestroy(hdr_buf_);
    }
  }
};

namespace
{
int handleEvents(TSCont cont, TSEvent event, void *edata);
void destroyCont(InterceptPlugin::State *state);
} // namespace

InterceptPlugin::InterceptPlugin(Transaction &transaction, InterceptPlugin::Type type) : TransactionPlugin(transaction)
{
  TSCont cont = TSContCreate(handleEvents, TSMutexCreate());
  state_      = new State(cont, this);
  TSContDataSet(cont, state_);
  TSHttpTxn txn = static_cast<TSHttpTxn>(transaction.getAtsHandle());
  if (type == SERVER_INTERCEPT) {
    TSHttpTxnServerIntercept(cont, txn);
  } else {
    TSHttpTxnIntercept(cont, txn);
  }
}

InterceptPlugin::~InterceptPlugin()
{
  if (state_->cont_) {
    LOG_DEBUG("Relying on callback for cleanup");
    state_->plugin_ = nullptr; // prevent callback from invoking plugin
  } else {                     // safe to cleanup
    LOG_DEBUG("Normal cleanup");
    delete state_;
  }
}

bool
InterceptPlugin::produce(const void *data, int data_size)
{
  std::lock_guard<Mutex> lock(*getMutex());
  if (!state_->net_vc_) {
    LOG_ERROR("Intercept not operational");
    return false;
  }
  if (!state_->output_.buffer_) {
    state_->output_.buffer_ = TSIOBufferCreate();
    state_->output_.reader_ = TSIOBufferReaderAlloc(state_->output_.buffer_);
    state_->output_.vio_    = TSVConnWrite(state_->net_vc_, state_->cont_, state_->output_.reader_, INT64_MAX);
  }
  int num_bytes_written = TSIOBufferWrite(state_->output_.buffer_, data, data_size);
  if (num_bytes_written != data_size) {
    LOG_ERROR("Error while writing to buffer! Attempted %d bytes but only wrote %d bytes", data_size, num_bytes_written);
    return false;
  }
  TSVIOReenable(state_->output_.vio_);
  state_->num_bytes_written_ += data_size;
  LOG_DEBUG("Wrote %d bytes in response", data_size);
  return true;
}

bool
InterceptPlugin::setOutputComplete()
{
  std::lock_guard<Mutex> scopedLock(*getMutex());
  if (!state_->net_vc_) {
    LOG_ERROR("Intercept not operational");
    return false;
  }
  if (!state_->output_.buffer_) {
    LOG_ERROR("No output produced so far");
    return false;
  }
  TSVIONBytesSet(state_->output_.vio_, state_->num_bytes_written_);
  TSVIOReenable(state_->output_.vio_);
  state_->plugin_io_done_ = true;
  LOG_DEBUG("Response complete");
  return true;
}

Headers &
InterceptPlugin::getRequestHeaders()
{
  return state_->request_headers_;
}

bool
InterceptPlugin::doRead()
{
  int avail = TSIOBufferReaderAvail(state_->input_.reader_);
  if (avail == TS_ERROR) {
    LOG_ERROR("Error while getting number of bytes available");
    return false;
  }

  int consumed = 0; // consumed is used to update the input buffers
  if (avail > 0) {
    int64_t num_body_bytes_in_block;
    int64_t data_len; // size of all data (header + body) in a block
    const char *data, *startptr;
    TSIOBufferBlock block = TSIOBufferReaderStart(state_->input_.reader_);
    while (block != nullptr) {
      startptr = data         = TSIOBufferBlockReadStart(block, state_->input_.reader_, &data_len);
      num_body_bytes_in_block = 0;
      if (!state_->hdr_parsed_) {
        const char *endptr = data + data_len;
        if (TSHttpHdrParseReq(state_->http_parser_, state_->hdr_buf_, state_->hdr_loc_, &data, endptr) == TS_PARSE_DONE) {
          LOG_DEBUG("Parsed header");
          string content_length_str = state_->request_headers_.value("Content-Length");
          if (!content_length_str.empty()) {
            const char *start_ptr = content_length_str.data();
            char *end_ptr;
            int content_length = strtol(start_ptr, &end_ptr, 10 /* base */);
            if ((errno != ERANGE) && (end_ptr != start_ptr) && (*end_ptr == '\0')) {
              LOG_DEBUG("Got content length: %d", content_length);
              state_->expected_body_size_ = content_length;
            } else {
              LOG_ERROR("Invalid content length header [%s]; Assuming no content", content_length_str.c_str());
            }
          }
          if (state_->request_headers_.value("Transfer-Encoding") == "chunked") {
            // implementing a "dechunker" is non-trivial and in the real
            // world, most browsers don't send chunked requests
            LOG_ERROR("Support for chunked request not implemented! Assuming no body");
          }
          LOG_DEBUG("Expecting %d bytes of request body", state_->expected_body_size_);
          state_->hdr_parsed_ = true;
          // remaining data in this block is body; 'data' will be pointing to first byte of the body
          num_body_bytes_in_block = endptr - data;
        }
        consume(string(startptr, data - startptr), InterceptPlugin::REQUEST_HEADER);
      } else {
        num_body_bytes_in_block = data_len;
      }
      if (num_body_bytes_in_block) {
        state_->num_body_bytes_read_ += num_body_bytes_in_block;
        consume(string(data, num_body_bytes_in_block), InterceptPlugin::REQUEST_BODY);
      }
      consumed += data_len;
      block = TSIOBufferBlockNext(block);
    }
  }
  LOG_DEBUG("Consumed %d bytes from input vio", consumed);
  TSIOBufferReaderConsume(state_->input_.reader_, consumed);

  // Modify the input VIO to reflect how much data we've completed.
  TSVIONDoneSet(state_->input_.vio_, TSVIONDoneGet(state_->input_.vio_) + consumed);

  if (isWebsocket()) {
    TSVIOReenable(state_->input_.vio_);
    return true;
  }

  if ((state_->hdr_parsed_) && (state_->num_body_bytes_read_ >= state_->expected_body_size_)) {
    LOG_DEBUG("Completely read body");
    if (state_->num_body_bytes_read_ > state_->expected_body_size_) {
      LOG_ERROR("Read more data than specified in request");
      // TODO: any further action required?
    }
    handleInputComplete();
  } else {
    LOG_DEBUG("Reenabling input vio as %d bytes still need to be read", state_->expected_body_size_ - state_->num_body_bytes_read_);
    TSVIOReenable(state_->input_.vio_);
  }
  return true;
}

void
InterceptPlugin::handleEvent(int abstract_event, void *edata)
{
  TSEvent event = static_cast<TSEvent>(abstract_event);
  LOG_DEBUG("Received event %d", event);

  switch (event) {
  case TS_EVENT_NET_ACCEPT:
    LOG_DEBUG("Handling net accept");
    state_->net_vc_        = static_cast<TSVConn>(edata);
    state_->input_.buffer_ = TSIOBufferCreate();
    state_->input_.reader_ = TSIOBufferReaderAlloc(state_->input_.buffer_);
    state_->input_.vio_    = TSVConnRead(state_->net_vc_, state_->cont_, state_->input_.buffer_,
                                      INT64_MAX /* number of bytes to read - high value initially */);

    state_->hdr_buf_ = TSMBufferCreate();
    state_->hdr_loc_ = TSHttpHdrCreate(state_->hdr_buf_);
    state_->request_headers_.reset(state_->hdr_buf_, state_->hdr_loc_);
    TSHttpHdrTypeSet(state_->hdr_buf_, state_->hdr_loc_, TS_HTTP_TYPE_REQUEST);
    break;

  case TS_EVENT_VCONN_WRITE_READY: // nothing to do
    LOG_DEBUG("Got write ready");
    break;

  case TS_EVENT_VCONN_READ_READY:
    LOG_DEBUG("Handling read ready");
    if (doRead()) {
      break;
    }
    // else fall through into the next shut down cases
    LOG_ERROR("Error while reading request!");
    // fallthrough

  case TS_EVENT_VCONN_READ_COMPLETE: // fall throughs intentional
  case TS_EVENT_VCONN_WRITE_COMPLETE:
  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_ERROR:             // erroring out, nothing more to do
  case TS_EVENT_NET_ACCEPT_FAILED: // somebody canceled the transaction
    if (event == TS_EVENT_ERROR) {
      LOG_ERROR("Unknown Error!");
    } else if (event == TS_EVENT_NET_ACCEPT_FAILED) {
      LOG_ERROR("Got net_accept_failed!");
    }
    LOG_DEBUG("Shutting down intercept");
    destroyCont(state_);
    break;

  default:
    LOG_ERROR("Unknown event %d", event);
  }
}

namespace
{
class TryLockGuard
{
public:
  TryLockGuard(Mutex &m) : _m(m), _isLocked(m.try_lock()) {}

  bool
  isLocked() const
  {
    return _isLocked;
  }

  ~TryLockGuard()
  {
    if (_isLocked) {
      _m.unlock();
    }
  }

private:
  std::recursive_mutex &_m;
  const bool _isLocked;
};

int
handleEvents(TSCont cont, TSEvent pristine_event, void *pristine_edata)
{
  // Separating pristine and mutable data helps debugging
  TSEvent event = pristine_event;
  void *edata   = pristine_edata;

  InterceptPlugin::State *state = static_cast<InterceptPlugin::State *>(TSContDataGet(cont));
  if (!state) { // plugin is done, return.
    return 0;
  }

  TryLockGuard scopedTryLock(*(state->plugin_mutex_));
  if (!scopedTryLock.isLocked()) {
    LOG_ERROR("Couldn't get plugin lock. Will retry");
    if (event != TS_EVENT_TIMEOUT) { // save only "non-retry" info
      state->saved_event_ = event;
      state->saved_edata_ = edata;
    }
    state->timeout_action_ = TSContScheduleOnPool(cont, 1, TS_THREAD_POOL_NET);
    return 0;
  }
  if (event == TS_EVENT_TIMEOUT) { // we have a saved event to restore
    state->timeout_action_ = nullptr;
    if (state->plugin_io_done_) { // plugin is done, so can't send it saved event
      event = TS_EVENT_VCONN_EOS; // fake completion
      edata = nullptr;
    } else {
      event = state->saved_event_;
      edata = state->saved_edata_;
    }
  }
  if (state->plugin_) {
    utils::internal::dispatchInterceptEvent(state->plugin_, event, edata);
  } else { // plugin was destroyed before intercept was completed; cleaning up here
    LOG_DEBUG("Cleaning up as intercept plugin is already destroyed");
    destroyCont(state);
    TSContDataSet(cont, nullptr);
    delete state;
  }
  return 0;
}

void
destroyCont(InterceptPlugin::State *state)
{
  if (state->net_vc_) {
    TSVConnShutdown(state->net_vc_, 1, 1);
    TSVConnClose(state->net_vc_);
    state->net_vc_ = nullptr;
  }

  if (state->cont_) {
    if (state->timeout_action_) {
      TSActionCancel(state->timeout_action_);
      state->timeout_action_ = nullptr;
    }
    TSContDestroy(state->cont_);
    state->cont_ = nullptr;
  }
}
} // namespace
