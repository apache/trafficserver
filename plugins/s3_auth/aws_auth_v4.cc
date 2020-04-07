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
 * @file aws_auth_v4.cc
 * @brief AWS Auth v4 signing utility.
 * @see aws_auth_v4.h
 */

#include <cstring>        /* strlen() */
#include <string>         /* stoi() */
#include <ctime>          /* strftime(), time(), gmtime_r() */
#include <iomanip>        /* std::setw */
#include <sstream>        /* std::stringstream */
#include <openssl/sha.h>  /* SHA(), sha256_Update(), SHA256_Final, etc. */
#include <openssl/hmac.h> /* HMAC() */

#ifdef AWS_AUTH_V4_DETAILED_DEBUG_OUTPUT
#include <iostream>
#endif

#include "aws_auth_v4.h"

/**
 * @brief Lower-case Base16 encode a character string (hexadecimal format)
 *
 * @see AWS spec: http://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
 * Base16 RFC4648: https://tools.ietf.org/html/rfc4648#section-8
 *
 * @param in ptr to an input counted string to be base16 encoded.
 * @param inLen input character string length
 * @return base16 encoded string.
 */
String
base16Encode(const char *in, size_t inLen)
{
  if (nullptr == in || inLen == 0) {
    return {};
  }

  std::stringstream result;

  const char *src    = in;
  const char *srcEnd = in + inLen;

  while (src < srcEnd) {
    result << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>((*src) & 0xFF);
    src++;
  }
  return result.str();
}

/**
 * @brief URI-encode a character string (AWS specific version, see spec)
 *
 * @see AWS spec: http://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
 *
 * @todo Consider reusing / converting to TSStringPercentEncode() using a custom map to account for the AWS specific rules.
 *       Currently we don't build a library/archive so we could link with the unit-test binary. Also using
 *       different sets of encode/decode functions during runtime and unit-testing did not seem as a good idea.
 * @param in string to be URI encoded
 * @param isObjectName if true don't encode '/', keep it as it is.
 * @return encoded string.
 */
String
uriEncode(const String &in, bool isObjectName)
{
  std::stringstream result;

  for (char i : in) {
    if (isalnum(i) || i == '-' || i == '_' || i == '.' || i == '~') {
      /* URI encode every byte except the unreserved characters:
       * 'A'-'Z', 'a'-'z', '0'-'9', '-', '.', '_', and '~'. */
      result << i;
    } else if (i == ' ') {
      /* The space character is a reserved character and must be encoded as "%20" (and not as "+"). */
      result << "%20";
    } else if (isObjectName && i == '/') {
      /* Encode the forward slash character, '/', everywhere except in the object key name. */
      result << "/";
    } else {
      /* Letters in the hexadecimal value must be upper-case, for example "%1A". */
      result << "%" << std::uppercase << std::setfill('0') << std::setw(2) << std::hex << (int)i;
    }
  }

  return result.str();
}

/**
 * @brief checks if the string is URI-encoded (AWS specific encoding version, see spec)
 *
 * @see AWS spec: http://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
 *
 * @note According to the following RFC if the string is encoded and contains '%' it should
 *       be followed by 2 hexadecimal symbols otherwise '%' should be encoded with %25:
 *          https://tools.ietf.org/html/rfc3986#section-2.1
 *
 * @param in string to be URI checked
 * @param isObjectName if true encoding didn't encode '/', kept it as it is.
 * @return true if encoded, false not encoded.
 */
bool
isUriEncoded(const String &in, bool isObjectName)
{
  for (size_t pos = 0; pos < in.length(); pos++) {
    char c = in[pos];

    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      /* found a unreserved character which should not have been be encoded regardless
       * 'A'-'Z', 'a'-'z', '0'-'9', '-', '.', '_', and '~'.  */
      continue;
    }

    if (' ' == c) {
      /* space should have been encoded with %20 if the string was encoded */
      return false;
    }

    if ('/' == c && !isObjectName) {
      /* if this is not an object name '/' should have been encoded */
      return false;
    }

    if ('%' == c) {
      if (pos + 2 < in.length() && std::isxdigit(in[pos + 1]) && std::isxdigit(in[pos + 2])) {
        /* if string was encoded we should have exactly 2 hexadecimal chars following it */
        return true;
      } else {
        /* lonely '%' should have been encoded with %25 according to the RFC so likely not encoded */
        return false;
      }
    }
  }

  return false;
}

