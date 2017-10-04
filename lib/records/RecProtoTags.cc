/** @file

  Protocol Tags

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

#include "RecProtoTags.h"

void
ts_session_protocol_well_known_name_tags_init()
{
  TSProtoTags.put(TS_PROTO_TAG_HTTP_1_0);
  TSProtoTags.put(TS_PROTO_TAG_HTTP_1_1);
  TSProtoTags.put(TS_PROTO_TAG_HTTP_2_0);
  TSProtoTags.put(TS_PROTO_TAG_TLS_1_3);
  TSProtoTags.put(TS_PROTO_TAG_TLS_1_2);
  TSProtoTags.put(TS_PROTO_TAG_TLS_1_1);
  TSProtoTags.put(TS_PROTO_TAG_TLS_1_0);
  TSProtoTags.put(TS_PROTO_TAG_TCP);
  TSProtoTags.put(TS_PROTO_TAG_UDP);
  TSProtoTags.put(TS_PROTO_TAG_IPV4);
  TSProtoTags.put(TS_PROTO_TAG_IPV6);
}

const char *
RecNormalizeProtoTag(const char *tag)
{
  return TSProtoTags.get(tag);
}
