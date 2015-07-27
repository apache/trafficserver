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

#include "ts/ink_string.h"
#include "HttpTransact.h"
#include "HttpTransactHeaders.h"

#define MAX_FIELD_VALUE_SIZE 512

char request1[] = "GET http://people.netscape.com/jwz/hacks-1.gif HTTP/1.0\r\n"
                  "If-Modified-Since: Wednesday, 26-Feb-97 06:58:17 GMT; length=842\r\n"
                  "Referer: http://people.netscape.com/jwz/index.html\r\n"
                  "Proxy-Connection: Referer, User-Agent\r\n"
                  "Vary: If-Modified-Since, Host, Accept, Proxy-Connection, Crap\r\n"
                  "User-Agent:  Mozilla/3.01 (X11; I; Linux 2.0.28 i586)\r\n"
                  "Crappy-Field: value1-on-line-1, value2-on-line-1\r\n"
                  "Crappy-Field: value-on-line-2\r\n"
                  "Blowme: Crapshoot\r\n"
                  "Pragma: no-cache\r\n"
                  "Host: people.netscape.com\r\n"
                  "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, */*\r\n\r\n";

char response1[] = "HTTP/1.0 200 !132d63600000000000000200 OK\r\n"
                   "Server: WN/1.14.6\r\n"
                   "Date: Tue, 26 Aug 1997 21:51:23 GMT\r\n"
                   "Last-modified: Fri, 25 Jul 1997 15:07:05 GMT\r\n"
                   "Content-type: text/html\r\n"
                   "Content-length: 3831\r\n"
                   "Accept-Range: bytes, lines\r\n"
                   "Title: General Casualty - Home Page\r\n";

char response2[] = "HTTP/1.0 304 Not Modified\r\n"
                   "Date: Wed, 30 Jul 1997 22:31:20 GMT\r\n"
                   "Via: 1.0 trafficserver.apache.org (Traffic-Server/1.0b [ONM])\r\n"
                   "Server: Apache/1.1.1\r\n\r\n";

void print_header(HttpHeader *header);

void
readin_header(HttpHeader *new_header, char *req_buffer, int length)
{
  int bytes_used = 0;

  const char *tmp = req_buffer;
  while (*tmp) {
    if (new_header->parse(tmp, length, &bytes_used, false, HTTP_MESSAGE_TYPE_REQUEST) != 0)
      break;
    tmp += length;
  }
}

// void print_header(HttpHeader *header)
// {
//     bool done = false;
//     int bytes_used = 0;
//     char dumpbuf;

//     header->dump_setup ();
//     while (!done) {
//      header->dump (&dumpbuf, 1, &bytes_used, &done);
//      putc (dumpbuf, stdout);
//     }
// }

inline void
make_comma_separated_header_field_value(HttpHeader *header, const char *fieldname, char *full_str)
{
  MIMEHeaderFieldValue *hfv;
  const char *str;

  hfv = header->mime().get(fieldname, strlen(fieldname));

  /* This if is needed to put in the commas correctly */
  if (hfv) {
    str = hfv->get_raw();
    ink_strlcat(full_str, str, MAX_FIELD_VALUE_SIZE);
    hfv = hfv->next();
  }
  while (hfv) {
    str = hfv->get_raw();
    ink_strlcat(full_str, ", ", MAX_FIELD_VALUE_SIZE);
    ink_strlcat(full_str, str, MAX_FIELD_VALUE_SIZE);
    hfv = hfv->next();
  }
}

void
test_headers()
{
  HttpHeader *req;
  char str1[MAX_FIELD_VALUE_SIZE];
  char str2[MAX_FIELD_VALUE_SIZE];
  char str3[MAX_FIELD_VALUE_SIZE];

  req = new (HttpHeaderAllocator.alloc()) HttpHeader();

  readin_header(req, request1, sizeof(request1));
  printf("[test_headers] This is the header that was read in:\n");
  print_header(req);

  printf("[test_headers] Ok, let us see what the Proxy-Connection field is ...\n");
  printf("[test_headers] the value of Blowme is %s\n", req->mime().get_raw("Blowme", strlen("Blowme")));
  //     make_comma_separated_header_field_value(req, "Proxy-Connection", str1);
  //     printf("[test_headers] Proxy-Connection is %s\n", str1);

  //     printf("[test_headers] Let us try that with crappy-field...\n");
  //     make_comma_separated_header_field_value(req, "Crappy-Field", str2);
  //     printf("[test_headers] Crappy-Field is : %s\n", str2);

  //     printf("[test_headers] Let us try that with Vary, now...\n");
  //     make_comma_separated_header_field_value(req, "Vary", str3);
  //     printf("[test_headers] Vary is %s\n", str3);
}
