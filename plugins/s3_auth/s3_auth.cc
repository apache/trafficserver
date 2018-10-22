/** @file

  This is a simple URL signature generator for AWS S3 services.

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
#include <ctime>
#include <cstring>
#include <getopt.h>
#include <sys/time.h>

#include <cstdio>
#include <cstdlib>
#include <climits>
#include <cctype>

#include <fstream> /* std::ifstream */
#include <string>
#include <unordered_map>

#include <openssl/sha.h>
#include <openssl/hmac.h>

#include <ts/ts.h>
#include <ts/remap.h>
#include "tscore/ink_config.h"

// Special snowflake here, only availbale when building inside the ATS source tree.
#include "tscore/ink_atomic.h"
#include "aws_auth_v4.h"

///////////////////////////////////////////////////////////////////////////////
// Some constants.
//
static const char PLUGIN_NAME[] = "s3_auth";
static const char DATE_FMT[]    = "%a, %d %b %Y %H:%M:%S %z";

/**
 * @brief Rebase a relative path onto the configuration directory.
 */
static String
makeConfigPath(const String &path)
{
  if (path.empty() || path[0] == '/') {
    return path;
  }

  return String(TSConfigDirGet()) + "/" + path;
}

/**
 * @brief a helper function which loads the entry-point to region from files.
 * @param args classname + filename in '<classname>:<filename>' format.
 * @return true if successful, false otherwise.
 */