String
canonicalEncode(const String &in, bool isObjectName)
{
  String canonical;
  if (!isUriEncoded(in, isObjectName)) {
    /* Not URI-encoded */
    canonical = uriEncode(in, isObjectName);
  } else {
    /* URI-encoded, then don't encode since AWS does not encode which is not mentioned in the spec,
     * asked AWS, still waiting for confirmation */
    canonical = in;
  }

  return canonical;
}

/**
 * @brief trim the white-space character from the beginning and the end of the string ("in-place", just moving pointers around)
 *
 * @param in ptr to an input string
 * @param inLen input character count
 * @param newLen trimmed string character count.
 * @return pointer to the trimmed string.
 */
const char *
trimWhiteSpaces(const char *in, size_t inLen, size_t &newLen)
{
  if (nullptr == in || inLen == 0) {
    return in;
  }

  const char *first = in;
  while (size_t(first - in) < inLen && isspace(*first)) {
    first++;
  }

  const char *last = in + inLen - 1;
  while (last > in && isspace(*last)) {
    last--;
  }

  newLen = last - first + 1;
  return first;
}

/**
 * @brief Trim white spaces from beginning and end.
 * @returns trimmed string
 */
String
trimWhiteSpaces(const String &s)
{
  /* @todo do this better? */
  static const String whiteSpace = " \t\n\v\f\r";
  size_t start                   = s.find_first_not_of(whiteSpace);
  if (String::npos == start) {
    return String();
  }
  size_t stop = s.find_last_not_of(whiteSpace);
  return s.substr(start, stop - start + 1);
}

/*
 * Group of static inline helper function for less error prone parameter handling and unit test logging.
 */
inline static void
sha256Update(SHA256_CTX *ctx, const char *in, size_t inLen)
{
  SHA256_Update(ctx, in, inLen);
#ifdef AWS_AUTH_V4_DETAILED_DEBUG_OUTPUT
  std::cout << String(in, inLen);
#endif
}

inline static void
sha256Update(SHA256_CTX *ctx, const char *in)
{
  sha256Update(ctx, in, strlen(in));
}

inline static void
sha256Update(SHA256_CTX *ctx, const String &in)
{
  sha256Update(ctx, in.c_str(), in.length());
}

inline static void
sha256Final(unsigned char hex[SHA256_DIGEST_LENGTH], SHA256_CTX *ctx)
{
  SHA256_Final(hex, ctx);
}

/**
 * @brief: Payload SHA 256 = Hex(SHA256Hash(<payload>) (no new-line char at end)
 *
 * @todo support for signing of PUSH, POST content / payload
 * @param signPayload specifies whether the content / payload should be signed
 * @return signature of the content or "UNSIGNED-PAYLOAD" to mark that the payload is not signed
 */
String
getPayloadSha256(bool signPayload)
{
  static const String UNSIGNED_PAYLOAD("UNSIGNED-PAYLOAD");

  if (!signPayload) {
    return UNSIGNED_PAYLOAD;
  }

  unsigned char payloadHash[SHA256_DIGEST_LENGTH];
  SHA256((const unsigned char *)"", 0, payloadHash); /* empty content */

  return base16Encode((char *)payloadHash, SHA256_DIGEST_LENGTH);
}

/**
 * @brief Get Canonical Uri SHA256 Hash
 *
 * Hex(SHA256Hash(<CanonicalRequest>))
 * AWS spec: http://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
 *
 * @param api an TS API wrapper that will provide interface to HTTP request elements (method, path, query, headers, etc).
 * @param signPayload specifies if the content / payload should be signed.
 * @param includeHeaders headers that must be signed
 * @param excludeHeaders headers that must not be signed
 * @param signedHeaders a reference to a string to which the signed headers names will be appended
 * @return SHA256 hash of the canonical request.
 */
