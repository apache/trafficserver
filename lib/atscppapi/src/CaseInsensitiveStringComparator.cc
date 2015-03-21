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
 * @file CaseInsensitiveStringComparator.cc
 */

#include "atscppapi/CaseInsensitiveStringComparator.h"

namespace
{
static char NORMALIZED_CHARACTERS[256];
static volatile bool normalizer_initialized(false);
}

using atscppapi::CaseInsensitiveStringComparator;
using std::string;

bool CaseInsensitiveStringComparator::operator()(const string &lhs, const string &rhs) const
{
  return (compare(lhs, rhs) < 0);
}

int
CaseInsensitiveStringComparator::compare(const string &lhs, const string &rhs) const
{
  if (!normalizer_initialized) {
    // this initialization is safe to execute in concurrent threads - hence no locking
    for (int i = 0; i < 256; ++i) {
      NORMALIZED_CHARACTERS[i] = static_cast<unsigned char>(i);
    }
    for (unsigned char i = 'A'; i < 'Z'; ++i) {
      NORMALIZED_CHARACTERS[i] = 'a' + (i - 'A');
    }
    normalizer_initialized = true;
  }
  size_t lhs_size = lhs.size();
  size_t rhs_size = rhs.size();
  if ((lhs_size > 0) && (rhs_size > 0)) {
    size_t num_chars_to_compare = (lhs_size < rhs_size) ? lhs_size : rhs_size;
    for (size_t i = 0; i < num_chars_to_compare; ++i) {
      unsigned char normalized_lhs_char = NORMALIZED_CHARACTERS[static_cast<const unsigned char>(lhs[i])];
      unsigned char normalized_rhs_char = NORMALIZED_CHARACTERS[static_cast<const unsigned char>(rhs[i])];
      if (normalized_lhs_char < normalized_rhs_char) {
        return -1;
      }
      if (normalized_lhs_char > normalized_rhs_char) {
        return 1;
      }
    }
  }
  if (lhs_size < rhs_size) {
    return -1;
  }
  if (lhs_size > rhs_size) {
    return 1;
  }
  return 0; // both strings are equal
}
