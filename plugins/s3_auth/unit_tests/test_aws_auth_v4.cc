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
 * @file aws_auth_v4_test.cc
 * @brief Unit tests for functions implementing S3 auth version 4
 */

#include <cstring>
#include <openssl/hmac.h>   /* EVP_MAX_MD_SIZE */
#define CATCH_CONFIG_MAIN   /* include main function */
#include <catch.hpp>        /* catch unit-test framework */
#include "../aws_auth_v4.h" /* S3 auth v4 utility */

/* uriEncode() ***************************************************************************************************************** */

TEST_CASE("uriEncode(): encode empty input", "[AWS][auth][utility]")
{
  String in("");
  String encoded = uriEncode(in, /* isObjectName */ false);
  CHECK(0 == encoded.length()); /* 0 encoded because of the invalid input */
}

TEST_CASE("uriEncode(): encode unreserved chars", "[s3_auth]")
{
  const String in = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    "abcdefghijklmnopqrstuvwxyz"
                    "0123456789"
                    "-._~";
  String encoded = uriEncode(in, /* isObjectName */ false);

  CHECK(in.length() == encoded.length());
  CHECK_FALSE(encoded.compare("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                              "abcdefghijklmnopqrstuvwxyz"
                              "0123456789"
                              "-._~"));
}

TEST_CASE("uriEncode(): encode reserved chars in a name which is not object name", "[AWS][auth][utility]")
{
  const String in = " /!\"#$%&'()*+,:;<=>?@[\\]^`{|}"; /* some printable but reserved chars */
  String encoded  = uriEncode(in, /* isObjectName */ false);

  CHECK(3 * in.length() == encoded.length()); /* size of "%NN" = 3 */
  CHECK_FALSE(encoded.compare("%20%2F%21%22%23%24%25%26%27%28%29%2A%2B%2C%3A%3B%3C%3D%3E%3F%40%5B%5C%5D%5E%60%7B%7C%7D"));
}

TEST_CASE("uriEncode(): encode reserved chars in an object name", "[AWS][auth][utility]")
{
  const String in = " /!\"#$%&'()*+,:;<=>?@[\\]^`{|}"; /* some printable but reserved chars */
  String encoded  = uriEncode(in, /* isObjectName */ true);

  CHECK(3 * in.length() - 2 == encoded.length()); /* size of "%NN" = 3, '/' is not encoded */
  CHECK_FALSE(encoded.compare("%20/%21%22%23%24%25%26%27%28%29%2A%2B%2C%3A%3B%3C%3D%3E%3F%40%5B%5C%5D%5E%60%7B%7C%7D"));
}

TEST_CASE("isUriEncoded(): check an empty input", "[AWS][auth][utility]")
{
  CHECK(false == isUriEncoded(""));
}

TEST_CASE("isUriEncoded(): '%' and nothing else", "[AWS][auth][utility]")
{
  CHECK(false == isUriEncoded("%"));
}

TEST_CASE("isUriEncoded(): '%' but no hex digits", "[AWS][auth][utility]")
{
  CHECK(false == isUriEncoded("XXX%XXX"));
}

TEST_CASE("isUriEncoded(): '%' but only one hex digit", "[AWS][auth][utility]")
{
  CHECK(false == isUriEncoded("XXXXX%1XXXXXX"));
  CHECK(false == isUriEncoded("XXX%1")); // test end of string case
}

TEST_CASE("isUriEncoded(): '%' and 2 hex digit", "[AWS][auth][utility]")
{
  CHECK(true == isUriEncoded("XXX%12XXX"));
  CHECK(true == isUriEncoded("XXX%12")); // test end of string case
}

TEST_CASE("isUriEncoded(): space not encoded", "[AWS][auth][utility]")
{
  // Having a space always means it was not encoded.
  CHECK(false == isUriEncoded("XXXXX XXXXXX"));
}

TEST_CASE("isUriEncoded(): '/' in strings which are not object names", "[AWS][auth][utility]")
{
  // This is not an object name so if we have '/' => the string was not encoded.
  CHECK(false == isUriEncoded("XXXXX/XXXXXX", /* isObjectName */ false));

  // There is no '/' and '%2F' shows that it was encoded.
  CHECK(true == isUriEncoded("XXXXX%2FXXXXXX", /* isObjectName */ false));

  // This is not an object name so if we have '/' => the string was not encoded despite '%20' in it.
  CHECK(false == isUriEncoded("XXXXX/%20XXXXX", /* isObjectName */ false));
}

TEST_CASE("isUriEncoded(): '/' in strings that are object names", "[AWS][auth][utility]")
{
  // This is an object name so having '/' is normal but not enough to conclude if it is encoded or not.
  CHECK(false == isUriEncoded("XXXXX/XXXXXX", /* isObjectName */ true));

  // There is no '/' and '%2F' shows it is encoded.
  CHECK(true == isUriEncoded("XXXXX%2FXXXXXX", /* isObjectName */ true));

  // This is an object name so having '/' is normal and because of '%20' we can conclude it was encoded.
  CHECK(true == isUriEncoded("XXXXX/%20XXXXX", /* isObjectName */ true));
}