String
getCanonicalRequestSha256Hash(TsInterface &api, bool signPayload, const StringSet &includeHeaders, const StringSet &excludeHeaders,
                              String &signedHeaders)
{
  int length;
  const char *str = nullptr;
  unsigned char canonicalRequestSha256Hash[SHA256_DIGEST_LENGTH];
  SHA256_CTX canonicalRequestSha256Ctx;

  SHA256_Init(&canonicalRequestSha256Ctx);

#ifdef AWS_AUTH_V4_DETAILED_DEBUG_OUTPUT
  std::cout << "<CanonicalRequest>";
#endif

  /* <HTTPMethod>\n */
  str = api.getMethod(&length);
  sha256Update(&canonicalRequestSha256Ctx, str, length);
  sha256Update(&canonicalRequestSha256Ctx, "\n");

  /* URI Encoded Canonical URI
   * <CanonicalURI>\n */
  str = api.getPath(&length);
  String path("/");
  path.append(str, length);
  String canonicalUri = canonicalEncode(path, /* isObjectName */ true);
  sha256Update(&canonicalRequestSha256Ctx, canonicalUri);
  sha256Update(&canonicalRequestSha256Ctx, "\n");

  /* Sorted Canonical Query String
   * <CanonicalQueryString>\n */
  const char *query = api.getQuery(&length);

  StringSet paramNames;
  StringMap paramsMap;
  std::istringstream istr(String(query, length));
  String token;
  StringSet container;

  while (std::getline(istr, token, '&')) {
    String::size_type pos(token.find_first_of('='));
    String param(token.substr(0, pos == String::npos ? token.size() : pos));
    String value(pos == String::npos ? "" : token.substr(pos + 1, token.size()));

    String encodedParam = canonicalEncode(param, /* isObjectName */ false);
    paramNames.insert(encodedParam);
    paramsMap[encodedParam] = canonicalEncode(value, /* isObjectName */ false);
  }

  String queryStr;
  for (const auto &paramName : paramNames) {
    if (!queryStr.empty()) {
      queryStr.append("&");
    }
    queryStr.append(paramName);
    queryStr.append("=").append(paramsMap[paramName]);
  }
  sha256Update(&canonicalRequestSha256Ctx, queryStr);
  sha256Update(&canonicalRequestSha256Ctx, "\n");

  /* Sorted Canonical Headers
   *  <CanonicalHeaders>\n */
  StringSet signedHeadersSet;
  StringMap headersMap;

  for (HeaderIterator it = api.headerBegin(); it != api.headerEnd(); it++) {
    int nameLen;
    int valueLen;
    const char *name  = it.getName(&nameLen);
    const char *value = it.getValue(&valueLen);

    if (nullptr == name || 0 == nameLen) {
      continue;
    }

    String lowercaseName(name, nameLen);
    std::transform(lowercaseName.begin(), lowercaseName.end(), lowercaseName.begin(), ::tolower);

    /* Host, content-type and x-amx-* headers are mandatory */
    bool xAmzHeader        = (lowercaseName.length() >= X_AMZ.length() && 0 == lowercaseName.compare(0, X_AMZ.length(), X_AMZ));
    bool contentTypeHeader = (0 == CONTENT_TYPE.compare(lowercaseName));
    bool hostHeader        = (0 == HOST.compare(lowercaseName));
    if (!xAmzHeader && !contentTypeHeader && !hostHeader) {
      /* Skip internal headers (starting with '@'*/
      if ('@' == name[0] /* exclude internal headers */) {
        continue;
      }

      /* @todo do better here, since iterating over the headers in ATS is known to be less efficient,
       * come up with a better way if include headers set is non-empty */
      bool include =
        (!includeHeaders.empty() && includeHeaders.end() != includeHeaders.find(lowercaseName)); /* requested to be included */
      bool exclude =
        (!excludeHeaders.empty() && excludeHeaders.end() != excludeHeaders.find(lowercaseName)); /* requested to be excluded */

      if ((includeHeaders.empty() && exclude) || (!includeHeaders.empty() && (!include || exclude))) {
#ifdef AWS_AUTH_V4_DETAILED_DEBUG_OUTPUT
        std::cout << "ignore header: " << String(name, nameLen) << std::endl;
#endif
        continue;
      }
    }

    size_t trimValueLen   = 0;
    const char *trimValue = trimWhiteSpaces(value, valueLen, trimValueLen);

    signedHeadersSet.insert(lowercaseName);
    if (headersMap.find(lowercaseName) == headersMap.end()) {
      headersMap[lowercaseName] = String(trimValue, trimValueLen);
    } else {
      headersMap[lowercaseName].append(",").append(String(trimValue, trimValueLen));
    }
  }

  for (const auto &it : signedHeadersSet) {
    sha256Update(&canonicalRequestSha256Ctx, it);
    sha256Update(&canonicalRequestSha256Ctx, ":");
    sha256Update(&canonicalRequestSha256Ctx, headersMap[it]);
    sha256Update(&canonicalRequestSha256Ctx, "\n");
  }
  sha256Update(&canonicalRequestSha256Ctx, "\n");

  for (const auto &it : signedHeadersSet) {
    if (!signedHeaders.empty()) {
      signedHeaders.append(";");
    }
    signedHeaders.append(it);
  }

  sha256Update(&canonicalRequestSha256Ctx, signedHeaders);
  sha256Update(&canonicalRequestSha256Ctx, "\n");

  /* Hex(SHA256Hash(<payload>) (no new-line char at end)
   * @TODO support non-empty content, i.e. POST */
  String payloadSha256Hash = getPayloadSha256(signPayload);
  sha256Update(&canonicalRequestSha256Ctx, payloadSha256Hash);

  /* Hex(SHA256Hash(<CanonicalRequest>)) */
  sha256Final(canonicalRequestSha256Hash, &canonicalRequestSha256Ctx);
#ifdef AWS_AUTH_V4_DETAILED_DEBUG_OUTPUT
  std::cout << "</CanonicalRequest>" << std::endl;
#endif
  return base16Encode((char *)canonicalRequestSha256Hash, SHA256_DIGEST_LENGTH);
}