static bool
loadRegionMap(StringMap &m, const String &filename)
{
  static const char *EXPECTED_FORMAT = "<s3-entry-point>:<s3-region>";

  String path(makeConfigPath(filename));

  std::ifstream ifstr;
  String line;
  unsigned lineno = 0;

  ifstr.open(path.c_str());
  if (!ifstr) {
    TSError("[%s] failed to load s3-region map from '%s'", PLUGIN_NAME, path.c_str());
    return false;
  }

  TSDebug(PLUGIN_NAME, "loading region mapping from '%s'", path.c_str());

  m[""] = ""; /* set a default just in case if the user does not specify it */

  while (std::getline(ifstr, line)) {
    String::size_type pos;

    ++lineno;

    // Allow #-prefixed comments.
    pos = line.find_first_of('#');
    if (pos != String::npos) {
      line.resize(pos);
    }

    if (line.empty()) {
      continue;
    }

    std::size_t d = line.find(':');
    if (String::npos == d) {
      TSError("[%s] failed to parse region map string '%s', expected format: '%s'", PLUGIN_NAME, line.c_str(), EXPECTED_FORMAT);
      return false;
    }

    String entrypoint(trimWhiteSpaces(String(line, 0, d)));
    String region(trimWhiteSpaces(String(line, d + 1, String::npos)));

    if (region.empty()) {
      TSDebug(PLUGIN_NAME, "<s3-region> in '%s' cannot be empty (skipped), expected format: '%s'", line.c_str(), EXPECTED_FORMAT);
      continue;
    }

    if (entrypoint.empty()) {
      TSDebug(PLUGIN_NAME, "added default region %s", region.c_str());
    } else {
      TSDebug(PLUGIN_NAME, "added entry-point:%s, region:%s", entrypoint.c_str(), region.c_str());
    }

    m[entrypoint] = region;
  }

  if (m.at("").empty()) {
    TSDebug(PLUGIN_NAME, "default region was not defined");
  }

  ifstr.close();
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// Cache for the secrets file, to avoid reading / loding them repeatedly on
// a reload of remap.config. This gets cached for 60s (not configurable).
//
class S3Config;

class ConfigCache
{
public:
  S3Config *get(const char *fname);

private:
  std::unordered_map<std::string, std::pair<S3Config *, int>> _cache;
  static const int _ttl = 60;
};

ConfigCache gConfCache;

///////////////////////////////////////////////////////////////////////////////
// One configuration setup
//
int event_handler(TSCont, TSEvent, void *); // Forward declaration

class S3Config
{
public:
  S3Config(bool get_cont = true)
  {
    if (get_cont) {
      _cont = TSContCreate(event_handler, nullptr);
      TSContDataSet(_cont, static_cast<void *>(this));
    }
  }

  ~S3Config()
  {
    _secret_len = _keyid_len = _token_len = 0;
    TSfree(_secret);
    TSfree(_keyid);
    TSfree(_token);
    if (_cont) {
      TSContDestroy(_cont);
    }
  }

  // Is this configuration usable?
  bool
  valid() const
  {
    /* Check mandatory parameters first */
    if (!_secret || !(_secret_len > 0) || !_keyid || !(_keyid_len > 0) || (2 != _version && 4 != _version)) {
      return false;
    }

    /* Optional parameters, issue warning if v2 parameters are used with v4 and vice-versa (wrong parameters are ignored anyways) */
    if (2 == _version) {
      if (_v4includeHeaders_modified && !_v4includeHeaders.empty()) {
        TSError("[%s] headers are not being signed with AWS auth v2, included headers parameter ignored", PLUGIN_NAME);
      }
      if (_v4excludeHeaders_modified && !_v4excludeHeaders.empty()) {
        TSError("[%s] headers are not being signed with AWS auth v2, excluded headers parameter ignored", PLUGIN_NAME);
      }
      if (_region_map_modified && !_region_map.empty()) {
        TSError("[%s] region map is not used with AWS auth v2, parameter ignored", PLUGIN_NAME);
      }
      if (nullptr != _token || _token_len > 0) {
        TSError("[%s] session token support with AWS auth v2 is not implemented, parameter ignored", PLUGIN_NAME);
      }
    } else {
      /* 4 == _version */
      if (_virt_host_modified) {
        TSError("[%s] virtual host not used with AWS auth v4, parameter ignored", PLUGIN_NAME);
      }
    }
    return true;
  }

  void
  acquire()
  {
    ink_atomic_increment(&_ref_count, 1);
  }

  void
  release()
  {
    TSDebug(PLUGIN_NAME, "ref_count is %d", _ref_count);
    if (1 >= ink_atomic_decrement(&_ref_count, 1)) {
      TSDebug(PLUGIN_NAME, "configuration deleted, due to ref-counting");
      delete this;
    }
  }

  // Used to copy relevant configurations that can be configured in a config file. Note: we intentionally
  // don't override/use the assignment operator, since we only copy things IF they have been modified.
  void
  copy_changes_from(const S3Config *src)
  {
    if (src->_secret) {
      _secret     = TSstrdup(src->_secret);
      _secret_len = src->_secret_len;
    }

    if (src->_keyid) {
      _keyid     = TSstrdup(src->_keyid);
      _keyid_len = src->_keyid_len;
    }

    if (src->_token) {
      _token     = TSstrdup(src->_token);
      _token_len = src->_token_len;
    }

    if (src->_version_modified) {
      _version          = src->_version;
      _version_modified = true;
    }

    if (src->_virt_host_modified) {
      _virt_host          = src->_virt_host;
      _virt_host_modified = true;
    }

    if (src->_v4includeHeaders_modified) {
      _v4includeHeaders          = src->_v4includeHeaders;
      _v4includeHeaders_modified = true;
    }

    if (src->_v4excludeHeaders_modified) {
      _v4excludeHeaders          = src->_v4excludeHeaders;
      _v4excludeHeaders_modified = true;
    }

    if (src->_region_map_modified) {
      _region_map          = src->_region_map;
      _region_map_modified = true;
    }
  }

  // Getters
  bool
  virt_host() const
  {
    return _virt_host;
  }

  const char *
  secret() const
  {
    return _secret;
  }

  const char *
  keyid() const
  {
    return _keyid;
  }

  const char *
  token() const
  {
    return _token;
  }

  int
  secret_len() const
  {
    return _secret_len;
  }

  int
  keyid_len() const
  {
    return _keyid_len;
  }

  int
  token_len() const
  {
    return _token_len;
  }

  int
  version() const
  {
    return _version;
  }

  const StringSet &
  v4includeHeaders()
  {
    return _v4includeHeaders;
  }

  const StringSet &
  v4excludeHeaders()
  {
    return _v4excludeHeaders;
  }

  const StringMap &
  v4RegionMap()
  {
    return _region_map;
  }

  // Setters
  void
  set_secret(const char *s)
  {
    TSfree(_secret);
    _secret     = TSstrdup(s);
    _secret_len = strlen(s);
  }
  void
  set_keyid(const char *s)
  {
    TSfree(_keyid);
    _keyid     = TSstrdup(s);
    _keyid_len = strlen(s);
  }
  void
  set_token(const char *s)
  {
    TSfree(_token);
    _token     = TSstrdup(s);
    _token_len = strlen(s);
  }
  void
  set_virt_host(bool f = true)
  {
    _virt_host          = f;
    _virt_host_modified = true;
  }
  void
  set_version(const char *s)
  {
    _version          = strtol(s, nullptr, 10);
    _version_modified = true;
  }

  void
  set_include_headers(const char *s)
  {
    ::commaSeparateString<StringSet>(_v4includeHeaders, s);
    _v4includeHeaders_modified = true;
  }

  void
  set_exclude_headers(const char *s)
  {
    ::commaSeparateString<StringSet>(_v4excludeHeaders, s);
    _v4excludeHeaders_modified = true;

    /* Exclude headers that are meant to be changed */
    _v4excludeHeaders.insert("x-forwarded-for");
    _v4excludeHeaders.insert("forwarded");
    _v4excludeHeaders.insert("via");
  }

  void
  set_region_map(const char *s)
  {
    loadRegionMap(_region_map, s);
    _region_map_modified = true;
  }

  // Parse configs from an external file
  bool parse_config(const std::string &filename);

  // This should be called from the remap plugin, to setup the TXN hook for
  // SEND_REQUEST_HDR, such that we always attach the appropriate S3 auth.
  void
  schedule(TSHttpTxn txnp) const
  {
    TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, _cont);
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, _cont); // To release the config lease
  }

