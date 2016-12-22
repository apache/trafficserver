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

#include <cstring>
#include "atscppapi/CaseInsensitiveStringComparator.h"

using atscppapi::CaseInsensitiveStringComparator;
using std::string;

/**
 * This class should eventually be removed, but because it's in a public API we cannot remove
 * it until the next major release.
 */
bool
CaseInsensitiveStringComparator::operator()(const string &lhs, const string &rhs) const
{
  return (compare(lhs, rhs) < 0);
}

int
CaseInsensitiveStringComparator::compare(const string &lhs, const string &rhs) const
{
  return strcasecmp(lhs.c_str(), rhs.c_str());
}