/**
 * @brief Default AWS entry-point host name to region based on (S3):
 *
 * @see http://docs.aws.amazon.com/general/latest/gr/rande.html#s3_region
 * it is used to get the region programmatically  w/o configuration
 * parameters and can (meant to) be overwritten if necessary.
 * @todo may be if one day AWS naming/mapping becomes 100% consistent
 * we could just extract (calculate) the right region from hostname.
 */
const StringMap
createDefaultRegionMap()
{
  StringMap m;
  /* us-east-2 */
  m["s3.us-east-2.amazonaws.com"]           = "us-east-2";
  m["s3-us-east-2.amazonaws.com"]           = "us-east-2";
  m["s3.dualstack.us-east-2.amazonaws.com"] = "us-east-2";
  /* "us-east-1" */
  m["s3.amazonaws.com"]                     = "us-east-1";
  m["s3.us-east-1.amazonaws.com"]           = "us-east-1";
  m["s3-external-1.amazonaws.com"]          = "us-east-1";
  m["s3.dualstack.us-east-1.amazonaws.com"] = "us-east-1";
  /* us-west-1 */
  m["s3.us-west-1.amazonaws.com"]           = "us-west-1";
  m["s3-us-west-1.amazonaws.com"]           = "us-west-1";
  m["s3.dualstack.us-west-1.amazonaws.com"] = "us-west-1";
  /* us-west-2 */
  m["s3.us-west-2.amazonaws.com"]           = "us-west-2";
  m["s3-us-west-2.amazonaws.com"]           = "us-west-2";
  m["s3.dualstack.us-west-2.amazonaws.com"] = "us-west-2";
  /* ap-south-1 */
  m["s3.ap-south-1.amazonaws.com"]           = "ap-south-1";
  m["s3-ap-south-1.amazonaws.com"]           = "ap-south-1";
  m["s3.dualstack.ap-south-1.amazonaws.com"] = "ap-south-1";
  /* ap-northeast-3 */
  m["s3.ap-northeast-3.amazonaws.com"]           = "ap-northeast-3";
  m["s3-ap-northeast-3.amazonaws.com"]           = "ap-northeast-3";
  m["s3.dualstack.ap-northeast-3.amazonaws.com"] = "ap-northeast-3";
  /* ap-northeast-2 */
  m["s3.ap-northeast-2.amazonaws.com"]           = "ap-northeast-2";
  m["s3-ap-northeast-2.amazonaws.com"]           = "ap-northeast-2";
  m["s3.dualstack.ap-northeast-2.amazonaws.com"] = "ap-northeast-2";
  /* ap-southeast-1 */
  m["s3.ap-southeast-1.amazonaws.com"]           = "ap-southeast-1";
  m["s3-ap-southeast-1.amazonaws.com"]           = "ap-southeast-1";
  m["s3.dualstack.ap-southeast-1.amazonaws.com"] = "ap-southeast-1";
  /* ap-southeast-2 */
  m["s3.ap-southeast-2.amazonaws.com"]           = "ap-southeast-2";
  m["s3-ap-southeast-2.amazonaws.com"]           = "ap-southeast-2";
  m["s3.dualstack.ap-southeast-2.amazonaws.com"] = "ap-southeast-2";
  /* ap-northeast-1 */
  m["s3.ap-northeast-1.amazonaws.com"]           = "ap-northeast-1";
  m["s3-ap-northeast-1.amazonaws.com"]           = "ap-northeast-1";
  m["s3.dualstack.ap-northeast-1.amazonaws.com"] = "ap-northeast-1";
  /* ca-central-1 */
  m["s3.ca-central-1.amazonaws.com"]           = "ca-central-1";
  m["s3-ca-central-1.amazonaws.com"]           = "ca-central-1";
  m["s3.dualstack.ca-central-1.amazonaws.com"] = "ca-central-1";
  /* cn-north-1 */
  m["s3.cn-north-1.amazonaws.com.cn"] = "cn-north-1";
  /* cn-northwest-1 */
  m["s3.cn-northwest-1.amazonaws.com.cn"] = "cn-northwest-1";
  /* eu-central-1 */
  m["s3.eu-central-1.amazonaws.com"]           = "eu-central-1";
  m["s3-eu-central-1.amazonaws.com"]           = "eu-central-1";
  m["s3.dualstack.eu-central-1.amazonaws.com"] = "eu-central-1";
  /* eu-west-1 */
  m["s3.eu-west-1.amazonaws.com"]           = "eu-west-1";
  m["s3-eu-west-1.amazonaws.com"]           = "eu-west-1";
  m["s3.dualstack.eu-west-1.amazonaws.com"] = "eu-west-1";
  /* eu-west-2 */
  m["s3.eu-west-2.amazonaws.com"]           = "eu-west-2";
  m["s3-eu-west-2.amazonaws.com"]           = "eu-west-2";
  m["s3.dualstack.eu-west-2.amazonaws.com"] = "eu-west-2";
  /* eu-west-3 */
  m["s3.eu-west-3.amazonaws.com"]           = "eu-west-3";
  m["s3-eu-west-3.amazonaws.com"]           = "eu-west-3";
  m["s3.dualstack.eu-west-3.amazonaws.com"] = "eu-west-3";
  /* sa-east-1 */
  m["s3.sa-east-1.amazonaws.com"]           = "sa-east-1";
  m["s3-sa-east-1.amazonaws.com"]           = "sa-east-1";
  m["s3.dualstack.sa-east-1.amazonaws.com"] = "sa-east-1";
  /* default "us-east-1" * */
  m[""] = "us-east-1";
  return m;
}
const StringMap defaultDefaultRegionMap = createDefaultRegionMap();

