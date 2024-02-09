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
 * @file ClientRequest.cc
 */

#include "tscpp/api/ClientRequest.h"
#include <cstdlib>
#include "ts/ts.h"
#include "tscpp/api/noncopyable.h"
#include "logging_internal.h"

using namespace atscppapi;

/**
 * @private
 */
struct atscppapi::ClientRequestState : noncopyable {
  TSHttpTxn txn_;
  TSMBuffer pristine_hdr_buf_;
  TSMLoc pristine_url_loc_;
  Url pristine_url_;
  ClientRequestState(TSHttpTxn txn) : txn_(txn), pristine_hdr_buf_(nullptr), pristine_url_loc_(nullptr) {}
};

atscppapi::ClientRequest::ClientRequest(void *ats_txn_handle, void *hdr_buf, void *hdr_loc) : Request(hdr_buf, hdr_loc)
{
  state_ = new ClientRequestState(static_cast<TSHttpTxn>(ats_txn_handle));
}

atscppapi::ClientRequest::~ClientRequest()
{
  if (state_->pristine_url_loc_ && state_->pristine_hdr_buf_) {
    TSMLoc null_parent_loc = nullptr;
    LOG_DEBUG("Releasing pristine url loc for transaction %p; hdr_buf %p, url_loc %p", state_->txn_, state_->pristine_hdr_buf_,
              state_->pristine_url_loc_);
    TSHandleMLocRelease(state_->pristine_hdr_buf_, null_parent_loc, state_->pristine_url_loc_);
  }

  delete state_;
}

const Url &
atscppapi::ClientRequest::getPristineUrl() const
{
  if (!state_->pristine_url_loc_) {
    TSReturnCode ret = TSHttpTxnPristineUrlGet(state_->txn_, &state_->pristine_hdr_buf_, &state_->pristine_url_loc_);

    if ((state_->pristine_hdr_buf_ != nullptr) && (state_->pristine_url_loc_ != nullptr) && ret == TS_SUCCESS) {
      state_->pristine_url_.init(state_->pristine_hdr_buf_, state_->pristine_url_loc_);
      LOG_DEBUG("Pristine URL initialized");
    } else {
      LOG_ERROR("Failed to get pristine URL for transaction %p; hdr_buf %p, url_loc %p", state_->txn_, state_->pristine_hdr_buf_,
                state_->pristine_url_loc_);
    }
  } else {
    LOG_DEBUG("Pristine URL already initialized");
  }

  return state_->pristine_url_;
}
