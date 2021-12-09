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
#include <string_view>
#include <sstream>
#include <vector>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <arpa/inet.h>
#include <limits>
#include <getopt.h>

#include "ts/ts.h"
#include "ts/experimental.h"
#include "ts/remap.h"
#include "tscore/ink_defs.h"
#include "tscpp/util/TextView.h"

#include "HttpDataFetcherImpl.h"
#include "gzip.h"
#include "Utils.h"

using namespace std;
using namespace EsiLib;

#define DEBUG_TAG "combo_handler"

// Because STL vs. C library leads to ugly casting, fix it once.
inline int
length(std::string const &str)
{
  return static_cast<int>(str.size());
}

constexpr unsigned DEFAULT_MAX_FILE_COUNT = 100;
constexpr int MAX_QUERY_LENGTH            = 4096;

unsigned MaxFileCount = DEFAULT_MAX_FILE_COUNT;

// We hardcode "immutable" here because it's not yet defined in the ATS API
#define HTTP_IMMUTABLE "immutable"

int arg_idx = -1;
static string SIG_KEY_NAME;
static vector<string> HEADER_ALLOWLIST;

#define DEFAULT_COMBO_HANDLER_PATH "admin/v1/combo"
static string COMBO_HANDLER_PATH{DEFAULT_COMBO_HANDLER_PATH};

