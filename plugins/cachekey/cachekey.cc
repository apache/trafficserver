/*
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

/**
 * @file cachekey.cc
 * @brief Cache key manipulation.
 */

#include <cstring> /* strlen() */
#include <sstream> /* istringstream */
#include <utility>
#include "cachekey.h"

static void
append(String &target, unsigned n)
{
  char buf[sizeof("4294967295")];
  snprintf(buf, sizeof(buf), "%u", n);
  target.append(buf);
}

static void
appendEncoded(String &target, const char *s, size_t len)
{
  if (0 == len) {
    return;
  }

  char tmp[len * 2];
  size_t written;

  /* The default table does not encode the comma, so we need to use our own table here. */
  static const unsigned char map[32] = {
    0xFF, 0xFF, 0xFF,
    0xFF,       // control
    0xB4,       // space " # %
    0x08,       // ,
    0x00,       //
    0x0A,       // < >
    0x00, 0x00, //
    0x00,       //
    0x1E, 0x80, // [ \ ] ^ `
    0x00, 0x00, //
    0x1F,       // { | } ~ DEL
    0x00, 0x00, 0x00,
    0x00, // all non-ascii characters unmodified
    0x00, 0x00, 0x00,
    0x00, //               .
    0x00, 0x00, 0x00,
    0x00, //               .
    0x00, 0x00, 0x00,
    0x00 //               .
  };

  if (TSStringPercentEncode(s, len, tmp, sizeof(tmp), &written, map) == TS_SUCCESS) {
    target.append(tmp, written);
  } else {
    /* If the encoding fails (pretty unlikely), then just append what we have.
     * This is just a best-effort encoding anyway. */
    target.append(s, len);
  }
}

template <typename ContainerType, typename Iterator>
static String
containerToString(ContainerType &c, const String &sdelim, const String &delim)
{
  String result;
  for (Iterator arg(c.begin()); arg != c.end(); ++arg) {
    result.append(arg == c.begin() ? sdelim : delim);
    result.append(*arg);
  }
  return result;
}

static void
appendToContainer(StringSet &c, const String &s)
{
  c.insert(s);
}

static void
appendToContainer(StringList &c, const String &s)
{
  c.push_back(s);
}

template <typename T>
static String
getKeyQuery(const char *query, int length, const ConfigQuery &config)
{
  std::istringstream istr(String(query, length));
  String token;
  T container;

  while (std::getline(istr, token, '&')) {
    String::size_type pos(token.find_first_of('='));
    String param(token.substr(0, pos == String::npos ? token.size() : pos));

    if (config.toBeAdded(param)) {
      ::appendToContainer(container, token);
    }
  }

  return containerToString<T, typename T::const_iterator>(container, "?", "&");
}

static void
ltrim(String &target)
{
  String::size_type p(target.find_first_not_of(' '));

  if (p != target.npos) {
    target.erase(0, p);
  }
}

static TSMLoc
nextDuplicate(TSMBuffer buffer, TSMLoc hdr, TSMLoc field)
{
  TSMLoc next = TSMimeHdrFieldNextDup(buffer, hdr, field);
  TSHandleMLocRelease(buffer, hdr, field);
  return next;
}

/**
 * @brief Iterates through all User-Agent headers and fields and classifies them using provided classifier.
 * @param c classifier
 * @param buf marshal buffer from the request
 * @param hdrs headers handle from the request
 * @param classname reference to the string where the class name will be returned
 */
static bool
classifyUserAgent(const Classifier &c, TSMBuffer buf, TSMLoc hdrs, String &classname)
{
  TSMLoc field;
  bool matched = false;

  field = TSMimeHdrFieldFind(buf, hdrs, TS_MIME_FIELD_USER_AGENT, TS_MIME_LEN_USER_AGENT);
  while (field != TS_NULL_MLOC && !matched) {
    const char *value;
    int len;
    int count = TSMimeHdrFieldValuesCount(buf, hdrs, field);

    for (int i = 0; i < count; ++i) {
      value = TSMimeHdrFieldValueStringGet(buf, hdrs, field, i, &len);
      const String val(value, len);
      if (c.classify(val, classname)) {
        matched = true;
        break;
      }
    }

    field = ::nextDuplicate(buf, hdrs, field);
  }

  TSHandleMLocRelease(buf, hdrs, field);
  return matched;
}

static String
getUri(TSMBuffer buf, TSMLoc url)
{
  String uri;
  int uriLen;
  const char *uriPtr = TSUrlStringGet(buf, url, &uriLen);
  if (nullptr != uriPtr && 0 != uriLen) {
    uri.assign(uriPtr, uriLen);
    TSfree((void *)uriPtr);
  } else {
    CacheKeyError("failed to get URI");
  }
  return uri;
}

