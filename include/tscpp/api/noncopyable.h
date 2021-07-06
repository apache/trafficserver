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
 * @file noncopyable.h
 * @brief A base class used to prevent derived classes from being copyable, this effectively
 * eliminates the copy constructor and assignment operator.
 */

#pragma once

namespace atscppapi
{
/**
 * @brief noncopyable is a base class that will prevent derived classes from being copied.
 *
 * @private Prevent Doxygen from showing this class in the inheritance diagrams.
 *
 * To use noncopyable you only need to inherit from this class and you're derived class
 * will become uncopyable
 *
 * \code
 * class myClass : uncopyable {
 * public:
 *  int test_;
 * }
 *
 * // The following will fail:
 * myClass a;
 * myClass b(a); // fails
 * myClass c = a; // fails
 * \endcode
 */
class noncopyable
{
protected:
  noncopyable() {}
  ~noncopyable() {}

  noncopyable(const noncopyable &) = delete;
  const noncopyable &operator=(const noncopyable &) = delete;
};

} // namespace atscppapi
