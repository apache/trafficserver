/** @file

   fuzzing src/records/HdrsUtils.cc

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

#include "records/RecHttp.h"
#include "tscore/ink_defs.h"
#include "tscore/Diags.h"

#define kMinInputLength 8
#define kMaxInputLength 1024

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *input_data, size_t Size)
{
  if (Size < kMinInputLength || Size > kMaxInputLength) {
    return 1;
  }

  std::string alpn_input((char *)input_data, Size);

  unsigned char alpn_wire_format[MAX_ALPN_STRING] = {0xab};
  int           alpn_wire_format_len              = MAX_ALPN_STRING;

  DiagsPtr::set(new Diags("fuzzing", "", "", nullptr));
  ts_session_protocol_well_known_name_indices_init();

  convert_alpn_to_wire_format(alpn_input, alpn_wire_format, alpn_wire_format_len);

  delete diags();

  return 0;
}