TEST_CASE("isUriEncoded(): no reserved chars in the input", "[AWS][auth][utility]")
{
  const String encoded = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                         "abcdefghijklmnopqrstuvwxyz"
                         "0123456789"
                         "-._~";
  CHECK(false == isUriEncoded(encoded));
}

TEST_CASE("isUriEncoded(): reserved chars in the input", "[AWS][auth][utility]")
{
  // some printable but reserved chars " /!\"#$%&'()*+,:;<=>?@[\\]^`{|}"
  const String encoded = "%20%2F%21%22%23%24%25%26%27%28%29%2A%2B%2C%3A%3B%3C%3D%3E%3F%40%5B%5C%5D%5E%60%7B%7C%7D";

  CHECK(true == isUriEncoded(encoded));
}

/* base16Encode() ************************************************************************************************************** */

TEST_CASE("base16Encode(): base16 encode empty string", "[utility]")
{
  const char *in = nullptr;
  size_t inLen   = 0;
  String encoded = base16Encode(in, inLen);

  CHECK(0 == encoded.length());
}

TEST_CASE("base16Encode(): base16 encode RFC4648 test vectors", "[utility]")
{
  /* use the test vectors from RFC4648: https://tools.ietf.org/html/rfc4648#section-10 (just convert to lower case) */
  const char *bench[] = {"",       "",     "f",        "66",    "fo",         "666f",   "foo",
                         "666f6f", "foob", "666f6f62", "fooba", "666f6f6261", "foobar", "666f6f626172"};

  for (size_t i = 0; i < sizeof(bench) / sizeof(char *); i += 2) {
    const char *in = bench[i];
    size_t inLen   = strlen(in);
    String encoded = base16Encode(in, inLen);

    CHECK(inLen * 2 == encoded.length());
    CHECK_FALSE(encoded.compare(bench[i + 1]));
  }
}

/* trimWhiteSpaces() ******************************************************************************************************** */

TEST_CASE("trimWhiteSpaces(): trim invalid arguments, check pointers", "[utility]")
{
  const char *in = nullptr;
  size_t inLen   = 0;
  size_t outLen  = 0;

  const char *start = trimWhiteSpaces(in, inLen, outLen);

  CHECK(in == start);
}

TEST_CASE("trimWhiteSpaces(): trim empty input, check pointers", "[utility]")
{
  const char *in = "";
  size_t inLen   = 0;
  size_t outLen  = 0;

  const char *start = trimWhiteSpaces(in, inLen, outLen);

  CHECK(in == start);
}

TEST_CASE("trimWhiteSpaces(): trim nothing to trim, check pointers", "[utility]")
{
  const char in[] = "Important Message";
  size_t inLen    = strlen(in);
  size_t newLen   = 0;

  const char *start = trimWhiteSpaces(in, inLen, newLen);

  CHECK(in == start);
  CHECK(inLen == newLen);
}

TEST_CASE("trimWhiteSpaces(): trim beginning, check pointers", "[utility]")
{
  const char in[] = " \t\nImportant Message";
  size_t inLen    = strlen(in);
  size_t newLen   = 0;

  const char *start = trimWhiteSpaces(in, inLen, newLen);

  CHECK(in + 3 == start);
  CHECK(inLen - 3 == newLen);
}

TEST_CASE("trimWhiteSpaces(): trim end, check pointers", "[utility]")
{
  const char in[] = "Important Message \t\n";
  size_t inLen    = strlen(in);
  size_t newLen   = 0;

  const char *start = trimWhiteSpaces(in, inLen, newLen);

  CHECK(in == start);
  CHECK(inLen - 3 == newLen);
}

TEST_CASE("trimWhiteSpaces(): trim both ends, check pointers", "[utility]")
{
  const char in[] = "\v\t\n Important Message \t\n";
  size_t inLen    = strlen(in);
  size_t newLen   = 0;

  const char *start = trimWhiteSpaces(in, inLen, newLen);

  CHECK(in + 4 == start);
  CHECK(inLen - 7 == newLen);
}

TEST_CASE("trimWhiteSpaces(): trim both, check string", "[utility]")
{
  String in      = "\v\t\n Important Message \t\n";
  String trimmed = trimWhiteSpaces(in);

  CHECK_FALSE(trimmed.compare("Important Message"));
  CHECK(in.length() - 7 == trimmed.length());
}

TEST_CASE("trimWhiteSpaces(): trim right, check string", "[utility]")
{
  String in      = "Important Message \t\n";
  String trimmed = trimWhiteSpaces(in);

  CHECK_FALSE(trimmed.compare("Important Message"));
  CHECK(in.length() - 3 == trimmed.length());
}

TEST_CASE("trimWhiteSpaces(): trim left, check string", "[utility]")
{
  String in      = "\v\t\n Important Message";
  String trimmed = trimWhiteSpaces(in);

  CHECK_FALSE(trimmed.compare("Important Message"));
  CHECK(in.length() - 4 == trimmed.length());
}

TEST_CASE("trimWhiteSpaces(): trim empty, check string", "[utility]")
{
  String in      = "\v\t\n  \t\n";
  String trimmed = trimWhiteSpaces(in);

  CHECK(trimmed.empty());
  CHECK(0 == trimmed.length());
}

