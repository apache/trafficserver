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

#include "ats_fcgi_client.h"
#include "ats_fastcgi.h"
#include "fcgi_protocol.h"
#include "ts/ink_defs.h"
#include <iostream>
#include <sstream>
#include <string>
#include <locale>
#include <ts/ts.h>
#include <map>
#include <iterator>
#include "utils_internal.h"
#include <atscppapi/Headers.h>
#include <atscppapi/utils.h>

using namespace atscppapi;
using namespace ats_plugin;
using namespace std;

struct ats_plugin::FCGIClientState {
  FCGI_BeginRequest *request;
  FCGI_Header *header, *postHeader;
  unsigned char *buff, *pBuffInc;
  FCGIRecordList *records = nullptr;
  TSHttpTxn txn_;
  map<string, string> requestHeaders;
  int request_id_;

  FCGIClientState()
    : request(nullptr), header(nullptr), postHeader(nullptr), buff(nullptr), pBuffInc(nullptr), records(nullptr), request_id_(0){};

  ~FCGIClientState()
  {
    request_id_ = 0;
    free(request);
    free(header);
    TSfree(postHeader);
    TSfree(buff);
    TSfree(records);
  };
};
// input to the constructor will be either unique transaction id or int type
// requestId
FCGIClientRequest::FCGIClientRequest(int request_id, TSHttpTxn txn)
{
  first_chunk            = true;
  state_                 = new FCGIClientState();
  _headerRecord          = nullptr;
  state_->txn_           = txn;
  state_->request_id_    = request_id;
  state_->requestHeaders = GenerateFcgiRequestHeaders();
  // TODO Call printFCGIRequestHeaders() to printFCGIHeaders
  // printFCGIRequestHeaders();
  string str("POST"), value;
  if (str.compare(state_->requestHeaders["REQUEST_METHOD"]) == 0) {
    Transaction &transaction = utils::internal::getTransaction(state_->txn_);
    Headers &h               = transaction.getClientRequest().getHeaders();

    if (h.isInitialized()) {
      string key("Content-Length");
      atscppapi::header_field_iterator it = h.find(key);
      if (it != h.end()) {
        atscppapi::HeaderField hf(*it);
        string value                             = hf.values(","); // delimiter for header values
        state_->requestHeaders["CONTENT_LENGTH"] = value.c_str();
      }

      key = string("Content-type");
      it  = h.find(key);
      if (it != h.end()) {
        HeaderField hf1(*it);
        string value                           = hf1.values(","); // delimiter for header values
        state_->requestHeaders["CONTENT_TYPE"] = value.c_str();
      }
    }

    int contentLength = 0;
    string cl         = state_->requestHeaders["CONTENT_LENGTH"];
    stringstream strToInt(cl);
    strToInt >> contentLength;
    state_->buff = (unsigned char *)TSmalloc(BUF_SIZE + contentLength);
  } else {
    state_->buff = (unsigned char *)TSmalloc(BUF_SIZE);
  }

  state_->pBuffInc = state_->buff;
}

// destructor will reset the client_req_id and delete the recListState_ object
// holding response records received from fcgi server
FCGIClientRequest::~FCGIClientRequest()
{
  if (_headerRecord)
    delete _headerRecord;

  delete state_;
}

bool
endsWith(const std::string &mainStr, const std::string &toMatch)
{
  if (mainStr.size() >= toMatch.size() && mainStr.compare(mainStr.size() - toMatch.size(), toMatch.size(), toMatch) == 0)
    return true;
  else
    return false;
}