/**
 * @description default list of headers to be excluded from the signing
 */
const StringSet
createDefaultExcludeHeaders()
{
  StringSet m;
  /* exclude headers that are meant to be changed */
  m.insert("x-forwarded-for");
  m.insert("forwarded");
  m.insert("via");
  return m;
}
const StringSet defaultExcludeHeaders = createDefaultExcludeHeaders();

/**
 * @description default list of headers to be included in the signing
 */
const StringSet
createDefaultIncludeHeaders()
{
  StringSet m;
  return m;
}
const StringSet defaultIncludeHeaders = createDefaultIncludeHeaders();

/**
 * @brief Get AWS (S3) region from the entry-point
 *
 * @see Implementation based on the following:
 *   http://docs.aws.amazon.com/AmazonS3/latest/dev/BucketRestrictions.html
 *   http://docs.aws.amazon.com/general/latest/gr/rande.html#s3_region
 *
 * @param regionMap map containing entry-point to region mapping
 * @param entryPoint entry-point name
 * @param entryPointLen - entry point string length
 */
String
getRegion(const StringMap &regionMap, const char *entryPoint, size_t entryPointLen)
{
  String region;
  size_t dot = String::npos;
  String hostname(entryPoint, entryPointLen);

  /* Start looking for a match from the top-level domain backwards to keep the mapping generic
   * (so we can override it if we need later) */
  do {
    String name;
    dot = hostname.rfind('.', dot - 1);
    if (String::npos != dot) {
      name = hostname.substr(dot + 1);
    } else {
      name = hostname;
    }
    if (regionMap.end() != regionMap.find(name)) {
      region = regionMap.at(name);
      break;
    }
  } while (String::npos != dot);

  if (region.empty() && regionMap.end() != regionMap.find("")) {
    region = regionMap.at(""); /* default region if nothing matches */
  }

  return region;
}