/* AWS Regions ***************************************************************************************************** */

TEST_CASE("AWSRegions: get region empty input", "[AWS][auth][utility]")
{
  const char *host = "";
  String s         = getRegion(defaultDefaultRegionMap, host, strlen(host));
  CHECK_FALSE(s.compare("us-east-1"));
}

TEST_CASE("AWSRegions: get region by providing no bucket name", "[AWS][auth][utility]")
{
  const char *host = "s3.eu-west-2.amazonaws.com";
  String s         = getRegion(defaultDefaultRegionMap, host, strlen(host));
  CHECK_FALSE(s.compare("eu-west-2"));
}

TEST_CASE("AWSRegions: get region by providing bucket name having single label", "[AWS][auth][utility]")
{
  const char *host = "label1.label2.s3.eu-west-2.amazonaws.com";
  String s         = getRegion(defaultDefaultRegionMap, host, strlen(host));
  CHECK_FALSE(s.compare("eu-west-2"));
}

TEST_CASE("AWSRegions: get region by providing bucket name having multiple labels", "[AWS][auth][utility]")
{
  const char *host = "label1.label2.s3.eu-west-2.amazonaws.com";
  String s         = getRegion(defaultDefaultRegionMap, host, strlen(host));
  CHECK_FALSE(s.compare("eu-west-2"));
}

TEST_CASE("AWSRegions: get region by providing bucket name having single label not matching any entry point",
          "[AWS][auth][utility]")
{
  const char *host = "THIS_NEVER_MATCHES.eu-west-2.amazonaws.com";
  String s         = getRegion(defaultDefaultRegionMap, host, strlen(host));
  CHECK_FALSE(s.compare("us-east-1"));
}

TEST_CASE("AWSRegions: get region by providing bucket name having multiple labels not matching any entry point",
          "[AWS][auth][utility]")
{
  const char *host = "label1.label2.THIS_NEVER_MATCHES.eu-west-2.amazonaws.com";
  String s         = getRegion(defaultDefaultRegionMap, host, strlen(host));
  CHECK_FALSE(s.compare("us-east-1"));
}

/* AWS spec tests/example ****************************************************************************************** */

/* Test from docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
 * User id, secret and time */
const char *awsSecretAccessKey = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
const char *awsAccessKeyId     = "AKIAIOSFODNN7EXAMPLE";
const char *awsService         = "s3";

void
ValidateBench(TsInterface &api, bool signPayload, time_t *now, const char *bench[], const StringSet &includedHeaders,
              const StringSet &excludedHeaders)
{
  /* Test the main entry point for calculation of the Authorization header content */
  AwsAuthV4 util(api, now, signPayload, awsAccessKeyId, strlen(awsAccessKeyId), awsSecretAccessKey, strlen(awsSecretAccessKey),
                 awsService, strlen(awsService), includedHeaders, excludedHeaders, defaultDefaultRegionMap);
  String authorizationHeader = util.getAuthorizationHeader();
  CAPTURE(authorizationHeader);
  CHECK_FALSE(authorizationHeader.compare(bench[0]));

  /* Test payload hash */
  String payloadHash = util.getPayloadHash();
  CAPTURE(payloadHash);
  CHECK_FALSE(payloadHash.compare(bench[5]));

  /* Test the date time header content */
  size_t dateLen   = 0;
  const char *date = util.getDateTime(&dateLen);
  CAPTURE(String(date, dateLen));
  CHECK_FALSE(String(date, dateLen).compare(bench[2]));

  /* Now test particular test points to pinpoint problems easier in case of regression */

  /* test the canonization of the request */
  String signedHeaders;
  String canonicalReq = getCanonicalRequestSha256Hash(api, signPayload, includedHeaders, excludedHeaders, signedHeaders);
  CAPTURE(canonicalReq);
  CHECK_FALSE(canonicalReq.compare(bench[1]));
  CAPTURE(signedHeaders);
  CHECK_FALSE(signedHeaders.compare(bench[6]));

  /* Test the formating of the date and time */
  char dateTime[sizeof("20170428T010203Z")];
  size_t dateTimeLen = getIso8601Time(now, dateTime, sizeof(dateTime));
  CAPTURE(String(dateTime, dateTimeLen));
  CHECK_FALSE(String(dateTime, dateTimeLen).compare(bench[2]));

  /* Test the region name */
  int hostLen      = 0;
  const char *host = api.getHost(&hostLen);
  String awsRegion = getRegion(defaultDefaultRegionMap, host, hostLen);

  /* Test string to sign calculation */
  String stringToSign = getStringToSign(host, hostLen, dateTime, dateTimeLen, awsRegion.c_str(), awsRegion.length(), awsService,
                                        strlen(awsService), canonicalReq.c_str(), canonicalReq.length());
  CAPTURE(stringToSign);
  CHECK_FALSE(stringToSign.compare(bench[3]));

  /* Test the signature calculation */
  char signature[EVP_MAX_MD_SIZE];
  size_t signatureLen =
    getSignature(awsSecretAccessKey, strlen(awsSecretAccessKey), awsRegion.c_str(), awsRegion.length(), awsService,
                 strlen(awsService), dateTime, 8, stringToSign.c_str(), stringToSign.length(), signature, EVP_MAX_MD_SIZE);
  String base16Signature = base16Encode(signature, signatureLen);
  CAPTURE(base16Signature);
  CHECK_FALSE(base16Signature.compare(bench[4]));
}

