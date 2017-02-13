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
#include <time.h>
#include <string.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>

#include <openssl/sha.h>
#include <openssl/hmac.h>

#include <ts/ts.h>
#include <ts/remap.h>

///////////////////////////////////////////////////////////////////////////////
// Some constants.
//
static const char PLUGIN_NAME[] = "s3_auth";
static const char DATE_FMT[]    = "%a, %d %b %Y %H:%M:%S %z";

///////////////////////////////////////////////////////////////////////////////
// One configuration setup
//
int event_handler(TSCont, TSEvent, void *); // Forward declaration

class S3Config
{
public:
  S3Config() : _secret(NULL), _secret_len(0), _keyid(NULL), _keyid_len(0), _virt_host(false), _version(2), _cont(NULL)
  {
    _cont = TSContCreate(event_handler, NULL);
    TSContDataSet(_cont, static_cast<void *>(this));
  }

  ~S3Config()
  {
    _secret_len = _keyid_len = 0;
    TSfree(_secret);
    TSfree(_keyid);
    TSContDestroy(_cont);
  }

  // Is this configuration usable?
  bool
  valid() const
  {
    return _secret && (_secret_len > 0) && _keyid && (_keyid_len > 0) && (2 == _version);
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
  set_virt_host(bool f = true)
  {
    _virt_host = f;
  }
  void
  set_version(const char *s)
  {
    _version = strtol(s, NULL, 10);
  }

  // Parse configs from an external file
  bool parse_config(const char *config);

  // This should be called from the remap plugin, to setup the TXN hook for
  // SEND_REQUEST_HDR, such that we always attach the appropriate S3 auth.
  void
  schedule(TSHttpTxn txnp) const
  {
    TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, _cont);
  }

private:
  char *_secret;
  size_t _secret_len;
  char *_keyid;
  size_t _keyid_len;
  bool _virt_host;
  int _version;
  TSCont _cont;
};

