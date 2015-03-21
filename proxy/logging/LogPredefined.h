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

#ifndef LOG_PREDEFINED_H
#define LOG_PREDEFINED_H

#include "libts.h"

class LogFormat;
class LogConfig;

// Collects all the necesary info to build a pre-defined object.
struct PreDefinedFormatInfo {
  LogFormat *format;
  const char *filename;
  const char *header;
  LogFileFormat filefmt;
  bool collatable; // whether log collation is supported

  LINK(PreDefinedFormatInfo, link);

  PreDefinedFormatInfo(LogFormat *fmt, const char *fname, const char *hdr, LogFileFormat _f, bool _c)
    : format(fmt), filename(fname), header(hdr), filefmt(_f), collatable(_c)
  {
  }

  static const char *const squid;
  static const char *const common;
  static const char *const extended;
  static const char *const extended2;
};

typedef Queue<PreDefinedFormatInfo> PreDefinedFormatInfoList;

struct PreDefinedFormatList {
  PreDefinedFormatList();
  ~PreDefinedFormatList();

  // Initialize the predefined format list from the given LogConfig. This has the side-effect of
  // adding the predefined LogFormats to the LogConfig global_format_list.
  void init(LogConfig *config);

  PreDefinedFormatInfoList formats;
};

// Return a PreDefinedFormatInfo structure for the ASCII error log.
PreDefinedFormatInfo *MakePredefinedErrorLog(LogConfig *config);

#endif /* LOG_PREDEFINED_H */