/**
 * Test from docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
 * Example: GET Object
 */
TEST_CASE("AWSAuthSpecByExample: GET Object", "[AWS][auth][SpecByExample]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("test.txt");
  api._params.assign("");
  api._query.assign("");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("Range", "bytes=0-9"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));

  const char *bench[] = {
    /* Authorization Header */
    "AWS4-HMAC-SHA256 "
    "Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/aws4_request,"
    "SignedHeaders=host;range;x-amz-content-sha256;x-amz-date,"
    "Signature=f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41",
    /* Canonical Request sha256 */
    "7344ae5b7ee6c3e7e6b0fe0640412a37625d1fbfff95c48bbb2dc43964946972",
    /* Date and time*/
    "20130524T000000Z",
    /* String to sign */
    "AWS4-HMAC-SHA256\n"
    "20130524T000000Z\n"
    "20130524/us-east-1/s3/aws4_request\n"
    "7344ae5b7ee6c3e7e6b0fe0640412a37625d1fbfff95c48bbb2dc43964946972",
    /* Signature */
    "f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41",
    /* Payload hash */
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    /* Signed Headers */
    "host;range;x-amz-content-sha256;x-amz-date",
  };

  ValidateBench(api, /*signePayload */ true, &now, bench, defaultIncludeHeaders, defaultExcludeHeaders);
}

/**
 * Test from docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
 * Example: GET Bucket Lifecycle
 */
TEST_CASE("AWSAuthSpecByExample: GET Bucket Lifecycle", "[AWS][auth][SpecByExample]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("");
  api._params.assign("");
  api._query.assign("lifecycle");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));

  const char *bench[] = {
    /* Authorization Header */
    "AWS4-HMAC-SHA256 "
    "Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/aws4_request,"
    "SignedHeaders=host;x-amz-content-sha256;x-amz-date,"
    "Signature=fea454ca298b7da1c68078a5d1bdbfbbe0d65c699e0f91ac7a200a0136783543",
    /* Canonical Request sha256 */
    "9766c798316ff2757b517bc739a67f6213b4ab36dd5da2f94eaebf79c77395ca",
    /* Date and time*/
    "20130524T000000Z",
    /* String to sign */
    "AWS4-HMAC-SHA256\n"
    "20130524T000000Z\n"
    "20130524/us-east-1/s3/aws4_request\n"
    "9766c798316ff2757b517bc739a67f6213b4ab36dd5da2f94eaebf79c77395ca",
    /* Signature */
    "fea454ca298b7da1c68078a5d1bdbfbbe0d65c699e0f91ac7a200a0136783543",
    /* Payload hash */
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    /* Signed Headers */
    "host;x-amz-content-sha256;x-amz-date",
  };

  ValidateBench(api, /*signePayload */ true, &now, bench, defaultIncludeHeaders, defaultExcludeHeaders);
}

/**
 * Test from docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
 * Example: Get Bucket (List Objects)
 */
TEST_CASE("AWSAuthSpecByExample: Get Bucket List Objects", "[AWS][auth][SpecByExample]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("");
  api._params.assign("");
  api._query.assign("max-keys=2&prefix=J");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));

  const char *bench[] = {
    /* Authorization Header */
    "AWS4-HMAC-SHA256 "
    "Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/aws4_request,"
    "SignedHeaders=host;x-amz-content-sha256;x-amz-date,"
    "Signature=34b48302e7b5fa45bde8084f4b7868a86f0a534bc59db6670ed5711ef69dc6f7",
    /* Canonical Request sha256 */
    "df57d21db20da04d7fa30298dd4488ba3a2b47ca3a489c74750e0f1e7df1b9b7",
    /* Date and time*/
    "20130524T000000Z",
    /* String to sign */
    "AWS4-HMAC-SHA256\n"
    "20130524T000000Z\n"
    "20130524/us-east-1/s3/aws4_request\n"
    "df57d21db20da04d7fa30298dd4488ba3a2b47ca3a489c74750e0f1e7df1b9b7",
    /* Signature */
    "34b48302e7b5fa45bde8084f4b7868a86f0a534bc59db6670ed5711ef69dc6f7",
    /* Payload hash */
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    /* Signed Headers */
    "host;x-amz-content-sha256;x-amz-date",
  };

  ValidateBench(api, /*signePayload */ true, &now, bench, defaultIncludeHeaders, defaultExcludeHeaders);
}

/**
 * Test based on docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
 * but this time don't sign the payload to test "UNSIGNED-PAYLOAD" feature.
 * Example: Get Bucket (List Objects)
 */