map<string, string>
FCGIClientRequest::GenerateFcgiRequestHeaders()
{
  map<string, string> fcgiReqHeader;
  Transaction &transaction = utils::internal::getTransaction(state_->txn_);
  Headers &h               = transaction.getClientRequest().getHeaders();
  if (h.isInitialized()) {
    for (auto it : h) {
      atscppapi::HeaderField hf(it);
      std::string str = hf.name().c_str();
      std::string http("HTTP_");
      std::locale loc;

      for (std::string::size_type i = 0; i < str.length(); ++i) {
        http += std::toupper(str[i], loc);
      }
      fcgiReqHeader[http] = hf.values();
    }
  }

  // if string ends with '/' char then request global html file to server
  string index;
  string requestScript = transaction.getClientRequest().getUrl().getPath();
  if (endsWith(requestScript, "/")) {
    ats_plugin::FcgiPluginConfig *gConfig = InterceptGlobal::plugin_data->getGlobalConfigObj();
    index                                 = gConfig->getHtml();
    requestScript += index;
  }

  fcgiReqHeader["DOCUMENT_ROOT"]     = InterceptGlobal::plugin_data->getGlobalConfigObj()->getDocumentRootDir();
  fcgiReqHeader["SCRIPT_FILENAME"]   = fcgiReqHeader["DOCUMENT_ROOT"] + requestScript;
  fcgiReqHeader["GATEWAY_INTERFACE"] = "FastCGI/1.1";
  fcgiReqHeader["REQUEST_METHOD"]    = HTTP_METHOD_STRINGS[transaction.getClientRequest().getMethod()];
  fcgiReqHeader["SCRIPT_NAME"]       = "/" + requestScript;
  fcgiReqHeader["QUERY_STRING"]      = transaction.getClientRequest().getUrl().getQuery();
  fcgiReqHeader["REQUEST_URI"]       = "/" + requestScript;

  // TODO map fcgiconfig with request headers.
  // atsfcgiconfig::FCGIParams *params      = fcgiGlobal::plugin_data->getGlobalConfigObj()->getFcgiParams();
  // atsfcgiconfig::FCGIParams::iterator it = params->begin();
  // for (it = params->begin(); it != params->end(); ++it)
  //   cout << it->first << " => " << it->second << endl;
  fcgiReqHeader["SERVER_SOFTWARE"] = "ATS 7.1.1";
  fcgiReqHeader["REMOTE_ADDR"]     = "127.0.0.1";
  fcgiReqHeader["REMOTE_PORT"]     = "8090";
  fcgiReqHeader["SERVER_ADDR"]     = "127.0.0.1";
  fcgiReqHeader["SERVER_PORT"]     = "60000";
  fcgiReqHeader["SERVER_NAME"]     = "ATS 7.1.1";
  fcgiReqHeader["SERVER_PROTOCOL"] = "HTTP/1.1";
  fcgiReqHeader["FCGI_ROLE"]       = "RESPONDER";
  return fcgiReqHeader;
}

void
FCGIClientRequest::printFCGIRequestHeaders()
{
  for (const auto &it : state_->requestHeaders) {
    cout << it.first << " => " << it.second << endl;
  }
}

void
FCGIClientRequest::emptyParam()
{
  string str("POST");
  state_->pBuffInc = state_->buff;
  // if Method is not post, then writing empty FCGI_STDIN to buffer
  if (str.compare(state_->requestHeaders["REQUEST_METHOD"]) != 0) {
    state_->postHeader                  = createHeader(FCGI_STDIN);
    state_->postHeader->contentLengthB0 = 0;
    state_->postHeader->contentLengthB1 = 0;
    serialize(state_->pBuffInc, state_->postHeader, sizeof(FCGI_Header));
    state_->pBuffInc += sizeof(FCGI_Header);
    return;
  }
  TSDebug(PLUGIN_NAME, "empty Post Header Len: %ld ", state_->pBuffInc - state_->buff);
}

FCGI_Header *
FCGIClientRequest::createHeader(uchar type)
{
  FCGI_Header *tmp = (FCGI_Header *)calloc(1, sizeof(FCGI_Header));
  tmp->version     = FCGI_VERSION_1;
  tmp->type        = type;
  fcgiHeaderSetRequestId(tmp, state_->request_id_);
  return tmp;
}

