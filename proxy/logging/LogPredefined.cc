/** @file

  This file implements predefined log formats

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

#include "LogObject.h"
#include "LogConfig.h"
#include "LogFormat.h"
#include "LogPredefined.h"

PreDefinedFormatInfo *
MakePredefinedErrorLog(LogConfig *config)
{
  LogFormat *fmt;

  fmt = MakeTextLogFormat("error");
  config->global_format_list.add(fmt, false);

  // The error log is always ASCII, and currently does not work correctly with log collation.
  return new PreDefinedFormatInfo(fmt, "error.log", NULL /* no log header */, LOG_FILE_ASCII, false);
}

PreDefinedFormatList::PreDefinedFormatList()
{
}

PreDefinedFormatList::~PreDefinedFormatList()
{
  PreDefinedFormatInfo *info;
  while (!this->formats.empty()) {
    info = this->formats.pop();
    delete info;
  }
}

void
PreDefinedFormatList::init(LogConfig *config)
{
  LogFormat *fmt;

// All these predefined formats work with log collation. They are optionally binary or ASCII, each
// with a different config option.
#define make_format(is_ascii) ((is_ascii) ? LOG_FILE_ASCII : LOG_FILE_BINARY)
#undef make_format
}