TEST_CASE("AWSAuthSpecByExample: GET Bucket List Objects, unsigned pay-load", "[AWS][auth][SpecByExample]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("");
  api._query.assign("max-keys=2&prefix=J");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "UNSIGNED-PAYLOAD"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));

  const char *bench[] = {
    /* Authorization Header */
    "AWS4-HMAC-SHA256 "
    "Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/aws4_request,"
    "SignedHeaders=host;x-amz-content-sha256;x-amz-date,"
    "Signature=b1a076428fa68c2c42202ee5a5718b8207f725e451e2157d6b1c393e01fc2e68",
    /* Canonical Request sha256 */
    "528623330c85041d6fb82795b6f8d5771825d3568b9f0bc1faa8a49e1f5f9cfc",
    /* Date and time*/
    "20130524T000000Z",
    /* String to sign */
    "AWS4-HMAC-SHA256\n"
    "20130524T000000Z\n"
    "20130524/us-east-1/s3/aws4_request\n"
    "528623330c85041d6fb82795b6f8d5771825d3568b9f0bc1faa8a49e1f5f9cfc",
    /* Signature */
    "b1a076428fa68c2c42202ee5a5718b8207f725e451e2157d6b1c393e01fc2e68",
    /* Payload hash */
    "UNSIGNED-PAYLOAD",
    /* Signed Headers */
    "host;x-amz-content-sha256;x-amz-date",
  };

  ValidateBench(api, /*signePayload */ false, &now, bench, defaultIncludeHeaders, defaultExcludeHeaders);
}

/**
 * Test based on docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
 * but this time don't sign the payload to test "UNSIGNED-PAYLOAD" feature and
 * have extra headers to be excluded from the signature (internal and changing headers)
 * Example: Get Bucket (List Objects)
 */
TEST_CASE("AWSAuthSpecByExample: GET Bucket List Objects, unsigned pay-load, exclude internal and changing headers", "[AWS][auth]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("");
  api._params.assign("");
  api._query.assign("max-keys=2&prefix=J");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "UNSIGNED-PAYLOAD"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));
  api._headers.insert(std::make_pair("@internal", "internal value"));
  api._headers.insert(std::make_pair("x-forwarded-for", "192.168.7.1"));
  api._headers.insert(
    std::make_pair("via", "http/1.1 tcp ipv4 ats_dev[7e67ac60-c204-450d-90be-a426dd3b569f] (ApacheTrafficServer/7.2.0)"));

  const char *bench[] = {
    /* Authorization Header */
    "AWS4-HMAC-SHA256 "
    "Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/aws4_request,"
    "SignedHeaders=host;x-amz-content-sha256;x-amz-date,"
    "Signature=b1a076428fa68c2c42202ee5a5718b8207f725e451e2157d6b1c393e01fc2e68",
    /* Canonical Request sha256 */
    "528623330c85041d6fb82795b6f8d5771825d3568b9f0bc1faa8a49e1f5f9cfc",
    /* Date and time*/
    "20130524T000000Z",
    /* String to sign */
    "AWS4-HMAC-SHA256\n"
    "20130524T000000Z\n"
    "20130524/us-east-1/s3/aws4_request\n"
    "528623330c85041d6fb82795b6f8d5771825d3568b9f0bc1faa8a49e1f5f9cfc",
    /* Signature */
    "b1a076428fa68c2c42202ee5a5718b8207f725e451e2157d6b1c393e01fc2e68",
    /* Payload hash */
    "UNSIGNED-PAYLOAD",
    /* Signed Headers */
    "host;x-amz-content-sha256;x-amz-date",
  };

  ValidateBench(api, /*signePayload */ false, &now, bench, defaultIncludeHeaders, defaultExcludeHeaders);
}

/**
 * Test based on docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
 * but this time query param value is already encoded, it should not URI encode it twice
 * according to AWS real behavior undocumented in the specification.
 */
TEST_CASE("AWSAuthSpecByExample: GET Bucket List Objects, query param value already URI encoded", "[AWS][auth]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("PATH==");
  api._params.assign("");
  api._query.assign("key=TEST==");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "UNSIGNED-PAYLOAD"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));

  const char *bench[] = {
    /* Authorization Header */
    "AWS4-HMAC-SHA256 "
    "Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/aws4_request,"
    "SignedHeaders=host;x-amz-content-sha256;x-amz-date,"
    "Signature=3b195c74eaa89790c596114a9fcb8515d6a3cc78577f8941c46b09ee7f501194",
    /* Canonical Request sha256 */
    "7b3c74369013172ce9cb3da99b5d14e358382953e7b560b4c9bf0a46e73933cb",
    /* Date and time*/
    "20130524T000000Z",
    /* String to sign */
    "AWS4-HMAC-SHA256\n"
    "20130524T000000Z\n"
    "20130524/us-east-1/s3/aws4_request\n"
    "7b3c74369013172ce9cb3da99b5d14e358382953e7b560b4c9bf0a46e73933cb",
    /* Signature */
    "3b195c74eaa89790c596114a9fcb8515d6a3cc78577f8941c46b09ee7f501194",
    /* Payload hash */
    "UNSIGNED-PAYLOAD",
    /* Signed Headers */
    "host;x-amz-content-sha256;x-amz-date",
  };

  ValidateBench(api, /*signePayload */ false, &now, bench, defaultIncludeHeaders, defaultExcludeHeaders);

  /* Now make query param value encoded beforehand and expect the same result,
   * it should not URI encode it twice according to AWS real behavior undocumented in the specification.*/
  api._path.assign("PATH%3D%3D");
  api._query.assign("key=TEST%3D%3D");
  ValidateBench(api, /*signePayload */ false, &now, bench, defaultIncludeHeaders, defaultExcludeHeaders);
}

