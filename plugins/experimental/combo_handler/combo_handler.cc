/** @file

    ATS plugin to do combo handling.

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

#include <list>
#include <string>
#include <time.h>
#include <arpa/inet.h>

#include <ts/ts.h>

#include <HttpDataFetcherImpl.h> 
#include <gzip.h>
#include <Utils.h>

using namespace std;
using namespace EsiLib;

#define DEBUG_TAG "combo_handler"

static string SIG_KEY_NAME;

#define DEFAULT_COMBO_HANDLER_PATH "admin/v1/combo"
static string COMBO_HANDLER_PATH;
static int COMBO_HANDLER_PATH_SIZE;

#define LOG_ERROR(fmt, args...) do {                                    \
    TSError("[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__ , ##args ); \
    TSDebug(DEBUG_TAG, "[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__ , ##args ); \
  } while (0)

#define LOG_DEBUG(fmt, args...) do {                                    \
    TSDebug(DEBUG_TAG, "[%s:%d] [%s] DEBUG: " fmt, __FILE__, __LINE__, __FUNCTION__ , ##args ); \
  } while (0)

typedef list<string> StringList;

struct ClientRequest {
  TSHttpStatus status;
  unsigned int client_ip;
  int client_port;
  StringList file_urls;
  bool gzip_accepted;
  string defaultBucket;	//default Bucket is set to l
  ClientRequest()
    : status(TS_HTTP_STATUS_OK), client_ip(0), client_port(0), gzip_accepted(false), defaultBucket("l") { };
};

struct InterceptData {
  TSVConn net_vc;
  TSCont contp;

  struct IoHandle {
    TSVIO vio;
    TSIOBuffer buffer;
    TSIOBufferReader reader;

    IoHandle()
      : vio(0), buffer(0), reader(0) { };

    ~IoHandle() {
      if (reader) {
        TSIOBufferReaderFree(reader);
      }
      if (buffer) {
        TSIOBufferDestroy(buffer);
      }
    };
  };

  IoHandle input;
  IoHandle output;
  TSHttpParser http_parser;

  string body;
  TSMBuffer req_hdr_bufp;
  TSMLoc req_hdr_loc;
  bool req_hdr_parsed;
  bool initialized;
  ClientRequest creq;
  HttpDataFetcherImpl *fetcher;
  bool read_complete;
  bool write_complete;
  string gzipped_data;
  
  InterceptData(TSCont cont) 
    : net_vc(0), contp(cont), input(), output(), req_hdr_bufp(0), req_hdr_loc(0), req_hdr_parsed(false),
      initialized(false), fetcher(0), read_complete(false), write_complete(false) {
    http_parser = TSHttpParserCreate();
  }

  bool init(TSVConn vconn);
  void setupWrite();

  ~InterceptData();
};

bool
InterceptData::init(TSVConn vconn)
{
  if (initialized) {
    LOG_ERROR("InterceptData already initialized!");
    return false;
  }
  
  net_vc = vconn;

  input.buffer = TSIOBufferCreate();
  input.reader = TSIOBufferReaderAlloc(input.buffer);
  input.vio = TSVConnRead(net_vc, contp, input.buffer, INT_MAX);

  req_hdr_bufp = TSMBufferCreate();
  req_hdr_loc = TSHttpHdrCreate(req_hdr_bufp);
  TSHttpHdrTypeSet(req_hdr_bufp, req_hdr_loc, TS_HTTP_TYPE_REQUEST);

  fetcher = new HttpDataFetcherImpl(contp, creq.client_ip, creq.client_port, "combohandler_fetcher");

  initialized = true;
  LOG_DEBUG("InterceptData initialized!");
  return true;
}

void
InterceptData::setupWrite()
{
  TSAssert(output.buffer == 0);
  output.buffer = TSIOBufferCreate();
  output.reader = TSIOBufferReaderAlloc(output.buffer);
  output.vio = TSVConnWrite(net_vc, contp, output.reader, INT_MAX);
}

InterceptData::~InterceptData()
{
  if (req_hdr_loc) {
    TSHandleMLocRelease(req_hdr_bufp, TS_NULL_MLOC, req_hdr_loc);
  }
  if (req_hdr_bufp) {
    TSMBufferDestroy(req_hdr_bufp);
  }
  if (fetcher) {
    delete fetcher;
  }
  TSHttpParserDestroy(http_parser); 
  if (net_vc) {
    TSVConnClose(net_vc);
  }
}

// forward declarations
static int handleReadRequestHeader(TSCont contp, TSEvent event, void *edata);
static bool isComboHandlerRequest(TSMBuffer bufp, TSMLoc hdr_loc, TSMLoc url_loc);
static void getClientRequest(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc hdr_loc, TSMLoc url_loc,
                             ClientRequest &creq);
static void parseQueryParameters(const char *query, int query_len, ClientRequest &creq);
static void checkGzipAcceptance(TSMBuffer bufp, TSMLoc hdr_loc, ClientRequest &creq);
static int handleServerEvent(TSCont contp, TSEvent event, void *edata);
static bool initRequestProcessing(InterceptData &int_data, void *edata, bool &write_response);
static bool readInterceptRequest(InterceptData &int_data);
static bool writeResponse(InterceptData &int_data);
static bool writeErrorResponse(InterceptData &int_data, int &n_bytes_written);
static bool writeStandardHeaderFields(InterceptData &int_data, int &n_bytes_written);
static void prepareResponse(InterceptData &int_data, ByteBlockList &body_blocks, string &resp_header_fields);
static bool getContentType(TSMBuffer bufp, TSMLoc hdr_loc, string &resp_header_fields);
static bool getDefaultBucket(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc hdr_obj, ClientRequest &creq);


void
TSPluginInit(int argc, const char *argv[])
{
  if ((argc > 1) && (strcmp(argv[1], "-") != 0)) {
    COMBO_HANDLER_PATH =  argv[1];
    if (COMBO_HANDLER_PATH == "/") {
      COMBO_HANDLER_PATH.clear();
    } else {
      if (COMBO_HANDLER_PATH[0] == '/') {
        COMBO_HANDLER_PATH.erase(0, 1);
      }
      if (COMBO_HANDLER_PATH[COMBO_HANDLER_PATH.size() - 1] == '/') {
        COMBO_HANDLER_PATH.erase(COMBO_HANDLER_PATH.size() - 1, 1);
      }
    }
  } else {
    COMBO_HANDLER_PATH = DEFAULT_COMBO_HANDLER_PATH;
  }
  COMBO_HANDLER_PATH_SIZE = static_cast<int>(COMBO_HANDLER_PATH.size());
  LOG_DEBUG("Combo handler path is [%s]", COMBO_HANDLER_PATH.c_str());

  SIG_KEY_NAME = ((argc > 2) && (strcmp(argv[2], "-") != 0)) ? argv[2] : "";
  LOG_DEBUG("Signature key is [%s]", SIG_KEY_NAME.c_str());

  TSCont rrh_contp = TSContCreate(handleReadRequestHeader, NULL);
  if (!rrh_contp || (rrh_contp == TS_ERROR_PTR)) {
    LOG_ERROR("Could not create read request header continuation");
    return;
  }
  if (TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, rrh_contp) == TS_ERROR) {
    LOG_ERROR("Error while registering to read request hook");
    TSContDestroy(rrh_contp);
    return;
  }
  Utils::init(&TSDebug, &TSError);
  LOG_DEBUG("Plugin started");
}

static int
handleReadRequestHeader(TSCont contp, TSEvent event, void *edata)
{
  TSAssert(event == TS_EVENT_HTTP_READ_REQUEST_HDR);

  LOG_DEBUG("handling read request header event...");
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  TSEvent reenable_to_event = TS_EVENT_HTTP_CONTINUE;
  TSMBuffer bufp;
  TSMLoc hdr_loc;

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    TSMLoc url_loc = TSHttpHdrUrlGet(bufp, hdr_loc);
    if (url_loc && (url_loc != TS_ERROR_PTR)) {
      if (isComboHandlerRequest(bufp, hdr_loc, url_loc)) {
        TSCont contp = TSContCreate(handleServerEvent, TSMutexCreate());
        if (!contp || (contp == TS_ERROR_PTR)) {
          LOG_ERROR("[%s] Could not create intercept request", __FUNCTION__);
          reenable_to_event = TS_EVENT_HTTP_ERROR;
        } else {
          if (TSHttpTxnServerIntercept(contp, txnp) == TS_SUCCESS) {
            InterceptData *int_data = new InterceptData(contp);
            TSContDataSet(contp, int_data);
            // todo: check if these two cacheable sets are required
            TSHttpTxnSetReqCacheableSet(txnp);
            TSHttpTxnSetRespCacheableSet(txnp);
            getClientRequest(txnp, bufp, hdr_loc, url_loc, int_data->creq);
            LOG_DEBUG("Setup server intercept to handle client request");
          } else {
            TSContDestroy(contp);
            LOG_ERROR("Could not setup server intercept");
            reenable_to_event = TS_EVENT_HTTP_ERROR;
          }
        }
      }
      TSHandleMLocRelease(bufp, hdr_loc, url_loc);
    } else {
      LOG_ERROR("Could not get request URL");
    }
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  } else {
    LOG_ERROR("Could not get client request");
  }

  TSHttpTxnReenable(txnp, reenable_to_event);
  return 1;
}

static bool
isComboHandlerRequest(TSMBuffer bufp, TSMLoc hdr_loc, TSMLoc url_loc)
{
  int method_len;
  bool retval = false;
  const char *method = TSHttpHdrMethodGet(bufp, hdr_loc, &method_len);

  if (method == TS_ERROR_PTR) {
    LOG_ERROR("Could not obtain method!", __FUNCTION__);
  } else {
    if ((method_len != TS_HTTP_LEN_GET) || (strncasecmp(method, TS_HTTP_METHOD_GET, TS_HTTP_LEN_GET) != 0)) {
      LOG_DEBUG("Unsupported method [%.*s]", method_len, method);
    } else {
      retval = true;
    }
    TSHandleStringRelease(bufp, hdr_loc, method);

    if (retval) {
      int path_len;
      const char *path = TSUrlPathGet(bufp, url_loc, &path_len);
      if (path == TS_ERROR_PTR) {
        LOG_ERROR("Could not get path from request URL");
        retval = false;
      } else {
        retval = (path_len == COMBO_HANDLER_PATH_SIZE) &&
          (strncasecmp(path, COMBO_HANDLER_PATH.c_str(), COMBO_HANDLER_PATH_SIZE) == 0);
        LOG_DEBUG("Path [%.*s] is %s combo handler path", path_len, path, (retval ? "a" : "not a"));
        TSHandleStringRelease(bufp, hdr_loc, path);
      }
    }
  }
  return retval;
}

static bool
getDefaultBucket(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc hdr_obj, ClientRequest &creq)
{
  LOG_DEBUG("In getDefaultBucket");
  TSMLoc field_loc;
  const char* host;
  int host_len = 0;
  bool defaultBucketFound = false;

  field_loc=TSMimeHdrFieldFind(bufp, hdr_obj, TS_MIME_FIELD_HOST, -1);
  if (field_loc == TS_ERROR_PTR) {
    LOG_ERROR("Host field not found.");
    return false;
  }

  host=TSMimeHdrFieldValueGet (bufp, hdr_obj, field_loc, 0, &host_len);
  if (!host || host_len <= 0) {
    LOG_ERROR("Error Extracting Host Header");
    TSHandleMLocRelease (bufp, hdr_obj, field_loc);
    return false;
  }

  LOG_DEBUG("host: %s", host);
  for(int i = 0 ; i < host_len; i++)
    {
      if (host[i] == '.')
        {
          creq.defaultBucket = string(host, i);
          defaultBucketFound = true;
          break;
        }
    }

  TSHandleMLocRelease (bufp, hdr_obj, field_loc);
  TSHandleStringRelease(bufp, field_loc, host);

  LOG_DEBUG("defaultBucket: %s", creq.defaultBucket.data());
  return defaultBucketFound;
}

static void
getClientRequest(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc hdr_loc, TSMLoc url_loc, ClientRequest &creq)
{
  int query_len;
  const char *query = TSUrlHttpQueryGet(bufp, url_loc, &query_len);

  if (query == TS_ERROR_PTR) {
    LOG_ERROR("Could not get query from request URL");
  } else {
    if (!getDefaultBucket(txnp, bufp, hdr_loc, creq))
      {
        LOG_ERROR("failed getting Default Bucket for the request");
        TSHandleStringRelease(bufp, url_loc, query);
        return;
      }
    parseQueryParameters(query, query_len, creq);
    TSHandleStringRelease(bufp, url_loc, query);
    creq.client_ip = ntohl(TSHttpTxnClientIPGet(txnp));
    if (TSHttpTxnClientRemotePortGet(txnp, &creq.client_port) != TS_SUCCESS) {
      creq.client_port = 0;
    } else {
      creq.client_port = ntohs(static_cast<uint16_t>(creq.client_port));
    }
    checkGzipAcceptance(bufp, hdr_loc, creq);
  }
}

static void
parseQueryParameters(const char *query, int query_len, ClientRequest &creq)
{
  creq.status = TS_HTTP_STATUS_OK;
  int param_start_pos = 0;
  bool sig_verified = false;
  int colon_pos = -1;
  string file_url("http://localhost/");
  size_t file_base_url_size = file_url.size();
  const char *common_prefix = 0;
  int common_prefix_size = 0;
  const char *common_prefix_path = 0;
  int common_prefix_path_size = 0;

  for (int i = 0; i <= query_len; ++i) {
    if ((i == query_len) || (query[i] == '&') || (query[i] == '?')) {
      int param_len = i - param_start_pos;
      if (param_len) {
        const char *param = query + param_start_pos;
        if ((param_len >= 4) && (strncmp(param, "sig=", 4) == 0)) {
          if (SIG_KEY_NAME.size()) {
            if (!param_start_pos) {
              LOG_DEBUG("Signature cannot be the first parameter in query [%.*s]", query_len, query);
            } else if (param_len == 4) {
              LOG_DEBUG("Signature empty in query [%.*s]", query_len, query);
            } else {
              // TODO - really verify the signature
              LOG_DEBUG("Verified signature successfully");
              sig_verified = true;
            } else {
              LOG_DEBUG("Signature [%.*s] on query [%.*s] is invalid", param_len - 4, param + 4,
                        param_start_pos, query);
            }
          }
        } else {
          LOG_DEBUG("Verification not configured; ignoring signature...");
        }
        break; // nothing useful after the signature
      }
      if ((param_len >= 2) && (param[0] == 'p') && (param[1] == '=')) {
        common_prefix_size = param_len - 2;
        common_prefix_path_size = 0;
        if (common_prefix_size) {
          common_prefix = param + 2;
          for (int i = 0; i < common_prefix_size; ++i) {
            if (common_prefix[i] == ':') {
              common_prefix_path = common_prefix;
              common_prefix_path_size = i;
              ++i; // go beyond the ':'
              common_prefix += i;
              common_prefix_size -= i;
              break;
            }
          }
        }
        LOG_DEBUG("Common prefix is [%.*s], common prefix path is [%.*s]", common_prefix_size, common_prefix,
                  common_prefix_path_size, common_prefix_path);
      }
      else {
        if (common_prefix_path_size) {
          if (colon_pos >= param_start_pos) { // we have a colon in this param as well?
            LOG_ERROR("Ambiguous 'bucket': [%.*s] specified in common prefix and [%.*s] specified in "
                      "current parameter [%.*s]", common_prefix_path_size, common_prefix_path,
                      colon_pos - param_start_pos, param, param_len, param);
            creq.file_urls.clear();
            break;
          }
          file_url.append(common_prefix_path, common_prefix_path_size);
        }
        else if (colon_pos >= param_start_pos) { // we have a colon
          if ((colon_pos == param_start_pos) || (colon_pos == (i - 1))) {
            LOG_ERROR("Colon-separated path [%.*s] has empty part(s)", param_len, param);
            creq.file_urls.clear();
            break;
          }
          file_url.append(param, colon_pos - param_start_pos); // appending pre ':' part first
            
          // modify these to point to the "actual" file path
          param_start_pos = colon_pos + 1;
          param_len = i - param_start_pos;
          param = query + param_start_pos;
        } else {
          file_url += creq.defaultBucket; // default path
        }
        file_url += '/';
        if (common_prefix_size) {
          file_url.append(common_prefix, common_prefix_size);
        }
        file_url.append(param, param_len);
        creq.file_urls.push_back(file_url);
        LOG_DEBUG("Added file path [%s]", file_url.c_str());
        file_url.resize(file_base_url_size);
      }
    }
    param_start_pos = i + 1;
  } else if (query[i] == ':') {
    colon_pos = i;
  }
}
if (!creq.file_urls.size()) {
  creq.status = TS_HTTP_STATUS_BAD_REQUEST;
 } else if (SIG_KEY_NAME.size() && !sig_verified) {
  LOG_DEBUG("Invalid/empty signature found; Need valid signature");
  creq.status = TS_HTTP_STATUS_FORBIDDEN;
  creq.file_urls.clear();
 }
}

static void
checkGzipAcceptance(TSMBuffer bufp, TSMLoc hdr_loc, ClientRequest &creq)
{
  creq.gzip_accepted = false;
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_ACCEPT_ENCODING,
                                          TS_MIME_LEN_ACCEPT_ENCODING);
  if ((field_loc != TS_ERROR_PTR) && field_loc) {
    const char *value;
    int value_len;
    int n_values = TSMimeHdrFieldValuesCount(bufp, hdr_loc, field_loc);

    for (int i = 0; i < n_values; ++i) {
      if (TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, i, &value, &value_len) == TS_SUCCESS) {
        if ((value_len == TS_HTTP_LEN_GZIP) && (strncasecmp(value, TS_HTTP_VALUE_GZIP, value_len) == 0)) {
          creq.gzip_accepted = true;
        }
        TSHandleStringRelease(bufp, hdr_loc, value);
      } else {
        LOG_DEBUG("Error while getting value # %d of header [%.*s]", i, TS_MIME_LEN_ACCEPT_ENCODING,
                  TS_MIME_FIELD_ACCEPT_ENCODING);
      }
      if (creq.gzip_accepted) {
        break;
      }
    }
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }
  LOG_DEBUG("Client %s gzip encoding", (creq.gzip_accepted ? "accepts" : "does not accept"));
}

static int
handleServerEvent(TSCont contp, TSEvent event, void *edata)
{
  InterceptData *int_data = static_cast<InterceptData *>(TSContDataGet(contp));
  bool write_response = false;

  switch (event) {
  case TS_EVENT_NET_ACCEPT_FAILED:
    LOG_DEBUG("Received net accept failed event; going to abort continuation");
    int_data->read_complete = int_data->write_complete = true;
    break;

  case TS_EVENT_NET_ACCEPT:
    LOG_DEBUG("Received net accept event");
    if (!initRequestProcessing(*int_data, edata, write_response)) {
      LOG_ERROR("Could not initialize request processing");
      return 0;
    }
    break;

  case TS_EVENT_VCONN_READ_READY:
    LOG_DEBUG("Received read ready event");
    if (!readInterceptRequest(*int_data)) {
      LOG_ERROR("Error while reading from input vio");
      return 0;
    }
    break;

  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
    LOG_DEBUG("Received read complete/eos event %d", event);
    int_data->read_complete = true;
    break;

  case TS_EVENT_VCONN_WRITE_READY:
    LOG_DEBUG("Received write ready event");
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    LOG_DEBUG("Received write complete event");
    int_data->write_complete = true;
    break;

  case TS_EVENT_ERROR:
    LOG_ERROR("Received error event!");
    break;

  default:
    if (int_data->fetcher && int_data->fetcher->isFetchEvent(event)) {
      if (!int_data->fetcher->handleFetchEvent(event, edata)) {
        LOG_ERROR("Couldn't handle fetch request event %d", event);
      }
      write_response = int_data->fetcher->isFetchComplete();
    } else {
      LOG_DEBUG("Unexpected event %d", event);
    }
    break;
  }

  if (write_response) {
    if (!writeResponse(*int_data)) {
      LOG_ERROR("Couldn't write response");
      int_data->write_complete = true;
    } else {
      LOG_DEBUG("Wrote response successfully");
    }
  }

  if (int_data->read_complete && int_data->write_complete) {
    LOG_DEBUG("Completed request processing. Shutting down...");
    delete int_data;
    TSContDestroy(contp);
  }

  return 1;
}

static bool
initRequestProcessing(InterceptData &int_data, void *edata, bool &write_response)
{
  TSAssert(int_data.initialized == false);
  if (!int_data.init(static_cast<TSVConn>(edata))) {
    LOG_ERROR("Could not initialize intercept data!");
    return false;
  }

  if (int_data.creq.status == TS_HTTP_STATUS_OK) {
    for (StringList::iterator iter = int_data.creq.file_urls.begin();
         iter != int_data.creq.file_urls.end(); ++iter) {
      if (!int_data.fetcher->addFetchRequest(*iter)) {
        LOG_ERROR("Couldn't add fetch request for URL [%s]", iter->c_str());
      } else {
        LOG_DEBUG("Added fetch request for URL [%s]", iter->c_str());
      }
    }
  } else {
    LOG_DEBUG("Client request status [%d] not ok; Not fetching URLs", int_data.creq.status);
    write_response = true;
  }
  return true;
}

static bool
readInterceptRequest(InterceptData &int_data)
{
  TSAssert(!int_data.read_complete);
  int avail = TSIOBufferReaderAvail(int_data.input.reader);
  if (avail == TS_ERROR) {
    LOG_ERROR("Error while getting number of bytes available");
    return false;
  }
  
  int consumed = 0;
  if (avail > 0) {
    int data_len;
    const char *data;
    TSIOBufferBlock block = TSIOBufferReaderStart(int_data.input.reader);
    while (block != NULL) {
      data = TSIOBufferBlockReadStart(block, int_data.input.reader, &data_len);
      const char *endptr = data + data_len;
      if (TSHttpHdrParseReq(int_data.http_parser, int_data.req_hdr_bufp, int_data.req_hdr_loc,
                             &data, endptr) == TS_PARSE_DONE) {
        int_data.read_complete = true;
      }
      consumed += data_len;
      block = TSIOBufferBlockNext(block);
      if (block == TS_ERROR_PTR) {
        LOG_ERROR("Error while getting block from ioreader");
        return false;
      }
    }
  }
  LOG_DEBUG("Consumed %d bytes from input vio", consumed);
  
  if (TSIOBufferReaderConsume(int_data.input.reader, consumed) == TS_ERROR) {
    LOG_ERROR("Error while consuming data from input vio");
    return false;
  }
  
  // Modify the input VIO to reflect how much data we've completed.
  if (TSVIONDoneSet(int_data.input.vio, TSVIONDoneGet(int_data.input.vio) + consumed) == TS_ERROR) {
    LOG_ERROR("Error while setting ndone on input vio");
    return false;
  }

  if (!int_data.read_complete) {
    LOG_DEBUG("Re-enabling input VIO as request header not completely read yet");
    TSVIOReenable(int_data.input.vio);
  }
  return true;
}

static const string OK_REPLY_LINE("HTTP/1.0 200 OK\r\n");
static const string BAD_REQUEST_RESPONSE("HTTP/1.0 400 Bad Request\r\n\r\n");
static const string ERROR_REPLY_RESPONSE("HTTP/1.0 500 Internal Server Error\r\n\r\n");
static const string FORBIDDEN_RESPONSE("HTTP/1.0 403 Forbidden\r\n\r\n");
static const char GZIP_ENCODING_FIELD[] = { "Content-Encoding: gzip\r\n" };
static const int GZIP_ENCODING_FIELD_SIZE = sizeof(GZIP_ENCODING_FIELD) - 1;

static bool
writeResponse(InterceptData &int_data)
{
  int_data.setupWrite();
  
  ByteBlockList body_blocks;
  string resp_header_fields;
  prepareResponse(int_data, body_blocks, resp_header_fields);

  int n_bytes_written = 0;
  if (int_data.creq.status != TS_HTTP_STATUS_OK) {
    if (!writeErrorResponse(int_data, n_bytes_written)) {
      LOG_ERROR("Couldn't write response error");
      return false;
    }
  } else {
    n_bytes_written = OK_REPLY_LINE.size();
    if (TSIOBufferWrite(int_data.output.buffer, OK_REPLY_LINE.data(), n_bytes_written) == TS_ERROR) {
      LOG_ERROR("Error while writing reply line");
      return false;
    }
    
    if (!writeStandardHeaderFields(int_data, n_bytes_written)) {
      LOG_ERROR("Could not write standard header fields");
      return false;
    }
    
    if (resp_header_fields.size()) {
      if (TSIOBufferWrite(int_data.output.buffer, resp_header_fields.data(),
                           resp_header_fields.size()) == TS_ERROR) {
        LOG_ERROR("Error while writing additional response header fields");
        return false;
      }
      n_bytes_written += resp_header_fields.size();
    }
    
    if (TSIOBufferWrite(int_data.output.buffer, "\r\n", 2) == TS_ERROR) {
      LOG_ERROR("Error while writing header terminator");
      return false;
    }
    n_bytes_written += 2;
    
    for (ByteBlockList::iterator iter = body_blocks.begin(); iter != body_blocks.end(); ++iter) {
      if (TSIOBufferWrite(int_data.output.buffer, iter->data, iter->data_len) == TS_ERROR) {
        LOG_ERROR("Error while writing content");
        return false;
      }
      n_bytes_written += iter->data_len;
    }
  }
    
  LOG_DEBUG("Wrote reply of size %d", n_bytes_written);
  if (TSVIONBytesSet(int_data.output.vio, n_bytes_written) == TS_ERROR) {
    LOG_ERROR("Error while setting nbytes to %d on output vio", n_bytes_written);
    return false;
  }
  
  if (TSVIOReenable(int_data.output.vio) == TS_ERROR) {
    LOG_ERROR("Error while reenabling output VIO");
    return false;
  }
  return true;
}

static void
prepareResponse(InterceptData &int_data, ByteBlockList &body_blocks, string &resp_header_fields)
{
  bool got_content_type = false;

  if (int_data.creq.status == TS_HTTP_STATUS_OK) {
    HttpDataFetcherImpl::ResponseData resp_data;
    TSMLoc field_loc;
    time_t expires_time;
    bool got_expires_time = false;
    for (StringList::iterator iter = int_data.creq.file_urls.begin(); iter != int_data.creq.file_urls.end();
         ++iter) {
      if (int_data.fetcher->getData(*iter, resp_data)) {
        body_blocks.push_back(ByteBlock(resp_data.content, resp_data.content_len));
        if (!got_content_type) {
          got_content_type = getContentType(resp_data.bufp, resp_data.hdr_loc, resp_header_fields);
        }
        field_loc = TSMimeHdrFieldFind(resp_data.bufp, resp_data.hdr_loc, TS_MIME_FIELD_EXPIRES,
                                        TS_MIME_LEN_EXPIRES);
        if (field_loc && (field_loc != TS_ERROR_PTR)) {
          time_t curr_field_expires_time;
          int n_values = TSMimeHdrFieldValuesCount(resp_data.bufp, resp_data.hdr_loc, field_loc);
          if ((n_values != TS_ERROR) && (n_values > 0)) {
            if (TSMimeHdrFieldValueDateGet(resp_data.bufp, resp_data.hdr_loc, field_loc,
                                            &curr_field_expires_time) == TS_SUCCESS) {
              if (!got_expires_time) {
                expires_time = curr_field_expires_time;
                got_expires_time = true;
              } else if (curr_field_expires_time < expires_time) {
                expires_time = curr_field_expires_time;
              }
            } else {
              LOG_DEBUG("Error while getting date value");
            }
          }
          TSHandleMLocRelease(resp_data.bufp, resp_data.hdr_loc, field_loc);
        }
      } else {
        LOG_ERROR("Could not get content for requested URL [%s]", iter->c_str());
        int_data.creq.status = TS_HTTP_STATUS_BAD_REQUEST;
        break;
      }
    }
    if (int_data.creq.status == TS_HTTP_STATUS_OK) {
      if (got_expires_time) {
        if (expires_time <= 0) {
          resp_header_fields.append("Expires: 0\r\n");
        } else {
          char line_buf[128];
          int line_size = strftime(line_buf, 128, "Expires: %a, %d %b %Y %T GMT\r\n", gmtime(&expires_time));
          resp_header_fields.append(line_buf, line_size);
        }
      }
      LOG_DEBUG("Prepared response header field\n%s", resp_header_fields.c_str());
    }
  }

  if ((int_data.creq.status == TS_HTTP_STATUS_OK) && int_data.creq.gzip_accepted) {
    if (!gzip(body_blocks, int_data.gzipped_data)) {
      LOG_ERROR("Could not gzip content!");
      int_data.creq.status = TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
    } else {
      body_blocks.clear();
      body_blocks.push_back(ByteBlock(int_data.gzipped_data.data(), int_data.gzipped_data.size()));
      resp_header_fields.append(GZIP_ENCODING_FIELD, GZIP_ENCODING_FIELD_SIZE);
    }
  }
}

static bool
getContentType(TSMBuffer bufp, TSMLoc hdr_loc, string &resp_header_fields)
{
  bool retval = false;
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_TYPE,
                                          TS_MIME_LEN_CONTENT_TYPE);
  if (field_loc && (field_loc != TS_ERROR_PTR)) {
    bool values_added = false;
    const char *value;
    int value_len;
    int n_values = TSMimeHdrFieldValuesCount(bufp, hdr_loc, field_loc);
    for (int i = 0; i < n_values; ++i) {
      if (TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, i, &value, &value_len) == TS_SUCCESS) {
        if (!values_added) {
          resp_header_fields.append("Content-Type: ");
          values_added = true;
        } else {
          resp_header_fields.append(", ");
        }
        resp_header_fields.append(value, value_len);
        TSHandleStringRelease(bufp, hdr_loc, value);
      } else {
        LOG_DEBUG("Error while getting Content-Type value #%d", i);
      }
    }
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    if (values_added) {
      resp_header_fields.append("\r\n");
      retval = true;
    }
  }
  return retval;
}

static const char INVARIANT_FIELD_LINES[] = { "Vary: Accept-Encoding\r\n"
                                              "Cache-Control: max-age=315360000\r\n" };
static const char INVARIANT_FIELD_LINES_SIZE = sizeof(INVARIANT_FIELD_LINES) - 1;

static bool
writeStandardHeaderFields(InterceptData &int_data, int &n_bytes_written)
{
  if (TSIOBufferWrite(int_data.output.buffer, INVARIANT_FIELD_LINES,
                       INVARIANT_FIELD_LINES_SIZE) == TS_ERROR) {
    LOG_ERROR("Error while writing invariant fields");
    return false;
  }
  n_bytes_written += INVARIANT_FIELD_LINES_SIZE;
  time_t time_now = static_cast<time_t>(TShrtime() / 1000000000); // it returns nanoseconds!
  char last_modified_line[128];
  int last_modified_line_size = strftime(last_modified_line, 128, "Last-Modified: %a, %d %b %Y %T GMT\r\n",
                                         gmtime(&time_now));
  if (TSIOBufferWrite(int_data.output.buffer, last_modified_line, last_modified_line_size) == TS_ERROR) {
    LOG_ERROR("Error while writing last-modified fields");
    return false;
  }
  n_bytes_written += last_modified_line_size;
  return true;
}

static bool
writeErrorResponse(InterceptData &int_data, int &n_bytes_written)
{
  const string *response;
  switch (int_data.creq.status) {
  case TS_HTTP_STATUS_BAD_REQUEST:
    response = &BAD_REQUEST_RESPONSE;
    break;
  case TS_HTTP_STATUS_FORBIDDEN:
    response = &FORBIDDEN_RESPONSE;
    break;
  default:
    response = &ERROR_REPLY_RESPONSE;
    break;
  } 
  if (TSIOBufferWrite(int_data.output.buffer, response->data(), response->size()) == TS_ERROR) {
    LOG_ERROR("Error while writing error response");
    return false;
  }
  n_bytes_written += response->size();
  return true;
}