FCGI_BeginRequest *
FCGIClientRequest::createBeginRequest()
{
  state_->request = (FCGI_BeginRequest *)TSmalloc(sizeof(FCGI_BeginRequest));
  // TODO send the request id here
  state_->request->header                  = createHeader(FCGI_BEGIN_REQUEST);
  state_->request->body                    = (FCGI_BeginRequestBody *)calloc(1, sizeof(FCGI_BeginRequestBody));
  state_->request->body->roleB0            = FCGI_RESPONDER;
  state_->request->body->flags             = FCGI_KEEP_CONN;
  state_->request->header->contentLengthB0 = sizeof(FCGI_BeginRequestBody);

  // serialize request header
  serialize(state_->pBuffInc, state_->request->header, sizeof(FCGI_Header));
  state_->pBuffInc += sizeof(FCGI_Header);
  serialize(state_->pBuffInc, state_->request->body, sizeof(FCGI_BeginRequestBody));
  state_->pBuffInc += sizeof(FCGI_BeginRequestBody);
  TSDebug(PLUGIN_NAME, "Header Len: %ld ", state_->pBuffInc - state_->buff);
  // FCGI Params headers
  state_->header = createHeader(FCGI_PARAMS);
  int len = 0, nb = 0;

  for (const auto &it : state_->requestHeaders) {
    nb = serializeNameValue(state_->pBuffInc, it);
    len += nb;
  }

  state_->header->contentLengthB0 = BYTE_0(len);
  state_->header->contentLengthB1 = BYTE_1(len);
  TSDebug(PLUGIN_NAME, "ParamsLen: %d ContLenB0: %d ContLenB1: %d", len, state_->header->contentLengthB0,
          state_->header->contentLengthB1);
  serialize(state_->pBuffInc, state_->header, sizeof(FCGI_Header));
  state_->pBuffInc += sizeof(FCGI_Header);

  for (const auto &it : state_->requestHeaders) {
    nb = serializeNameValue(state_->pBuffInc, it);
    state_->pBuffInc += nb;
  }

  state_->header->contentLengthB0 = 0;
  state_->header->contentLengthB1 = 0;
  serialize(state_->pBuffInc, state_->header, sizeof(FCGI_Header));
  state_->pBuffInc += sizeof(FCGI_Header);
  return state_->request;
}

void
FCGIClientRequest::postBodyChunk()
{
  state_->pBuffInc   = state_->buff;
  int dataLen        = 0;
  state_->postHeader = createHeader(FCGI_STDIN);
  dataLen            = postData.length();

  state_->postHeader->contentLengthB0 = BYTE_0(dataLen);
  state_->postHeader->contentLengthB1 = BYTE_1(dataLen);
  serialize(state_->pBuffInc, state_->postHeader, sizeof(FCGI_Header));
  state_->pBuffInc += sizeof(FCGI_Header);
  memcpy(state_->pBuffInc, postData.c_str(), dataLen);
  state_->pBuffInc += dataLen;

  state_->postHeader->contentLengthB0 = 0;
  state_->postHeader->contentLengthB1 = 0;
  serialize(state_->pBuffInc, state_->postHeader, sizeof(FCGI_Header));
  state_->pBuffInc += sizeof(FCGI_Header);
  TSDebug(PLUGIN_NAME, "Serialized Post Data. Post Header Len: %ld ", state_->pBuffInc - state_->buff);
}

unsigned char *
FCGIClientRequest::addClientRequest(int &dataLen)
{
  dataLen = state_->pBuffInc - state_->buff;
  return state_->buff;
}

void
FCGIClientRequest::serialize(uchar *buffer, void *st, size_t size)
{
  memcpy(buffer, st, size);
}

