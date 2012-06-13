/** @file

  A brief file description

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

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <string>
#include <list>
#include <arpa/inet.h>
#include <pthread.h>
#include "ts/ts.h"

#include "EsiProcessor.h"
#include "HttpDataFetcher.h"
#include "Utils.h"
#include "HandlerManager.h"
#include "serverIntercept.h"
#include "Stats.h"
#include "gzip.h"
#include "HttpDataFetcherImpl.h"
#include "FailureInfo.h"
using std::string;
using std::list;
using namespace EsiLib;
using namespace Stats;

static HandlerManager *gHandlerManager;

#define DEBUG_TAG "plugin_esi"
#define PROCESSOR_DEBUG_TAG "plugin_esi_processor"
#define PARSER_DEBUG_TAG "plugin_esi_parser"
#define FETCHER_DEBUG_TAG "plugin_esi_fetcher"
#define VARS_DEBUG_TAG "plugin_esi_vars"
#define HANDLER_MGR_DEBUG_TAG "plugin_esi_handler_mgr"
#define EXPR_DEBUG_TAG VARS_DEBUG_TAG

#define MIME_FIELD_XESI "X-Esi"
#define MIME_FIELD_XESI_LEN 5

enum DataType { DATA_TYPE_RAW_ESI = 0, DATA_TYPE_GZIPPED_ESI = 1, DATA_TYPE_PACKED_ESI = 2 };
static const char *DATA_TYPE_NAMES_[] = { "RAW_ESI",
                                          "GZIPPED_ESI",
                                          "PACKED_ESI" };

static const char *HEADER_MASK_PREFIX = "Mask-";
static const int HEADER_MASK_PREFIX_SIZE = 5;

struct ContData
{
  enum STATE { READING_ESI_DOC, FETCHING_DATA, PROCESSING_COMPLETE };
  STATE curr_state;
  TSVIO input_vio;
  TSIOBufferReader input_reader;
  TSVIO output_vio;
  TSIOBuffer output_buffer;
  TSIOBufferReader output_reader;
  Variables *esi_vars;
  HttpDataFetcherImpl *data_fetcher;
  EsiProcessor *esi_proc;
  string debug_tag;
  bool initialized;
  bool xform_closed;
  TSCont contp;
  DataType input_type;
  DocNodeList node_list;
  string packed_node_list;
  char *request_url;
  bool os_response_cacheable;
  list<string> post_headers;
  TSHttpTxn txnp;
  bool gzip_output;
  string gzipped_data;
  sockaddr const* client_addr;
  bool got_server_state;
  
  ContData(TSCont contptr, TSHttpTxn tx)
    : curr_state(READING_ESI_DOC), input_vio(NULL), output_vio(NULL), output_buffer(NULL), output_reader(NULL),
      esi_vars(NULL), data_fetcher(NULL), esi_proc(NULL), initialized(false),
      xform_closed(false), contp(contptr), input_type(DATA_TYPE_RAW_ESI),
      packed_node_list(""), request_url(NULL), os_response_cacheable(true), txnp(tx), gzip_output(false),
      gzipped_data(""), got_server_state(false) {
    client_addr = TSHttpTxnClientAddrGet(txnp);
  }
  
  void getClientState();

  void getServerState();

  void checkXformStatus();

  bool init();

  ~ContData();
};

class TSStatSystem : public StatSystem {
public:
  void create(int handle) {
        g_stat_indices[handle]=TSStatCreate(Stats::STAT_NAMES[handle], TS_RECORDDATATYPE_INT, TS_STAT_PERSISTENT, TS_STAT_SYNC_COUNT);
  }
//  void increment(int handle, TSMgmtInt step = 1) {
  void increment(int handle, int step = 1) {
    TSStatIntIncrement(g_stat_indices[handle], step);
  }
};


static const char *
createDebugTag(const char *prefix, TSCont contp, string &dest)
{
  char buf[1024];
  snprintf(buf, 1024, "%s_%p", prefix, contp);
  dest.assign(buf);
  return dest.c_str();
}

static bool
checkHeaderValue(TSMBuffer bufp, TSMLoc hdr_loc, const char *name, int name_len,
                 const char *exp_value = 0, int exp_value_len = 0, bool prefix = false); // forward decl

static bool
checkForCacheHeader(const char *name, int name_len, const char *value, int value_len, bool &cacheable);

void
ContData::checkXformStatus() {
  if (!xform_closed) {
    int retval = TSVConnClosedGet(contp);
    if ((retval == TS_ERROR) || retval) {
      if (retval == TS_ERROR) {
        TSDebug(debug_tag.c_str(), "[%s] Error while getting close status of transformation at state %d",
                 __FUNCTION__, curr_state);
      } else {
        TSDebug(debug_tag.c_str(), "[%s] Vconn closed", __FUNCTION__);
      }
      xform_closed = true;
    }
  }
}

bool
ContData::init()
{
  if (initialized) {
    TSError("[%s] ContData already initialized!", __FUNCTION__);
    return false;
  }

  createDebugTag(DEBUG_TAG, contp, debug_tag);
  checkXformStatus();
  
  bool retval = false;

  if (!xform_closed) {
    // Get upstream VIO
    input_vio = TSVConnWriteVIOGet(contp);
    if (!input_vio) {
      TSError("[%s] Error while getting input vio", __FUNCTION__);
      goto lReturn;
    }
    input_reader = TSVIOReaderGet(input_vio);
    
    // Get downstream VIO
    TSVConn output_conn;
    output_conn = TSTransformOutputVConnGet(contp);
    if (!output_conn) {
      TSError("[%s] Error while getting transform VC", __FUNCTION__);
      goto lReturn;
    }
    output_buffer = TSIOBufferCreate();
    output_reader = TSIOBufferReaderAlloc(output_buffer);
    
    // we don't know how much data we are going to write, so INT_MAX
    output_vio = TSVConnWrite(output_conn, contp, output_reader, INT_MAX);
    
    string fetcher_tag, vars_tag, expr_tag, parser_tag, proc_tag;
    if (!data_fetcher) {
      data_fetcher = new HttpDataFetcherImpl(contp, client_addr,
                                             createDebugTag(FETCHER_DEBUG_TAG, contp, fetcher_tag));
    }
    if (!esi_vars) {
      esi_vars = new Variables(createDebugTag(VARS_DEBUG_TAG, contp, vars_tag), &TSDebug, &TSError);
    }
    esi_proc = new EsiProcessor(createDebugTag(PROCESSOR_DEBUG_TAG, contp, proc_tag),
                                createDebugTag(PARSER_DEBUG_TAG, contp, fetcher_tag),
                                createDebugTag(EXPR_DEBUG_TAG, contp, expr_tag),
                                &TSDebug, &TSError, *data_fetcher, *esi_vars, *gHandlerManager);

    if (!got_server_state) {
      getServerState();
    }
    TSDebug(debug_tag.c_str(), "[%s] Set input data type to [%s]", __FUNCTION__,
             DATA_TYPE_NAMES_[input_type]);

    retval = true;
  } else {
    TSDebug(debug_tag.c_str(), "[%s] Transformation closed during initialization; Returning false",
             __FUNCTION__);
  }

lReturn:
  initialized = true;
  return retval;
}

void
ContData::getClientState() {
  TSMBuffer req_bufp;
  TSMLoc req_hdr_loc;
  if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Error while retrieving client request", __FUNCTION__);
    return;
  }

  if (!esi_vars) {
    string vars_tag;
    esi_vars = new Variables(createDebugTag(VARS_DEBUG_TAG, contp, vars_tag), &TSDebug, &TSError);
  }
  if (!data_fetcher) {
    string fetcher_tag;
    data_fetcher = new HttpDataFetcherImpl(contp, client_addr,
                                           createDebugTag(FETCHER_DEBUG_TAG, contp, fetcher_tag));
  }
  if (req_bufp && req_hdr_loc) {
    TSMLoc url_loc;
    if(TSHttpHdrUrlGet(req_bufp, req_hdr_loc, &url_loc) != TS_SUCCESS) {
        TSError("[%s] Error while retrieving hdr url", __FUNCTION__);
/*FIXME Does this leak?*/
        return;
    }
    if (url_loc) {
      if (request_url) {
        TSfree(request_url);
      }
//FIXME TSUrlStringGet says it can accept a null length but it lies.
      int length;
      request_url = TSUrlStringGet(req_bufp, url_loc, &length);
      TSDebug(DEBUG_TAG, "[%s] Got request URL [%s]", __FUNCTION__, request_url ? request_url : "(null)");
      int query_len;
      const char *query = TSUrlHttpQueryGet(req_bufp, url_loc, &query_len);
      if (query) {
        esi_vars->populate(query, query_len);
      }
      TSHandleMLocRelease(req_bufp, req_hdr_loc, url_loc);
    }
    TSMLoc field_loc = TSMimeHdrFieldGet(req_bufp, req_hdr_loc, 0);
    while (field_loc) {
      TSMLoc next_field_loc;
      const char *name;
      int name_len;

      name = TSMimeHdrFieldNameGet(req_bufp, req_hdr_loc, field_loc, &name_len);
      if (name) {
        int n_values;
        n_values = TSMimeHdrFieldValuesCount(req_bufp, req_hdr_loc, field_loc);
        if (n_values && (n_values != TS_ERROR)) {
          const char *value;
          int value_len;
          for (int i = 0; i < n_values; ++i) {
            value = TSMimeHdrFieldValueStringGet(req_bufp, req_hdr_loc, field_loc, i, &value_len);
            if ( NULL != value || value_len ) {
                HttpHeader header(name, name_len, value, value_len);
                esi_vars->populate(header);
                data_fetcher->useHeader(header);
                if (Utils::areEqual(name, name_len, TS_MIME_FIELD_ACCEPT_ENCODING,
                                    TS_MIME_LEN_ACCEPT_ENCODING) &&
                    Utils::areEqual(value, value_len, TS_HTTP_VALUE_GZIP,
                                    TS_HTTP_LEN_GZIP)) {
                  TSDebug(DEBUG_TAG, "[%s] Client accepts gzip encoding; will compress output", __FUNCTION__);
                  gzip_output = true;
                }
            }
          }
        }
      }
      
      next_field_loc = TSMimeHdrFieldNext(req_bufp, req_hdr_loc, field_loc);
      TSHandleMLocRelease(req_bufp, req_hdr_loc, field_loc);
      field_loc = next_field_loc;
    }
  }
  TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_hdr_loc);
}