private:
  char *_secret            = nullptr;
  size_t _secret_len       = 0;
  char *_keyid             = nullptr;
  size_t _keyid_len        = 0;
  char *_token             = nullptr;
  size_t _token_len        = 0;
  bool _virt_host          = false;
  int _version             = 2;
  bool _version_modified   = false;
  bool _virt_host_modified = false;
  TSCont _cont             = nullptr;
  int _ref_count           = 1;
  StringSet _v4includeHeaders;
  bool _v4includeHeaders_modified = false;
  StringSet _v4excludeHeaders;
  bool _v4excludeHeaders_modified = false;
  StringMap _region_map;
  bool _region_map_modified = false;
};

bool
S3Config::parse_config(const std::string &config_fname)
{
  if (0 == config_fname.size()) {
    TSError("[%s] called without a config file, this is broken", PLUGIN_NAME);
    return false;
  } else {
    char line[512]; // These are long lines ...
    FILE *file = fopen(config_fname.c_str(), "r");

    if (nullptr == file) {
      TSError("[%s] unable to open %s", PLUGIN_NAME, config_fname.c_str());
      return false;
    }

    while (fgets(line, sizeof(line), file) != nullptr) {
      char *pos1, *pos2;

      // Skip leading white spaces
      pos1 = line;
      while (*pos1 && isspace(*pos1)) {
        ++pos1;
      }
      if (!*pos1 || ('#' == *pos1)) {
        continue;
      }

      // Skip trailig white spaces
      pos2 = pos1;
      pos1 = pos2 + strlen(pos2) - 1;
      while ((pos1 > pos2) && isspace(*pos1)) {
        *(pos1--) = '\0';
      }
      if (pos1 == pos2) {
        continue;
      }

      // Identify the keys (and values if appropriate)
      if (0 == strncasecmp(pos2, "secret_key=", 11)) {
        set_secret(pos2 + 11);
      } else if (0 == strncasecmp(pos2, "access_key=", 11)) {
        set_keyid(pos2 + 11);
      } else if (0 == strncasecmp(pos2, "session_token=", 14)) {
        set_token(pos2 + 14);
      } else if (0 == strncasecmp(pos2, "version=", 8)) {
        set_version(pos2 + 8);
      } else if (0 == strncasecmp(pos2, "virtual_host", 12)) {
        set_virt_host();
      } else if (0 == strncasecmp(pos2, "v4-include-headers=", 19)) {
        set_include_headers(pos2 + 19);
      } else if (0 == strncasecmp(pos2, "v4-exclude-headers=", 19)) {
        set_exclude_headers(pos2 + 19);
      } else if (0 == strncasecmp(pos2, "v4-region-map=", 14)) {
        set_region_map(pos2 + 14);
      } else {
        // ToDo: warnings?
      }
    }

    fclose(file);
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// Implementation for the ConfigCache, it has to go here since we have a sort
// of circular dependency. Note that we always parse / get the configuration
// for the file, either from cache or by making one. The user of this just
// has to copy the relevant portions, but should not use the returned object
// directly (i.e. it must be copied).
//
S3Config *
ConfigCache::get(const char *fname)
{
  struct timeval tv;

  gettimeofday(&tv, nullptr);

  // Make sure the filename is an absolute path, prepending the config dir if needed
  std::string config_fname = makeConfigPath(fname);

  auto it = _cache.find(config_fname);

  if (it != _cache.end()) {
    if (tv.tv_sec > (it->second.second + _ttl)) {
      // Update the cached configuration file.
      S3Config *s3 = new S3Config(false); // false == this config does not get the continuation

      TSDebug(PLUGIN_NAME, "Configuration from %s is stale, reloading", config_fname.c_str());
      it->second.second = tv.tv_sec;
      if (nullptr != it->second.first) {
        // The previous config update / reload attempt did not fail, safe to call release.
        it->second.first->release();
      }
      if (s3->parse_config(config_fname)) {
        it->second.first = s3;
      } else {
        // Failed the configuration parse... Set the cache response to nullptr
        s3->release();
        it->second.first = nullptr;
      }
    } else {
      TSDebug(PLUGIN_NAME, "Configuration from %s is fresh, reusing", config_fname.c_str());
    }
    return it->second.first;
  } else {
    // Create a new cached file.
    S3Config *s3 = new S3Config(false); // false == this config does not get the continuation

    if (s3->parse_config(config_fname)) {
      _cache[config_fname] = std::make_pair(s3, tv.tv_sec);
      TSDebug(PLUGIN_NAME, "Parsing and caching configuration from %s, version:%d", config_fname.c_str(), s3->version());
    } else {
      s3->release();
      return nullptr;
    }

    return s3;
  }

  TSAssert(!"Configuration parsing / caching failed");
  return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// This class is used to perform the S3 auth generation.
//
class S3Request
{
public:
  S3Request(TSHttpTxn txnp) : _txnp(txnp), _bufp(nullptr), _hdr_loc(TS_NULL_MLOC), _url_loc(TS_NULL_MLOC) {}
  ~S3Request()
  {
    TSHandleMLocRelease(_bufp, _hdr_loc, _url_loc);
    TSHandleMLocRelease(_bufp, TS_NULL_MLOC, _hdr_loc);
  }

  bool
  initialize()
  {
    if (TS_SUCCESS != TSHttpTxnServerReqGet(_txnp, &_bufp, &_hdr_loc)) {
      return false;
    }
    if (TS_SUCCESS != TSHttpHdrUrlGet(_bufp, _hdr_loc, &_url_loc)) {
      return false;
    }

    return true;
  }

  TSHttpStatus authorizeV2(S3Config *s3);
  TSHttpStatus authorizeV4(S3Config *s3);
  TSHttpStatus authorize(S3Config *s3);
  bool set_header(const char *header, int header_len, const char *val, int val_len);

private:
  TSHttpTxn _txnp;
  TSMBuffer _bufp;
  TSMLoc _hdr_loc, _url_loc;
};

///////////////////////////////////////////////////////////////////////////
// Set a header to a specific value. This will avoid going to through a
// remove / add sequence in case of an existing header.
// but clean.
bool
S3Request::set_header(const char *header, int header_len, const char *val, int val_len)
{
  if (!header || header_len <= 0 || !val || val_len <= 0) {
    return false;
  }

  bool ret         = false;
  TSMLoc field_loc = TSMimeHdrFieldFind(_bufp, _hdr_loc, header, header_len);

  if (!field_loc) {
    // No existing header, so create one
    if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(_bufp, _hdr_loc, header, header_len, &field_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(_bufp, _hdr_loc, field_loc, -1, val, val_len)) {
        TSMimeHdrFieldAppend(_bufp, _hdr_loc, field_loc);
        ret = true;
      }
      TSHandleMLocRelease(_bufp, _hdr_loc, field_loc);
    }
  } else {
    TSMLoc tmp = nullptr;
    bool first = true;

    while (field_loc) {
      if (first) {
        first = false;
        if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(_bufp, _hdr_loc, field_loc, -1, val, val_len)) {
          ret = true;
        }
      } else {
        TSMimeHdrFieldDestroy(_bufp, _hdr_loc, field_loc);
      }
      tmp = TSMimeHdrFieldNextDup(_bufp, _hdr_loc, field_loc);
      TSHandleMLocRelease(_bufp, _hdr_loc, field_loc);
      field_loc = tmp;
    }
  }

  if (ret) {
    TSDebug(PLUGIN_NAME, "Set the header %.*s: %.*s", header_len, header, val_len, val);
  }

  return ret;
}

// dst poinsts to starting offset of dst buffer
// dst_len remaining space in buffer
static size_t
str_concat(char *dst, size_t dst_len, const char *src, size_t src_len)
{
  size_t to_copy = (src_len < dst_len) ? src_len : dst_len;

  if (to_copy > 0) {
    (void)strncat(dst, src, to_copy);
  }

  return to_copy;
}

TSHttpStatus
S3Request::authorize(S3Config *s3)
{
  TSHttpStatus status = TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  switch (s3->version()) {
  case 2:
    status = authorizeV2(s3);
    break;
  case 4:
    status = authorizeV4(s3);
    break;
  default:
    break;
  }
  return status;
}

TSHttpStatus
S3Request::authorizeV4(S3Config *s3)
{
  TsApi api(_bufp, _hdr_loc, _url_loc);
  time_t now = time(nullptr);

  AwsAuthV4 util(api, &now, /* signPayload */ false, s3->keyid(), s3->keyid_len(), s3->secret(), s3->secret_len(), "s3", 2,
                 s3->v4includeHeaders(), s3->v4excludeHeaders(), s3->v4RegionMap());
  String payloadHash = util.getPayloadHash();
  if (!set_header(X_AMZ_CONTENT_SHA256.c_str(), X_AMZ_CONTENT_SHA256.length(), payloadHash.c_str(), payloadHash.length())) {
    return TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  }

  /* set x-amz-date header */
  size_t dateTimeLen   = 0;
  const char *dateTime = util.getDateTime(&dateTimeLen);
  if (!set_header(X_AMX_DATE.c_str(), X_AMX_DATE.length(), dateTime, dateTimeLen)) {
    return TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  }

  /* set X-Amz-Security-Token if we have a token */
  if (nullptr != s3->token() && '\0' != *(s3->token()) &&
      !set_header(X_AMZ_SECURITY_TOKEN.data(), X_AMZ_SECURITY_TOKEN.size(), s3->token(), s3->token_len())) {
    return TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  }

  String auth = util.getAuthorizationHeader();
  if (auth.empty()) {
    return TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  }

  if (!set_header(TS_MIME_FIELD_AUTHORIZATION, TS_MIME_LEN_AUTHORIZATION, auth.c_str(), auth.length())) {
    return TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  }

  return TS_HTTP_STATUS_OK;
}

// Method to authorize the S3 request:
//
// StringToSign = HTTP-VERB + "\n" +
//    Content-MD5 + "\n" +
//    Content-Type + "\n" +
//    Date + "\n" +
//    CanonicalizedAmzHeaders +
//    CanonicalizedResource;
//
// ToDo:
// -----
//     1) UTF8
//     2) Support POST type requests
//     3) Canonicalize the Amz headers
//
//  Note: This assumes that the URI path has been appropriately canonicalized by remapping
//
TSHttpStatus
S3Request::authorizeV2(S3Config *s3)
{
  TSHttpStatus status = TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  TSMLoc host_loc = TS_NULL_MLOC, md5_loc = TS_NULL_MLOC, contype_loc = TS_NULL_MLOC;
  int method_len = 0, path_len = 0, param_len = 0, host_len = 0, con_md5_len = 0, con_type_len = 0, date_len = 0;
  const char *method = nullptr, *path = nullptr, *param = nullptr, *host = nullptr, *con_md5 = nullptr, *con_type = nullptr,
             *host_endp = nullptr;
  char date[128]; // Plenty of space for a Date value
  time_t now = time(nullptr);
  struct tm now_tm;

  // Start with some request resources we need
  if (nullptr == (method = TSHttpHdrMethodGet(_bufp, _hdr_loc, &method_len))) {
    return TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  }
  if (nullptr == (path = TSUrlPathGet(_bufp, _url_loc, &path_len))) {
    return TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  }

  // get matrix parameters
  param = TSUrlHttpParamsGet(_bufp, _url_loc, &param_len);

  // Next, setup the Date: header, it's required.
  if (nullptr == gmtime_r(&now, &now_tm)) {
    return TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  }
  if ((date_len = strftime(date, sizeof(date) - 1, DATE_FMT, &now_tm)) <= 0) {
    return TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  }

  // Add the Date: header to the request (this overwrites any existing Date header)
  set_header(TS_MIME_FIELD_DATE, TS_MIME_LEN_DATE, date, date_len);

  // If the configuration is a "virtual host" (foo.s3.aws ...), extract the
  // first portion into the Host: header.
  if (s3->virt_host()) {
    host_loc = TSMimeHdrFieldFind(_bufp, _hdr_loc, TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST);
    if (host_loc) {
      host      = TSMimeHdrFieldValueStringGet(_bufp, _hdr_loc, host_loc, -1, &host_len);
      host_endp = static_cast<const char *>(memchr(host, '.', host_len));
    } else {
      return TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }
  }

  // Just in case we add Content-MD5 if present
  md5_loc = TSMimeHdrFieldFind(_bufp, _hdr_loc, TS_MIME_FIELD_CONTENT_MD5, TS_MIME_LEN_CONTENT_MD5);
  if (md5_loc) {
    con_md5 = TSMimeHdrFieldValueStringGet(_bufp, _hdr_loc, md5_loc, -1, &con_md5_len);
  }

  // get the Content-Type if available - (buggy) clients may send it
  // for GET requests too
  contype_loc = TSMimeHdrFieldFind(_bufp, _hdr_loc, TS_MIME_FIELD_CONTENT_TYPE, TS_MIME_LEN_CONTENT_TYPE);
  if (contype_loc) {
    con_type = TSMimeHdrFieldValueStringGet(_bufp, _hdr_loc, contype_loc, -1, &con_type_len);
  }

  // For debugging, lets produce some nice output
  if (TSIsDebugTagSet(PLUGIN_NAME)) {
    TSDebug(PLUGIN_NAME, "Signature string is:");
    // ToDo: This should include the Content-MD5 and Content-Type (for POST)
    TSDebug(PLUGIN_NAME, "%.*s", method_len, method);
    if (con_md5) {
      TSDebug(PLUGIN_NAME, "%.*s", con_md5_len, con_md5);
    }

    if (con_type) {
      TSDebug(PLUGIN_NAME, "%.*s", con_type_len, con_type);
    }

    TSDebug(PLUGIN_NAME, "%.*s", date_len, date);

    const size_t left_size   = 1024;
    char left[left_size + 1] = "/";
    size_t loff              = 1;

    // ToDo: What to do with the CanonicalizedAmzHeaders ...
    if (host && host_endp) {
      loff += str_concat(&left[loff], (left_size - loff), host, static_cast<int>(host_endp - host));
      loff += str_concat(&left[loff], (left_size - loff), "/", 1);
    }

    loff += str_concat(&left[loff], (left_size - loff), path, path_len);

    if (param) {
      loff += str_concat(&left[loff], (left_size - loff), ";", 1);
      str_concat(&left[loff], (left_size - loff), param, param_len);
    }

    TSDebug(PLUGIN_NAME, "%s", left);
  }

// Produce the SHA1 MAC digest
#ifndef HAVE_HMAC_CTX_NEW
  HMAC_CTX ctx[1];
#else
  HMAC_CTX *ctx;
#endif
  unsigned int hmac_len;
  size_t hmac_b64_len;
  unsigned char hmac[SHA_DIGEST_LENGTH];
  char hmac_b64[SHA_DIGEST_LENGTH * 2];

#ifndef HAVE_HMAC_CTX_NEW
  HMAC_CTX_init(ctx);
#else
  ctx = HMAC_CTX_new();
#endif
  HMAC_Init_ex(ctx, s3->secret(), s3->secret_len(), EVP_sha1(), nullptr);
  HMAC_Update(ctx, (unsigned char *)method, method_len);
  HMAC_Update(ctx, (unsigned char *)"\n", 1);
  HMAC_Update(ctx, (unsigned char *)con_md5, con_md5_len);
  HMAC_Update(ctx, (unsigned char *)"\n", 1);
  HMAC_Update(ctx, (unsigned char *)con_type, con_type_len);
  HMAC_Update(ctx, (unsigned char *)"\n", 1);
  HMAC_Update(ctx, (unsigned char *)date, date_len);
  HMAC_Update(ctx, (unsigned char *)"\n/", 2);

  if (host && host_endp) {
    HMAC_Update(ctx, (unsigned char *)host, host_endp - host);
    HMAC_Update(ctx, (unsigned char *)"/", 1);
  }

  HMAC_Update(ctx, (unsigned char *)path, path_len);
  if (param) {
    HMAC_Update(ctx, (unsigned char *)";", 1); // TSUrlHttpParamsGet() does not include ';'
    HMAC_Update(ctx, (unsigned char *)param, param_len);
  }

  HMAC_Final(ctx, hmac, &hmac_len);
#ifndef HAVE_HMAC_CTX_NEW
  HMAC_CTX_cleanup(ctx);
#else
  HMAC_CTX_free(ctx);
#endif

  // Do the Base64 encoding and set the Authorization header.
  if (TS_SUCCESS == TSBase64Encode((const char *)hmac, hmac_len, hmac_b64, sizeof(hmac_b64) - 1, &hmac_b64_len)) {
    char auth[256]; // This is way bigger than any string we can think of.
    int auth_len = snprintf(auth, sizeof(auth), "AWS %s:%.*s", s3->keyid(), static_cast<int>(hmac_b64_len), hmac_b64);

    if ((auth_len > 0) && (auth_len < static_cast<int>(sizeof(auth)))) {
      set_header(TS_MIME_FIELD_AUTHORIZATION, TS_MIME_LEN_AUTHORIZATION, auth, auth_len);
      status = TS_HTTP_STATUS_OK;
    }
  }

  // Cleanup
  TSHandleMLocRelease(_bufp, _hdr_loc, contype_loc);
  TSHandleMLocRelease(_bufp, _hdr_loc, md5_loc);
  TSHandleMLocRelease(_bufp, _hdr_loc, host_loc);

  return status;
}

///////////////////////////////////////////////////////////////////////////////
// This is the main continuation.
int
event_handler(TSCont cont, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  S3Config *s3   = static_cast<S3Config *>(TSContDataGet(cont));

  S3Request request(txnp);
  TSHttpStatus status  = TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  TSEvent enable_event = TS_EVENT_HTTP_CONTINUE;

  switch (event) {
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    if (request.initialize()) {
      status = request.authorize(s3);
    }

    if (TS_HTTP_STATUS_OK == status) {
      TSDebug(PLUGIN_NAME, "Succesfully signed the AWS S3 URL");
    } else {
      TSDebug(PLUGIN_NAME, "Failed to sign the AWS S3 URL, status = %d", status);
      TSHttpTxnStatusSet(txnp, status);
      enable_event = TS_EVENT_HTTP_ERROR;
    }
    break;
  case TS_EVENT_HTTP_TXN_CLOSE:
    s3->release(); // Release the configuration lease when txn closes
    break;
  default:
    TSError("[%s] Unknown event for this plugin", PLUGIN_NAME);
    TSDebug(PLUGIN_NAME, "unknown event for this plugin");
    break;
  }

  TSHttpTxnReenable(txnp, enable_event);
  return 0;
}

///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "plugin is successfully initialized");
  return TS_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// One instance per remap.config invocation.
//
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  static const struct option longopt[] = {
    {const_cast<char *>("access_key"), required_argument, nullptr, 'a'},
    {const_cast<char *>("config"), required_argument, nullptr, 'c'},
    {const_cast<char *>("secret_key"), required_argument, nullptr, 's'},
    {const_cast<char *>("version"), required_argument, nullptr, 'v'},
    {const_cast<char *>("virtual_host"), no_argument, nullptr, 'h'},
    {const_cast<char *>("v4-include-headers"), required_argument, nullptr, 'i'},
    {const_cast<char *>("v4-exclude-headers"), required_argument, nullptr, 'e'},
    {const_cast<char *>("v4-region-map"), required_argument, nullptr, 'm'},
    {const_cast<char *>("session_token"), required_argument, nullptr, 't'},
    {nullptr, no_argument, nullptr, '\0'},
  };

  S3Config *s3          = new S3Config(true); // true == this config gets the continuation
  S3Config *file_config = nullptr;

  // argv contains the "to" and "from" URLs. Skip the first so that the
  // second one poses as the program name.
  --argc;
  ++argv;

  while (true) {
    int opt = getopt_long(argc, static_cast<char *const *>(argv), "", longopt, nullptr);

    switch (opt) {
    case 'c':
      file_config = gConfCache.get(optarg); // Get cached, or new, config object, from a file
      if (!file_config) {
        TSError("[%s] invalid configuration file, %s", PLUGIN_NAME, optarg);
        *ih = nullptr;
        s3->release();
        return TS_ERROR;
      }
      break;
    case 'a':
      s3->set_keyid(optarg);
      break;
    case 's':
      s3->set_secret(optarg);
      break;
    case 't':
      s3->set_token(optarg);
      break;
    case 'h':
      s3->set_virt_host();
      break;
    case 'v':
      s3->set_version(optarg);
      break;
    case 'i':
      s3->set_include_headers(optarg);
      break;
    case 'e':
      s3->set_exclude_headers(optarg);
      break;
    case 'm':
      s3->set_region_map(optarg);
      break;
    }

    if (opt == -1) {
      break;
    }
  }

  // Copy the config file secret into our instance of the configuration.
  if (file_config) {
    s3->copy_changes_from(file_config);
  }

  // Make sure we got both the shared secret and the AWS secret
  if (!s3->valid()) {
    TSError("[%s] requires both shared and AWS secret configuration", PLUGIN_NAME);
    s3->release();
    *ih = nullptr;
    return TS_ERROR;
  }

  // Note that we don't acquire() the s3 config, it's implicit that we hold at least one ref
  *ih = static_cast<void *>(s3);
  TSDebug(PLUGIN_NAME, "New rule: access_key=%s, virtual_host=%s, version=%d", s3->keyid(), s3->virt_host() ? "yes" : "no",
          s3->version());

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  S3Config *s3 = static_cast<S3Config *>(ih);

  s3->release();
}

///////////////////////////////////////////////////////////////////////////////
// This is the main "entry" point for the plugin, called for every request.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo * /* rri */)
{
  S3Config *s3 = static_cast<S3Config *>(ih);

  if (s3) {
    TSAssert(s3->valid());
    s3->acquire(); // Increasement ref-count
    // Now schedule the continuation to update the URL when going to origin.
    // Note that in most cases, this is a No-Op, assuming you have reasonable
    // cache hit ratio. However, the scheduling is next to free (very cheap).
    // Another option would be to use a single global hook, and pass the "s3"
    // configs via a TXN argument.
    s3->schedule(txnp);
  } else {
    TSDebug(PLUGIN_NAME, "Remap context is invalid");
    TSError("[%s] No remap context available, check code / config", PLUGIN_NAME);
    TSHttpTxnStatusSet(txnp, TS_HTTP_STATUS_INTERNAL_SERVER_ERROR);
  }

  // This plugin actually doesn't do anything with remapping. Ever.
  return TSREMAP_NO_REMAP;
}