uint32_t
FCGIClientRequest::serializeNameValue(uchar *buffer, const std::pair<string, string> &it)
{
  uchar *p = buffer;
  uint32_t nl, vl;
  nl = it.first.length();
  vl = it.second.length();

  if (nl < 128) {
    *p++ = BYTE_0(nl);
  } else {
    *p++ = BYTE_3(nl);
    *p++ = BYTE_2(nl);
    *p++ = BYTE_1(nl);
    *p++ = BYTE_0(nl);
  }

  if (vl < 128) {
    *p++ = BYTE_0(vl);
  } else {
    *p++ = BYTE_3(vl);
    *p++ = BYTE_2(vl);
    *p++ = BYTE_1(vl);
    *p++ = BYTE_0(vl);
  }
  memcpy(p, it.first.c_str(), nl);
  p += nl;
  memcpy(p, it.second.c_str(), vl);
  p += vl;
  return p - buffer;
}

void
FCGIClientRequest::fcgiHeaderSetRequestId(FCGI_Header *h, int request_id)
{
  h->requestIdB0 = BYTE_0(request_id);
  h->requestIdB1 = BYTE_1(request_id);
}

void
FCGIClientRequest::fcgiHeaderSetContentLen(FCGI_Header *h, uint16_t len)
{
  h->contentLengthB0 = BYTE_0(len);
  h->contentLengthB1 = BYTE_1(len);
}

uint32_t
FCGIClientRequest::fcgiHeaderGetContentLen(FCGI_Header *h)
{
  return (h->contentLengthB1 << 8) + h->contentLengthB0;
}

int
FCGIClientRequest::fcgiProcessHeader(uchar ch, FCGIRecordList *rec)
{
  FCGI_Header *h;
  FCGI_State *state = &(rec->state);
  h                 = rec->header;

  switch (*state) {
  case fcgi_state_version:
    h->version = ch;
    *state     = fcgi_state_type;
    break;
  case fcgi_state_type:
    h->type = ch;
    *state  = fcgi_state_request_id_hi;
    break;
  case fcgi_state_request_id_hi:
    h->requestIdB1 = ch;
    *state         = fcgi_state_request_id_lo;
    break;
  case fcgi_state_request_id_lo:
    h->requestIdB0 = ch;
    *state         = fcgi_state_content_len_hi;
    break;
  case fcgi_state_content_len_hi:
    h->contentLengthB1 = ch;
    *state             = fcgi_state_content_len_lo;
    break;
  case fcgi_state_content_len_lo:
    h->contentLengthB0 = ch;
    *state             = fcgi_state_padding_len;
    break;
  case fcgi_state_padding_len:
    h->paddingLength = ch;
    *state           = fcgi_state_reserved;
    break;
  case fcgi_state_reserved:
    h->reserved = ch;
    *state      = fcgi_state_content_begin;
    break;

  case fcgi_state_content_begin:
  case fcgi_state_content_proc:
  case fcgi_state_padding:
  case fcgi_state_done:
    return FCGI_PROCESS_DONE;
  }
  return FCGI_PROCESS_AGAIN;
}

int
FCGIClientRequest::fcgiProcessContent(uchar **beg_buf, uchar *end_buf, FCGIRecordList *rec)
{
  size_t tot_len, con_len, cpy_len, offset, nb = end_buf - *beg_buf;
  FCGI_State *state = &(rec->state);
  FCGI_Header *h    = rec->header;
  offset            = rec->offset;

  if (*state == fcgi_state_padding) {
    *state = fcgi_state_done;
    *beg_buf += (size_t)((int)rec->length - (int)offset + (int)h->paddingLength);
    return FCGI_PROCESS_DONE;
  }

  con_len = rec->length - offset;
  tot_len = con_len + h->paddingLength;

  if (con_len <= nb)
    cpy_len = con_len;
  else {
    cpy_len = nb;
  }

  memcpy(rec->content + offset, *beg_buf, cpy_len);

  if (tot_len <= nb) {
    rec->offset += tot_len;
    *state = fcgi_state_done;
    *beg_buf += tot_len;
    return FCGI_PROCESS_DONE;
  } else if (con_len <= nb) {
    /* Have to still skip all or some of padding */
    *state = fcgi_state_padding;
    rec->offset += nb;
    *beg_buf += nb;
    return FCGI_PROCESS_AGAIN;
  } else {
    rec->offset += nb;
    *beg_buf += nb;
    return FCGI_PROCESS_AGAIN;
  }

  return 0;
}