void
ContData::getServerState() {
  got_server_state = true;
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  if (!TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc)) {
    TSDebug(DEBUG_TAG, "[%s] Could not get server response; Assuming cache object", __FUNCTION__);
//FIXME In theory it should be DATA_TYPE_PACKED_ESI but that doesn't work. Forcing to RAW_ESI for now.
    input_type = DATA_TYPE_RAW_ESI;
    return;
  }
  if (checkHeaderValue(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_ENCODING,
                       TS_MIME_LEN_CONTENT_ENCODING, TS_HTTP_VALUE_GZIP, TS_HTTP_LEN_GZIP)) {
    input_type = DATA_TYPE_GZIPPED_ESI;
  } else {
    input_type = DATA_TYPE_RAW_ESI;
  }
  int n_mime_headers = TSMimeHdrFieldsCount(bufp, hdr_loc);
  TSMLoc field_loc;
  const char *name, *act_name, *value;
  int name_len, act_name_len, value_len;
  string header;
  for (int i = 0; i < n_mime_headers; ++i) {
    field_loc = TSMimeHdrFieldGet(bufp, hdr_loc, i);
    if (!field_loc) {
      TSDebug(DEBUG_TAG, "[%s] Error while obtaining header field #%d", __FUNCTION__, i);
      continue;
    }
    name = TSMimeHdrFieldNameGet(bufp, hdr_loc, field_loc, &name_len);
    if (name) {
      if (Utils::areEqual(name, name_len, TS_MIME_FIELD_TRANSFER_ENCODING, TS_MIME_LEN_TRANSFER_ENCODING)) {
        TSDebug(DEBUG_TAG, "[%s] Not retaining transfer encoding header", __FUNCTION__);
      } else if (Utils::areEqual(name, name_len, MIME_FIELD_XESI, MIME_FIELD_XESI_LEN)) {
        TSDebug(DEBUG_TAG, "[%s] Not retaining 'X-Esi' header", __FUNCTION__);
      } else if (Utils::areEqual(name, name_len, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH)) {
        TSDebug(DEBUG_TAG, "[%s] Not retaining 'Content-length' header", __FUNCTION__);
      }  else {
        if ((name_len > HEADER_MASK_PREFIX_SIZE) &&
            (strncmp(name, HEADER_MASK_PREFIX, HEADER_MASK_PREFIX_SIZE) == 0)) {
          act_name = name + HEADER_MASK_PREFIX_SIZE;
          act_name_len = name_len - HEADER_MASK_PREFIX_SIZE;
        } else {
          act_name = name;
          act_name_len = name_len;
        }
        header.assign(act_name, act_name_len);
        header.append(": ");
        int n_field_values = TSMimeHdrFieldValuesCount(bufp, hdr_loc, field_loc);
        for (int j = 0; j < n_field_values; ++j) {
          value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, j, &value_len);
          if ( NULL == value || !value_len ) {
            TSDebug(DEBUG_TAG, "[%s] Error while getting value #%d of header [%.*s]",
                     __FUNCTION__, j, act_name_len, act_name);
          } else {
            if (Utils::areEqual(act_name, act_name_len, TS_MIME_FIELD_VARY, TS_MIME_LEN_VARY) &&
                Utils::areEqual(value, value_len, TS_MIME_FIELD_ACCEPT_ENCODING,
                                TS_MIME_LEN_ACCEPT_ENCODING)) {
              TSDebug(DEBUG_TAG, "[%s] Not retaining 'vary: accept-encoding' header", __FUNCTION__);
            } else if (Utils::areEqual(act_name, act_name_len, TS_MIME_FIELD_CONTENT_ENCODING,
                                       TS_MIME_LEN_CONTENT_ENCODING) &&
                       Utils::areEqual(value, value_len, TS_HTTP_VALUE_GZIP, TS_HTTP_LEN_GZIP)) {
              TSDebug(DEBUG_TAG, "[%s] Not retaining 'content-encoding: gzip' header", __FUNCTION__);
            } else {
              if (header[header.size() - 2] != ':') {
                header.append(", ");
              }
              header.append(value, value_len);
              checkForCacheHeader(act_name, act_name_len, value, value_len,
                                  os_response_cacheable);
              if (!os_response_cacheable) {
                TSDebug(DEBUG_TAG, "[%s] Header [%.*s] with value [%.*s] is a no-cache header",
                         __FUNCTION__, act_name_len, act_name, value_len, value);
                break;
              }
            }
          } // end if got value string
        } // end value iteration
        if (static_cast<int>(header.size()) > (act_name_len + 2 /* for ': ' */ )) {
          header += "\r\n";
          post_headers.push_back(header);
        }
      } // end if processable header
    } // end if got header name
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    if (!os_response_cacheable) {
      post_headers.clear();
      break;
    }
  } // end header iteration
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
}

