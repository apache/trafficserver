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

#if !defined (_inktomiplus_h_)
#define	_inktomiplus_h_

/* Removed for now, to fix build on Solaris
#define std *** _FIXME_REMOVE_DEPENDENCY_ON_THE_STL_ ***
*/

#include "ink_config.h"
#include "ink_platform.h"
#include "ink_align.h"
#include "ink_apidefs.h"
#include "ink_args.h"
#include "ink_assert.h"
#include "ink_atomic.h"
#include "ink_base64.h"
#include "ink_code.h"
#include "ink_defs.h"
#include "ink_error.h"
#include "ink_exception.h"
#include "ink_file.h"
#include "ink_hash_table.h"
#include "ink_hrtime.h"
#include "ink_inout.h"
#include "ink_llqueue.h"
#include "ink_lockfile.h"
#include "ink_memory.h"
#include "ink_mutex.h"
#include "ink_queue.h"
#include "ink_rand.h"
#include "ink_resolver.h"
#include "ink_sock.h"
#include "ink_inet.h"
#include "ink_sprintf.h"
#include "ink_stack_trace.h"
#include "ink_string++.h"
#include "ink_string.h"
#include "ink_syslog.h"
#include "ink_thread.h"
#include "ink_time.h"
#include "fastlz.h"

#include "Allocator.h"
#include "Arena.h"
#include "Bitops.h"
#include "Compatability.h"
#include "ConsistentHash.h"
#include "DynArray.h"
#include "EventNotify.h"
#include "Hash.h"
#include "HashFNV.h"
#include "HashMD5.h"
#include "HashSip.h"
#include "I_Version.h"
#include "InkPool.h"
#include "List.h"
#include "INK_MD5.h"
#include "MMH.h"
#include "Map.h"
#include "MimeTable.h"
#include "ParseRules.h"
#include "Ptr.h"
#include "RawHashTable.h"
#include "Regex.h"
#include "SimpleTokenizer.h"
#include "TextBuffer.h"
#include "Tokenizer.h"
#include "MatcherUtils.h"
#include "Diags.h"
#include "Regression.h"
#include "HostLookup.h"
#include "InkErrno.h"
#include "Vec.h"

#endif /*_inktomiplus_h_*/