int
FCGIClientRequest::fcgiProcessRecord(uchar **beg_buf, uchar *end_buf, FCGIRecordList *rec)
{
  int rv;
  while (rec->state < fcgi_state_content_begin) {
    if ((rv = fcgiProcessHeader(**beg_buf, rec)) == FCGI_PROCESS_ERR)
      return FCGI_PROCESS_ERR;
    (*beg_buf)++;
    if (*beg_buf == end_buf)
      return FCGI_PROCESS_AGAIN;
  }
  if (rec->state == fcgi_state_content_begin) {
    rec->length  = fcgiHeaderGetContentLen(rec->header);
    rec->content = (uchar *)TSmalloc(rec->length);
    rec->state   = (FCGI_State)(int(rec->state) + 1);
  }

  return fcgiProcessContent(beg_buf, end_buf, rec);
}

static char *
convert_mime_hdr_to_string(TSMBuffer bufp, TSMLoc hdr_loc)
{
  TSIOBuffer output_buffer;
  TSIOBufferReader reader;
  int64_t total_avail;

  TSIOBufferBlock block;
  const char *block_start;
  int64_t block_avail;

  char *output_string;
  int output_len;

  output_buffer = TSIOBufferCreate();

  if (!output_buffer) {
    TSError("[InkAPITest] couldn't allocate IOBuffer");
  }

  reader = TSIOBufferReaderAlloc(output_buffer);

  /* This will print  just MIMEFields and not
     the http request line */
  TSMimeHdrPrint(bufp, hdr_loc, output_buffer);

  /* Find out how the big the complete header is by
     seeing the total bytes in the buffer.  We need to
     look at the buffer rather than the first block to
     see the size of the entire header */
  total_avail = TSIOBufferReaderAvail(reader);

  /* Allocate the string with an extra byte for the string
     terminator */
  output_string = (char *)TSmalloc(total_avail + 1);
  output_len    = 0;

  /* We need to loop over all the buffer blocks to make
     sure we get the complete header since the header can
     be in multiple blocks */
  block = TSIOBufferReaderStart(reader);
  while (block) {
    block_start = TSIOBufferBlockReadStart(block, reader, &block_avail);

    /* We'll get a block pointer back even if there is no data
       left to read so check for this condition and break out of
       the loop. A block with no data to read means we've exhausted
       buffer of data since if there was more data on a later
       block in the chain, this block would have been skipped over */
    if (block_avail == 0) {
      break;
    }

    memcpy(output_string + output_len, block_start, block_avail);
    output_len += block_avail;

    /* Consume the data so that we get to the next block */
    TSIOBufferReaderConsume(reader, block_avail);

    /* Get the next block now that we've consumed the
       data off the last block */
    block = TSIOBufferReaderStart(reader);
  }

  /* Terminate the string */
  output_string[output_len] = '\0';
  output_len++;

  /* Free up the TSIOBuffer that we used to print out the header */
  TSIOBufferReaderFree(reader);
  TSIOBufferDestroy(output_buffer);

  return output_string;
}

