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
 * @file GzipInflateTransformation.cc
 */

#include <string>
#include <cstring>
#include <vector>
#include <zlib.h>
#include <cinttypes>
#include "atscppapi/TransformationPlugin.h"
#include "atscppapi/GzipInflateTransformation.h"
#include "logging_internal.h"

using namespace atscppapi::transformations;
using std::string;
using std::vector;

namespace
{
const int WINDOW_BITS             = 31; // Always use 31 for gzip.
unsigned int INFLATE_SCALE_FACTOR = 6;
}

/**
 * @private
 */
struct atscppapi::transformations::GzipInflateTransformationState : noncopyable {
  z_stream z_stream_;
  bool z_stream_initialized_;
  int64_t bytes_produced_;
  TransformationPlugin::Type transformation_type_;

  GzipInflateTransformationState(TransformationPlugin::Type type)
    : z_stream_initialized_(false), bytes_produced_(0), transformation_type_(type)
  {
    memset(&z_stream_, 0, sizeof(z_stream_));

    int err = inflateInit2(&z_stream_, WINDOW_BITS);
    if (Z_OK != err) {
      LOG_ERROR("inflateInit2 failed with error code '%d'.", err);
    } else {
      z_stream_initialized_ = true;
    }
  };

  ~GzipInflateTransformationState()
  {
    if (z_stream_initialized_) {
      int err = inflateEnd(&z_stream_);
      if (Z_OK != err && Z_STREAM_END != err) {
        LOG_ERROR("Unable to inflateEnd(), returned error code '%d'", err);
      }
    }
  };
};

GzipInflateTransformation::GzipInflateTransformation(Transaction &transaction, TransformationPlugin::Type type)
  : TransformationPlugin(transaction, type)
{
  state_ = new GzipInflateTransformationState(type);
}

GzipInflateTransformation::~GzipInflateTransformation()
{
  delete state_;
}

void
GzipInflateTransformation::consume(const string &data)
{
  if (data.size() == 0) {
    return;
  }

  if (!state_->z_stream_initialized_) {
    LOG_ERROR("Unable to inflate output because the z_stream was not initialized.");
    return;
  }

  int err                = Z_OK;
  int iteration          = 0;
  int inflate_block_size = INFLATE_SCALE_FACTOR * data.size();
  vector<char> buffer(inflate_block_size);

  // Setup the compressed input
  state_->z_stream_.next_in  = reinterpret_cast<unsigned char *>(const_cast<char *>(data.c_str()));
  state_->z_stream_.avail_in = data.length();

  // Loop while we have more data to inflate
  while (state_->z_stream_.avail_in > 0 && err != Z_STREAM_END) {
    LOG_DEBUG("Iteration %d: Gzip has %d bytes to inflate", ++iteration, state_->z_stream_.avail_in);

    // Setup where the decompressed output will go.
    state_->z_stream_.next_out  = reinterpret_cast<unsigned char *>(&buffer[0]);
    state_->z_stream_.avail_out = inflate_block_size;

    /* Uncompress */
    err = inflate(&state_->z_stream_, Z_SYNC_FLUSH);

    if (err != Z_OK && err != Z_STREAM_END) {
      LOG_ERROR("Iteration %d: Inflate failed with error '%d'", iteration, err);
      state_->z_stream_.next_out = NULL;
      return;
    }

    LOG_DEBUG("Iteration %d: Gzip inflated a total of %d bytes, producingOutput...", iteration,
              (inflate_block_size - state_->z_stream_.avail_out));
    produce(string(&buffer[0], (inflate_block_size - state_->z_stream_.avail_out)));
    state_->bytes_produced_ += (inflate_block_size - state_->z_stream_.avail_out);
  }
  state_->z_stream_.next_out = NULL;
}

void
GzipInflateTransformation::handleInputComplete()
{
  int64_t bytes_written = setOutputComplete();
  if (state_->bytes_produced_ != bytes_written) {
    LOG_ERROR("Gzip bytes produced sanity check failed, inflated bytes = %" PRId64 " != written bytes = %" PRId64,
              state_->bytes_produced_, bytes_written);
  }
}
