/** @file

 Cache Control Extensions Caching Utilities

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

#include <cstring>
#include <strings.h>
#include <string>
#include <netinet/in.h>
#include <unistd.h>

#include "stale_response.h"
#include "BodyData.h"
#include "CacheUpdate.h"
#include "UrlComponents.h"
#include "NumberToString.h"

using namespace std;

// unique url parameter that shoud never leave ATS
static const char ASYNC_PARM[]  = "swrasync=asyncmrl";
static const int ASYNC_PARM_LEN = sizeof(ASYNC_PARM) - 1;
// unique header that should never leave ATS
const char SERVER_INTERCEPT_HEADER[]  = "X-CCExtensions-Intercept";
const int SERVER_INTERCEPT_HEADER_LEN = sizeof(SERVER_INTERCEPT_HEADER) - 1;

/*-----------------------------------------------------------------------------------------------*/
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
    TSDebug(PLUGIN_TAG_BAD, "[%s] couldn't allocate IOBuffer", __FUNCTION__);
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
  output_string = static_cast<char *>(TSmalloc(total_avail + 1));
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

/*-----------------------------------------------------------------------------------------------*/
bool
has_trailing_parameter(TSMBuffer hdr_url_buf, TSMLoc hdr_url_loc)
{
  bool bFound = false;

  TSMLoc url_loc;
  TSHttpHdrUrlGet(hdr_url_buf, hdr_url_loc, &url_loc);
  // create the new url
  UrlComponents reqUrl;
  reqUrl.populate(hdr_url_buf, url_loc);
  string newQuery = reqUrl.getQuery();
  string::size_type idx;
  idx = newQuery.find(ASYNC_PARM);
  if ((idx != string::npos) && (idx + ASYNC_PARM_LEN) == newQuery.length()) {
    bFound = true;
  }
  TSHandleMLocRelease(hdr_url_buf, hdr_url_loc, url_loc);
  TSDebug(PLUGIN_TAG, "[%s] %d", __FUNCTION__, bFound);
  return bFound;
}

/*-----------------------------------------------------------------------------------------------*/
void
add_trailing_parameter(TSMBuffer hdr_url_buf, TSMLoc hdr_url_loc)
{
  TSMLoc url_loc;
  TSHttpHdrUrlGet(hdr_url_buf, hdr_url_loc, &url_loc);
  // create the new url
  UrlComponents reqUrl;
  reqUrl.populate(hdr_url_buf, url_loc);
  string newQuery = reqUrl.getQuery();

  if (newQuery.length()) {
    newQuery += "&";
    newQuery += ASYNC_PARM;
  } else {
    newQuery += ASYNC_PARM;
  }
  reqUrl.setQuery(newQuery);
  string newUrl;
  reqUrl.construct(newUrl);
  // parse ans set the new url
  const char *start = newUrl.c_str();
  const char *end   = newUrl.size() + start;
  TSUrlParse(hdr_url_buf, url_loc, &start, end);

  TSDebug(PLUGIN_TAG, "[%s] [%s]", __FUNCTION__, newQuery.c_str());
  TSHandleMLocRelease(hdr_url_buf, hdr_url_loc, url_loc);
}

/*-----------------------------------------------------------------------------------------------*/
bool
strip_trailing_parameter(TSMBuffer hdr_url_buf, TSMLoc hdr_url_loc)
{
  bool stripped = false;
  TSMLoc url_loc;
  TSHttpHdrUrlGet(hdr_url_buf, hdr_url_loc, &url_loc);
  // create the new url
  UrlComponents reqUrl;
  reqUrl.populate(hdr_url_buf, url_loc);
  string newQuery = reqUrl.getQuery();
  string::size_type idx;
  idx = newQuery.find(ASYNC_PARM);
  if ((idx != string::npos) && (idx + ASYNC_PARM_LEN) == newQuery.length()) {
    if (idx > 0) {
      idx -= 1;
    }
    newQuery.erase(idx);
    stripped = true;
  }
  if (stripped) {
    TSUrlHttpQuerySet(hdr_url_buf, url_loc, newQuery.c_str(), newQuery.size());
  }
  TSHandleMLocRelease(hdr_url_buf, hdr_url_loc, url_loc);
  TSDebug(PLUGIN_TAG, "[%s] stripped=%d [%s]", __FUNCTION__, stripped, newQuery.c_str());
  return stripped;
}

