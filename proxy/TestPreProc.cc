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
#include "TestPreProc.h"
#include "IOBufferPool.h"
#include "IOBuffer.h"
#include "HttpPreProc.h"
#include "HttpMessage.h"
#include "HttpPreProcMessageManager.h"
#include "ts/RawHashTable.h"
#include "ts/ink_time.h" /* ink_time_wall_seconds() */
#include <string.h>
#include <iostream.h>

/* local functions prototypes */
void dumpMessage(const HttpMessage &msg);
void testPreProc();

/* some global requests */
char *request1  = "GET http://trafficserver.apache.org HTTP/1.1\r\n\
Accept: text/*, text/html, text/html; level=1\r\n\
Accept-Charset: iso-8859-5, unicode-1-1;q=0.8\r\n\r\n";
char *response1 = "HTTP/1.1 200\r\n\r\n";
//////////////////////////////////////////////////////////////////////////////
// RequestInput().
//////////////////////////////////////////////////////////////////////////////
RequestInput::RequestInput(const char *str, IOBuffer *cb) : m_sp(0), m_len(0), m_cb(cb)
{
  m_len = strlen(str);
  m_sp  = request1;

  return;
}

//////////////////////////////////////////////////////////////////////////////
// ~RequestInput()
//////////////////////////////////////////////////////////////////////////////
RequestInput::~RequestInput()
{
  return;
}

//////////////////////////////////////////////////////////////////////////////
// run()
//////////////////////////////////////////////////////////////////////////////
void
RequestInput::run()
{
  unsigned maxBytes = 0;

  char *buff          = m_cb->getWrite(&maxBytes);
  unsigned writeBytes = (m_len < maxBytes) ? m_len : maxBytes;

  writeBytes = ink_strlcpy(buff, m_sp, maxBytes);
  m_cb->wrote(writeBytes);

  m_len -= writeBytes;
  m_sp += writeBytes;

  return;
}

//////////////////////////////////////////////////////////////////////////////
// dumpMessage()
//////////////////////////////////////////////////////////////////////////////
void
dumpMessage(const HttpMessage &msg)
{
  if (msg.isResponse()) {
    cout << "Http response" << endl;
  }
  if (msg.isRequest()) {
    cout << "Http request" << endl;
  }

  cout << "Major version: " << msg.getMajorVersion() << endl;
  cout << "Minor version: " << msg.getMinorVersion() << endl;
  cout << "Method       : ";
  switch (msg.getMethod()) {
  case HttpMessage::METHOD_NONE:
    cout << "NONE" << endl;
  case HttpMessage::METHOD_OPTIONS:
    cout << "OPTIONS" << endl;
    break;
  case HttpMessage::METHOD_GET:
    cout << "GET" << endl;
    break;
  case HttpMessage::METHOD_HEAD:
    cout << "HEAD" << endl;
    break;
  case HttpMessage::METHOD_POST:
    cout << "POST" << endl;
    break;
  case HttpMessage::METHOD_PUT:
    cout << "PUT" << endl;
    break;
  case HttpMessage::METHOD_DELETE:
    cout << "DELETE" << endl;
    break;
  case HttpMessage::METHOD_TRACE:
    cout << "TRACE" << endl;
    break;
  }

  cout << "Scheme     : ";
  switch (msg.getScheme()) {
  case HttpMessage::SCHEME_NONE:
    cout << "NONE" << endl;
    break;
  case HttpMessage::SCHEME_HTTP:
    cout << "HTTP" << endl;
    break;
  }
  cout << "Status code: " << msg.getStatusCode() << endl;
  cout << "Request URI: " << msg.getRequestURI() << endl;

  return;
}

//////////////////////////////////////////////////////////////////////////////
// testPreProc()
//////////////////////////////////////////////////////////////////////////////
double
testPreProc(unsigned loopCount)
{
  IOBufferPool pool(96 /* bufferSize */, 20 /* bufferCount */);
  IOBuffer *cb = pool.newBuffer();

  HttpPreProcMessageManager msgMgr;

  HttpPreProc pp(cb, &msgMgr);

  /* start counting time here */
  double startTime = ink_time_wall_seconds();

  for (unsigned i = 0; i < loopCount; i++) {
    RequestInput requestInput(request1, cb);

    while (!requestInput.isDone()) {
      requestInput.run();
      pp.process();
    }
  }

  double endTime = ink_time_wall_seconds();

  return (endTime - startTime);
}

//////////////////////////////////////////////////////////////////////////////
// main().
//////////////////////////////////////////////////////////////////////////////
main()
{
  for (unsigned lc = 1; lc < 10000; lc *= 10) {
    double elapsedTime = testPreProc(lc);
    cout << "Elapsed time for " << lc << "loops is " << elapsedTime << endl;
  }

  return (0);
}