ContData::~ContData()
{
  TSDebug(debug_tag.c_str(), "[%s] Destroying continuation data", __FUNCTION__);
  if (output_reader) {
    TSIOBufferReaderFree(output_reader);
  }
  if (output_buffer) {
    TSIOBufferDestroy(output_buffer);
  }
  if (request_url) {
    TSfree(request_url);
  }
  if (esi_vars) {
    delete esi_vars;
  }
  if (data_fetcher) {
    delete data_fetcher;
  }
  if (esi_proc) {
    delete esi_proc;
  }
}

static void
cacheNodeList(ContData *cont_data) {
  if (TSHttpTxnAborted(cont_data->txnp)) {
    TSDebug(cont_data->debug_tag.c_str(), "[%s] Not caching node list as txn has been aborted", __FUNCTION__);
    return;
  }
  string post_request("");
  post_request.append(TS_HTTP_METHOD_POST);
  post_request += ' ';
  post_request.append(cont_data->request_url);
  post_request.append(" HTTP/1.0\r\n");
  post_request.append(SERVER_INTERCEPT_HEADER);
  post_request.append(": cache=1\r\n");
  for (list<string>::iterator list_iter = cont_data->post_headers.begin();
       list_iter != cont_data->post_headers.end(); ++list_iter) {
    post_request.append(ECHO_HEADER_PREFIX);
    post_request.append(*list_iter);
  }
  post_request.append(TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
  post_request.append(": ");
  post_request.append(TS_HTTP_VALUE_GZIP, TS_HTTP_LEN_GZIP);
  post_request.append("\r\n");

  string body;
  cont_data->esi_proc->packNodeList(body, false);
  char buf[64];
  snprintf(buf, 64, "%s: %ld\r\n\r\n", TS_MIME_FIELD_CONTENT_LENGTH, body.size());

  post_request.append(buf);
  post_request.append(body);
  
  TSFetchEvent event_ids;
  TSFetchUrl(post_request.data(), post_request.size(), cont_data->client_addr,
                  cont_data->contp, NO_CALLBACK, event_ids);
}

static int
transformData(TSCont contp)
{
  ContData *cont_data;
  int64_t toread, consumed = 0, avail;
  bool input_vio_buf_null = false;
  bool process_input_complete = false; 

  // Get the output (downstream) vconnection where we'll write data to.
  cont_data = static_cast<ContData *>(TSContDataGet(contp));

  // If the input VIO's buffer is NULL, we need to terminate the transformation
  if (!TSVIOBufferGet(cont_data->input_vio)) {
    input_vio_buf_null = true;
    if (cont_data->curr_state == ContData::PROCESSING_COMPLETE) {
      TSDebug((cont_data->debug_tag).c_str(), "[%s] input_vio NULL, marking transformation to be terminated",
               __FUNCTION__);
      return 1;
    } else if (cont_data->curr_state == ContData::READING_ESI_DOC) {
      TSDebug((cont_data->debug_tag).c_str(), "[%s] input_vio NULL while in read state. Assuming end of input",
               __FUNCTION__);
      process_input_complete = true;
    } else {
      if (!cont_data->data_fetcher->isFetchComplete()) {
        TSDebug((cont_data->debug_tag).c_str(),
                 "[%s] input_vio NULL, but data needs to be fetched. Returning control", __FUNCTION__);
        return 1;
      } else {
        TSDebug((cont_data->debug_tag).c_str(),
                 "[%s] input_vio NULL, but processing needs to (and can) be completed", __FUNCTION__);
      }
    }
  }

  if (!process_input_complete && (cont_data->curr_state == ContData::READING_ESI_DOC)) {
    // Determine how much data we have left to read.
    toread = TSVIONTodoGet(cont_data->input_vio);
    TSDebug((cont_data->debug_tag).c_str(), "[%s] upstream VC has %ld bytes available to read",
             __FUNCTION__, toread);
    
    if (toread > 0) {
      avail = TSIOBufferReaderAvail(cont_data->input_reader);
      if (avail == TS_ERROR) {
        TSError("[%s] Error while getting number of bytes available", __FUNCTION__);
        return 0;
      }
      
      // There are some data available for reading. Let's parse it
      if (avail > 0) {
        int64_t data_len;
        const char *data;
        TSIOBufferBlock block = TSIOBufferReaderStart(cont_data->input_reader);
        // Now start extraction
        while (block != NULL) {
          data = TSIOBufferBlockReadStart(block, cont_data->input_reader, &data_len);
          if (cont_data->input_type == DATA_TYPE_RAW_ESI) { 
            cont_data->esi_proc->addParseData(data, data_len);
          } else if (cont_data->input_type == DATA_TYPE_GZIPPED_ESI) {
            cont_data->gzipped_data.append(data, data_len);
          } else {
            cont_data->packed_node_list.append(data, data_len);
          }
          TSDebug((cont_data->debug_tag).c_str(),
                   "[%s] Added chunk of %lu bytes starting with [%.10s] to parse list",
                   __FUNCTION__, data_len, (data_len ? data : "(null)"));
          consumed += data_len;
          
          block = TSIOBufferBlockNext(block);
/*FIXME this chunk of code looks to be in error. Commenting out.
          if (!block) {
            TSError("[%s] Error while getting block from ioreader", __FUNCTION__);
            return 0;
          }
*/
        }
      }
      TSDebug((cont_data->debug_tag).c_str(), "[%s] Consumed %ld bytes from upstream VC",
               __FUNCTION__, consumed);
      
      TSIOBufferReaderConsume(cont_data->input_reader, consumed);
      
      // Modify the input VIO to reflect how much data we've completed.
      TSVIONDoneSet(cont_data->input_vio, TSVIONDoneGet(cont_data->input_vio) + consumed);

      toread = TSVIONTodoGet(cont_data->input_vio); // set this for the test after this if block
    }
    
    if (toread > 0) { // testing this again because it might have changed in previous if block
      // let upstream know we are ready to read new data
      TSContCall(TSVIOContGet(cont_data->input_vio), TS_EVENT_VCONN_WRITE_READY, cont_data->input_vio);
    } else {
      // we have consumed everything that there was to read
      process_input_complete = true;
    }
  }
  if (process_input_complete) {
    TSDebug((cont_data->debug_tag).c_str(), "[%s] Completed reading input...", __FUNCTION__);
    if (cont_data->input_type == DATA_TYPE_PACKED_ESI) { 
      TSDebug(DEBUG_TAG, "[%s] Going to use packed node list of size %d",
               __FUNCTION__, (int) cont_data->packed_node_list.size());
      cont_data->esi_proc->usePackedNodeList(cont_data->packed_node_list);
    } else {
      if (cont_data->input_type == DATA_TYPE_GZIPPED_ESI) {
        BufferList buf_list;
        if (gunzip(cont_data->gzipped_data.data(), cont_data->gzipped_data.size(), buf_list)) {
          for (BufferList::iterator iter = buf_list.begin(); iter != buf_list.end(); ++iter) {
            cont_data->esi_proc->addParseData(iter->data(), iter->size());
          }
        } else {
          TSError("[%s] Error while gunzipping data", __FUNCTION__);
        }
      }
      if (cont_data->esi_proc->completeParse()) {
        if (cont_data->os_response_cacheable) {
          cacheNodeList(cont_data);
        }
      }
    }
    cont_data->curr_state = ContData::FETCHING_DATA;
    if (!input_vio_buf_null) {
      TSContCall(TSVIOContGet(cont_data->input_vio), TS_EVENT_VCONN_WRITE_COMPLETE,
                  cont_data->input_vio);
    }
  }

  if (cont_data->curr_state == ContData::FETCHING_DATA) { // retest as state may have changed in previous block
    if (cont_data->data_fetcher->isFetchComplete()) {
      TSDebug((cont_data->debug_tag).c_str(), "[%s] data ready; going to process doc", __FUNCTION__);
      const char *out_data;
      int out_data_len;
      EsiProcessor::ReturnCode retval = cont_data->esi_proc->process(out_data, out_data_len);
      if (retval == EsiProcessor::NEED_MORE_DATA) {
        TSDebug((cont_data->debug_tag).c_str(), "[%s] ESI processor needs more data; "
                 "will wait for all data to be fetched", __FUNCTION__);
        return 1;
      }
      cont_data->curr_state = ContData::PROCESSING_COMPLETE;
      if (retval == EsiProcessor::SUCCESS) {
        TSDebug((cont_data->debug_tag).c_str(),
                 "[%s] ESI processor output document of size %d starting with [%.10s]", 
                 __FUNCTION__, out_data_len, (out_data_len ? out_data : "(null)"));
      } else {
        TSError("[%s] ESI processor failed to process document; will return empty document", __FUNCTION__);
        out_data = "";
        out_data_len = 0;
      }

      // make sure transformation has not been prematurely terminated 
      if (!cont_data->xform_closed) {
        string cdata;
        if (cont_data->gzip_output) {
          if (!gzip(out_data, out_data_len, cdata)) {
            TSError("[%s] Error while gzipping content", __FUNCTION__);
            out_data_len = 0;
            out_data = "";
          } else {
            TSDebug((cont_data->debug_tag).c_str(), "[%s] Compressed document from size %d to %d bytes",
                     __FUNCTION__, out_data_len, (int) cdata.size());
            out_data_len = cdata.size();
            out_data = cdata.data();
          }
        }

        if (TSIOBufferWrite(TSVIOBufferGet(cont_data->output_vio), out_data, out_data_len) == TS_ERROR) {
          TSError("[%s] Error while writing bytes to downstream VC", __FUNCTION__);
          return 0;
        }
        
        TSVIONBytesSet(cont_data->output_vio, out_data_len);
        
        // Reenable the output connection so it can read the data we've produced.
        TSVIOReenable(cont_data->output_vio);
      }
    } else {
      TSDebug((cont_data->debug_tag).c_str(), "[%s] Data not available yet; cannot process document",
               __FUNCTION__);
    }
  }

  return 1;
}

static int
transformHandler(TSCont contp, TSEvent event, void *edata)
{
  TSVIO input_vio;
  ContData *cont_data;

  cont_data = static_cast<ContData *>(TSContDataGet(contp));

  // we need these later, but declaring now avoid compiler warning w.r.t. goto
  bool process_event = true;
  const char *cont_debug_tag;
  bool shutdown, is_fetch_event;
  
  if (!cont_data->initialized) {
    if (!cont_data->init()) {
      TSError("[%s] Could not initialize continuation data; shutting down transformation", __FUNCTION__);
      goto lShutdown;
    }
    TSDebug((cont_data->debug_tag).c_str(), "[%s] initialized continuation data", __FUNCTION__);
  }

  cont_debug_tag = (cont_data->debug_tag).c_str(); // just a handy reference

  cont_data->checkXformStatus();

  is_fetch_event = cont_data->data_fetcher->isFetchEvent(event);

  if (cont_data->xform_closed) {
    TSDebug(cont_debug_tag, "[%s] Transformation closed. Post-processing...", __FUNCTION__);
    if (cont_data->curr_state == ContData::PROCESSING_COMPLETE) {
      TSDebug(cont_debug_tag, "[%s] Processing is complete, not processing current event %d",
               __FUNCTION__, event);
      process_event = false;
    } else if (cont_data->curr_state == ContData::READING_ESI_DOC) {
      TSDebug(cont_debug_tag, "[%s] Parsing is incomplete, will force end of input",
               __FUNCTION__);
      cont_data->curr_state = ContData::FETCHING_DATA;
    }
    if (cont_data->curr_state == ContData::FETCHING_DATA) { // retest as it may be modified in prev. if block
      if (cont_data->data_fetcher->isFetchComplete()) {
        TSDebug(cont_debug_tag,
                 "[%s] Requested data has been fetched; will skip event and marking processing as complete ",
                 __FUNCTION__);
        cont_data->curr_state = ContData::PROCESSING_COMPLETE;
        process_event = false;
      } else {
        if (is_fetch_event) {
          TSDebug(cont_debug_tag, "[%s] Going to process received data",
                   __FUNCTION__);
        } else {
          TSDebug(cont_debug_tag, "[%s] Ignoring event %d; Will wait for pending data",
                   __FUNCTION__, event);
          // transformation is over, but data hasn't been fetched; 
          // let's wait for data to be fetched - we will be called
          // by Fetch API and go through this loop again
          process_event = false;
        }
      }
    }
  }
  
  if (process_event) {
    switch (event) {
    case TS_EVENT_ERROR:
      // doubt: what is this code doing?
      input_vio = TSVConnWriteVIOGet(contp);
      if (!input_vio) {
        TSError("[%s] Error while getting upstream vio", __FUNCTION__);
      } else {
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
      }
      // FetchSM also might send this; let's just output whatever we have
      cont_data->curr_state = ContData::FETCHING_DATA;
      transformData(contp);
      break;
      
    case TS_EVENT_VCONN_WRITE_COMPLETE:
    case TS_EVENT_VCONN_WRITE_READY:     // we write only once to downstream VC
      TSDebug(cont_debug_tag, "[%s] shutting down transformation", __FUNCTION__);
      TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
      break;
      
    case TS_EVENT_IMMEDIATE:
      TSDebug(cont_debug_tag, "[%s] handling TS_EVENT_IMMEDIATE...", __FUNCTION__);
      transformData(contp);
      break;

    default:
      if (is_fetch_event) {
        TSDebug(cont_debug_tag, "[%s] Handling fetch event %d...", __FUNCTION__, event);
        if (cont_data->data_fetcher->handleFetchEvent(event, edata)) {
          if ((cont_data->curr_state == ContData::FETCHING_DATA) &&
              cont_data->data_fetcher->isFetchComplete()) {
            // there's a small chance that fetcher is ready even before
            // parsing is complete; hence we need to check the state too
            TSDebug(cont_debug_tag, "[%s] fetcher is ready with data, going into process stage",
                     __FUNCTION__);
            transformData(contp);
          } 
        } else {
          TSError("[%s] Could not handle fetch event!", __FUNCTION__);
        }
      } else {
        TSAssert(!"Unexpected event");
      }
      break;
    }
  }

  shutdown = (cont_data->xform_closed && (cont_data->curr_state == ContData::PROCESSING_COMPLETE));

  if (shutdown) {
    if (process_event && is_fetch_event) {
      // we need to return control to the fetch API to give up it's
      // lock on our continuation which will fail if we destroy
      // ourselves right now
      TSDebug(cont_debug_tag, "[%s] Deferring shutdown as data event was just processed", __FUNCTION__);
      TSContSchedule(contp, 10, TS_THREAD_POOL_TASK);
    } else {
      goto lShutdown;
    }
  }

  return 1;

lShutdown:
  TSDebug((cont_data->debug_tag).c_str(), "[%s] transformation closed; cleaning up data...", __FUNCTION__);
  delete cont_data;
  TSContDestroy(contp);
  return 1;
  
}

struct RespHdrModData {
  bool cache_txn;
  bool gzip_encoding;
};

static void
addMimeHeaderField(TSMBuffer bufp, TSMLoc hdr_loc, const char *name, int name_len,
                   const char *value, int value_len) {
  TSMLoc field_loc = (TSMLoc)NULL;
  TSMimeHdrFieldCreate(bufp, hdr_loc, &field_loc);
  if (!field_loc) {
    TSError("[%s] Error while creating mime field", __FUNCTION__);
  } else {
    if (TSMimeHdrFieldNameSet(bufp, hdr_loc, field_loc, name, name_len) != TS_SUCCESS) {
      TSError("[%s] Error while setting name [%.*s] for MIME header field", __FUNCTION__, name_len, name);
    } else {
      if (TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, field_loc, 0, value, value_len) != TS_SUCCESS) {
        TSError("[%s] Error while inserting value [%.*s] string to MIME field [%.*s]", __FUNCTION__,
                 value_len, value, name_len, name);
      } else {
        if (TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc) != TS_SUCCESS) {
          TSError("[%s] Error while appending MIME field with name [%.*s] and value [%.*s]", __FUNCTION__,
                   name_len, name, value_len, value);
        }
      }
    }
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }
}