#define LOG_ERROR(fmt, args...)                                                               \
  do {                                                                                        \
    TSError("[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##args);            \
    TSDebug(DEBUG_TAG, "[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##args); \
  } while (0)

#define LOG_DEBUG(fmt, args...)                                                               \
  do {                                                                                        \
    TSDebug(DEBUG_TAG, "[%s:%d] [%s] DEBUG: " fmt, __FILE__, __LINE__, __FUNCTION__, ##args); \
  } while (0)

using StringList = list<string>;

struct ClientRequest {
  TSHttpStatus status         = TS_HTTP_STATUS_OK;
  const sockaddr *client_addr = nullptr;
  StringList file_urls;
  bool gzip_accepted = false;
  string defaultBucket; // default Bucket will be set to HOST header
  ClientRequest() : defaultBucket("l"){};
};

struct InterceptData {
  TSVConn net_vc;
  TSCont contp;

  struct IoHandle {
    TSVIO vio               = nullptr;
    TSIOBuffer buffer       = nullptr;
    TSIOBufferReader reader = nullptr;

    IoHandle() = default;

    ~IoHandle()
    {
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
    : net_vc(nullptr),
      contp(cont),
      input(),
      output(),
      req_hdr_bufp(nullptr),
      req_hdr_loc(nullptr),
      req_hdr_parsed(false),
      initialized(false),
      fetcher(nullptr),
      read_complete(false),
      write_complete(false)
  {
    http_parser = TSHttpParserCreate();
  }

  bool init(TSVConn vconn);
  void setupWrite();

  ~InterceptData();
};

/*
 * This class is responsible for keeping track of and processing the various
 * Cache-Control values between all the requested documents
 */
struct CacheControlHeader {
  enum Publicity { PRIVATE, PUBLIC, DEFAULT };

  // Update the object with a document's Cache-Control header
  void update(TSMBuffer bufp, TSMLoc hdr_loc);

  // Return the Cache-Control for the combined document
  string generate() const;

  // Cache-Control values we're keeping track of
  unsigned int _max_age = numeric_limits<unsigned int>::max();
  Publicity _publicity  = Publicity::DEFAULT;
  bool _immutable       = true;
};

class ContentTypeHandler
{
public:
  ContentTypeHandler(std::string &resp_header_fields) : _resp_header_fields(resp_header_fields) {}

  // Returns false if _content_type_allowlist is not empty, and content-type field is either not present or not in the
  // allowlist.  Adds first Content-type field it encounters in the headers passed to this function.
  //
  bool nextObjectHeader(TSMBuffer bufp, TSMLoc hdr_loc);

  // Load allowlist from config file.
  //
  static void loadAllowList(std::string const &file_spec);

private:
  // Add Content-Type field to these.
  //
  std::string &_resp_header_fields;

  bool _added_content_type{false};

  static vector<std::string> _content_type_allowlist;
};

vector<std::string> ContentTypeHandler::_content_type_allowlist;

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
  input.vio    = TSVConnRead(net_vc, contp, input.buffer, INT64_MAX);

  req_hdr_bufp = TSMBufferCreate();
  req_hdr_loc  = TSHttpHdrCreate(req_hdr_bufp);
  TSHttpHdrTypeSet(req_hdr_bufp, req_hdr_loc, TS_HTTP_TYPE_REQUEST);

  fetcher = new HttpDataFetcherImpl(contp, creq.client_addr, "combohandler_fetcher");

  initialized = true;
  LOG_DEBUG("InterceptData initialized!");
  return true;
}

void
InterceptData::setupWrite()
{
  TSAssert(output.buffer == nullptr);
  output.buffer = TSIOBufferCreate();
  output.reader = TSIOBufferReaderAlloc(output.buffer);
  output.vio    = TSVConnWrite(net_vc, contp, output.reader, INT64_MAX);
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

void
CacheControlHeader::update(TSMBuffer bufp, TSMLoc hdr_loc)
{
  bool found_immutable = false;
  bool found_private   = false;

  // Load each value from the Cache-Control header into the vector values
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL);
  if (field_loc != TS_NULL_MLOC) {
    int n_values = TSMimeHdrFieldValuesCount(bufp, hdr_loc, field_loc);
    if ((n_values != TS_ERROR) && (n_values > 0)) {
      for (int i = 0; i < n_values; i++) {
        // Grab this current header value
        int _val_len    = 0;
        const char *val = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, i, &_val_len);

        // Update max-age if necessary
        if (strncasecmp(val, TS_HTTP_VALUE_MAX_AGE, TS_HTTP_LEN_MAX_AGE) == 0) {
          unsigned int max_age = 0;
          char *ptr            = const_cast<char *>(val);
          ptr += TS_HTTP_LEN_MAX_AGE;
          while ((*ptr == ' ') || (*ptr == '\t')) {
            ptr++;
          }
          if (*ptr == '=') {
            ptr++;
            max_age = atoi(ptr);
          }
          if (max_age > 0 && max_age < _max_age) {
            _max_age = max_age;
          }
          // If we find even a single occurrence of private, the whole response must be private
        } else if (strncasecmp(val, TS_HTTP_VALUE_PRIVATE, TS_HTTP_LEN_PRIVATE) == 0) {
          found_private = true;
          // Every requested document must have immutable for the final response to be immutable
        } else if (strncasecmp(val, HTTP_IMMUTABLE, strlen(HTTP_IMMUTABLE)) == 0) {
          found_immutable = true;
        }
      }
    }
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }

  if (!found_immutable) {
    LOG_DEBUG("Did not see an immutable cache control. The response will be not be immutable");
    _immutable = false;
  }

  if (found_private) {
    LOG_DEBUG("Saw a private cache control. The response will be private");
    _publicity = Publicity::PRIVATE;
  }
}

string
CacheControlHeader::generate() const
{
  unsigned int max_age;
  char line_buf[256];
  const char *publicity;
  const char *immutable;

  // Previously, all combo_cache documents were public. However, that's a bug. If any requested document is private the combo_cache
  // document should private as well.
  if (_publicity == Publicity::PUBLIC || _publicity == Publicity::DEFAULT) {
    publicity = TS_HTTP_VALUE_PUBLIC;
  } else {
    publicity = TS_HTTP_VALUE_PRIVATE;
  }

  immutable = (_immutable ? ", " HTTP_IMMUTABLE : "");
  max_age   = (_max_age == numeric_limits<unsigned int>::max() ? 315360000 : _max_age); // default is 10 years

  sprintf(line_buf, "Cache-Control: max-age=%u, %s%s\r\n", max_age, publicity, immutable);
  return string(line_buf);
}

// forward declarations
static int handleReadRequestHeader(TSCont contp, TSEvent event, void *edata);
static bool isComboHandlerRequest(TSMBuffer bufp, TSMLoc hdr_loc, TSMLoc url_loc);
static void getClientRequest(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc hdr_loc, TSMLoc url_loc, ClientRequest &creq);
static void parseQueryParameters(const char *query, int query_len, ClientRequest &creq);
static void checkGzipAcceptance(TSMBuffer bufp, TSMLoc hdr_loc, ClientRequest &creq);
static int handleServerEvent(TSCont contp, TSEvent event, void *edata);
static bool initRequestProcessing(InterceptData &int_data, void *edata, bool &write_response);
static bool readInterceptRequest(InterceptData &int_data);
static bool writeResponse(InterceptData &int_data);
static bool writeErrorResponse(InterceptData &int_data, int &n_bytes_written);
static bool writeStandardHeaderFields(InterceptData &int_data, int &n_bytes_written);
static void prepareResponse(InterceptData &int_data, ByteBlockList &body_blocks, string &resp_header_fields);
static bool getDefaultBucket(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc hdr_obj, ClientRequest &creq);

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = "combo_handler";
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[combo_handler][%s] plugin registration failed", __FUNCTION__);
    return;
  }

  if (argc > 1) {
    int c;
    static const struct option longopts[] = {
      {"max-files", required_argument, nullptr, 'f'},
      {nullptr, 0, nullptr, 0},
    };

    int longindex = 0;
    optind        = 1; // Force restart to avoid problems with other plugins.
    while ((c = getopt_long(argc, const_cast<char *const *>(argv), "f:", longopts, &longindex)) != -1) {
      switch (c) {
      case 'f': {
        char *tmp = nullptr;
        long n    = strtol(optarg, &tmp, 0);
        if (tmp == optarg) {
          TSError("[%s] %s requires a numeric argument", DEBUG_TAG, longopts[longindex].name);
        } else if (n < 1) {
          TSError("[%s] %s must be a positive number", DEBUG_TAG, longopts[longindex].name);
        } else {
          MaxFileCount = n;
          TSDebug(DEBUG_TAG, "Max files set to %u", MaxFileCount);
        }
        break;
      }
      default:
        TSError("[%s] Unrecognized option '%s'", DEBUG_TAG, argv[optind - 1]);
        break;
      }
    }
  }

  if (argc >= optind && (argv[optind][0] != '-' || argv[optind][1])) {
    COMBO_HANDLER_PATH = argv[optind];
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
  }
  ++optind;
  LOG_DEBUG("Combo handler path is [%.*s]", length(COMBO_HANDLER_PATH), COMBO_HANDLER_PATH.data());

  SIG_KEY_NAME = (argc > optind && (argv[optind][0] != '-' || argv[optind][1])) ? argv[optind] : "";
  ++optind;
  LOG_DEBUG("Signature key is [%.*s]", length(SIG_KEY_NAME), SIG_KEY_NAME.data());

  if (argc > optind && (argv[optind][0] != '-' || argv[optind][1])) {
    stringstream strstream(argv[optind++]);
    string header;
    while (getline(strstream, header, ':')) {
      HEADER_ALLOWLIST.push_back(header);
    }
  }
  ++optind;

  for (unsigned int i = 0; i < HEADER_ALLOWLIST.size(); i++) {
    LOG_DEBUG("AllowList: %s", HEADER_ALLOWLIST[i].c_str());
  }

  std::string content_type_allowlist_filespec = (argc > optind && (argv[optind][0] != '-' || argv[optind][1])) ? argv[optind] : "";
  if (content_type_allowlist_filespec.empty()) {
    LOG_DEBUG("No Content-Type allowlist file specified (all content types allowed)");
  } else {
    // If we have a path and it's not an absolute path, make it relative to the
    // configuration directory.
    if (content_type_allowlist_filespec[0] != '/') {
      content_type_allowlist_filespec = std::string(TSConfigDirGet()) + '/' + content_type_allowlist_filespec;
    }
    LOG_DEBUG("Content-Type allowlist file: %s", content_type_allowlist_filespec.c_str());
    ContentTypeHandler::loadAllowList(content_type_allowlist_filespec);
  }
  ++optind;

  TSCont rrh_contp = TSContCreate(handleReadRequestHeader, nullptr);
  if (!rrh_contp) {
    LOG_ERROR("Could not create read request header continuation");
    return;
  }

  TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, rrh_contp);

  if (TSUserArgIndexReserve(TS_USER_ARGS_TXN, DEBUG_TAG, "will save plugin-enable flag here", &arg_idx) != TS_SUCCESS) {
    LOG_ERROR("failed to reserve private data slot");
    return;
  } else {
    LOG_DEBUG("txn_arg_idx: %d", arg_idx);
  }

  Utils::init(&TSDebug, &TSError);
  LOG_DEBUG("Plugin started");
}

