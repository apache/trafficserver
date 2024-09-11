/**
  @section license License

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
#pragma once

#include <sstream>
#include <swoc/BufferWriter.h>

/// @brief Simple storage to keep the jsonrpc server's response.
///
///        With small content it will just use the LocalBufferWriter, if the
///        content gets bigger, then it will just save the buffer into a string
///        and reuse the already created LocalBufferWriter. If stored data fits in the
///        original bw, then no need to create the extra string. If message fist in the
///        first chunk, no extra space will be allocated into the storage string.
///        This is not meant to be performant as is mainly used for single request
///        data storage, which will be at some point stored into a string anyways.
/// @note  User should deal with the buffer limit.
///
template <size_t N> class MessageStorage
{
  std::string                _content;
  swoc::LocalBufferWriter<N> _bw;
  size_t                     _written{0};

public:
  char *
  writable_data()
  {
    return _bw.aux_data();
  }

  void
  save(size_t n)
  {
    _bw.commit(n);

    if (_bw.remaining() == 0) { // no more space available, flush what's on the bw
                                // and reset it.
      flush();
    }
  }

  size_t
  available() const
  {
    return _bw.remaining();
  }

  void
  flush()
  {
    if (_bw.size() == 0) {
      return;
    }

    if (_written == 0) {
      _content.reserve(_bw.size());
    } else {
      // need more space.
      _content.reserve(_written + _bw.size());
    }

    _content.append(_bw.data(), _bw.size());
    _written += _bw.size();

    _bw.clear();
  }

  std::string
  str()
  {
    if (stored() <= _bw.size()) {
      // just get it directly from the BW.
      return {_bw.data(), _bw.size()};
    }

    // There is content in the bw that needs to be saved into the internal string.
    flush();
    return _content;
  }

  size_t
  stored() const
  {
    return _written ? _written : _bw.size();
  }
};