/**
 * @brief Constructor setting up the cache key prefix, initializing request info.
 * @param txn transaction handle.
 * @param separator cache key elements separator
 * @param uriType type of the URI used to create the cachekey ("remap" or "pristine")
 * @param rri remap request info
 */
CacheKey::CacheKey(TSHttpTxn txn, String separator, CacheKeyUriType uriType, TSRemapRequestInfo *rri)
  : _txn(txn), _separator(std::move(separator)), _uriType(uriType)
{
  _key.reserve(512);

  _remap = (nullptr != rri);

  /* Get the URI and header to base the cachekey on.
   * @TODO it might make sense to add more supported URI types */

  if (_remap) {
    CacheKeyDebug("setting cache key from a remap plugin");
    if (PRISTINE == _uriType) {
      if (TS_SUCCESS != TSHttpTxnPristineUrlGet(_txn, &_buf, &_url)) {
        /* Failing here is unlikely. No action seems the only reasonable thing to do from within this plug-in */
        CacheKeyError("failed to get pristine URI handle");
        return;
      }
      CacheKeyDebug("using pristine uri '%s'", getUri(_buf, _url).c_str());
    } else {
      _buf = rri->requestBufp;
      _url = rri->requestUrl;
      CacheKeyDebug("using remap uri '%s'", getUri(_buf, _url).c_str());
    }
    _hdrs = rri->requestHdrp;
  } else {
    CacheKeyDebug("setting cache key from a global plugin");
    if (TS_SUCCESS != TSHttpTxnClientReqGet(_txn, &_buf, &_hdrs)) {
      /* Failing here is unlikely. No action seems the only reasonable thing to do from within this plug-in */
      CacheKeyError("failed to get client request handle");
      return;
    }

    if (PRISTINE == _uriType) {
      if (TS_SUCCESS != TSHttpTxnPristineUrlGet(_txn, &_buf, &_url)) {
        TSHandleMLocRelease(_buf, TS_NULL_MLOC, _hdrs);
        CacheKeyError("failed to get pristine URI handle");
        return;
      }
      CacheKeyDebug("using pristine uri '%s'", getUri(_buf, _url).c_str());
    } else {
      if (TS_SUCCESS != TSHttpHdrUrlGet(_buf, _hdrs, &_url)) {
        TSHandleMLocRelease(_buf, TS_NULL_MLOC, _hdrs);
        CacheKeyError("failed to get URI handle");
        return;
      }
      CacheKeyDebug("using post-remap uri '%s','", getUri(_buf, _url).c_str());
    }
  }
  _valid = true; /* success, we got all necessary elements - URI, headers, etc. */
}

CacheKey::~CacheKey()
{
  if (_valid) {
    /* free resources only if valid, if not valid it is assumed nothing was allocated or was freed */
    if (_remap) {
      /* _buf and _hdrs are assigned from remap info - no need to release here. */
      if (PRISTINE == _uriType) {
        if (TS_SUCCESS != TSHandleMLocRelease(_buf, TS_NULL_MLOC, _url)) {
          CacheKeyError("failed to release pristine URI handle");
        }
      }
    } else {
      if (TS_SUCCESS != TSHandleMLocRelease(_buf, TS_NULL_MLOC, _hdrs) &&
          TS_SUCCESS != TSHandleMLocRelease(_buf, TS_NULL_MLOC, _url)) {
        CacheKeyError("failed to release URI and headers handle");
      }
    }
  }
}

/**
 * @brief Append unsigned integer to the key.
 * @param number unsigned integer
 */
void
CacheKey::append(unsigned n)
{
  _key.append(_separator);
  ::append(_key, n);
}

/**
 * @brief Append a string to the key.
 * @param s string
 */
void
CacheKey::append(const String &s)
{
  _key.append(_separator);
  ::appendEncoded(_key, s.data(), s.size());
}

/**
 * @brief Append null-terminated C-style string to the key.
 * @param s null-terminated C-style string.
 */
void
CacheKey::append(const char *s)
{
  _key.append(_separator);
  ::appendEncoded(_key, s, strlen(s));
}

/**
 * @brief Append first n characters from array if characters pointed by s.
 * @param n number of characters
 * @param s character array pointer
 */
void
CacheKey::append(const char *s, unsigned n)
{
  _key.append(_separator);
  ::appendEncoded(_key, s, n);
}

