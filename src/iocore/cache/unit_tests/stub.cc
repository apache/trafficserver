/** @file

  Stub file for linking libinknet.a from unit tests

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

#include "ts/apidefs.h"
TSVConn
TSHttpConnectWithPluginId(sockaddr const * /* addr ATS_UNUSED */, const char * /* tag ATS_UNUSED */, int64_t /* id ATS_UNUSED */)
{
  return TSVConn{};
}

int         TS_MIME_LEN_CONTENT_LENGTH   = 0;
const char *TS_MIME_FIELD_CONTENT_LENGTH = "";

TSIOBufferBlock
TSIOBufferReaderStart(TSIOBufferReader /* readerp ATS_UNUSED */)
{
  return TSIOBufferBlock{};
}

TSIOBufferBlock
TSIOBufferBlockNext(TSIOBufferBlock /* blockp ATS_UNUSED */)
{
  return TSIOBufferBlock{};
}

const char *
TSIOBufferBlockReadStart(TSIOBufferBlock /* blockp ATS_UNUSED */, TSIOBufferReader /* readerp ATS_UNUSED */,
                         int64_t * /* avail ATS_UNUSED */)
{
  return "";
}

void
TSIOBufferReaderConsume(TSIOBufferReader /* readerp ATS_UNUSED */, int64_t /* nbytes ATS_UNUSED */)
{
}

#include "proxy/FetchSM.h"
ClassAllocator<FetchSM> FetchSMAllocator("unusedFetchSMAllocator");
void
FetchSM::ext_launch()
{
}
void
FetchSM::ext_destroy()
{
}
ssize_t
FetchSM::ext_read_data(char *, unsigned long)
{
  return 0;
}
void
FetchSM::ext_add_header(char const *, int, char const *, int)
{
}
void
FetchSM::ext_write_data(void const *, unsigned long)
{
}
void *
FetchSM::ext_get_user_data()
{
  return nullptr;
}
void
FetchSM::ext_set_user_data(void *)
{
}
void
FetchSM::ext_init(Continuation *, char const *, char const *, char const *, sockaddr const *, int)
{
}
