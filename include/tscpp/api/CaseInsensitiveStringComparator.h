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
 * @file CaseInsensitiveStringComparator.h
 * @brief A case insensitive comparator that can be used with STL containers.
 */

#pragma once

#include <string>

namespace atscppapi
{
/**
 * @brief A case insensitive comparator that can be used with standard library containers.
 *
 * The primary use for this class is to make all Headers case insensitive.
 */
class CaseInsensitiveStringComparator
{
public:
  /**
   * @return true if lhs is lexicographically "less-than" rhs; meant for use in std::map or other standard library containers.
   */
  bool operator()(const std::string &lhs, const std::string &rhs) const;

  /**
   * @return numerical value of lexicographical comparison a la strcmp
   */
  int compare(const std::string &lhs, const std::string &rhs) const;
};
} // namespace atscppapi