/*
  Handle TS_EVENT_HTTP_OS_DNS event after TS_EVENT_HTTP_POST_REMAP and
  TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE in order to make this plugin
  "per-remap configurable", that is, we enable combo for specific channels,
  and disable for other channels.

  In yahoo's original code, this function handle TS_EVENT_HTTP_READ_REQUEST_HDR.
  Because TS_EVENT_HTTP_READ_REQUEST_HDR is before TSRemapDoRemap, we can not
  read "plugin_enable" flag in the READ_REQUEST_HDR event.
*/
static int
handleReadRequestHeader(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  if (event != TS_EVENT_HTTP_OS_DNS) {
    LOG_ERROR("unknown event for this plugin %d", event);
    return 0;
  }

  if (1 != reinterpret_cast<intptr_t>(TSUserArgGet(txnp, arg_idx))) {
    LOG_DEBUG("combo is disabled for this channel");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  LOG_DEBUG("combo is enabled for this channel");
  LOG_DEBUG("handling TS_EVENT_HTTP_OS_DNS event");

  TSEvent reenable_to_event = TS_EVENT_HTTP_CONTINUE;
  TSMBuffer bufp;
  TSMLoc hdr_loc;

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
    TSMLoc url_loc;
    if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
      if (isComboHandlerRequest(bufp, hdr_loc, url_loc)) {
        TSCont contp = TSContCreate(handleServerEvent, TSMutexCreate());
        if (!contp) {
          LOG_ERROR("[%s] Could not create intercept request", __FUNCTION__);
          reenable_to_event = TS_EVENT_HTTP_ERROR;
        } else {
          TSHttpTxnServerIntercept(contp, txnp);
          InterceptData *int_data = new InterceptData(contp);
          TSContDataSet(contp, int_data);
          // todo: check if these two cacheable sets are required
          TSHttpTxnReqCacheableSet(txnp, 1);
          TSHttpTxnRespCacheableSet(txnp, 1);
          getClientRequest(txnp, bufp, hdr_loc, url_loc, int_data->creq);
          LOG_DEBUG("Setup server intercept to handle client request");
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
  bool retval        = false;
  const char *method = TSHttpHdrMethodGet(bufp, hdr_loc, &method_len);

  if (!method) {
    LOG_ERROR("Could not obtain method!");
  } else {
    if ((method_len != TS_HTTP_LEN_GET) || (strncasecmp(method, TS_HTTP_METHOD_GET, TS_HTTP_LEN_GET) != 0)) {
      LOG_DEBUG("Unsupported method [%.*s]", method_len, method);
    } else {
      retval = true;
    }

    if (retval) {
      int path_len;
      const char *path = TSUrlPathGet(bufp, url_loc, &path_len);
      if (!path) {
        LOG_ERROR("Could not get path from request URL");
        retval = false;
      } else {
        retval = (path_len == length(COMBO_HANDLER_PATH)) &&
                 (strncasecmp(path, COMBO_HANDLER_PATH.data(), COMBO_HANDLER_PATH.size()) == 0);
        LOG_DEBUG("Path [%.*s] is %s combo handler path", path_len, path, (retval ? "a" : "not a"));
      }
    }
  }
  return retval;
}

static bool
getDefaultBucket(TSHttpTxn /* txnp ATS_UNUSED */, TSMBuffer bufp, TSMLoc hdr_obj, ClientRequest &creq)
{
  LOG_DEBUG("In getDefaultBucket");
  TSMLoc field_loc;
  const char *host;
  int host_len            = 0;
  bool defaultBucketFound = false;

  field_loc = TSMimeHdrFieldFind(bufp, hdr_obj, TS_MIME_FIELD_HOST, -1);
  if (field_loc == TS_NULL_MLOC) {
    LOG_ERROR("Host field not found");
    return false;
  }

  host = TSMimeHdrFieldValueStringGet(bufp, hdr_obj, field_loc, -1, &host_len);
  if (!host || host_len <= 0) {
    LOG_ERROR("Error Extracting Host Header");
    TSHandleMLocRelease(bufp, hdr_obj, field_loc);
    return false;
  }

  LOG_DEBUG("host: %.*s", host_len, host);
  creq.defaultBucket = string(host, host_len);
  defaultBucketFound = true;

  TSHandleMLocRelease(bufp, hdr_obj, field_loc);

  LOG_DEBUG("defaultBucket: %s", creq.defaultBucket.data());
  return defaultBucketFound;
}

static void
getClientRequest(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc hdr_loc, TSMLoc url_loc, ClientRequest &creq)
{
  int query_len;
  const char *query = TSUrlHttpQueryGet(bufp, url_loc, &query_len);

  if (!query) {
    LOG_ERROR("Could not get query from request URL");
    creq.status = TS_HTTP_STATUS_BAD_REQUEST;
    return;
  } else {
    if (!getDefaultBucket(txnp, bufp, hdr_loc, creq)) {
      LOG_ERROR("failed getting Default Bucket for the request");
      return;
    }
    if (query_len > MAX_QUERY_LENGTH) {
      creq.status = TS_HTTP_STATUS_BAD_REQUEST;
      LOG_ERROR("querystring too long");
      return;
    }
    parseQueryParameters(query, query_len, creq);
    creq.client_addr = TSHttpTxnClientAddrGet(txnp);
    checkGzipAcceptance(bufp, hdr_loc, creq);
  }
}

static void
parseQueryParameters(const char *query, int query_len, ClientRequest &creq)
{
  creq.status         = TS_HTTP_STATUS_OK;
  int param_start_pos = 0;
  bool sig_verified   = false;
  int colon_pos       = -1;
  string file_url("http://localhost/");
  size_t file_base_url_size      = file_url.size();
  const char *common_prefix      = nullptr;
  int common_prefix_size         = 0;
  const char *common_prefix_path = nullptr;
  int common_prefix_path_size    = 0;

  for (int i = 0; i <= query_len; ++i) {
    if ((i == query_len) || (query[i] == '&')) {
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
            }
            if (!sig_verified) {
              LOG_DEBUG("Signature [%.*s] on query [%.*s] is invalid", param_len - 4, param + 4, param_start_pos, query);
            }
          } else {
            LOG_DEBUG("Verification not configured, ignoring signature");
          }
          break; // nothing useful after the signature
        }
        if ((param_len >= 2) && (param[0] == 'p') && (param[1] == '=')) {
          common_prefix_size      = param_len - 2;
          common_prefix_path_size = 0;
          if (common_prefix_size) {
            common_prefix = param + 2;
            for (int i = 0; i < common_prefix_size; ++i) {
              if (common_prefix[i] == ':') {
                common_prefix_path      = common_prefix;
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
        } else {
          if (common_prefix_path_size) {
            if (colon_pos >= param_start_pos) { // we have a colon in this param as well?
              LOG_ERROR("Ambiguous 'bucket': [%.*s] specified in common prefix and [%.*s] specified in "
                        "current parameter [%.*s]",
                        common_prefix_path_size, common_prefix_path, colon_pos - param_start_pos, param, param_len, param);
              creq.file_urls.clear();
              break;
            }
            file_url.append(common_prefix_path, common_prefix_path_size);
          } else if (colon_pos >= param_start_pos) { // we have a colon
            if ((colon_pos == param_start_pos) || (colon_pos == (i - 1))) {
              LOG_ERROR("Colon-separated path [%.*s] has empty part(s)", param_len, param);
              creq.file_urls.clear();
              break;
            }
            file_url.append(param, colon_pos - param_start_pos); // appending pre ':' part first

            // modify these to point to the "actual" file path
            param_start_pos = colon_pos + 1;
            param_len       = i - param_start_pos;
            param           = query + param_start_pos;
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

  if (creq.file_urls.size() > MaxFileCount) {
    creq.status = TS_HTTP_STATUS_BAD_REQUEST;
    LOG_ERROR("too many files in url");
    creq.file_urls.clear();
  }
}

static void
checkGzipAcceptance(TSMBuffer bufp, TSMLoc hdr_loc, ClientRequest &creq)
{
  creq.gzip_accepted = false;
  TSMLoc field_loc   = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
  if (field_loc != TS_NULL_MLOC) {
    const char *value;
    int value_len;
    int n_values = TSMimeHdrFieldValuesCount(bufp, hdr_loc, field_loc);

    for (int i = 0; i < n_values; ++i) {
      value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, i, &value_len);
      if (value) {
        if ((value_len == TS_HTTP_LEN_GZIP) && (strncasecmp(value, TS_HTTP_VALUE_GZIP, value_len) == 0)) {
          creq.gzip_accepted = true;
        }
      } else {
        LOG_DEBUG("Error while getting value # %d of header [%.*s]", i, TS_MIME_LEN_ACCEPT_ENCODING, TS_MIME_FIELD_ACCEPT_ENCODING);
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
  bool write_response     = false;

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
    LOG_DEBUG("Completed request processing, shutting down");
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
    for (StringList::iterator iter = int_data.creq.file_urls.begin(); iter != int_data.creq.file_urls.end(); ++iter) {
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
    int64_t data_len;
    const char *data;
    TSIOBufferBlock block = TSIOBufferReaderStart(int_data.input.reader);
    while (block != nullptr) {
      data               = TSIOBufferBlockReadStart(block, int_data.input.reader, &data_len);
      const char *endptr = data + data_len;
      if (TSHttpHdrParseReq(int_data.http_parser, int_data.req_hdr_bufp, int_data.req_hdr_loc, &data, endptr) == TS_PARSE_DONE) {
        int_data.read_complete = true;
      }
      consumed += data_len;
      block = TSIOBufferBlockNext(block);
    }
  }
  LOG_DEBUG("Consumed %d bytes from input vio", consumed);

  TSIOBufferReaderConsume(int_data.input.reader, consumed);

  // Modify the input VIO to reflect how much data we've completed.
  TSVIONDoneSet(int_data.input.vio, TSVIONDoneGet(int_data.input.vio) + consumed);

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
static const char GZIP_ENCODING_FIELD[]   = {"Content-Encoding: gzip\r\n"};
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
      if (TSIOBufferWrite(int_data.output.buffer, resp_header_fields.data(), resp_header_fields.size()) == TS_ERROR) {
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
  TSVIONBytesSet(int_data.output.vio, n_bytes_written);

  TSVIOReenable(int_data.output.vio);
  return true;
}

static void
prepareResponse(InterceptData &int_data, ByteBlockList &body_blocks, string &resp_header_fields)
{
  if (int_data.creq.status == TS_HTTP_STATUS_OK) {
    HttpDataFetcherImpl::ResponseData resp_data;
    TSMLoc field_loc;
    time_t expires_time;
    bool got_expires_time = false;
    int num_headers       = HEADER_ALLOWLIST.size();
    int flags_list[num_headers];
    CacheControlHeader cch;

    for (int i = 0; i < num_headers; i++) {
      flags_list[i] = 0;
    }

    ContentTypeHandler cth(resp_header_fields);

    for (StringList::iterator iter = int_data.creq.file_urls.begin(); iter != int_data.creq.file_urls.end(); ++iter) {
      if (int_data.fetcher->getData(*iter, resp_data) && resp_data.status == TS_HTTP_STATUS_OK) {
        body_blocks.push_back(ByteBlock(resp_data.content, resp_data.content_len));
        if (find(HEADER_ALLOWLIST.begin(), HEADER_ALLOWLIST.end(), TS_MIME_FIELD_CONTENT_TYPE) == HEADER_ALLOWLIST.end()) {
          if (!cth.nextObjectHeader(resp_data.bufp, resp_data.hdr_loc)) {
            LOG_ERROR("Content type missing or forbidden for requested URL [%s]", iter->c_str());
            int_data.creq.status = TS_HTTP_STATUS_FORBIDDEN;
            break;
          }
        }

        // Load this document's Cache-Control header into our managing object
        cch.update(resp_data.bufp, resp_data.hdr_loc);

        field_loc = TSMimeHdrFieldFind(resp_data.bufp, resp_data.hdr_loc, TS_MIME_FIELD_EXPIRES, TS_MIME_LEN_EXPIRES);
        if (field_loc != TS_NULL_MLOC) {
          time_t curr_field_expires_time;
          int n_values = TSMimeHdrFieldValuesCount(resp_data.bufp, resp_data.hdr_loc, field_loc);
          if ((n_values != TS_ERROR) && (n_values > 0)) {
            curr_field_expires_time = TSMimeHdrFieldValueDateGet(resp_data.bufp, resp_data.hdr_loc, field_loc);
            if (!got_expires_time) {
              expires_time     = curr_field_expires_time;
              got_expires_time = true;
            } else if (curr_field_expires_time < expires_time) {
              expires_time = curr_field_expires_time;
            }
          }
          TSHandleMLocRelease(resp_data.bufp, resp_data.hdr_loc, field_loc);
        }

        for (int i = 0; i < num_headers; i++) {
          if (flags_list[i]) {
            continue;
          }

          const string &header = HEADER_ALLOWLIST[i];

          field_loc = TSMimeHdrFieldFind(resp_data.bufp, resp_data.hdr_loc, header.c_str(), header.size());
          if (field_loc != TS_NULL_MLOC) {
            bool values_added = false;
            const char *value;
            int value_len;
            int n_values = TSMimeHdrFieldValuesCount(resp_data.bufp, resp_data.hdr_loc, field_loc);
            if ((n_values != TS_ERROR) && (n_values > 0)) {
              for (int k = 0; k < n_values; k++) {
                value = TSMimeHdrFieldValueStringGet(resp_data.bufp, resp_data.hdr_loc, field_loc, k, &value_len);
                if (!values_added) {
                  resp_header_fields.append(header + ": ");
                  values_added = true;
                } else {
                  resp_header_fields.append(", ");
                }
                resp_header_fields.append(value, value_len);
              }
              if (values_added) {
                resp_header_fields.append("\r\n");
                flags_list[i] = 1;
              }
            }
            TSHandleMLocRelease(resp_data.bufp, resp_data.hdr_loc, field_loc);
          }
        }

      } else {
        LOG_ERROR("Could not get content for requested URL [%s]", iter->c_str());
        int_data.creq.status = TS_HTTP_STATUS_BAD_REQUEST;
        break;
      }
    }
    if (int_data.creq.status == TS_HTTP_STATUS_OK) {
      // Add in Cache-Control header
      if (find(HEADER_ALLOWLIST.begin(), HEADER_ALLOWLIST.end(), TS_MIME_FIELD_CACHE_CONTROL) == HEADER_ALLOWLIST.end()) {
        resp_header_fields.append(cch.generate());
      }
      if (find(HEADER_ALLOWLIST.begin(), HEADER_ALLOWLIST.end(), TS_MIME_FIELD_EXPIRES) == HEADER_ALLOWLIST.end()) {
        if (got_expires_time) {
          if (expires_time <= 0) {
            resp_header_fields.append("Expires: 0\r\n");
          } else {
            char line_buf[128];
            struct tm gm_expires_time;
            int line_size = strftime(line_buf, 128, "Expires: %a, %d %b %Y %T GMT\r\n", gmtime_r(&expires_time, &gm_expires_time));
            resp_header_fields.append(line_buf, line_size);
          }
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

bool
ContentTypeHandler::nextObjectHeader(TSMBuffer bufp, TSMLoc hdr_loc)
{
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_TYPE, TS_MIME_LEN_CONTENT_TYPE);
  if (field_loc != TS_NULL_MLOC) {
    bool values_added = false;
    const char *value;
    int value_len;
    int n_values = TSMimeHdrFieldValuesCount(bufp, hdr_loc, field_loc);
    for (int i = 0; i < n_values; ++i) {
      value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, i, &value_len);
      ts::TextView tv{value, value_len};
      tv = tv.prefix(';').rtrim(std::string_view(" \t"));
      if (_content_type_allowlist.empty()) {
        ;
      } else if (std::find_if(_content_type_allowlist.begin(), _content_type_allowlist.end(), [tv](ts::TextView tv2) -> bool {
                   return strcasecmp(tv, tv2) == 0;
                 }) == _content_type_allowlist.end()) {
        return false;
      } else if (tv.empty()) {
        // allowlist is bad, contains an empty string.
        return false;
      }
      if (!_added_content_type) {
        if (!values_added) {
          _resp_header_fields.append("Content-Type: ");
          values_added = true;
        } else {
          _resp_header_fields.append(", ");
        }
        _resp_header_fields.append(value, value_len);
      }
    }
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    if (values_added) {
      _resp_header_fields.append("\r\n");

      // Assume that the Content-type field from the first header covers all the responses being combined.
      _added_content_type = true;
    }
    return true;
  }
  // No content type header field so doesn't pass allowlist if there is one.
  return _content_type_allowlist.empty();
}

void
ContentTypeHandler::loadAllowList(std::string const &file_spec)
{
  std::fstream fs;
  char line_buffer[256];
  bool extra_junk_on_line{false};
  int line_num = 0;

  fs.open(file_spec, std::ios_base::in);
  if (fs.good()) {
    for (;;) {
      ++line_num;
      fs.getline(line_buffer, sizeof(line_buffer));
      if (!fs.good()) {
        break;
      }
      constexpr std::string_view bs{" \t"sv};
      ts::TextView line{line_buffer, std::size_t(fs.gcount() - 1)};
      line.ltrim(bs);
      if (line.empty() || line[0] == '#') {
        // Empty/comment line.
        continue;
      }
      ts::TextView content_type{line.take_prefix_at(bs)};
      line.trim(bs);
      if (line.size() && (line[0] != '#')) {
        extra_junk_on_line = true;
        break;
      }
      _content_type_allowlist.emplace_back(content_type);
    }
  }
  if (fs.fail() && !(fs.eof() && (fs.gcount() == 0))) {
    LOG_ERROR("Error reading Content-Type allowlist config file %s, line %d", file_spec.c_str(), line_num);
  } else if (extra_junk_on_line) {
    LOG_ERROR("More than one type on line %d in Content-Type allowlist config file %s", line_num, file_spec.c_str());
  } else if (_content_type_allowlist.empty()) {
    LOG_ERROR("Content-type allowlist config file %s must have at least one entry", file_spec.c_str());
  } else {
    // End of file.
    return;
  }
  _content_type_allowlist.clear();
  // An empty string marks object as bad.
  _content_type_allowlist.emplace_back("");
}

static const char INVARIANT_FIELD_LINES[]    = {"Vary: Accept-Encoding\r\n"};
static const char INVARIANT_FIELD_LINES_SIZE = sizeof(INVARIANT_FIELD_LINES) - 1;

static bool
writeStandardHeaderFields(InterceptData &int_data, int &n_bytes_written)
{
  if (find(HEADER_ALLOWLIST.begin(), HEADER_ALLOWLIST.end(), TS_MIME_FIELD_VARY) == HEADER_ALLOWLIST.end()) {
    if (TSIOBufferWrite(int_data.output.buffer, INVARIANT_FIELD_LINES, INVARIANT_FIELD_LINES_SIZE) == TS_ERROR) {
      LOG_ERROR("Error while writing invariant fields");
      return false;
    }
    n_bytes_written += INVARIANT_FIELD_LINES_SIZE;
  }

  if (find(HEADER_ALLOWLIST.begin(), HEADER_ALLOWLIST.end(), TS_MIME_FIELD_LAST_MODIFIED) == HEADER_ALLOWLIST.end()) {
    time_t time_now = static_cast<time_t>(TShrtime() / 1000000000); // it returns nanoseconds!
    char last_modified_line[128];
    struct tm gmnow;
    int last_modified_line_size =
      strftime(last_modified_line, 128, "Last-Modified: %a, %d %b %Y %T GMT\r\n", gmtime_r(&time_now, &gmnow));
    if (TSIOBufferWrite(int_data.output.buffer, last_modified_line, last_modified_line_size) == TS_ERROR) {
      LOG_ERROR("Error while writing last-modified fields");
      return false;
    }
    n_bytes_written += last_modified_line_size;
  }

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

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  TSUserArgSet(rh, arg_idx, (void *)1); /* Save for later hooks */
  return TSREMAP_NO_REMAP;              /* Continue with next remap plugin in chain */
}

/*
  Initialize the plugin as a remap plugin.
*/
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[TSRemapInit] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->size < sizeof(TSRemapInterface)) {
    strncpy(errbuf, "[TSRemapInit] - Incorrect size of TSRemapInterface structure", errbuf_size - 1);
    return TS_ERROR;
  }

  if (TSUserArgIndexReserve(TS_USER_ARGS_TXN, DEBUG_TAG, "will save plugin-enable flag here", &arg_idx) != TS_SUCCESS) {
    LOG_ERROR("failed to reserve private data slot");
    return TS_ERROR;
  } else {
    LOG_DEBUG("txn_arg_idx: %d", arg_idx);
  }

  TSDebug(DEBUG_TAG, "%s plugin's remap part is initialized", DEBUG_TAG);

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  *ih = nullptr;

  TSDebug(DEBUG_TAG, "%s Remap Instance for '%s' created", DEBUG_TAG, argv[0]);
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  return;
}