/**
 * @brief Constructs the string to sign
 *
 * @see AWS spec: http://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html

 * @param entryPoint  entry-point name
 * @param entryPointLen entry-point name length
 * @param dateTime - ISO 8601 time
 * @param dateTimeLen - ISO 8601 time length
 * @param region AWS region name
 * @param region AWS region name length
 * @param service service name
 * @param serviceLen service name length
 * @param sha256Hash canonical request SHA 256 hash
 * @param sha256HashLen canonical request SHA 256 hash length
 * @returns the string to sign
 */
String
getStringToSign(const char *entryPoint, size_t EntryPointLen, const char *dateTime, size_t dateTimeLen, const char *region,
                size_t regionLen, const char *service, size_t serviceLen, const char *sha256Hash, size_t sha256HashLen)
{
  String stringToSign;

  /* AWS4-HMAC-SHA256\n (hard-coded, other values? */
  stringToSign.append("AWS4-HMAC-SHA256\n");

  /* time stamp in ISO8601 format: <YYYYMMDDTHHMMSSZ>\n */
  stringToSign.append(dateTime, dateTimeLen);
  stringToSign.append("\n");

  /* Scope: date.Format(<YYYYMMDD>) + "/" + <region> + "/" + <service> + "/aws4_request" */
  stringToSign.append(dateTime, 8); /* Get only the YYYYMMDD */
  stringToSign.append("/");
  stringToSign.append(region, regionLen);
  stringToSign.append("/");
  stringToSign.append(service, serviceLen);
  stringToSign.append("/aws4_request\n");
  stringToSign.append(sha256Hash, sha256HashLen);

  return stringToSign;
}