static int
modifyResponseHeader(TSCont contp, TSEvent event, void *edata) {
  int retval = 0;
  RespHdrModData *mod_data = static_cast<RespHdrModData *>(TSContDataGet(contp));
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  if (event != TS_EVENT_HTTP_SEND_RESPONSE_HDR) {
    TSError("[%s] Unexpected event (%d)", __FUNCTION__, event);
    goto lReturn;
  }
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
    int n_mime_headers = TSMimeHdrFieldsCount(bufp, hdr_loc);
    TSMLoc field_loc;
    const char *name, *value;
    int name_len, value_len;
    for (int i = 0; i < n_mime_headers; ++i) {
      field_loc = TSMimeHdrFieldGet(bufp, hdr_loc, i);
      if (!field_loc) {
        TSDebug(DEBUG_TAG, "[%s] Error while obtaining header field #%d", __FUNCTION__, i);
        continue;
      }
      name = TSMimeHdrFieldNameGet(bufp, hdr_loc, field_loc, &name_len);
      if (name) {
        bool destroy_header = false;
        if (Utils::areEqual(name, name_len, SERVER_INTERCEPT_HEADER, SERVER_INTERCEPT_HEADER_LEN)) {
          destroy_header = true;
        } else if (Utils::areEqual(name, name_len, TS_MIME_FIELD_AGE, TS_MIME_LEN_AGE)) {
          destroy_header = true;
        } else if (!mod_data->cache_txn &&
                   Utils::areEqual(name, name_len, MIME_FIELD_XESI, MIME_FIELD_XESI_LEN)) {
          destroy_header = true;
        } else if ((name_len > HEADER_MASK_PREFIX_SIZE) &&
                   (strncmp(name, HEADER_MASK_PREFIX, HEADER_MASK_PREFIX_SIZE) == 0)) {
          destroy_header = true;
        } else {
          int n_field_values = TSMimeHdrFieldValuesCount(bufp, hdr_loc, field_loc);
          for (int j = 0; j < n_field_values; ++j) {
            value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, j, &value_len);
            if ( NULL == value || !value_len ) {
              TSDebug(DEBUG_TAG, "[%s] Error while getting value #%d of header [%.*s]",
                       __FUNCTION__, j, name_len, name);
            } else {
              if (mod_data->cache_txn) { 
                bool response_cacheable, is_cache_header;
                is_cache_header = checkForCacheHeader(name, name_len, value, value_len, response_cacheable);
                if (is_cache_header && response_cacheable) {
                  destroy_header = true;
                }
              } 
            } // if got valid value for header
          } // end for
        }
        if (destroy_header) {
          TSDebug(DEBUG_TAG, "[%s] Removing header with name [%.*s]", __FUNCTION__, name_len, name);
          TSMimeHdrFieldDestroy(bufp, hdr_loc, field_loc);
          --n_mime_headers;
          --i;
        }
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    }
    if (mod_data->gzip_encoding &&
        !checkHeaderValue(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_ENCODING, TS_MIME_LEN_CONTENT_ENCODING,
                          TS_HTTP_VALUE_GZIP, TS_HTTP_LEN_GZIP)) {
      addMimeHeaderField(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_ENCODING, TS_MIME_LEN_CONTENT_ENCODING,
                         TS_HTTP_VALUE_GZIP, TS_HTTP_LEN_GZIP);
    }
    if (mod_data->cache_txn) {
      addMimeHeaderField(bufp, hdr_loc, TS_MIME_FIELD_VARY, TS_MIME_LEN_VARY, TS_MIME_FIELD_ACCEPT_ENCODING,
                         TS_MIME_LEN_ACCEPT_ENCODING);
    }
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSDebug(DEBUG_TAG, "[%s] Inspected client-bound headers", __FUNCTION__);
    retval = 1;
  } else {
    TSError("[%s] Error while getting response from txn", __FUNCTION__);
  }

