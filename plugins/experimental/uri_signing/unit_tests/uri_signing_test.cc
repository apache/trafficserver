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

/*
 * These are misc unit tests for uri signing
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

extern "C" {
#include <jansson.h>
#include <cjose/cjose.h>
#include "../jwt.h"
#include "../normalize.h"
#include "../parse.h"
}

bool
jwt_parsing_helper(const char *jwt_string)
{
  fprintf(stderr, "Parsing JWT from string: %s\n", jwt_string);
  bool resp;
  json_error_t jerr = {};
  size_t pt_ct      = strlen(jwt_string);
  struct jwt *jwt   = parse_jwt(json_loadb(jwt_string, pt_ct, 0, &jerr));

  if (jwt) {
    resp = jwt_validate(jwt);
  } else {
    resp = false;
  }

  jwt_delete(jwt);
  return resp;
}

bool
normalize_uri_helper(const char *uri, const char *expected_normal)
{
  size_t uri_ct = strlen(uri);
  int buff_size = uri_ct + 2;
  int err;
  char uri_normal[buff_size];
  memset(uri_normal, 0, buff_size);

  err = normalize_uri(uri, uri_ct, uri_normal, buff_size);

  if (err) {
    return false;
  }

  if (expected_normal && strcmp(expected_normal, uri_normal) == 0) {
    return true;
  }

  return false;
}

bool
remove_dot_helper(const char *path, const char *expected_path)
{
  fprintf(stderr, "Removing Dot Segments from Path: %s\n", path);
  size_t path_ct = strlen(path);
  path_ct++;
  int new_ct;
  char path_buffer[path_ct];
  memset(path_buffer, 0, path_ct);

  new_ct = remove_dot_segments(path, path_ct, path_buffer, path_ct);

  if (new_ct < 0) {
    return false;
  } else if (strcmp(expected_path, path_buffer) == 0) {
    return true;
  } else {
    return false;
  }
}

bool
jws_parsing_helper(const char *uri, const char *paramName, const char *expected_strip)
{
  bool resp;
  size_t uri_ct   = strlen(uri);
  size_t strip_ct = 0;
  char uri_strip[uri_ct + 1];
  memset(uri_strip, 0, sizeof uri_strip);
  cjose_jws_t *jws = get_jws_from_uri(uri, uri_ct, paramName, uri_strip, uri_ct, &strip_ct);
  if (jws) {
    resp = true;
    if (strcmp(uri_strip, expected_strip) != 0) {
      cjose_jws_release(jws);
      resp = false;
    }
  } else {
    resp = false;
  }
  cjose_jws_release(jws);
  return resp;
}

TEST_CASE("1", "[JWSParsingTest]")
{
  INFO("TEST 1, Test JWT Parsing From Token Strings");

  SECTION("Standard JWT Parsing")
  {
    REQUIRE(jwt_parsing_helper("{\"cdniets\":30,\"cdnistt\":1,\"exp\":7284188499,\"iss\":\"Content Access "
                               "Manager\",\"cdniuc\":\"uri-regex:http://foobar.local/testDir/*\"}"));
  }

  SECTION("JWT Parsing With Unknown Claim")
  {
    REQUIRE(jwt_parsing_helper("{\"cdniets\":30,\"cdnistt\":1,\"exp\":7284188499,\"iss\":\"Content Access "
                               "Manager\",\"cdniuc\":\"uri-regex:http://foobar.local/testDir/"
                               "*\",\"jamesBond\":\"Something,Something_else\"}"));
  }

  SECTION("JWT Parsing with unsupported crit claim passed")
  {
    REQUIRE(!jwt_parsing_helper("{\"cdniets\":30,\"cdnistt\":1,\"exp\":7284188499,\"iss\":\"Content Access "
                                "Manager\",\"cdniuc\":\"uri-regex:http://foobar.local/testDir/"
                                "*\",\"cdnicrit\":\"Something,Something_else\"}"));
  }

  SECTION("JWT Parsing with empty exp claim")
  {
    REQUIRE(jwt_parsing_helper("{\"cdniets\":30,\"cdnistt\":1,\"iss\":\"Content Access "
                               "Manager\",\"cdniuc\":\"uri-regex:http://foobar.local/testDir/*\"}"));
  }

  SECTION("JWT Parsing with unsupported cdniip claim")
  {
    REQUIRE(!jwt_parsing_helper("{\"cdniets\":30,\"cdnistt\":1,\"cdniip\":\"123.123.123.123\",\"iss\":\"Content Access "
                                "Manager\",\"cdniuc\":\"uri-regex:http://foobar.local/testDir/*\"}"));
  }

  SECTION("JWT Parsing with unsupported value for cdnistd claim")
  {
    REQUIRE(!jwt_parsing_helper("{\"cdniets\":30,\"cdnistt\":1,\"cdnistd\":4,\"iss\":\"Content Access "
                                "Manager\",\"cdniuc\":\"uri-regex:http://foobar.local/testDir/*\"}"));
  }
  fprintf(stderr, "\n");
}

TEST_CASE("2", "[JWSFromURLTest]")
{
  INFO("TEST 2, Test JWT Parsing and Stripping From URLs");

  SECTION("Token at end of URI")
  {
    REQUIRE(jws_parsing_helper(
      "www.foo.com/hellothere/"
      "URISigningPackage=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
      "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ.SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c",
      "URISigningPackage", "www.foo.com/hellothere"));
  }

  SECTION("No Token in URL")
  {
    REQUIRE(!jws_parsing_helper(
      "www.foo.com/hellothere/"
      "URISigningPackag=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
      "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ.SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c",
      "URISigningPackage", NULL));
  }

  SECTION("Token in middle of the URL")
  {
    REQUIRE(jws_parsing_helper("www.foo.com/hellothere/"
                               "URISigningPackage=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                               "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
                               "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c/Something/Else",
                               "URISigningPackage", "www.foo.com/hellothere/Something/Else"));
  }

  SECTION("Token at the start of the URL")
  {
    REQUIRE(jws_parsing_helper(":URISigningPackage=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                               "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
                               "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c/www.foo.com/hellothere/Something/Else",
                               "URISigningPackage", "/www.foo.com/hellothere/Something/Else"));
  }

  SECTION("Pass empty path parameter at end")
  {
    REQUIRE(!jws_parsing_helper("www.foobar.com/hellothere/URISigningPackage=", "URISigningPackage", NULL));
  }

  SECTION("Pass empty path parameter in the middle of URL")
  {
    REQUIRE(!jws_parsing_helper("www.foobar.com/hellothere/URISigningPackage=/Something/Else", "URISigningPackage", NULL));
  }

  SECTION("Partial package name in previous path parameter")
  {
    REQUIRE(jws_parsing_helper("www.foobar.com/URISig/"
                               "URISigningPackage=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                               "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
                               "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c/Something/Else",
                               "URISigningPackage", "www.foobar.com/URISig/Something/Else"));
  }

  SECTION("Package comes directly after two reserved characters")
  {
    REQUIRE(jws_parsing_helper("www.foobar.com/"
                               ":URISigningPackage=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                               "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
                               "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c/Something/Else",
                               "URISigningPackage", "www.foobar.com//Something/Else"));
  }

  SECTION("Package comes directly after string of reserved characters")
  {
    REQUIRE(jws_parsing_helper("www.foobar.com/?!/"
                               ":URISigningPackage=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                               "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
                               "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c/Something/Else",
                               "URISigningPackage", "www.foobar.com/?!//Something/Else"));
  }

  SECTION("Invalid token passed before a valid token")
  {
    REQUIRE(!jws_parsing_helper("www.foobar.com/URISigningPackage=/"
                                "URISigningPackage=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                                "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
                                "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c/Something/Else",
                                "URISigningPackage", NULL));
  }

  SECTION("Empty string as URL") { REQUIRE(!jws_parsing_helper("", "URISigningPackage", NULL)); }

  SECTION("Empty package name to parser")
  {
    REQUIRE(!jws_parsing_helper(
      "www.foobar.com/"
      "URISigningPackage=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
      "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ.SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c",
      "", NULL));
  }

  SECTION("Custom package name with a reserved character - at the end of the URI")
  {
    REQUIRE(jws_parsing_helper(
      "www.foobar.com/CustomPackage/"
      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
      "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c",
      "CustomPackage/", "www.foobar.com"));
  }

  SECTION("Custom package name with a reserved character - in the middle of the URI")
  {
    REQUIRE(jws_parsing_helper(
      "www.foobar.com/CustomPackage/"
      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
      "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c/Something/Else",
      "CustomPackage/", "www.foobar.com/Something/Else"));
  }

  SECTION("URI signing package passed as the only a query parameter")
  {
    REQUIRE(jws_parsing_helper(
      "www.foobar.com/Something/"
      "Here?URISigningPackage=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
      "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ.SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c",
      "URISigningPackage", "www.foobar.com/Something/Here"));
  }

  SECTION("URI signing package passed as first of many query parameters")
  {
    REQUIRE(jws_parsing_helper("www.foobar.com/Something/"
                               "Here?URISigningPackage=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                               "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
                               "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c&query3=foobar&query1=foo&query2=bar",
                               "URISigningPackage", "www.foobar.com/Something/Here?query3=foobar&query1=foo&query2=bar"));
  }

  SECTION("URI signing package passed as one of many query parameters - passed in middle")
  {
    REQUIRE(jws_parsing_helper("www.foobar.com/Something/"
                               "Here?query1=foo&query2=bar&URISigningPackage=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                               "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
                               "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c&query3=foobar",
                               "URISigningPackage", "www.foobar.com/Something/Here?query1=foo&query2=bar&query3=foobar"));
  }

  SECTION("URI signing package passed as last of many query parameters")
  {
    REQUIRE(jws_parsing_helper("www.foobar.com/Something/"
                               "Here?query1=foo&query2=bar&URISigningPackage=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                               "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
                               "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c",
                               "URISigningPackage", "www.foobar.com/Something/Here?query1=foo&query2=bar"));
  }
}

TEST_CASE("3", "[RemoveDotSegmentsTest]")
{
  INFO("TEST 3, Test Removal of Dot Segments From Paths");

  SECTION("../bar test") { REQUIRE(remove_dot_helper("../bar", "bar")); }

  SECTION("./bar test") { REQUIRE(remove_dot_helper("./bar", "bar")); }

  SECTION(".././bar test") { REQUIRE(remove_dot_helper(".././bar", "bar")); }

  SECTION("./../bar test") { REQUIRE(remove_dot_helper("./../bar", "bar")); }

  SECTION("/foo/./bar test") { REQUIRE(remove_dot_helper("/foo/./bar", "/foo/bar")); }

  SECTION("/bar/./ test") { REQUIRE(remove_dot_helper("/bar/./", "/bar/")); }

  SECTION("/. test") { REQUIRE(remove_dot_helper("/.", "/")); }

  SECTION("/bar/. test") { REQUIRE(remove_dot_helper("/bar/.", "/bar/")); }

  SECTION("/foo/../bar test") { REQUIRE(remove_dot_helper("/foo/../bar", "/bar")); }

  SECTION("/bar/../ test") { REQUIRE(remove_dot_helper("/bar/../", "/")); }

  SECTION("/.. test") { REQUIRE(remove_dot_helper("/..", "/")); }

  SECTION("/bar/.. test") { REQUIRE(remove_dot_helper("/bar/..", "/")); }

  SECTION("/foo/bar/.. test") { REQUIRE(remove_dot_helper("/foo/bar/..", "/foo/")); }

  SECTION("Single . test") { REQUIRE(remove_dot_helper(".", "")); }

  SECTION("Single .. test") { REQUIRE(remove_dot_helper("..", "")); }

  SECTION("Test foo/bar/.. test") { REQUIRE(remove_dot_helper("foo/bar/..", "foo/")); }

  SECTION("Test Empty Path Segment") { REQUIRE(remove_dot_helper("", "")); }

  SECTION("Test mixed operations") { REQUIRE(remove_dot_helper("/foo/bar/././something/../foobar", "/foo/bar/foobar")); }
  fprintf(stderr, "\n");
}

TEST_CASE("4", "[NormalizeTest]")
{
  INFO("TEST 4, Test Normalization of URIs");

  SECTION("Testing passing too small of a URI to normalize") { REQUIRE(!normalize_uri_helper("ht", NULL)); }

  SECTION("Testing passing non http/https protocol") { REQUIRE(!normalize_uri_helper("ht:", NULL)); }

  SECTION("Passing a uri with half encoded value at end") { REQUIRE(!normalize_uri_helper("http://www.foobar.co%4", NULL)); }

  SECTION("Passing a uri with half encoded value in the middle")
  {
    REQUIRE(!normalize_uri_helper("http://www.foobar.co%4psomethin/Path", NULL));
  }

  SECTION("Passing a uri with an empty path parameter")
  {
    REQUIRE(normalize_uri_helper("http://www.foobar.com", "http://www.foobar.com/"));
  }

  SECTION("Passing a uri with an empty path parameter and additional query params")
  {
    REQUIRE(normalize_uri_helper("http://www.foobar.com?query1=foo&query2=bar", "http://www.foobar.com/?query1=foo&query2=bar"));
  }

  SECTION("Empty path parameter with port")
  {
    REQUIRE(normalize_uri_helper("http://www.foobar.com:9301?query1=foo&query2=bar",
                                 "http://www.foobar.com:9301/?query1=foo&query2=bar"));
  }

  SECTION("Passing a uri with a username and password")
  {
    REQUIRE(normalize_uri_helper("http://foo%40:PaSsword@www.Foo%42ar.coM:80/", "http://foo%40:PaSsword@www.foobar.com/"));
  }

  SECTION("Testing Removal of standard http Port")
  {
    REQUIRE(normalize_uri_helper("http://foobar.com:80/Something/Here", "http://foobar.com/Something/Here"));
  }

  SECTION("Testing Removal of standard https Port")
  {
    REQUIRE(normalize_uri_helper("https://foobar.com:443/Something/Here", "https://foobar.com/Something/Here"));
  }

  SECTION("Testing passing of non-standard http Port")
  {
    REQUIRE(normalize_uri_helper("http://foobar.com:443/Something/Here", "http://foobar.com:443/Something/Here"));
  }

  SECTION("Testing passing of non-standard https Port")
  {
    REQUIRE(normalize_uri_helper("https://foobar.com:80/Something/Here", "https://foobar.com:80/Something/Here"));
  }

  SECTION("Testing the removal of . and .. in the path ")
  {
    REQUIRE(
      normalize_uri_helper("https://foobar.com:80/Something/Here/././foobar/../foo", "https://foobar.com:80/Something/Here/foo"));
  }

  SECTION("Testing . and .. segments in non path components")
  {
    REQUIRE(normalize_uri_helper("https://foobar.com:80/Something/Here?query1=/././foo/../bar",
                                 "https://foobar.com:80/Something/Here?query1=/././foo/../bar"));
  }

  SECTION("Testing standard decdoing of multiple characters")
  {
    REQUIRE(normalize_uri_helper("https://kelloggs%54ester.com/%53omething/Here", "https://kelloggstester.com/Something/Here"));
  }

  SECTION("Testing passing encoded reserved characters")
  {
    REQUIRE(
      normalize_uri_helper("https://kelloggs%54ester.com/%53omething/Here%3f", "https://kelloggstester.com/Something/Here%3F"));
  }

  SECTION("Mixed Bag Test case")
  {
    REQUIRE(normalize_uri_helper("https://foo:something@kellogs%54ester.com:443/%53omething/.././here",
                                 "https://foo:something@kellogstester.com/here"));
  }

  SECTION("Testing empty hostname with userinfon") { REQUIRE(!normalize_uri_helper("https://foo:something@", NULL)); }

  SECTION("Testing empty uri after http://") { REQUIRE(!normalize_uri_helper("http://", NULL)); }

  SECTION("Testing http:///////") { REQUIRE(!normalize_uri_helper("http:///////", NULL)); }

  SECTION("Testing empty uri after http://?/") { REQUIRE(!normalize_uri_helper("http://?/", NULL)); }
  fprintf(stderr, "\n");
}