/*-----------------------------------------------------------------------------------------------*/
void
fix_connection_close(StateInfo *state)
{
  TSMLoc connection_hdr_loc, connection_hdr_dup_loc;
  connection_hdr_loc = TSMimeHdrFieldFind(state->req_info->http_hdr_buf, state->req_info->http_hdr_loc, TS_MIME_FIELD_CONNECTION,
                                          TS_MIME_LEN_CONNECTION);

  while (connection_hdr_loc != TS_NULL_MLOC) {
    TSDebug(PLUGIN_TAG, "[%s] {%u} Found old Connection hdr", __FUNCTION__, state->req_info->key_hash);
    connection_hdr_dup_loc =
      TSMimeHdrFieldNextDup(state->req_info->http_hdr_buf, state->req_info->http_hdr_loc, connection_hdr_loc);
    TSMimeHdrFieldRemove(state->req_info->http_hdr_buf, state->req_info->http_hdr_loc, connection_hdr_loc);
    TSMimeHdrFieldDestroy(state->req_info->http_hdr_buf, state->req_info->http_hdr_loc, connection_hdr_loc);
    TSHandleMLocRelease(state->req_info->http_hdr_buf, state->req_info->http_hdr_loc, connection_hdr_loc);
    connection_hdr_loc = connection_hdr_dup_loc;
  }

  TSDebug(PLUGIN_TAG, "[%s] {%u} Creating Connection:close hdr", __FUNCTION__, state->req_info->key_hash);
  TSMimeHdrFieldCreateNamed(state->req_info->http_hdr_buf, state->req_info->http_hdr_loc, TS_MIME_FIELD_CONNECTION,
                            TS_MIME_LEN_CONNECTION, &connection_hdr_loc);
  TSMimeHdrFieldValueStringInsert(state->req_info->http_hdr_buf, state->req_info->http_hdr_loc, connection_hdr_loc, -1,
                                  TS_HTTP_VALUE_CLOSE, TS_HTTP_LEN_CLOSE);
  TSMimeHdrFieldAppend(state->req_info->http_hdr_buf, state->req_info->http_hdr_loc, connection_hdr_loc);
  TSHandleMLocRelease(state->req_info->http_hdr_buf, state->req_info->http_hdr_loc, connection_hdr_loc);
}

/*-----------------------------------------------------------------------------------------------*/
void
get_pristine_url(StateInfo *state)
{
  TSHttpTxn txnp = state->txnp;
  TSMBuffer hdr_url_buf;
  TSMLoc url_loc;
  // get the pristine url only works after remap state
  if (TSHttpTxnPristineUrlGet(txnp, &hdr_url_buf, &url_loc) == TS_SUCCESS) {
    int url_length;
    char *url           = TSUrlStringGet(hdr_url_buf, url_loc, &url_length);
    state->pristine_url = TSstrndup(url, url_length);
    TSfree(url);
    // release the buffer and loc
    TSHandleMLocRelease(hdr_url_buf, TS_NULL_MLOC, url_loc);
    TSDebug(PLUGIN_TAG, "[%s] {%u} pristine=[%s]", __FUNCTION__, state->req_info->key_hash, state->pristine_url);
  } else {
    TSDebug(PLUGIN_TAG_BAD, "[%s] {%u} TSHttpTxnPristineUrlGet failed!", __FUNCTION__, state->req_info->key_hash);
  }
}

/*-----------------------------------------------------------------------------------------------*/
bool
intercept_get_key(TSMBuffer bufp, TSMLoc hdr_loc, const char *name, int name_len, string &key)
{
  bool retval      = false;
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, name, name_len);
  if (field_loc) {
    int value_len;
    const char *value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, 0, &value_len);
    key.append(value, value_len);
    retval = true;
  }
  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  // TSDebug(PLUGIN_TAG, "[%s] key=[%s] found=%d",__FUNCTION__,name,key.c_str(),retval);
  return retval;
}