lReturn:
  delete mod_data;
  TSContDestroy(contp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return retval;
}

static bool
checkHeaderValue(TSMBuffer bufp, TSMLoc hdr_loc, const char *name, int name_len,
                 const char *exp_value, int exp_value_len, bool prefix) {
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, name, name_len);
  if (!field_loc) {
    return false;
  }
  bool retval = false;
  if (exp_value && exp_value_len) {
    const char *value;
    int value_len;
    int n_values = TSMimeHdrFieldValuesCount(bufp, hdr_loc, field_loc);
    for (int i = 0; i < n_values; ++i) {
      value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, i, &value_len);
      if ( NULL != value || value_len ) {
        if (prefix) {
          if ((value_len >= exp_value_len) && 
              (strncasecmp(value, exp_value, exp_value_len) == 0)) {
            retval = true;
          }
        } else if (Utils::areEqual(value, value_len, exp_value, exp_value_len)) {
          retval = true;
        }
      } else {
        TSDebug(DEBUG_TAG, "[%s] Error while getting value # %d of header [%.*s]", __FUNCTION__,
                 i, name_len, name);
      }
      if (retval) {
        break;
      }
    }
  } else { // only presence required
    retval = true;
  }
  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  return retval;
}

static void
maskOsCacheHeaders(TSHttpTxn txnp) {
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  if (TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't get server response from txn", __FUNCTION__);
    return;
  }
  int n_mime_headers = TSMimeHdrFieldsCount(bufp, hdr_loc);
  TSMLoc field_loc;
  const char *name, *value;
  int name_len, value_len, n_field_values;
  bool os_response_cacheable, is_cache_header, mask_header;
  string masked_name;
  for (int i = 0; i < n_mime_headers; ++i) {
    os_response_cacheable = true;
    field_loc = TSMimeHdrFieldGet(bufp, hdr_loc, i);
    if (!field_loc) {
      TSDebug(DEBUG_TAG, "[%s] Error while obtaining header field #%d", __FUNCTION__, i);
      continue;
    }
    name = TSMimeHdrFieldNameGet(bufp, hdr_loc, field_loc, &name_len);
    if (name) {
      mask_header = is_cache_header = false;
      n_field_values = TSMimeHdrFieldValuesCount(bufp, hdr_loc, field_loc);
      for (int j = 0; j < n_field_values; ++j) {
        value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, j, &value_len);
        if ( NULL == value || !value_len ) {
          TSDebug(DEBUG_TAG, "[%s] Error while getting value #%d of header [%.*s]",
                   __FUNCTION__, j, name_len, name);
        } else {
          is_cache_header = checkForCacheHeader(name, name_len, value, value_len, os_response_cacheable);
          if (!os_response_cacheable) {
            break;
          }
          if (is_cache_header) {
            TSDebug(DEBUG_TAG, "[%s] Masking OS cache header [%.*s] with value [%.*s]. ",
                     __FUNCTION__, name_len, name, value_len, value);
            mask_header = true;
          }
        } // end if got value string
      } // end value iteration
      if (mask_header) {
        masked_name.assign(HEADER_MASK_PREFIX);
        masked_name.append(name, name_len);
        if (TSMimeHdrFieldNameSet(bufp, hdr_loc, field_loc, masked_name.data(),
                                   masked_name.size()) != TS_SUCCESS) {
          TSError("[%s] Couldn't rename header [%.*s]", __FUNCTION__, name_len, name);
        }
      }
    } // end if got header name
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    if (!os_response_cacheable) {
      break;
    }
  } // end header iteration
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
}

