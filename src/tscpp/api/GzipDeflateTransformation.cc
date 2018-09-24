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
 * @file GzipDeflateTransformation.cc
 */

#include <cstring>
#include <vector>
#include <zlib.h>
#include <cinttypes>
#include <string_view>
#include "tscpp/api/TransformationPlugin.h"
#include "tscpp/api/GzipDeflateTransformation.h"
#include "logging_internal.h"

using namespace atscppapi::transformations;
using std::vector;

namespace
{
const int GZIP_MEM_LEVEL  = 8;
const int WINDOW_BITS     = 31; // Always use 31 for gzip.
const unsigned int ONE_KB = 1024;
} // namespace

/**
 * @private
 */
struct atscppapi::transformations::GzipDeflateTransformationState : noncopyable {
  z_stream z_stream_;
  bool z_stream_initialized_;
  TransformationPlugin::Type transformation_type_;
  int64_t bytes_produced_;

  GzipDeflateTransformationState(TransformationPlugin::Type type)
    : z_stream_initialized_(false), transformation_type_(type), bytes_produced_(0)
  {
    memset(&z_stream_, 0, sizeof(z_stream_));
    int err = deflateInit2(&z_stream_, Z_DEFAULT_COMPRESSION, Z_DEFLATED, WINDOW_BITS, GZIP_MEM_LEVEL, Z_DEFAULT_STRATEGY);

    if (Z_OK != err) {
      LOG_ERROR("deflateInit2 failed with error code '%d'.", err);
    } else {
      z_stream_initialized_ = true;
    }
  };

  ~GzipDeflateTransformationState()
  {
    if (z_stream_initialized_) {
      deflateEnd(&z_stream_);
    }
  };
};

GzipDeflateTransformation::GzipDeflateTransformation(Transaction &transaction, TransformationPlugin::Type type)
  : TransformationPlugin(transaction, type)
{
  state_ = new GzipDeflateTransformationState(type);
}

GzipDeflateTransformation::~GzipDeflateTransformation()
{
  delete state_;
}

void
GzipDeflateTransformation::consume(std::string_view data)
{
  if (data.size() == 0) {
    return;
  }

  if (!state_->z_stream_initialized_) {
    LOG_ERROR("Unable to deflate output because the z_stream was not initialized.");
    return;
  }

  int iteration               = 0;
  state_->z_stream_.data_type = Z_ASCII;
  state_->z_stream_.next_in   = reinterpret_cast<unsigned char *>(const_cast<char *>(data.data()));
  state_->z_stream_.avail_in  = data.length();

  // For small payloads the size can actually be greater than the original input
  // so we'll use twice the original size to avoid needless repeated calls to deflate.
  unsigned long buffer_size = (data.length() < ONE_KB) ? 2 * ONE_KB : data.length();
  vector<unsigned char> buffer(buffer_size);

  do {
    LOG_DEBUG("Iteration %d: Deflate will compress %ld bytes", ++iteration, data.size());
    state_->z_stream_.avail_out = buffer_size;
    state_->z_stream_.next_out  = &buffer[0];

    int err = deflate(&state_->z_stream_, Z_SYNC_FLUSH);
    if (Z_OK != err) {
      state_->z_stream_.next_out = nullptr;
      LOG_ERROR("Iteration %d: Deflate failed to compress %ld bytes with error code '%d'", iteration, data.size(), err);
      return;
    }

    int bytes_to_write = buffer_size - state_->z_stream_.avail_out;
    state_->bytes_produced_ += bytes_to_write;

    LOG_DEBUG("Iteration %d: Deflate compressed %ld bytes to %d bytes, producing output...", iteration, data.size(),
              bytes_to_write);
    produce(std::string_view(reinterpret_cast<char *>(&buffer[0]), static_cast<size_t>(bytes_to_write)));
  } while (state_->z_stream_.avail_out == 0);

  state_->z_stream_.next_out = nullptr;

  if (state_->z_stream_.avail_in != 0) {
    LOG_ERROR("Inflate finished with data still remaining in the buffer of size '%u'", state_->z_stream_.avail_in);
  }
}

void
GzipDeflateTransformation::handleInputComplete()
{
  // We will flush out anything that's remaining in the gzip buffer
  int status            = Z_OK;
  int iteration         = 0;
  const int buffer_size = 1024; // 1024 bytes is usually more than enough for the epilouge
  unsigned char buffer[buffer_size];

  /* Deflate remaining data */
  do {
    LOG_DEBUG("Iteration %d: Gzip deflate finalizing.", ++iteration);
    state_->z_stream_.data_type = Z_ASCII;
    state_->z_stream_.avail_out = buffer_size;
    state_->z_stream_.next_out  = buffer;

    status = deflate(&state_->z_stream_, Z_FINISH);

    int bytes_to_write = buffer_size - state_->z_stream_.avail_out;
    state_->bytes_produced_ += bytes_to_write;

    if (status == Z_OK || status == Z_STREAM_END) {
      LOG_DEBUG("Iteration %d: Gzip deflate finalize had an extra %d bytes to process, status '%d'. Producing output...", iteration,
                bytes_to_write, status);
      produce(std::string_view(reinterpret_cast<char *>(buffer), static_cast<size_t>(bytes_to_write)));
    } else if (status != Z_STREAM_END) {
      LOG_ERROR("Iteration %d: Gzip deflinate finalize produced an error '%d'", iteration, status);
    }
  } while (status == Z_OK);

  int64_t bytes_written = setOutputComplete();
  if (state_->bytes_produced_ != bytes_written) {
    LOG_ERROR("Gzip bytes produced sanity check failed, deflated bytes = %" PRId64 " != written bytes = %" PRId64,
              state_->bytes_produced_, bytes_written);
  }
}
