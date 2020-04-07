/** @file

   Catch-based unit tests for URL

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
 */

#include <cstdio>

#include "catch.hpp"

#include "URL.h"

TEST_CASE("ValidateURL", "[proxy][validurl]")
{
  static const struct {
    const char *const text;
    bool valid;
  } http_validate_hdr_field_test_case[] = {{"yahoo", true},
                                           {"yahoo.com", true},
                                           {"yahoo.wow.com", true},
                                           {"yahoo.wow.much.amaze.com", true},
                                           {"209.131.52.50", true},
                                           {"192.168.0.1", true},
                                           {"localhost", true},
                                           {"3ffe:1900:4545:3:200:f8ff:fe21:67cf", true},
                                           {"fe80:0:0:0:200:f8ff:fe21:67cf", true},
                                           {"fe80::200:f8ff:fe21:67cf", true},
                                           {"<svg onload=alert(1)>", false}, // Sample host header XSS attack
                                           {"jlads;f8-9349*(D&F*D(234jD*(FSD*(VKLJ#(*$@()#$)))))", false},
                                           {"\"\t\n", false},
                                           {"!@#$%^ &*(*&^%$#@#$%^&*(*&^%$#))", false},
                                           {":):(:O!!!!!!", false}};
  for (auto i : http_validate_hdr_field_test_case) {
    const char *const txt = i.text;
    if (validate_host_name({txt}) != i.valid) {
      std::printf("Validation of FQDN (host) header: \"%s\", expected %s, but not\n", txt, (i.valid ? "true" : "false"));
      CHECK(false);
    }
  }
}

namespace UrlImpl
{
bool url_is_strictly_compliant(const char *start, const char *end);
}
using namespace UrlImpl;

TEST_CASE("ParseRulesStrictURI", "[proxy][parseuri]")
{
  const struct {
    const char *const uri;
    bool valid;
  } http_strict_uri_parsing_test_case[] = {{"/home", true},
                                           {"/path/data?key=value#id", true},
                                           {"/ABCDEFGHIJKLMNOPQRSTUVWXYZ", true},
                                           {"/abcdefghijklmnopqrstuvwxyz", true},
                                           {"/0123456789", true},
                                           {":/?#[]@", true},
                                           {"!$&'()*+,;=", true},
                                           {"-._~", true},
                                           {"%", true},
                                           {"\n", false},
                                           {"\"", false},
                                           {"<", false},
                                           {">", false},
                                           {"\\", false},
                                           {"^", false},
                                           {"`", false},
                                           {"{", false},
                                           {"|", false},
                                           {"}", false},
                                           {"é", false}};

  for (auto i : http_strict_uri_parsing_test_case) {
    const char *const uri = i.uri;
    if (url_is_strictly_compliant(uri, uri + strlen(uri)) != i.valid) {
      std::printf("Strictly parse URI: \"%s\", expected %s, but not\n", uri, (i.valid ? "true" : "false"));
      CHECK(false);
    }
  }
}