static bool
isTxnTransformable(TSHttpTxn txnp, bool is_cache_txn) {
  //  We are only interested in transforming "200 OK" responses with a
  //  Content-Type: text/ header and with X-Esi header

  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSHttpStatus resp_status;
  bool header_obtained = false, intercept_header;
  bool retval = false;

  header_obtained = is_cache_txn ? TSHttpTxnCachedRespGet(txnp, &bufp, &hdr_loc) :
    TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc);
  if (header_obtained != TS_SUCCESS) {
    TSError("[%s] Couldn't get txn header", __FUNCTION__);
    goto lReturn;
  }

  intercept_header = checkHeaderValue(bufp, hdr_loc, SERVER_INTERCEPT_HEADER, SERVER_INTERCEPT_HEADER_LEN);
  if (intercept_header) {
    if (is_cache_txn) {
      TSDebug(DEBUG_TAG, "[%s] Packed ESI document found in cache; will process", __FUNCTION__);
      retval = true;
    } else {
      TSDebug(DEBUG_TAG, "[%s] Found Intercept header in server response; document not processable",
               __FUNCTION__);
    }
    goto lReturn; // found internal header; no other detection required
  }

  resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);
  if (static_cast<int>(resp_status) == static_cast<int>(TS_ERROR)) {
    TSError("[%s] Error while getting http status", __FUNCTION__);
    goto lReturn;
  }
  if (resp_status != TS_HTTP_STATUS_OK) {
    TSDebug(DEBUG_TAG, "[%s] Not handling non-OK response status %d", __FUNCTION__, resp_status);
    goto lReturn;
  }

  if (!checkHeaderValue(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_TYPE, TS_MIME_LEN_CONTENT_TYPE,
                        "text/", 5, true)) {
    TSDebug(DEBUG_TAG, "[%s] Not text content", __FUNCTION__);
    goto lReturn;
  }
  if (!checkHeaderValue(bufp, hdr_loc, MIME_FIELD_XESI, MIME_FIELD_XESI_LEN)) {
    TSDebug(DEBUG_TAG, "[%s] ESI header [%s] not found", __FUNCTION__, MIME_FIELD_XESI);
    goto lReturn;
  }

  retval = true;

