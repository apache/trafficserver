/** @file
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

#include "ts/ts.h"

#include <limits>

/**
  represents a value parsed from a Range request header field.
  Range is converted from a closed range into a half open.
 */

struct Range {
public:
  static int64_t constexpr maxval = (std::numeric_limits<int64_t>::max() >> 2);

  int64_t m_beg = -1;
  int64_t m_end = -1; // half open

  Range() {}
  explicit Range(int64_t const begin, int64_t const end) : m_beg(begin), m_end(end) {}

  bool isValid() const;

  int64_t size() const;

  /** parse a from a closed request range into a half open range
   * This will only correctly handle the *first* range that is
   * parsed via TSMimeHdrFieldValueStringGet with index '0'.
   * Range representing last N bytes will be coded as (-N, 0)
   */
  bool fromStringClosed(char const *const rangestr);

  /** parse a from a closed request range into a half open range
   */
  bool toStringClosed(char *const rangestr, int *const rangelen) const;

  /** block number of first range block
   */
  int64_t firstBlockFor(int64_t const blockbytes) const;

  /** block number of last (inclusive) range block
   */
  int64_t lastBlockFor(int64_t const blockbytes) const;

  /** block intersection
   */
  Range intersectedWith(Range const &other) const;

  /** is the given block inside held range?
   */
  bool blockIsInside(int64_t const blocksize, int64_t const blocknum) const;

  /** number of skip bytes for the given block
   */
  int64_t skipBytesForBlock(int64_t const blocksize, int64_t const blocknum) const;

  /** is this coded to indicate last N bytes?
   */
  bool isEndBytes() const;
};
