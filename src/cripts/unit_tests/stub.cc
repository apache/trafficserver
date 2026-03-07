/*
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

// Stubs for the ATS plugin API symbols referenced by CacheGroup.cc.
// Only the Group class (not Manager) is exercised in unit tests, so the
// TSCont/TSAction/TSMutex stubs never execute — they just satisfy the linker.

#include "ts/ts.h"

void
TSWarning(const char * /* fmt */, ...)
{
}

void
TSError(const char * /* fmt */, ...)
{
}

TSCont
TSContCreate(TSEventFunc, TSMutex)
{
  return nullptr;
}

void
TSContDataSet(TSCont, void *)
{
}

void *
TSContDataGet(TSCont)
{
  return nullptr;
}

TSMutex
TSMutexCreate()
{
  return nullptr;
}

TSAction
TSContScheduleEveryOnPool(TSCont, TSHRTime, TSThreadPool)
{
  return nullptr;
}

void
TSActionCancel(TSAction)
{
}

void
TSContDestroy(TSCont)
{
}

const char *
TSRuntimeDirGet()
{
  return "/tmp";
}