bool
S3Config::parse_config(const char *config)
{
  if (!config) {
    TSError("[%s] called without a config file, this is broken", PLUGIN_NAME);
    return false;
  } else {
    char filename[PATH_MAX + 1];

    if (*config != '/') {
      snprintf(filename, sizeof(filename) - 1, "%s/%s", TSConfigDirGet(), config);
      config = filename;
    }

    char line[512]; // These are long lines ...
    FILE *file = fopen(config, "r");

    if (NULL == file) {
      TSError("[%s] unable to open %s", PLUGIN_NAME, config);
      return false;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
      char *pos1, *pos2;

      // Skip leading white spaces
      pos1 = line;
      while (*pos1 && isspace(*pos1))
        ++pos1;
      if (!*pos1 || ('#' == *pos1)) {
        continue;
      }

      // Skip trailig white spaces
      pos2 = pos1;
      pos1 = pos2 + strlen(pos2) - 1;
      while ((pos1 > pos2) && isspace(*pos1))
        *(pos1--) = '\0';
      if (pos1 == pos2) {
        continue;
      }

      // Identify the keys (and values if appropriate)
      if (0 == strncasecmp(pos2, "secret_key=", 11)) {
        set_secret(pos2 + 11);
      } else if (0 == strncasecmp(pos2, "access_key=", 11)) {
        set_keyid(pos2 + 11);
      } else if (0 == strncasecmp(pos2, "version=", 8)) {
        set_version(pos2 + 8);
      } else if (0 == strncasecmp(pos2, "virtual_host", 12)) {
        set_virt_host();
      } else {
        // ToDo: warnings?
      }
    }

    fclose(file);
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// This class is used to perform the S3 auth generation.
//
class S3Request
{
public:
  S3Request(TSHttpTxn txnp) : _txnp(txnp), _bufp(NULL), _hdr_loc(TS_NULL_MLOC), _url_loc(TS_NULL_MLOC) {}
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
    TSMLoc tmp = NULL;
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

  if (to_copy > 0)
    (void)strncat(dst, src, to_copy);

  return to_copy;
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
S3Request::authorize(S3Config *s3)
{
  TSHttpStatus status = TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  TSMLoc host_loc = TS_NULL_MLOC, md5_loc = TS_NULL_MLOC, contype_loc = TS_NULL_MLOC;
  int method_len = 0, path_len = 0, param_len = 0, host_len = 0, con_md5_len = 0, con_type_len = 0, date_len = 0;
  const char *method = NULL, *path = NULL, *param = NULL, *host = NULL, *con_md5 = NULL, *con_type = NULL, *host_endp = NULL;
  char date[128]; // Plenty of space for a Date value
  time_t now = time(NULL);
  struct tm now_tm;

  // Start with some request resources we need
  if (NULL == (method = TSHttpHdrMethodGet(_bufp, _hdr_loc, &method_len))) {
    return TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  }
  if (NULL == (path = TSUrlPathGet(_bufp, _url_loc, &path_len))) {
    return TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;
  }

  // get matrix parameters
  param = TSUrlHttpParamsGet(_bufp, _url_loc, &param_len);

  // Next, setup the Date: header, it's required.
  if (NULL == gmtime_r(&now, &now_tm)) {
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
    if (con_md5)
      TSDebug(PLUGIN_NAME, "%.*s", con_md5_len, con_md5);

    if (con_type)
      TSDebug(PLUGIN_NAME, "%.*s", con_type_len, con_type);

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
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  HMAC_CTX ctx[1];
#else
  HMAC_CTX *ctx;
#endif
  unsigned int hmac_len;
  size_t hmac_b64_len;
  unsigned char hmac[SHA_DIGEST_LENGTH];
  char hmac_b64[SHA_DIGEST_LENGTH * 2];

#if OPENSSL_VERSION_NUMBER < 0x10100000L
  HMAC_CTX_init(ctx);
#else
  ctx = HMAC_CTX_new();
#endif
  HMAC_Init_ex(ctx, s3->secret(), s3->secret_len(), EVP_sha1(), NULL);
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
#if OPENSSL_VERSION_NUMBER < 0x10100000L
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
event_handler(TSCont cont, TSEvent /* event */, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  S3Request request(txnp);
  TSHttpStatus status = TS_HTTP_STATUS_INTERNAL_SERVER_ERROR;

  if (request.initialize()) {
    status = request.authorize(static_cast<S3Config *>(TSContDataGet(cont)));
  }

  if (TS_HTTP_STATUS_OK == status) {
    TSDebug(PLUGIN_NAME, "Succesfully signed the AWS S3 URL");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  } else {
    TSDebug(PLUGIN_NAME, "Failed to sign the AWS S3 URL, status = %d", status);
    TSHttpTxnSetHttpRetStatus(txnp, status);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
  }

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
    snprintf(errbuf, errbuf_size - 1, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
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
    {const_cast<char *>("access_key"), required_argument, NULL, 'a'}, {const_cast<char *>("config"), required_argument, NULL, 'c'},
    {const_cast<char *>("secret_key"), required_argument, NULL, 's'}, {const_cast<char *>("version"), required_argument, NULL, 'v'},
    {const_cast<char *>("virtual_host"), no_argument, NULL, 'h'},     {NULL, no_argument, NULL, '\0'}};

  S3Config *s3 = new S3Config();

  // argv contains the "to" and "from" URLs. Skip the first so that the
  // second one poses as the program name.
  --argc;
  ++argv;

  while (true) {
    int opt = getopt_long(argc, static_cast<char *const *>(argv), "", longopt, NULL);

    switch (opt) {
    case 'c':
      s3->parse_config(optarg);
      break;
    case 'k':
      s3->set_keyid(optarg);
      break;
    case 's':
      s3->set_secret(optarg);
      break;
    case 'h':
      s3->set_virt_host();
      break;
    case 'v':
      s3->set_version(optarg);
      break;
    }

    if (opt == -1) {
      break;
    }
  }

  // Make sure we got both the shared secret and the AWS secret
  if (!s3->valid()) {
    TSError("[%s] requires both shared and AWS secret configuration", PLUGIN_NAME);
    delete s3;
    *ih = NULL;
    return TS_ERROR;
  }

  *ih = static_cast<void *>(s3);
  TSDebug(PLUGIN_NAME, "New rule: secret_key=%s, access_key=%s, virtual_host=%s", s3->secret(), s3->keyid(),
          s3->virt_host() ? "yes" : "no");

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  S3Config *s3 = static_cast<S3Config *>(ih);

  delete s3;
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
    // Now schedule the continuation to update the URL when going to origin.
    // Note that in most cases, this is a No-Op, assuming you have reasonable
    // cache hit ratio. However, the scheduling is next to free (very cheap).
    // Another option would be to use a single global hook, and pass the "s3"
    // configs via a TXN argument.
    s3->schedule(txnp);
  } else {
    TSDebug(PLUGIN_NAME, "Remap context is invalid");
    TSError("[%s] No remap context available, check code / config", PLUGIN_NAME);
    TSHttpTxnSetHttpRetStatus(txnp, TS_HTTP_STATUS_INTERNAL_SERVER_ERROR);
  }

  // This plugin actually doesn't do anything with remapping. Ever.
  return TSREMAP_NO_REMAP;
}