lReturn:
  if (header_obtained) {
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  }
  return retval;
}

static bool
isCacheObjTransformable(TSHttpTxn txnp) {
  int obj_status;
  if (TSHttpTxnCacheLookupStatusGet(txnp, &obj_status) == TS_ERROR) {
    TSError("[%s] Couldn't get cache status of object", __FUNCTION__);
    return false;
  }
  if ((obj_status == TS_CACHE_LOOKUP_HIT_FRESH) || (obj_status == TS_CACHE_LOOKUP_HIT_STALE)) {
    TSDebug(DEBUG_TAG, "[%s] doc found in cache, will add transformation", __FUNCTION__);
    return isTxnTransformable(txnp, true);
  }
  TSDebug(DEBUG_TAG, "[%s] cache object's status is %d; not transformable",
           __FUNCTION__, obj_status);
  return false;
}

static bool
isInterceptRequest(TSHttpTxn txnp) {
  if (!TSHttpIsInternalRequest(txnp)) {
    TSDebug(DEBUG_TAG, "[%s] Skipping external request", __FUNCTION__);
    return false;
  }

  TSMBuffer bufp;
  TSMLoc hdr_loc;
  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Could not get client request", __FUNCTION__);
    return false;
  }

  bool valid_request = false;
  bool retval = false;
  int method_len;
  const char *method = TSHttpHdrMethodGet(bufp, hdr_loc, &method_len);
  if (!method) {
    TSError("[%s] Could not obtain method!", __FUNCTION__);
  } else {
    if ((method_len != TS_HTTP_LEN_POST) ||
        (strncasecmp(method, TS_HTTP_METHOD_POST, TS_HTTP_LEN_POST))) {
      TSDebug(DEBUG_TAG, "[%s] Method [%.*s] invalid, [%s] expected", __FUNCTION__, method_len, method,
               TS_HTTP_METHOD_POST);
    } else {
      TSDebug(DEBUG_TAG, "[%s] Valid server intercept method found", __FUNCTION__);
      valid_request = true;
    }
  }
  
  if (valid_request) {
    retval = checkHeaderValue(bufp, hdr_loc, SERVER_INTERCEPT_HEADER, SERVER_INTERCEPT_HEADER_LEN);
  }
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  return retval;
}