TEST_CASE("S3AuthV4UtilParams: signing multiple same name fields", "[AWS][auth][utility]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("");
  api._params.assign("");
  api._query.assign("max-keys=2&prefix=J");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("Content-Type", "gzip"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "UNSIGNED-PAYLOAD"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));
  api._headers.insert(std::make_pair("HeaderA", "HeaderAValue"));
  api._headers.insert(std::make_pair("HeaderB", "HeaderBValue1"));
  api._headers.insert(std::make_pair("HeaderB", "HeaderBValue2"));
  api._headers.insert(std::make_pair("HeaderC", " HeaderCValue1, HeaderCValue2 ")); // LWS between values
  api._headers.insert(std::make_pair("HeaderC", "HeaderCValue3,HeaderCValue4"));    // No LWS between values

  const char *bench[] = {
    /* Authorization Header */
    "AWS4-HMAC-SHA256 "
    "Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/aws4_request,"
    "SignedHeaders=content-type;headera;headerb;headerc;host;x-amz-content-sha256;x-amz-date,"
    "Signature=c9f57637044ddb0633430a8631f946a35d0ffff0cf7647aeaa2d0e985a69e674",

    /* Canonical Request sha256 */
    "e3429bb6a99fea0162c703ba6aaf771e16d2bb8f637c9300cd468fcbe1b66a0e",
    /* Date and time*/
    "20130524T000000Z",
    /* String to sign */
    "AWS4-HMAC-SHA256\n"
    "20130524T000000Z\n"
    "20130524/us-east-1/s3/aws4_request\n"
    "e3429bb6a99fea0162c703ba6aaf771e16d2bb8f637c9300cd468fcbe1b66a0e",
    /* Signature */
    "c9f57637044ddb0633430a8631f946a35d0ffff0cf7647aeaa2d0e985a69e674",
    /* Payload hash */
    "UNSIGNED-PAYLOAD",
    /* Signed Headers */
    "content-type;headera;headerb;headerc;host;x-amz-content-sha256;x-amz-date",
  };

  ValidateBench(api, /*signePayload */ false, &now, bench, defaultIncludeHeaders, defaultExcludeHeaders);
}

///* Utility parameters related tests ******************************************************************************** */

void
ValidateBenchCanonicalRequest(TsInterface &api, bool signPayload, time_t *now, const char *bench[],
                              const StringSet &includedHeaders, const StringSet &excludedHeaders)
{
  /* Test the main entry point for calculation of the Authorization header content */
  AwsAuthV4 util(api, now, signPayload, awsAccessKeyId, strlen(awsAccessKeyId), awsSecretAccessKey, strlen(awsSecretAccessKey),
                 awsService, strlen(awsService), includedHeaders, excludedHeaders, defaultDefaultRegionMap);

  /* test the canonization of the request */
  String signedHeaders;
  String canonicalReq = getCanonicalRequestSha256Hash(api, signPayload, includedHeaders, excludedHeaders, signedHeaders);
  CHECK_FALSE(signedHeaders.compare(bench[0]));
  CHECK_FALSE(canonicalReq.compare(bench[1]));
}

TEST_CASE("S3AuthV4UtilParams: include all headers by default", "[AWS][auth][utility]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("");
  api._params.assign("");
  api._query.assign("max-keys=2&prefix=J");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("Content-Type", "gzip"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "UNSIGNED-PAYLOAD"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));
  api._headers.insert(std::make_pair("HeaderA", "HeaderAValue"));
  api._headers.insert(std::make_pair("HeaderB", "HeaderBValue"));
  api._headers.insert(std::make_pair("HeaderC", "HeaderCValue"));
  api._headers.insert(std::make_pair("HeaderD", "HeaderDValue"));
  api._headers.insert(std::make_pair("HeaderE", "HeaderEValue"));
  api._headers.insert(std::make_pair("HeaderF", "HeaderFValue"));

  StringSet include = defaultIncludeHeaders;
  StringSet exclude = defaultExcludeHeaders;

  const char *bench[] = {
    /* Signed Headers */
    "content-type;headera;headerb;headerc;headerd;headere;headerf;host;x-amz-content-sha256;x-amz-date",
    /* Canonical Request sha256 */
    "819a275bbd601fd6f6ba39190ee8299d34fcb0f5e0a4c0d8017c35e79a026579",
  };

  ValidateBenchCanonicalRequest(api, /*signePayload */ false, &now, bench, include, exclude);
}