/**
 * @brief Calculates the final signature based on the following parameters and base16 encodes it.
 *
 * signing key = HMAC-SHA256(HMAC-SHA256(HMAC-SHA256(HMAC-SHA256("AWS4" + "<awsSecret>", <dateTime>),
 *                   <awsRegion>), <awsService>),"aws4_request")
 *
 * @see AWS spec: http://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
 *
 * @param awsSecret AWS secret
 * @param awsSecretLen AWS secret length
 * @param awsRegion AWS region
 * @param awsRegionLen AWS region length
 * @param awsService AWS Service name
 * @param awsServiceLen AWS service name length
 * @param dateTime ISO8601 date/time
 * @param dateTimeLen ISO8601 date/time length
 * @param stringToSign string to sign
 * @param stringToSignLen length of the string to sign
 * @param base16Signature output buffer where the base16 signature will be stored
 * @param base16SignatureLen size of the signature buffer = EVP_MAX_MD_SIZE (at least)
 *
 * @return number of characters written to the output buffer
 */
size_t
getSignature(const char *awsSecret, size_t awsSecretLen, const char *awsRegion, size_t awsRegionLen, const char *awsService,
             size_t awsServiceLen, const char *dateTime, size_t dateTimeLen, const char *stringToSign, size_t stringToSignLen,
             char *signature, size_t signatureLen)
{
  unsigned int dateKeyLen = EVP_MAX_MD_SIZE;
  unsigned char dateKey[EVP_MAX_MD_SIZE];
  unsigned int dateRegionKeyLen = EVP_MAX_MD_SIZE;
  unsigned char dateRegionKey[EVP_MAX_MD_SIZE];
  unsigned int dateRegionServiceKeyLen = EVP_MAX_MD_SIZE;
  unsigned char dateRegionServiceKey[EVP_MAX_MD_SIZE];
  unsigned int signingKeyLen = EVP_MAX_MD_SIZE;
  unsigned char signingKey[EVP_MAX_MD_SIZE];

  size_t keyLen = 4 + awsSecretLen;
  char key[keyLen];
  memcpy(key, "AWS4", 4);
  memcpy(key + 4, awsSecret, awsSecretLen);

  unsigned int len = signatureLen;
  if (HMAC(EVP_sha256(), key, keyLen, (unsigned char *)dateTime, dateTimeLen, dateKey, &dateKeyLen) &&
      HMAC(EVP_sha256(), dateKey, dateKeyLen, (unsigned char *)awsRegion, awsRegionLen, dateRegionKey, &dateRegionKeyLen) &&
      HMAC(EVP_sha256(), dateRegionKey, dateRegionKeyLen, (unsigned char *)awsService, awsServiceLen, dateRegionServiceKey,
           &dateRegionServiceKeyLen) &&
      HMAC(EVP_sha256(), dateRegionServiceKey, dateRegionServiceKeyLen, (unsigned char *)"aws4_request", 12, signingKey,
           &signingKeyLen) &&
      HMAC(EVP_sha256(), signingKey, signingKeyLen, (unsigned char *)stringToSign, stringToSignLen, (unsigned char *)signature,
           &len)) {
    return len;
  }

  return 0;
}

/**
 * @brief formats the time stamp in ISO8601 format: <YYYYMMDDTHHMMSSZ>
 */
size_t
getIso8601Time(time_t *now, char *dateTime, size_t dateTimeLen)
{
  struct tm tm;
  return strftime(dateTime, dateTimeLen, "%Y%m%dT%H%M%SZ", gmtime_r(now, &tm));
}

/**
 * @brief formats the time stamp in ISO8601 format: <YYYYMMDDTHHMMSSZ>
 */
const char *
AwsAuthV4::getDateTime(size_t *dateTimeLen)
{
  *dateTimeLen = sizeof(_dateTime) - 1;
  return _dateTime;
}

/**
 * @brief: HTTP content / payload SHA 256 = Hex(SHA256Hash(<payload>)
 * @return signature of the content or "UNSIGNED-PAYLOAD" to mark that the payload is not signed
 */
String
AwsAuthV4::getPayloadHash()
{
  return getPayloadSha256(_signPayload);
}

/**
 * @brief Get the value of the Authorization header (AWS authorization) v4
 * @return the Authorization header value
 */
