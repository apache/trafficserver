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

#include "URL.h"
#include <iostream.h>
#include <fstream.h>

URL *
create_url(const char *url_string)
{
  char buf[4096];
  int len    = 0;
  URL *url_p = new URL(url_string);
  URL &url   = *url_p;

  cout << "scheme        : " << url.getScheme() << endl;
  len      = url.getUserName(buf, sizeof(buf));
  buf[len] = '\0';
  cout << "user name     : " << buf << endl;
  cout << "UserNameExists: " << url.userNameExists() << endl;
  cout << "UserNameEmpty : " << url.userNameEmpty() << endl;

  len      = url.getPassword(buf, sizeof(buf));
  buf[len] = '\0';
  cout << "password      : " << buf << endl;
  cout << "PasswordExists: " << url.passwordExists() << endl;
  cout << "PasswordEmpty : " << url.passwordEmpty() << endl;

  len      = url.getHost(buf, sizeof(buf));
  buf[len] = '\0';
  cout << "host          : " << buf << endl;
  cout << "HostEmpty     : " << url.hostEmpty() << endl;

  cout << "port          : " << url.getPort() << endl;
  cout << "PortEmpty     : " << url.portEmpty() << endl;
  cout << "DefaultPort   : " << url.defaultPort() << endl;

  // get specifics for this scheme
  switch (url.getScheme()) {
  case URL_SCHEME_NONE:
  case URL_SCHEME_HTTP:
  case URL_SCHEME_HTTPS:
    len      = url.getHttpPath(buf, sizeof(buf));
    buf[len] = '\0';
    cout << "http path     : " << buf << endl;
    len      = url.getParams(buf, sizeof(buf));
    buf[len] = '\0';
    cout << "http params   : " << buf << endl;
    len      = url.getQuery(buf, sizeof(buf));
    buf[len] = '\0';
    cout << "http query    : " << buf << endl;
    len      = url.getFragment(buf, sizeof(buf));
    buf[len] = '\0';
    cout << "http fragment : " << buf << endl;
    break;

  default:
    break;
  }
  cout << "real length   : " << strlen(url_string) << endl;
  cout << "u-bound length: " << url.getUrlLengthUpperBound() << endl;
  cout << endl;

  int bl = url.dump(buf, sizeof(buf) - 1);
  cout << buf << endl << endl;
  cout << "bytes = " << bl << endl;

  return (url_p);
}

void
test_marshal(URL *url)
{
  char buf[8196];

  int bl = url->marshal(buf, sizeof(buf) - 1);
  cout << buf << endl << endl;
  cout << "bytes = " << bl << endl;

  // test unmarshal
  URL new_url;
  new_url.unmarshal(buf, bl);

  int bl2 = new_url.marshal(buf, sizeof(buf) - 1);
  cout << buf << endl << endl;
  cout << "bytes = " << bl << endl;

  return;
}

main()
{
  create_url("www.microsoft.com/isapi/redir.dll?TARGET=%2Foffice%2Fmigration%2F&nonie3home&homepage&&&&headline1&1006");
}