bool
FCGIClientRequest::fcgiProcessBuffer(uchar *beg_buf, uchar *end_buf, std::ostringstream &output)
{
  if (!_headerRecord)
    _headerRecord = new FCGIRecordList;

  while (1) {
    if (_headerRecord->state == fcgi_state_done) {
      FCGIRecordList *tmp = _headerRecord;
      _headerRecord       = new FCGIRecordList();
      delete tmp;
    }

    if (fcgiProcessRecord(&beg_buf, end_buf, _headerRecord) == FCGI_PROCESS_DONE) {
      if (first_chunk) {
        string start = std::string((char *)_headerRecord->content, _headerRecord->length);
        string end("\r\n\r\n");
        string headerString;
        int foundPos = start.find(end);
        if (foundPos != -1) {
          headerString = start.substr(0, foundPos + 4);
        }
        const char *buff = headerString.c_str();
        const char *start1;
        const char *endPtr;
        start1 = buff;
        endPtr = buff + strlen(buff) + 1;
        char *temp;
        TSMLoc mime_hdr_loc1 = (TSMLoc) nullptr;
        TSParseResult retval;
        TSMimeParser parser = TSMimeParserCreate();
        TSMBuffer bufp      = TSMBufferCreate();
        TSMimeHdrCreate(bufp, &mime_hdr_loc1);
        if ((retval = TSMimeHdrParse(parser, bufp, mime_hdr_loc1, &start1, endPtr)) == TS_PARSE_ERROR) {
          TSDebug(PLUGIN_NAME, "[FCGIClientRequest:%s] Hdr Parse Error.", __FUNCTION__);
        } else {
          if (retval == TS_PARSE_DONE) {
            temp = convert_mime_hdr_to_string(bufp, mime_hdr_loc1); // Implements TSMimeHdrPrint.
            if (strcmp(buff, temp) == 0) {
              Headers h(bufp, mime_hdr_loc1);
              string key("Status");
              if (h.isInitialized()) {
                atscppapi::header_field_iterator it = h.find(key);
                if (it != h.end()) {
                  atscppapi::HeaderField hf(*it);
                  string value = hf.values(","); // delimiter for header values
                  output << HTTP_VERSION_STRINGS[HTTP_VERSION_1_1] << " ";
                  output << value.c_str() << "\r\n";
                } else {
                  output << "HTTP/1.0 200 OK\r\n";
                }
                // it = h.begin();
                // for (it = h.begin(); it != h.end(); ++it) {
                //   atscppapi::HeaderField hf(*it);
                //   std::cout << "Name => " << hf.name() << "Value => " << hf.values() << std::endl;
                // }
              }
            } else {
              TSDebug(PLUGIN_NAME, "[FCGIClientRequest:%s] Incorrect Parsing.", __FUNCTION__);
              output << "HTTP/1.0 200 OK\r\n";
            }
            TSfree(temp);
          }
        }

        TSMimeHdrDestroy(bufp, mime_hdr_loc1);
        TSHandleMLocRelease(bufp, TS_NULL_MLOC, mime_hdr_loc1);
        TSMBufferDestroy(bufp);
        TSMimeParserDestroy(parser);
        // OS(XXX): merge -- check later, currently leaning towards that we do not have
        // to do this.
        // TSfree(buff);
        first_chunk = false;
      }
      if (_headerRecord->header->type == FCGI_STDOUT) {
        output << std::string((const char *)_headerRecord->content, _headerRecord->length);
      }
      if (_headerRecord->header->type == FCGI_STDERR) {
        // XXX(oschaaf): we may want to treat this differently, but for now this will do.
        output << "HTTP/1.0 500 Server Error\r\n\r\n";
        output << std::string((const char *)_headerRecord->content, _headerRecord->length);
        TSDebug(PLUGIN_NAME, "[ FCGIClientRequest:%s ] Response FCGI_STDERR.*****\n\n", __FUNCTION__);
        return true;
      }
      if (_headerRecord->header->type == FCGI_END_REQUEST) {
        TSDebug(PLUGIN_NAME, "[ FCGIClientRequest:%s ] Response FCGI_END_REQUEST.*****\n\n", __FUNCTION__);
        return true;
      }
    }

    if (beg_buf == end_buf)
      return false;
  }
}

bool
FCGIClientRequest::fcgiDecodeRecordChunk(uchar *beg_buf, size_t remain, std::ostringstream &output)
{
  return fcgiProcessBuffer((uchar *)beg_buf, (uchar *)beg_buf + (size_t)remain, output);
}

void
FCGIClientRequest::print_bytes(uchar *buf, int n)
{
  int i;
  printf("{");
  for (i = 0; i < n; i++)
    printf("%02x", buf[i]);
  printf("}\n");
}