/*-----------------------------------------------------------------------------------------------*/
BodyData *
intercept_check_request(StateInfo *state)
{
  uint32_t newKey      = 0;
  BodyData *pBodyFound = nullptr;
  TSHttpTxn txnp       = state->txnp;
  uint32_t oldKey      = state->req_info->key_hash;

#if (TS_VERSION_NUMBER >= 7000000)
  if (!TSHttpTxnIsInternal(txnp))
#else
  if (TSHttpIsInternalRequest(txnp) != TS_SUCCESS)
#endif
  {
    TSDebug(PLUGIN_TAG, "[%s] Skipping external request", __FUNCTION__);
    return pBodyFound;
  }

  TSMBuffer bufp;
  TSMLoc hdr_loc;
  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSDebug(PLUGIN_TAG_BAD, "[%s] TSHttpTxnClientReqGet failed!", __FUNCTION__);
    return pBodyFound;
  }

  bool valid_request = false;
  int method_len;
  const char *method = TSHttpHdrMethodGet(bufp, hdr_loc, &method_len);
  if (!method) {
    TSDebug(PLUGIN_TAG_BAD, "[%s] TSHttpHdrMethodGet failed!", __FUNCTION__);
  } else {
    if ((method_len == TS_HTTP_LEN_GET) && (strncasecmp(method, TS_HTTP_METHOD_GET, TS_HTTP_LEN_GET) == 0)) {
      valid_request = true;
    }
  }

  if (valid_request) {
    string headerKey;
    if (intercept_get_key(bufp, hdr_loc, SERVER_INTERCEPT_HEADER, SERVER_INTERCEPT_HEADER_LEN, headerKey)) {
      base16_decode(reinterpret_cast<unsigned char *>(&newKey), headerKey.c_str(), headerKey.length());
      pBodyFound = async_check_active(newKey, state->plugin_config);
      if (pBodyFound) {
        // header key can be differnt because of ATS port wierdness, so make state the same
        state->req_info->key_hash = newKey;
      } else {
        TSDebug(PLUGIN_TAG_BAD, "[%s] key miss %u this should not happen!", __FUNCTION__, newKey);
      }
    }
  }

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  TSDebug(PLUGIN_TAG, "[%s] {%u} oldKey=%u pBodyFound=%p", __FUNCTION__, newKey, oldKey, pBodyFound);
  return pBodyFound;
}

/*-----------------------------------------------------------------------------------------------*/
bool
intercept_fetch_the_url(StateInfo *state)
{
  bool bGood = false;

  if (state->pristine_url == nullptr) {
    TSDebug(PLUGIN_TAG_BAD, "[%s] {%u} pristine url nullptr should not happen", __FUNCTION__, state->req_info->key_hash);
    if (!async_remove_active(state->req_info->key_hash, state->plugin_config)) {
      TSDebug(PLUGIN_TAG_BAD, "[%s] didnt delete async active", __FUNCTION__);
    }
    return bGood;
  }

  // encode the key_hash should alwasy be 8 chars
  char tmpStr[10];
  base16_encode(tmpStr, reinterpret_cast<unsigned char const *>(&(state->req_info->key_hash)), 4);
  // create the request as a string
  string get_request("");
  get_request.append(TS_HTTP_METHOD_GET);
  get_request.append(" ");
  get_request.append(state->pristine_url);
  get_request.append(" HTTP/1.1\r\n");
  get_request.append(SERVER_INTERCEPT_HEADER);
  get_request.append(": ");
  get_request.append(tmpStr, 8);
  get_request.append("\r\n");

  char *allReqHeaders = convert_mime_hdr_to_string(state->req_info->http_hdr_buf, state->req_info->http_hdr_loc);
  get_request.append(allReqHeaders);
  TSfree(allReqHeaders);
  get_request.append("\r\n");

  // TSDebug(PLUGIN_TAG, "[%s] req len %d ",__FUNCTION__,(int)get_request.length());
  // TSDebug(PLUGIN_TAG, "[%s] reg \r\n|%s|\r\n",__FUNCTION__,get_request.c_str());

  BodyData *pBody = async_check_active(state->req_info->key_hash, state->plugin_config);
  if (pBody) {
    // TSDebug(PLUGIN_TAG_BAD, "[%s] sleep 4",__FUNCTION__); sleep(4);
    // This should be safe outside of locks
    pBody->intercept_active = true;
    TSFetchEvent event_ids  = {0, 0, 0};
    TSFetchUrl(get_request.data(), get_request.size(), state->req_info->client_addr, state->transaction_contp, NO_CALLBACK,
               event_ids);
    bGood = true;
    TSDebug(PLUGIN_TAG, "[%s] {%u} length=%d", __FUNCTION__, state->req_info->key_hash, (int)pBody->getSize());
  } else {
    TSDebug(PLUGIN_TAG_BAD, "[%s] {%u} cant find body", __FUNCTION__, state->req_info->key_hash);
  }

  return bGood;
}

/*-----------------------------------------------------------------------------------------------*/
