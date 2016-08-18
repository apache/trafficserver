/** @file

  A brief file description

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

#ifndef _P_CACHE_H__
#define _P_CACHE_H__

#include "ts/ink_platform.h"
#include "P_EventSystem.h"
#include "P_AIO.h"
#include "I_RecProcess.h"

#ifdef HTTP_CACHE
#include "HTTP.h"
#include "MIME.h"
#include "HttpTransactCache.h"
#endif

#include "I_Cache.h"
#include "P_CacheDisk.h"
#include "P_CacheDir.h"
#include "P_RamCache.h"
#include "P_CacheVol.h"
#include "P_CacheInternal.h"
#include "P_CacheHosting.h"
#include "P_CacheHttp.h"
#endif /* _P_CACHE_H */
