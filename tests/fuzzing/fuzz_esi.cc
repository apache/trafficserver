/** @file

  fuzzing plugins/esi

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

#include "EsiParser.h"
#include "mgmt/rpc/handlers/common/Utils.h"
#include "DocNode.h"

#define kMinInputLength 10
#define kMaxInputLength 1024

void
Debug(const char *tag, const char *fmt, ...)
{
}
void
Error(const char *fmt, ...)
{
}

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *input_data, size_t size_data)
{
  if (size_data < kMinInputLength || size_data > kMaxInputLength) {
    return 1;
  }

  std::string input(reinterpret_cast<const char *>(input_data), size_data);

  EsiLib::Utils::init(&Debug, &Error);
  EsiParser parser("parser_fuzzing", &Debug, &Error);

  EsiLib::DocNodeList node_list;
  bool                ret = parser.completeParse(node_list, input);

  if (ret == true) {
    EsiLib::DocNodeList node_list2;
    std::string         packed = node_list.pack();
    node_list2.unpack(packed);
    node_list2.clear();
  }
  node_list.clear();

  return 0;
}
