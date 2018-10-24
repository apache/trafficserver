/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "fcgi_protocol.h"
#include <atscppapi/noncopyable.h>
#include <iterator>
#include <map>
#include <string>
#include <ts/ts.h>
#include <cstring> //for memcpy

#define BUF_SIZE 5000

/* Bytes from LSB to MSB 0..3 */

#define BYTE_0(x) ((x)&0xff)
#define BYTE_1(x) ((x) >> 8 & 0xff)
#define BYTE_2(x) ((x) >> 16 & 0xff)
#define BYTE_3(x) ((x) >> 24 | 0x80)

typedef unsigned char uchar;

#define PRINT_OPAQUE_STRUCT(p) print_mem((p), sizeof(*(p)))

#define FCGI_PROCESS_AGAIN 1
#define FCGI_PROCESS_DONE 2
#define FCGI_PROCESS_ERR 3

namespace ats_plugin
{
using namespace atscppapi;

typedef enum {
  fcgi_state_version = 0,
  fcgi_state_type,
  fcgi_state_request_id_hi,
  fcgi_state_request_id_lo,
  fcgi_state_content_len_hi,
  fcgi_state_content_len_lo,
  fcgi_state_padding_len,
  fcgi_state_reserved,
  fcgi_state_content_begin,
  fcgi_state_content_proc,
  fcgi_state_padding,
  fcgi_state_done
} FCGI_State;

struct FCGIClientState;

struct FCGIRecordList {
  FCGI_Header *header;
  FCGI_EndRequestBody *endBody;
  uchar *content;
  FCGI_State state;

  size_t length, offset;

  FCGIRecordList() : content(nullptr), state(FCGI_State::fcgi_state_version), length(0), offset(0)
  {
    header = (FCGI_Header *)TSmalloc(sizeof(FCGI_Header));
    memset(header, 0, sizeof(FCGI_Header));
    endBody = (FCGI_EndRequestBody *)TSmalloc(sizeof(FCGI_EndRequestBody));
    memset(endBody, 0, sizeof(FCGI_EndRequestBody));
  };

  ~FCGIRecordList()
  {
    TSfree(header);
    TSfree(content);
  }
};

class FCGIClientRequest
{
public:
  std::string postData;
  FCGIClientRequest(int request_id, TSHttpTxn txn);
  ~FCGIClientRequest();

  std::map<std::string, std::string> GenerateFcgiRequestHeaders();
  void printFCGIRequestHeaders();

  // Request Creation
  FCGI_BeginRequest *createBeginRequest();
  FCGI_Header *createHeader(unsigned char type);
  void postBodyChunk();
  void emptyParam();

  void serialize(uchar *buffer, void *st, size_t size);
  void fcgiHeaderSetRequestId(FCGI_Header *h, int request_id);
  void fcgiHeaderSetContentLen(FCGI_Header *h, uint16_t len);
  uint32_t fcgiHeaderGetContentLen(FCGI_Header *h);
  uint32_t serializeNameValue(uchar *buffer, const std::pair<std::string, std::string> &it);
  unsigned char *addClientRequest(int &);

  // Response Decoding member functions
  bool fcgiProcessBuffer(uchar *beg_buf, uchar *end_buf, std::ostringstream &output);
  FCGIRecordList *fcgiRecordCreate();
  int fcgiProcessHeader(uchar ch, FCGIRecordList *rec);
  int fcgiProcessContent(uchar **beg_buf, uchar *end_buf, FCGIRecordList *rec);
  int fcgiProcessRecord(uchar **beg_buf, uchar *end_buf, FCGIRecordList *rec);

  bool fcgiDecodeRecordChunk(uchar *beg_buf, size_t remain, std::ostringstream &output);

  void print_bytes(uchar *buf, int n);

protected:
  struct FCGIClientState *state_;

private:
  bool first_chunk;
  FCGIRecordList *_headerRecord;
};
} // namespace ats_plugin
