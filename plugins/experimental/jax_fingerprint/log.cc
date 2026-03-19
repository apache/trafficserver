/** @file

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

#include "plugin.h"
#include "log.h"

#include "ts/ts.h"

#include <string>

bool
create_log_file(const std::string &filename, TSTextLogObject &log_handle)
{
  return (TS_SUCCESS == TSTextLogObjectCreate(filename.c_str(), TS_LOG_MODE_ADD_TIMESTAMP, &log_handle));
}

void
log_fingerprint(const JAxContext *ctx, TSTextLogObject &log_handle)
{
  if (log_handle == nullptr) {
    Dbg(dbg_ctl, "Log handle is not initialized.");
    return;
  }
  if (TS_ERROR == TSTextLogObjectWrite(log_handle, "Client: %s\t%s: %s", ctx->get_addr(), ctx->get_method_name(),
                                       ctx->get_fingerprint().c_str())) {
    Dbg(dbg_ctl, "Failed to write to log!");
  }
}
