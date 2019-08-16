/** @file

  A test plugin common testing functionality

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

  Implements code necessary for Reverse Proxy which mostly consists of
  general purpose hostname substitution in URLs.

 */

#include "plugin_testing_common.h"

void
PrintToStdErr(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
}

fs::path
getTemporaryDir()
{
  std::error_code ec;
  fs::path tmpDir = fs::canonical(fs::temp_directory_path(), ec);
  tmpDir /= "sandbox_XXXXXX";

  char dirNameTemplate[tmpDir.string().length() + 1];
  sprintf(dirNameTemplate, "%s", tmpDir.c_str());

  return fs::path(mkdtemp(dirNameTemplate));
}
