/** @file

  This file used for catch based tests. It is the main() stub.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
  See the NOTICE file distributed with this work for additional information regarding copyright
  ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance with the License.  You may obtain a
  copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
 */

#include "tscore/Diags.h"

class CatchDiags : public Diags
{
public:
  mutable std::vector<std::string> messages;

  CatchDiags() : Diags("catch", "", "", nullptr) {}

  void
  error_va(DiagsLevel diags_level, const SourceLocation *loc, const char *fmt, va_list ap) const override
  {
    char buff[32768];
    vsnprintf(buff, sizeof(buff), fmt, ap);
    messages.push_back(std::string{buff});
    va_end(ap);
  }
};
