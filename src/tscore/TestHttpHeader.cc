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

/*************************** -*- Mod: C++ -*- ******************************

   TestHeaderTokenizer.cc --
   Created On      : Mon Jan 20 11:48:10 1997


 ****************************************************************************/
#include "HttpHeaderTokenizer.h"
#include "tscore/ink_assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <memory.h>
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <stdarg.h>
#include <libgen.h>
#include <limits.h>
#include <fcntl.h>
#include <time.h>
#include <sys/param.h>
#include <sys/stat.h>

static void
add_field(HttpHeader *h, const char *name, const char *value)
{
  h->m_header_fields.set_raw_header_field(name, value);
}

/////////////////////////////////////////////////////////////
//
//  test_add_fields()
//
/////////////////////////////////////////////////////////////
static void
test_add_fields(HttpHeader *h)
{
  char long_accept_header[2048];
  memset(long_accept_header, 'B', sizeof(long_accept_header));
  long_accept_header[sizeof(long_accept_header) - 1] = '\0';
  add_field(h, "Accept", long_accept_header);
  add_field(h, "Accept", "image/gif");
  add_field(h, "Accept", "image/x-xbitmap");
  add_field(h, "Accept", "image/jpeg");
  add_field(h, "Accept", "image/pjpeg");
  add_field(h, "Accept", "*/*");
  add_field(h, "Set-Cookie", "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  add_field(h, "Set-Cookie", "1234567890987654321");
  return;
}

/////////////////////////////////////////////////////////////
//
//  test_hacked_http_header_field()
//
/////////////////////////////////////////////////////////////
static void
test_hacked_http_header_field()
{
  HttpHackedMultiValueRawHeaderField f;

#define ADD_FIELD(s) f.add(s, sizeof(s))
  ADD_FIELD("image/gif");
  ADD_FIELD("image/x-xbitmap");
  ADD_FIELD("image/jpeg");
  ADD_FIELD("image/pjpeg");
  ADD_FIELD("*/*");
#undef ADD_FIELD

  int count = f.get_count();
  cout << "count = " << count << endl;
  int length = 0;
  for (int i = 0; i < count; i++) {
    cout << "Accept: " << f.get(i, &length) << endl;
    cout << "(length = " << length << ")" << endl;
  }
  return;
}

/////////////////////////////////////////////////////////////
//
//  test_url_parse()
//
/////////////////////////////////////////////////////////////
void
test_url_parse(const char *url_string)
{
  URL url(url_string, strlen(url_string));
  char buf[4096];

  url.dump(buf, sizeof(buf));

  cout << buf << endl;

  return;
}

/////////////////////////////////////////////////////////////
//
//  test_url()
//
/////////////////////////////////////////////////////////////
void
test_url()
{
  test_url_parse("http://charm.example.com  ");
  test_url_parse("http://"
                 "webchat16.wbs.net:6666?private=herbalessences&color=4&volume=0&tagline=&picture=&home_page=hi@there.&ignore="
                 "edheldinruth+taz0069+speezman&back=&Room=Hot_Tub&handle=cagou67&mu="
                 "893e159ef7fe0ddb022c655cc1c30abd33d4ae6d90d22f8a&last_read_para=&npo=&fsection=input&chatmode=push&reqtype=input&"
                 "InputText=Sweetie%2C+do+you+have+time+to+go+to+a+private+room..if+not+I%27m+just+going+to+have+to+change+to+"
                 "normal+mode...let+me+know%3F%3F/");

  return;
}

/////////////////////////////////////////////////////////////
//
//  test_header_tokenizer()
//
// message_type HTTP_MESSAGE_TYPE_REQUEST or HTTP_MESSAGE_TYPE_RESPONSE
/////////////////////////////////////////////////////////////
void
test_header_tokenizer_run(const char *buf, HttpMessageType_t message_type)
{
  HttpHeaderTokenizer tokenizer;
  HttpHeader header;
  int bytes_used;

  tokenizer.start(&header, message_type, false);

  tokenizer.run(buf, strlen(buf), true, &bytes_used);

  cout << header << endl;

  return;
}

void
test_header_tokenizer()
{
  test_header_tokenizer_run("GET "
                            "http://"
                            "webchat16.wbs.net:6666?private=herbalessences&color=4&volume=0&tagline=&picture=&home_page=hi@there.&"
                            "ignore=edheldinruth+taz0069+speezman&back=&Room=Hot_Tub&handle=cagou67&mu="
                            "893e159ef7fe0ddb022c655cc1c30abd33d4ae6d90d22f8a&last_read_para=&npo=&fsection=input&chatmode=push&"
                            "reqtype=input&InputText=Sweetie%2C+do+you+have+time+to+go+to+a+private+room..if+not+I%27m+just+going+"
                            "to+have+to+change+to+normal+mode...let+me+know%3F%3F/ HTTP/1.0\r\n",
                            HTTP_MESSAGE_TYPE_REQUEST);

  return;
}

/////////////////////////////////////////////////////////////
//
//  TestHttpHeader()
//
/////////////////////////////////////////////////////////////
void
TestHttpHeader()
{
  HttpHeader h;
  h.m_message_type = HTTP_MESSAGE_TYPE_REQUEST;
  h.m_method       = HTTP_METHOD_GET;
  h.m_version      = HttpVersion(1, 0);

  test_add_fields(&h);

  cout << h << endl;

  cout << "concatenated accept" << endl;
  char accept_buf[4000];
  h.m_header_fields.get_comma_separated_accept_value(accept_buf, sizeof(accept_buf));
  cout << accept_buf << endl;

  int l;
  cout << "first accept" << endl;
  cout << h.m_header_fields.m_accept.get(0, &l) << endl;

  char buf[4096];
  int marshal_length = h.marshal(buf, sizeof(buf));

  HttpHeader h1;
  h1.unmarshal(buf, marshal_length);

  cout << "unmarshalled: " << endl;
  cout << h1 << endl;

  cout << "test url parser:" << endl;
  test_url();

  cout << "test_header_tokenizer:" << endl;
  test_header_tokenizer();

  return;
}