static bool
checkForCacheHeader(const char *name, int name_len, const char *value, int value_len, bool &cacheable) {
  cacheable = true;
  if (Utils::areEqual(name, name_len, TS_MIME_FIELD_EXPIRES, TS_MIME_LEN_EXPIRES)) {
    if ((value_len == 1) && (*value == '0')) {
      cacheable = false;
    }
    return true;
  }
  if (Utils::areEqual(name, name_len, TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL)) {
    if (Utils::areEqual(value, value_len, TS_HTTP_VALUE_PRIVATE, TS_HTTP_LEN_PRIVATE)) {
      cacheable = false;
    }
    return true;
  }
  return false;
}

static bool
addSendResponseHeaderHook(TSHttpTxn txnp, bool cache_txn, bool gzip_encoding) {
  TSCont contp = TSContCreate(modifyResponseHeader, NULL);
  if (!contp) {
    TSError("[%s] Could not create continuation", __FUNCTION__);
    return false;
  }
  TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
  RespHdrModData *cont_data = new RespHdrModData();
  cont_data->cache_txn = cache_txn;
  cont_data->gzip_encoding = gzip_encoding;
  TSContDataSet(contp, cont_data);
  return true;
}

static bool
addTransform(TSHttpTxn txnp, bool processing_os_response) {
  TSCont contp = 0;
  ContData *cont_data = 0;

  contp = TSTransformCreate(transformHandler, txnp);
  if (!contp) {
    TSError("[%s] Error while creating a new transformation", __FUNCTION__);
    goto lFail;
  }

  cont_data = new ContData(contp, txnp);
  TSContDataSet(contp, cont_data);

  cont_data->getClientState();
  if (processing_os_response) {
    cont_data->getServerState();
  }

  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, contp);

  if (!addSendResponseHeaderHook(txnp, !processing_os_response, cont_data->gzip_output)) {
    TSError("[%s] Couldn't add send response header hook", __FUNCTION__);
    goto lFail;
  }

  TSHttpTxnTransformedRespCache(txnp, 0);
  TSHttpTxnUntransformedRespCache(txnp, 0);

  TSDebug(DEBUG_TAG, "[%s] Added transformation (0x%p)", __FUNCTION__, contp);
  return true;

lFail:
  if (contp) {
    TSContDestroy(contp);
  }
  if (cont_data) {
    delete cont_data;
  }
  return false;
} 

pthread_key_t threadKey;
static int
globalHookHandler(TSCont contp, TSEvent event, void *edata) {
  TSHttpTxn txnp = (TSHttpTxn) edata;
  bool intercept_req = isInterceptRequest(txnp);

  
 
  
  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    TSDebug(DEBUG_TAG, "[%s] handling read request header event...", __FUNCTION__);
    if (intercept_req) {
      if (!setupServerIntercept(txnp)) {
        TSError("[%s] Could not setup server intercept", __FUNCTION__);
      } else {
        TSDebug(DEBUG_TAG, "[%s] Setup server intercept", __FUNCTION__);
      }
    } else {
      TSDebug(DEBUG_TAG, "[%s] Not setting up intercept", __FUNCTION__);
    }
    break;
    
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    if (!intercept_req) {
      if (event == TS_EVENT_HTTP_READ_RESPONSE_HDR) {
        bool mask_cache_headers = false;
        TSDebug(DEBUG_TAG, "[%s] handling read response header event...", __FUNCTION__);
        if (isCacheObjTransformable(txnp)) {
          // transformable cache object will definitely have a
          // transformation already as cache_lookup_complete would
          // have been processed before this
          TSDebug(DEBUG_TAG, "[%s] xform should already have been added on cache lookup. Not adding now",
                   __FUNCTION__);
          mask_cache_headers = true;
        } else if (isTxnTransformable(txnp, false)) {
          addTransform(txnp, true);
          Stats::increment(Stats::N_OS_DOCS);
          mask_cache_headers = true;
        }
        if (mask_cache_headers) {
          // we'll 'mask' OS cache headers so that traffic server will
          // not try to cache this. We cannot outright delete them
          // because we need them in our POST request; hence the 'masking'
          maskOsCacheHeaders(txnp);
        }
      } else {
        TSDebug(DEBUG_TAG, "[%s] handling cache lookup complete event...", __FUNCTION__);
        if (isCacheObjTransformable(txnp)) {
          // we make the assumption above that a transformable cache
          // object would already have a tranformation. We should revisit
          // that assumption in case we change the statement below
          addTransform(txnp, false);
          Stats::increment(Stats::N_CACHE_DOCS);
        }
      }
    }
    break;

  default:
    TSDebug(DEBUG_TAG, "[%s] Don't know how to handle event type %d", __FUNCTION__, event);
    break;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

static void
loadHandlerConf(const char *file_name, Utils::KeyValueMap &handler_conf) {
  std::list<string> conf_lines;
  TSFile conf_file = TSfopen(file_name, "r");
  if (conf_file != NULL) {
    char buf[1024];
    while (TSfgets(conf_file, buf, sizeof(buf) - 1) != NULL) {
      conf_lines.push_back(string(buf));
    }
    TSfclose(conf_file);
    Utils::parseKeyValueConfig(conf_lines, handler_conf);
    TSDebug(DEBUG_TAG, "[%s] Loaded handler conf file [%s]", __FUNCTION__, file_name);
  } else {
    TSError("[%s] Failed to open handler config file [%s]", __FUNCTION__, file_name);
  }
}


void
TSPluginInit(int argc, const char *argv[]) {
  Utils::init(&TSDebug, &TSError);
  Stats::init(new TSStatSystem());
  
  gHandlerManager = new HandlerManager(HANDLER_MGR_DEBUG_TAG, &TSDebug, &TSError);

  if ((argc > 1) && (strcmp(argv[1], "-") != 0)) {
    Utils::KeyValueMap handler_conf;
    loadHandlerConf(argv[1], handler_conf);
    gHandlerManager->loadObjects(handler_conf);
  }

  if(pthread_key_create(&threadKey,NULL)){
    TSError("[%s] Could not create key", __FUNCTION__);
    return;
  }
  
  TSCont global_contp = TSContCreate(globalHookHandler, NULL);
  if (!global_contp) {
    TSError("[%s] Could not create global continuation", __FUNCTION__);
    return;
  }
  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, global_contp);

  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, global_contp);

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, global_contp);

  TSDebug(DEBUG_TAG, "[%s] Plugin started and key is set", __FUNCTION__);
}