TEST_CASE("S3AuthV4UtilParams: include all headers explicit", "[AWS][auth][SpecByExample]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("");
  api._params.assign("");
  api._query.assign("max-keys=2&prefix=J");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("Content-Type", "gzip"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "UNSIGNED-PAYLOAD"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));
  api._headers.insert(std::make_pair("HeaderA", "HeaderAValue"));
  api._headers.insert(std::make_pair("HeaderB", "HeaderBValue"));
  api._headers.insert(std::make_pair("HeaderC", "HeaderCValue"));
  api._headers.insert(std::make_pair("HeaderD", "HeaderDValue"));
  api._headers.insert(std::make_pair("HeaderE", "HeaderEValue"));
  api._headers.insert(std::make_pair("HeaderF", "HeaderFValue"));

  StringSet include;
  commaSeparateString<StringSet>(include, "HeaderA,HeaderB,HeaderC,HeaderD,HeaderE,HeaderF");
  StringSet exclude = defaultExcludeHeaders;

  const char *bench[] = {
    /* Signed Headers */
    "content-type;headera;headerb;headerc;headerd;headere;headerf;host;x-amz-content-sha256;x-amz-date",
    /* Canonical Request sha256 */
    "819a275bbd601fd6f6ba39190ee8299d34fcb0f5e0a4c0d8017c35e79a026579",
  };

  ValidateBenchCanonicalRequest(api, /*signePayload */ false, &now, bench, include, exclude);
}

TEST_CASE("S3AuthV4UtilParams: exclude all headers explicit", "[AWS][auth][utility]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("");
  api._query.assign("max-keys=2&prefix=J");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("Content-Type", "gzip"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "UNSIGNED-PAYLOAD"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));
  api._headers.insert(std::make_pair("HeaderA", "HeaderAValue"));
  api._headers.insert(std::make_pair("HeaderB", "HeaderBValue"));
  api._headers.insert(std::make_pair("HeaderC", "HeaderCValue"));
  api._headers.insert(std::make_pair("HeaderD", "HeaderDValue"));
  api._headers.insert(std::make_pair("HeaderE", "HeaderEValue"));
  api._headers.insert(std::make_pair("HeaderF", "HeaderFValue"));

  StringSet include = defaultIncludeHeaders;
  StringSet exclude;
  commaSeparateString<StringSet>(exclude, "HeaderA,HeaderB,HeaderC,HeaderD,HeaderE,HeaderF");

  const char *bench[] = {
    /* Signed Headers */
    "content-type;host;x-amz-content-sha256;x-amz-date",
    /* Canonical Request sha256 */
    "ef3088997c69bc860e0bb36f97a8335f38863339e7fd01f2cd17b5391da575fb",
  };

  ValidateBenchCanonicalRequest(api, /*signePayload */ false, &now, bench, include, exclude);
}

TEST_CASE("S3AuthV4UtilParams: include/exclude non overlapping headers", "[AWS][auth][utility]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("");
  api._params.assign("");
  api._query.assign("max-keys=2&prefix=J");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("Content-Type", "gzip"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "UNSIGNED-PAYLOAD"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));
  api._headers.insert(std::make_pair("HeaderA", "HeaderAValue"));
  api._headers.insert(std::make_pair("HeaderB", "HeaderBValue"));
  api._headers.insert(std::make_pair("HeaderC", "HeaderCValue"));
  api._headers.insert(std::make_pair("HeaderD", "HeaderEValue"));
  api._headers.insert(std::make_pair("HeaderF", "HeaderFValue"));

  StringSet include, exclude;
  commaSeparateString<StringSet>(include, "HeaderA,HeaderB,HeaderC");
  commaSeparateString<StringSet>(exclude, "HeaderD,HeaderE,HeaderF");

  const char *bench[] = {
    /* Signed Headers */
    "content-type;headera;headerb;headerc;host;x-amz-content-sha256;x-amz-date",
    /* Canonical Request sha256 */
    "c1c7fb808eefdb712192efeed168fdecef0f8d95e8df5a2569d127068c425209",
  };

  ValidateBenchCanonicalRequest(api, /*signePayload */ false, &now, bench, include, exclude);
}

TEST_CASE("S3AuthV4UtilParams: include/exclude overlapping headers", "[AWS][auth][utility]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("");
  api._params.assign("");
  api._query.assign("max-keys=2&prefix=J");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("Content-Type", "gzip"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "UNSIGNED-PAYLOAD"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));
  api._headers.insert(std::make_pair("HeaderA", "HeaderAValue"));
  api._headers.insert(std::make_pair("HeaderB", "HeaderBValue"));
  api._headers.insert(std::make_pair("HeaderC", "HeaderCValue"));
  api._headers.insert(std::make_pair("HeaderD", "HeaderDValue"));
  api._headers.insert(std::make_pair("HeaderE", "HeaderEValue"));
  api._headers.insert(std::make_pair("HeaderF", "HeaderFValue"));

  StringSet include, exclude;
  commaSeparateString<StringSet>(include, "HeaderA,HeaderB,HeaderC");
  commaSeparateString<StringSet>(exclude, "HeaderC,HeaderD,HeaderE,HeaderF");

  const char *bench[] = {
    /* Signed Headers */
    "content-type;headera;headerb;host;x-amz-content-sha256;x-amz-date",
    /* Canonical Request sha256 */
    "0ac0bd67e304b3c25ec51f01b86c824f7439cdb0a5bc16acdebab73f34e12a57",
  };

  ValidateBenchCanonicalRequest(api, /*signePayload */ false, &now, bench, include, exclude);
}