/**
 * @brief Append to the cache key a custom prefix, capture from hots:port, capture from URI or default to host:port part of the
 * URI.
 * @note This is the only cache key component from the key which is always available.
 * @param prefix if not empty string will append the static prefix to the cache key.
 * @param prefixCapture if not empty will append regex capture/replacement from the host:port.
 * @param prefixCaptureUri if not empty will append regex capture/replacement from the whole URI.
 * @note if both prefix and pattern are not empty prefix will be added first, followed by the results from pattern.
 */
void
CacheKey::appendPrefix(const String &prefix, Pattern &prefixCapture, Pattern &prefixCaptureUri)
{
  // "true" would mean that the plugin config meant to override the default prefix (host:port).
  bool customPrefix = false;
  String host;
  int port = 0;

  if (!prefix.empty()) {
    customPrefix = true;
    append(prefix);
    CacheKeyDebug("added static prefix, key: '%s'", _key.c_str());
  }

  int hostLen;
  const char *hostPtr = TSUrlHostGet(_buf, _url, &hostLen);
  if (nullptr != hostPtr && 0 != hostLen) {
    host.assign(hostPtr, hostLen);
  } else {
    CacheKeyError("failed to get host");
  }
  port = TSUrlPortGet(_buf, _url);

  if (!prefixCapture.empty()) {
    customPrefix = true;

    String hostAndPort;
    hostAndPort.append(host).append(":");
    ::append(hostAndPort, port);

    StringVector captures;
    if (prefixCapture.process(hostAndPort, captures)) {
      for (auto &capture : captures) {
        append(capture);
      }
      CacheKeyDebug("added host:port capture prefix, key: '%s'", _key.c_str());
    }
  }

  if (!prefixCaptureUri.empty()) {
    customPrefix = true;

    String uri = getUri(_buf, _url);
    if (!uri.empty()) {
      StringVector captures;
      if (prefixCaptureUri.process(uri, captures)) {
        for (auto &capture : captures) {
          append(capture);
        }
        CacheKeyDebug("added URI capture prefix, key: '%s'", _key.c_str());
      }
    }
  }

  if (!customPrefix) {
    append(host);
    append(port);
    CacheKeyDebug("added default prefix, key: '%s'", _key.c_str());
  }
}

/**
 * @brief Appends to the cache key the path from the URI (default), regex capture/replacement from the URI path,
 * regex capture/replacement from URI as whole.
 * @note A path is always defined for a URI, though the defined path may be empty (zero length) (RFC 3986)
 * @param pathCapture if not empty will append regex capture/replacement from the URI path
 * @param pathCaptureUri if not empty will append regex capture/replacement from the URI as a whole
 * @todo enhance, i.e. /<regex>/<replace>/
 */
void
CacheKey::appendPath(Pattern &pathCapture, Pattern &pathCaptureUri)
{
  // "true" would mean that the plugin config meant to override the default path.
  bool customPath = false;
  String path;

  int pathLen;
  const char *pathPtr = TSUrlPathGet(_buf, _url, &pathLen);
  if (nullptr != pathPtr && 0 != pathLen) {
    path.assign(pathPtr, pathLen);
  }

  if (!pathCaptureUri.empty()) {
    customPath = true;

    String uri = getUri(_buf, _url);
    if (!uri.empty()) {
      StringVector captures;
      if (pathCaptureUri.process(uri, captures)) {
        for (auto &capture : captures) {
          append(capture);
        }
        CacheKeyDebug("added URI capture (path), key: '%s'", _key.c_str());
      }
    }
  }

  if (!pathCapture.empty()) {
    customPath = true;

    // If path is empty don't even try to capture/replace.
    if (!path.empty()) {
      StringVector captures;
      if (pathCapture.process(path, captures)) {
        for (auto &capture : captures) {
          append(capture);
        }
        CacheKeyDebug("added path capture, key: '%s'", _key.c_str());
      }
    }
  }

  if (!customPath && !path.empty()) {
    append(path);
  }
}

template <class T>
void
CacheKey::processHeader(const String &name, const ConfigHeaders &config, T &dst,
                        void (*fun)(const ConfigHeaders &config, const String &name_s, const String &value_s, T &captures))
{
  TSMLoc field;

  for (field = TSMimeHdrFieldFind(_buf, _hdrs, name.c_str(), name.size()); field != TS_NULL_MLOC;
       field = ::nextDuplicate(_buf, _hdrs, field)) {
    const char *value;
    int vlen;
    int count = TSMimeHdrFieldValuesCount(_buf, _hdrs, field);

    for (int i = 0; i < count; ++i) {
      value = TSMimeHdrFieldValueStringGet(_buf, _hdrs, field, i, &vlen);
      if (value == nullptr || vlen == 0) {
        CacheKeyDebug("missing value %d for header %s", i, name.c_str());
        continue;
      }

      String value_s(value, vlen);
      fun(config, name, value_s, dst);
    }
  }
}

