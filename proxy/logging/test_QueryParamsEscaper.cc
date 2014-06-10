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

/*
  QueryParamsEscaper(const std::vector<std::string> &params_to_hide)
    : _targets(params_to_hide), _num_targets(_targets.size()) { };
  bool is_escaping_required_for_url(const char *immutable_url, size_t url_len);

  // arg should point to mutable version of url tested previously via
  // is_escaping_required_for_url()
  void escape_url(char *mutable_url);
*/

#include "QueryParamsEscaper.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

using std::list;
using std::string;
using std::vector;

int
main(int argc, const char *argv[])
{
  {
    vector<string> params_to_hide;
    QueryParamsEscaper escaper(params_to_hide); // no params
    char url[] = { "http://server/path?login=blah&password=blah123" };
    assert(!escaper.is_escaping_required_for_url(url, sizeof(url) - 1));
    assert(!escaper.is_escaping_required_for_url(url, 0));
    assert(!escaper.is_escaping_required_for_url(0, 0));
  }

  {
    vector<string> params_to_hide;
    params_to_hide.push_back("login");
    QueryParamsEscaper escaper(params_to_hide);
    char url1[] = { "http://server/path?login=blah&password=blah123" };
    assert(escaper.is_escaping_required_for_url(url1, sizeof(url1) - 1));
    escaper.escape_url(url1);
    assert(strcmp(url1, "http://server/path?login=****&password=blah123") == 0);
    assert(!escaper.is_escaping_required_for_url(url1, 0));
    assert(!escaper.is_escaping_required_for_url(0, 0));
    escaper.reset();
    char url2[] = { "http://server/path?blah=login&password=blah123" }; // login is param value, not name
    assert(!escaper.is_escaping_required_for_url(url2, sizeof(url2) - 1));
    escaper.reset();
    char url3[] = { "http://server/login=blahpath?foo=bar" }; // login= is not a param, but part of path
    assert(!escaper.is_escaping_required_for_url(url3, sizeof(url3) - 1));
    escaper.reset();
    char url4[] = { "http://login=blahserver/path?foo=bar" }; // login= is not a param, but part of server name
    assert(!escaper.is_escaping_required_for_url(url4, sizeof(url4) - 1));
    escaper.reset();
    char url5[] = { "http://server/path?foo=bar&login=login1&login=login2&foo2=bar2" }; // multiple occurences
    assert(escaper.is_escaping_required_for_url(url5, sizeof(url5) - 1));
    escaper.escape_url(url5);
    assert(strcmp(url5, "http://server/path?foo=bar&login=******&login=******&foo2=bar2") == 0);
  }

  {
    vector<string> params_to_hide;
    params_to_hide.push_back("login");
    params_to_hide.push_back("password"); // multiple params to hide
    QueryParamsEscaper escaper(params_to_hide);
    char url1[] = { "http://server/path?login=blah&password=blah123" };
    assert(escaper.is_escaping_required_for_url(url1, sizeof(url1) - 1));
    escaper.escape_url(url1);
    assert(strcmp(url1, "http://server/path?login=****&password=*******") == 0);
    assert(!escaper.is_escaping_required_for_url(url1, 0));
    assert(!escaper.is_escaping_required_for_url(0, 0));
    escaper.reset();
    char url2[] = { "http://server/path?login=blah&password=blah123&login=blah&password=blah123" };
    assert(escaper.is_escaping_required_for_url(url2, sizeof(url2) - 1));
    escaper.escape_url(url2);
    assert(strcmp(url2, "http://server/path?login=****&password=*******&login=****&password=*******") == 0);
    escaper.reset();
    char url3[] = { "http://server/path?login=blah&password=&login=blah&password=" }; // empty values
    assert(escaper.is_escaping_required_for_url(url3, sizeof(url3) - 1));
    escaper.escape_url(url3);
    assert(strcmp(url3, "http://server/path?login=****&password=&login=****&password=") == 0);
    escaper.reset();

    // sub string test
    char url4[] = { "http://server/path?user_login=blah&new_password=123&login=test&old_password=456" };
    assert(escaper.is_escaping_required_for_url(url4, sizeof(url4) - 1));
    escaper.escape_url(url4);
    assert(strcmp(url4, "http://server/path?user_login=****&new_password=***&login=****&old_password=***") == 0);

    escaper.reset();
    char url5[] = { "http://127.0.0.1:12175/uas/js/userspace?v=0.0.2000-RC1.24082-1337&apiKey=consumer_key_7&" };
    assert(!escaper.is_escaping_required_for_url(url5, sizeof(url5) - 1));

    escaper.reset();
    char url6[] = { "http://127.0.0.1:12175/uas/js/userspace?v=0.0.2000-RC1.24082-1337&password=consumer_key_7&" };
    assert(escaper.is_escaping_required_for_url(url6, sizeof(url6) - 1));
    escaper.escape_url(url6);
    assert(
      strcmp(url6,
             "http://127.0.0.1:12175/uas/js/userspace?v=0.0.2000-RC1.24082-1337&password=**************&") == 0);

    escaper.reset();
    char url7[] = { "http://127.0.0.1:12175/uas/js/userspace?v=0.0.2000-RC1.24082-1337&apiKey=consumer_key_7#" };
    assert(!escaper.is_escaping_required_for_url(url7, sizeof(url7) - 1));

    escaper.reset();
    char url8[] = { "http://127.0.0.1:12175/uas/js/userspace?v=0.0.2000-RC1.24082-1337&password=" };
    assert(!escaper.is_escaping_required_for_url(url8, sizeof(url8) - 1));

    escaper.reset();
    char url9[] = { "http://127.0.0.1:12175/uas/js/js?login=0.0.2000-RC1.24082-1337&password=" };
    assert(escaper.is_escaping_required_for_url(url9, sizeof(url9) - 1));
    escaper.escape_url(url9);
    assert(strcmp(url9, "http://127.0.0.1:12175/uas/js/js?login=***********************&password=") == 0);

    escaper.reset();
    char url10[] = { "http://127.0.0.1/path?=b&=c&=d" };
    assert(!escaper.is_escaping_required_for_url(url10, sizeof(url10) - 1));

    escaper.reset();
    char url11[] = { ".?a=b&c=d" };
    url11[0] = '\0';
    assert(!escaper.is_escaping_required_for_url(url11, sizeof(url11) - 1));
  }
  return 0;
}
