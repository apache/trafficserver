/** @file

  Primary include file for the libts C++ library

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

  @section details Details

  libts is a collection of useful functions and methods. It
  includes commonly used:
    - data structures like queues, dynamic arrays atomic queues, etc.
    - string manipulation functions
    - bit operation functions
    - Fast-Allocators ...

  The library provides a uniform interface on all platforms making the
  job of porting the applications written using it very easy.

 */

#if !defined(_inktomiplus_h_)
#define _inktomiplus_h_

/* Removed for now, to fix build on Solaris
#define std *** _FIXME_REMOVE_DEPENDENCY_ON_THE_STL_ ***
*/

#include "hugepages.h"
#include "ts/ink_config.h"
#include "ts/ink_platform.h"
#include "ts/ink_align.h"
#include "ts/ink_apidefs.h"
#include "ts/ink_args.h"
#include "ts/ink_assert.h"
#include "ts/ink_atomic.h"
#include "ts/ink_base64.h"
#include "ts/ink_code.h"
#include "ts/ink_defs.h"
#include "ts/ink_error.h"
#include "ts/ink_exception.h"
#include "ts/ink_file.h"
#include "ts/ink_hash_table.h"
#include "ts/ink_hrtime.h"
#include "ts/ink_inout.h"
#include "ts/ink_llqueue.h"
#include "ts/ink_lockfile.h"
#include "ts/ink_memory.h"
#include "ts/ink_mutex.h"
#include "ts/ink_queue.h"
#include "ts/ink_rand.h"
#include "ts/ink_resolver.h"
#include "ts/ink_sock.h"
#include "ts/ink_inet.h"
#include "ts/ink_sprintf.h"
#include "ts/ink_stack_trace.h"
#include "ts/ink_string++.h"
#include "ts/ink_string.h"
#include "ts/ink_syslog.h"
#include "ts/ink_thread.h"
#include "ts/ink_time.h"
#include "ts/fastlz.h"

#include "ts/Allocator.h"
#include "ts/Arena.h"
#include "ts/Bitops.h"
#include "ts/ConsistentHash.h"
#include "ts/DynArray.h"
#include "ts/EventNotify.h"
#include "ts/Hash.h"
#include "ts/HashFNV.h"
#include "ts/HashMD5.h"
#include "ts/HashSip.h"
#include "ts/I_Version.h"
#include "ts/InkPool.h"
#include "ts/List.h"
#include "ts/INK_MD5.h"
#include "ts/MMH.h"
#include "ts/Map.h"
#include "ts/MimeTable.h"
#include "ts/ParseRules.h"
#include "ts/Ptr.h"
#include "ts/RawHashTable.h"
#include "ts/Regex.h"
#include "ts/SimpleTokenizer.h"
#include "ts/TextBuffer.h"
#include "ts/Tokenizer.h"
#include "ts/MatcherUtils.h"
#include "ts/Diags.h"
#include "ts/Regression.h"
#include "ts/HostLookup.h"
#include "ts/InkErrno.h"
#include "ts/Vec.h"
#include "ts/X509HostnameValidator.h"

#endif /*_inktomiplus_h_*/