template <class T>
void
captureWholeHeaders(const ConfigHeaders &config, const String &name, const String &value, T &captures)
{
  CacheKeyDebug("processing header %s", name.c_str());
  if (config.toBeAdded(name)) {
    String header;
    header.append(name).append(":").append(value);
    captures.insert(header);
    CacheKeyDebug("adding header '%s: %s'", name.c_str(), value.c_str());
  } else {
    CacheKeyDebug("failed to find header '%s'", name.c_str());
  }
}

template <class T>
void
captureFromHeaders(const ConfigHeaders &config, const String &name, const String &value, T &captures)
{
  CacheKeyDebug("processing capture from header %s", name.c_str());
  auto itMp = config.getCaptures().find(name);
  if (config.getCaptures().end() != itMp) {
    itMp->second->process(value, captures);
    CacheKeyDebug("found capture pattern for header '%s'", name.c_str());
  } else {
    CacheKeyDebug("failed to find header '%s'", name.c_str());
  }
}

/**
 * @brief Append headers by following the rules specified in the header configuration object.
 * @param config header-related configuration containing information about which headers need to be appended to the key.
 * @note Add the headers to hier-part (RFC 3986), always sort them in the cache key.
 */
void
CacheKey::appendHeaders(const ConfigHeaders &config)
{
  if (!config.toBeRemoved() && !config.toBeSkipped()) {
    /* Iterating header by header is not efficient according to comments inside traffic server API,
     * Iterate over an 'include'-kind of list or the capture definitions to avoid header by header iteration.
     * @todo: revisit this when (if?) adding regex matching for headers. */

    /* Adding whole headers, iterate over "--include-header" list */
    StringSet hdrSet; /* Sort and uniquify the header list in the cache key. */
    for (auto it = config.getInclude().begin(); it != config.getInclude().end(); ++it) {
      processHeader(*it, config, hdrSet, captureWholeHeaders);
    }

    /* Append to the cache key. It doesn't make sense to have the headers unordered in the cache key. */
    String headers_key = containerToString<StringSet, StringSet::const_iterator>(hdrSet, "", _separator);
    if (!headers_key.empty()) {
      append(headers_key);
    }
  }

  if (!config.getCaptures().empty()) {
    /* Adding captures from headers, iterate over "--capture-header" definitions */
    StringVector hdrCaptures;
    for (auto it = config.getCaptures().begin(); it != config.getCaptures().end(); ++it) {
      processHeader(it->first, config, hdrCaptures, captureFromHeaders);
    }

    /* Append to the cache key. Add the captures in the order capture definitions are captured / specified */
    for (auto &capture : hdrCaptures) {
      append(capture);
    }
  }
}

/**
 * @brief Append cookies by following the rules specified in the cookies config object.
 * @param config cookies-related configuration containing information about which cookies need to be appended to the key.
 * @note Add the cookies to "hier-part" (RFC 3986), always sort them in the cache key.
 */
void
CacheKey::appendCookies(const ConfigCookies &config)
{
  if (config.toBeRemoved() || config.toBeSkipped()) {
    /* Don't append any cookies to the cache key. */
    return;
  }

  TSMLoc field;
  StringSet cset; /* sort and uniquify the cookies list in the cache key */

  for (field = TSMimeHdrFieldFind(_buf, _hdrs, TS_MIME_FIELD_COOKIE, TS_MIME_LEN_COOKIE); field != TS_NULL_MLOC;
       field = ::nextDuplicate(_buf, _hdrs, field)) {
    int count = TSMimeHdrFieldValuesCount(_buf, _hdrs, field);

    for (int i = 0; i < count; ++i) {
      const char *value;
      int len;

      value = TSMimeHdrFieldValueStringGet(_buf, _hdrs, field, i, &len);
      if (value == nullptr || len == 0) {
        continue;
      }

      std::istringstream istr(String(value, len));
      String cookie;

      while (std::getline(istr, cookie, ';')) {
        ::ltrim(cookie); // Trim leading spaces.

        String::size_type pos(cookie.find_first_of('='));
        String name(cookie.substr(0, pos == String::npos ? cookie.size() : pos));

        /* We only add it to the cache key it is in the cookie set. */
        if (config.toBeAdded(name)) {
          cset.insert(cookie);
        }
      }
    }
  }

  /* We are iterating over the cookies in client order,
   * but the cache key needs a stable ordering, so we sort via std::set. */
  String cookies_keys = containerToString<StringSet, StringSet::const_iterator>(cset, "", ";");
  if (!cookies_keys.empty()) {
    append(cookies_keys);
  }
}

