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
 * @file shared_ptr.h
 *
 * Shared pointer declaration.
 */

#pragma once
#ifndef ASTCPPAPI_SHARED_PTR_H_
#define ASTCPPAPI_SHARED_PTR_H_

#include "ink_autoconf.h"

#if HAVE_STD_SHARED_PTR
#  include <memory>
#else
#  include <tr1/memory>
#endif

namespace atscppapi {

/**
 * Force the use of std::tr1::shared_ptr
 * \todo Consider adding a simple macro to check if c++0x/11 is enabled
 * and if so change it to std::shared_ptr and #include <memory>s
 */
#if HAVE_STD_SHARED_PTR
  using std::shared_ptr;
#else
  using std::tr1::shared_ptr;
#endif

} /* atscppapi */

#endif /* SHARED_PTR_H_ */