String
AwsAuthV4::getAuthorizationHeader()
{
  String signedHeaders;
  String canonicalReq = getCanonicalRequestSha256Hash(_api, _signPayload, _includedHeaders, _excludedHeaders, signedHeaders);

  int hostLen      = 0;
  const char *host = _api.getHost(&hostLen);

  String awsRegion = getRegion(_regionMap, host, hostLen);

  String stringToSign = getStringToSign(host, hostLen, _dateTime, sizeof(_dateTime) - 1, awsRegion.c_str(), awsRegion.length(),
                                        _awsService, _awsServiceLen, canonicalReq.c_str(), canonicalReq.length());
#ifdef AWS_AUTH_V4_DETAILED_DEBUG_OUTPUT
  std::cout << "<StringToSign>" << stringToSign << "</StringToSign>" << std::endl;
#endif

  char signature[EVP_MAX_MD_SIZE];
  size_t signatureLen =
    getSignature(_awsSecretAccessKey, _awsSecretAccessKeyLen, awsRegion.c_str(), awsRegion.length(), _awsService, _awsServiceLen,
                 _dateTime, 8, stringToSign.c_str(), stringToSign.length(), signature, EVP_MAX_MD_SIZE);

  String base16Signature = base16Encode(signature, signatureLen);
#ifdef AWS_AUTH_V4_DETAILED_DEBUG_OUTPUT
  std::cout << "<SignatureProvided>" << base16Signature << "</SignatureProvided>" << std::endl;
#endif

  std::stringstream authorizationHeader;
  authorizationHeader << "AWS4-HMAC-SHA256 ";
  authorizationHeader << "Credential=" << String(_awsAccessKeyId, _awsAccessKeyIdLen) << "/" << String(_dateTime, 8) << "/"
                      << awsRegion << "/" << String(_awsService, _awsServiceLen) << "/"
                      << "aws4_request"
                      << ",";
  authorizationHeader << "SignedHeaders=" << signedHeaders << ",";
  authorizationHeader << "Signature=" << base16Signature;

  return authorizationHeader.str();
}

/**
 * @brief Authorization v4 constructor
 *
 * @param api wrapper providing access to HTTP request elements (URI host, path, query, headers, etc.)
 * @param now current time-stamp
 * @param signPayload defines if the HTTP content / payload needs to be signed
 * @param awsAccessKeyId AWS access key ID
 * @param awsAccessKeyIdLen AWS access key ID length
 * @param awsSecretAccessKey AWS secret
 * @param awsSecretAccessKeyLen AWS secret length
 * @param awsService AWS Service name
 * @param awsServiceLen AWS service name length
 * @param includeHeaders set of headers to be signed
 * @param excludeHeaders set of headers not to be signed
 * @param regionMap entry-point to AWS region mapping
 */
AwsAuthV4::AwsAuthV4(TsInterface &api, time_t *now, bool signPayload, const char *awsAccessKeyId, size_t awsAccessKeyIdLen,
                     const char *awsSecretAccessKey, size_t awsSecretAccessKeyLen, const char *awsService, size_t awsServiceLen,
                     const StringSet &includedHeaders, const StringSet &excludedHeaders, const StringMap &regionMap)
  : _api(api),
    _signPayload(signPayload),
    _awsAccessKeyId(awsAccessKeyId),
    _awsAccessKeyIdLen(awsAccessKeyIdLen),
    _awsSecretAccessKey(awsSecretAccessKey),
    _awsSecretAccessKeyLen(awsSecretAccessKeyLen),
    _awsService(awsService),
    _awsServiceLen(awsServiceLen),
    _includedHeaders(includedHeaders.empty() ? defaultIncludeHeaders : includedHeaders),
    _excludedHeaders(excludedHeaders.empty() ? defaultExcludeHeaders : excludedHeaders),
    _regionMap(regionMap.empty() ? defaultDefaultRegionMap : regionMap)
{
  getIso8601Time(now, _dateTime, sizeof(_dateTime));
}