/**
 * @brief Append query parameters by following the rules specified in the query configuration object.
 * @param config query configuration containing information about which query parameters need to be appended to the key.
 * @note Keep the query parameters in the "query part" (RFC 3986).
 */
void
CacheKey::appendQuery(const ConfigQuery &config)
{
  /* No query parameters in the cache key? */
  if (config.toBeRemoved()) {
    return;
  }

  const char *query;
  int length;

  query = TSUrlHttpQueryGet(_buf, _url, &length);
  if (query == nullptr || length == 0) {
    return;
  }

  /* If need to skip all other rules just append the whole query to the key. */
  if (config.toBeSkipped()) {
    _key.append("?");
    _key.append(query, length);
    return;
  }

  /* Use the corresponding container based on whether we need
   * to sort the parameters (set) or keep the order (list) */
  String keyQuery;
  if (config.toBeSorted()) {
    keyQuery = getKeyQuery<StringSet>(query, length, config);
  } else {
    keyQuery = getKeyQuery<StringList>(query, length, config);
  }

  if (!keyQuery.empty()) {
    _key.append(keyQuery);
  }
}

/**
 * @brief Append User-Agent header captures specified in the Pattern configuration object.
 *
 * Apply given PCRE pattern/replacement to the first User-Agent value, and append any captured portions to cache key.
 * @param config PCRE pattern which contains capture groups.
 * @todo: TBD if ignoring the comma in the header as a field separator is generic enough.
 * @note Add the UA captures to hier-part (RFC 3986) in the original order.
 */
void
CacheKey::appendUaCaptures(Pattern &config)
{
  if (config.empty()) {
    return;
  }

  TSMLoc field;
  const char *value;
  int len;

  field = TSMimeHdrFieldFind(_buf, _hdrs, TS_MIME_FIELD_USER_AGENT, TS_MIME_LEN_USER_AGENT);
  if (field == TS_NULL_MLOC) {
    CacheKeyDebug("missing %.*s header", TS_MIME_LEN_USER_AGENT, TS_MIME_FIELD_USER_AGENT);
    return;
  }

  /* Now, strictly speaking, the User-Agent header should not contain a comma,
   * because that's really a field separator (RFC 2616). Unfortunately, the
   * iOS apps will send an embedded comma and we have to deal with it as if
   * it was a single header. */
  value = TSMimeHdrFieldValueStringGet(_buf, _hdrs, field, -1, &len);
  if (value && len) {
    String val(value, len);
    StringVector captures;

    if (config.process(val, captures)) {
      for (auto &capture : captures) {
        append(capture);
      }
    }
  }

  TSHandleMLocRelease(_buf, _hdrs, field);
}

/**
 * @brief Append the class name based on the User-Agent classification using the provided classifier.
 * @param classifier User-Agent header classifier which will return a single class name to be added to the key.
 * @return true if classification successful, false if no match was found.
 * @note Add the class to hier-part (RFC 3986).
 */
bool
CacheKey::appendUaClass(Classifier &classifier)
{
  String classname;
  bool matched = ::classifyUserAgent(classifier, _buf, _hdrs, classname);

  if (matched) {
    append(classname);
  } else {
    /* @todo: TBD do we need a default class name to be added to the key? */
  }

  return matched;
}

/**
 * @brief Update cache key.
 * @return true if success, false if failed to set the cache key.
 */
bool
CacheKey::finalize() const
{
  bool res = true;
  CacheKeyDebug("finalizing cache key '%s' from a %s plugin", _key.c_str(), (_remap ? "remap" : "global"));
  if (TS_SUCCESS != TSCacheUrlSet(_txn, &(_key[0]), _key.size())) {
    int len;
    char *url = TSHttpTxnEffectiveUrlStringGet(_txn, &len);
    if (nullptr != url) {
      if (_remap) {
        /* Remap instance. Always runs first by design (before TS_HTTP_POST_REMAP_HOOK) */
        CacheKeyError("failed to set cache key for url %.*s", len, url);
      } else {
        /* Global instance. We would fail and get here if a per-remap instance has already set the cache key
         * (currently TSCacheUrlSet() can be called only once successfully). Don't error, just debug.
         * @todo avoid the consecutive attempts and error only on unexpected failures. */
        CacheKeyDebug("failed to set cache key for url %.*s", len, url);
      }
      TSfree(url);
    }
    res = false;
  }
  return res;
}