TEST_CASE("S3AuthV4UtilParams: include/exclude overlapping headers missing include", "[AWS][auth][utility]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("");
  api._params.assign("");
  api._query.assign("max-keys=2&prefix=J");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("Content-Type", "gzip"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "UNSIGNED-PAYLOAD"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));
  api._headers.insert(std::make_pair("HeaderA", "HeaderAValue"));
  api._headers.insert(std::make_pair("HeaderB", "HeaderBValue"));
  api._headers.insert(std::make_pair("HeaderC", "HeaderCValue"));
  api._headers.insert(std::make_pair("HeaderD", "HeaderDValue"));
  api._headers.insert(std::make_pair("HeaderE", "HeaderEValue"));
  api._headers.insert(std::make_pair("HeaderF", "HeaderFValue"));

  StringSet include, exclude;
  commaSeparateString<StringSet>(include, "HeaderA,HeaderC");
  commaSeparateString<StringSet>(exclude, "HeaderC,HeaderD,HeaderE,HeaderF");

  const char *bench[] = {
    /* Signed Headers */
    "content-type;headera;host;x-amz-content-sha256;x-amz-date",
    /* Canonical Request sha256 */
    "5b5bef63c923fed685230feb91d8059fe8d56c80d21ba6922ee335ff3fcc45bf",
  };

  ValidateBenchCanonicalRequest(api, /*signePayload */ false, &now, bench, include, exclude);
}

TEST_CASE("S3AuthV4UtilParams: include/exclude overlapping headers missing exclude", "[AWS][auth][utility]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("");
  api._params.assign("");
  api._query.assign("max-keys=2&prefix=J");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("Content-Type", "gzip"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "UNSIGNED-PAYLOAD"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));
  api._headers.insert(std::make_pair("HeaderA", "HeaderAValue"));
  api._headers.insert(std::make_pair("HeaderB", "HeaderBValue"));
  api._headers.insert(std::make_pair("HeaderC", "HeaderCValue"));
  api._headers.insert(std::make_pair("HeaderD", "HeaderDValue"));
  api._headers.insert(std::make_pair("HeaderE", "HeaderEValue"));
  api._headers.insert(std::make_pair("HeaderF", "HeaderFValue"));

  StringSet include, exclude;
  commaSeparateString<StringSet>(include, "HeaderA,HeaderB,HeaderC");
  commaSeparateString<StringSet>(exclude, "HeaderC,HeaderD,HeaderF");

  const char *bench[] = {
    /* Signed Headers */
    "content-type;headera;headerb;host;x-amz-content-sha256;x-amz-date",
    /* Canonical Request sha256 */
    "0ac0bd67e304b3c25ec51f01b86c824f7439cdb0a5bc16acdebab73f34e12a57",
  };

  ValidateBenchCanonicalRequest(api, /*signePayload */ false, &now, bench, include, exclude);
}

/*
 * Mandatory headers Host, x-amz-* and Content-Type will must be included even if the user asked to exclude them.
 */
TEST_CASE("S3AuthV4UtilParams: include content type", "[AWS][auth][utility]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("");
  api._params.assign("");
  api._query.assign("max-keys=2&prefix=J");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("Content-Type", "gzip"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "UNSIGNED-PAYLOAD"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));

  StringSet include = defaultIncludeHeaders;
  StringSet exclude;
  commaSeparateString<StringSet>(exclude, "Content-Type,x-amz-content-sha256,x-amz-date");

  const char *bench[] = {
    /* Signed Headers */
    "content-type;host;x-amz-content-sha256;x-amz-date",
    /* Canonical Request sha256 */
    "ef3088997c69bc860e0bb36f97a8335f38863339e7fd01f2cd17b5391da575fb",
  };

  ValidateBenchCanonicalRequest(api, /*signePayload */ false, &now, bench, include, exclude);
}

/*
 * Mandatory headers Host, x-amz-* and Content-Type will must be included even if the user asked to exclude them.
 * Content-type should not be signed if missing from the HTTP request.
 */
TEST_CASE("S3AuthV4UtilParams: include missing content type", "[AWS][auth][utility]")
{
  time_t now = 1369353600; /* 5/24/2013 00:00:00 GMT */

  /* Define the HTTP request elements */
  MockTsInterface api;
  api._method.assign("GET");
  api._host.assign("examplebucket.s3.amazonaws.com");
  api._path.assign("");
  api._params.assign("");
  api._query.assign("max-keys=2&prefix=J");
  api._headers.insert(std::make_pair("Host", "examplebucket.s3.amazonaws.com"));
  api._headers.insert(std::make_pair("x-amz-content-sha256", "UNSIGNED-PAYLOAD"));
  api._headers.insert(std::make_pair("x-amz-date", "20130524T000000Z"));

  StringSet include = defaultIncludeHeaders;
  StringSet exclude;
  commaSeparateString<StringSet>(exclude, "Content-Type,x-amz-content-sha256,x-amz-date");

  const char *bench[] = {
    /* Signed Headers */
    "host;x-amz-content-sha256;x-amz-date",
    /* Canonical Request sha256 */
    "528623330c85041d6fb82795b6f8d5771825d3568b9f0bc1faa8a49e1f5f9cfc",
  };

  ValidateBenchCanonicalRequest(api, /*signePayload */ false, &now, bench, include, exclude);
}
