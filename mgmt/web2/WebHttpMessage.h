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

#ifndef _WEB_HTTP_MESSAGE
#define _WEB_HTTP_MESSAGE

#include <time.h>
#include "Tokenizer.h"
#include "WebUtils.h"

/****************************************************************************
 *
 *  WebHttpMessage.h - classes to store information about incoming requests
 *                        and create hdrs for outgoing requests
 *
 *
 *
 ****************************************************************************/

/* define method       */
enum Method_t {
  METHOD_NONE = 0,
  METHOD_OPTIONS,
  METHOD_GET,
  METHOD_HEAD,
  METHOD_POST,
  METHOD_PUT,
  METHOD_DELETE,
  METHOD_TRACE,
  METHOD_CONNECT
};

/* defined http status codes constants */
enum HttpStatus_t {
  STATUS_CONTINUE = 0,
  STATUS_SWITCHING_PROTOCOL,

  STATUS_OK,
  STATUS_CREATED,
  STATUS_ACCEPTED,
  STATUS_NON_AUTHORITATIVE_INFORMATION,
  STATUS_NO_CONTENT,
  STATUS_RESET_CONTENT,
  STATUS_PARTIAL_CONTENT,

  STATUS_MULTIPLE_CHOICES,
  STATUS_MOVED_PERMANENTLY,
  STATUS_MOVED_TEMPORARILY,
  STATUS_SEE_OTHER,
  STATUS_NOT_MODIFIED,
  STATUS_USE_PROXY,

  STATUS_BAD_REQUEST,
  STATUS_UNAUTHORIZED,
  STATUS_PAYMENT_REQUIRED,
  STATUS_FORBIDDEN,
  STATUS_NOT_FOUND,

  STATUS_INTERNAL_SERVER_ERROR,
  STATUS_NOT_IMPLEMENTED,
  STATUS_BAD_GATEWAY,
  STATUS_SERVICE_UNAVAILABLE,
  STATUS_GATEWAY_TIMEOUT,
  STATUS_HTTPVER_NOT_SUPPORTED
};

/* define scheme type */
enum Scheme_t {
  SCHEME_UNKNOWN = -1,
  SCHEME_NONE = 0,
  SCHEME_HTTP,
  SCHEME_SHTTP,
};

enum Content_t {
  TEXT_PLAIN = 0,
  TEXT_HTML,
  TEXT_CSS,
  TEXT_UNKNOWN,
  IMAGE_GIF,
  IMAGE_JPEG,
  IMAGE_PNG,
  APP_JAVA,
  APP_JAVASCRIPT,
  APP_X509,
  APP_AUTOCONFIG,
  APP_ZIP
};

extern const char *const httpStatStr[];
extern const char *const httpStatCode[];
extern const char *const contentTypeStr[];

class httpMessage
{
public:
  httpMessage();
  ~httpMessage();

  Method_t
  getMethod()
  {
    return method;
  };
  const char *
  getFile()
  {
    return file;
  };
  const char *
  getQuery()
  {
    return query;
  };
  Scheme_t
  getScheme()
  {
    return scheme;
  };
  const char *
  getHeader()
  {
    return header;
  };
  char *
  getBody()
  {
    return body;
  };
  int
  getConLen()
  {
    return conLen;
  };
  const char *
  getReferer()
  {
    if (referer != NULL) {
      return referer;
    }
    return NULL;
  };
  const char *
  getContentType()
  {
    return conType_str;
  };
  const char *
  getAuthMessage()
  {
    return authMessage;
  };
  time_t
  getModTime()
  {
    return modificationTime;
  };

  int addRequestLine(char *);
  void addHeader(char *);
  int addRequestBody(SocketInfo socketD);

  //  void Print();
  void getLogInfo(const char **request);

private:
  httpMessage(const httpMessage &);
  void getModDate();
  Method_t method;
  char *file;
  // char* referer;
  char *query;
  Scheme_t scheme;
  char *header;
  char *body;
  int conLen;
  char *referer;
  char *conType_str;
  char *authMessage;
  Tokenizer *parser;
  time_t modificationTime;
  int modContentLength;
  char *client_request; // Request as the client sent it.  For logs
};

class httpResponse
{
public:
  httpResponse();
  ~httpResponse();

  void
  setContentType(Content_t ct)
  {
    conType = ct;
  };
  // If this method is called, it will override the content type
  // stored in conType. Example: "Content-type: text/html\r\n".
  void setContentType(const char *str);

  void
  setLength(int x)
  {
    conLen = x;
  };
  int
  getLength()
  {
    return conLen;
  };

  void
  setStatus(HttpStatus_t s)
  {
    status = s;
  };
  HttpStatus_t
  getStatus()
  {
    return status;
  };


  void
  setRefresh(int i)
  {
    refresh = i;
  };
  int
  getRefresh()
  {
    return refresh;
  };

  const char *
  getRefreshURL()
  {
    return refreshURL;
  };
  void setRefreshURL(const char *url);

  const char *
  getLocationURL()
  {
    return locationURL;
  };
  void setLocationURL(const char *url);

  void setRealm(const char *);
  void
  setLastMod(time_t lm)
  {
    lastMod = lm;
  };

  void
  setCachable(int c)
  {
    if ((cachable = c) == 0) {
      lastMod = -1;
    }
  }
  int
  getCachable()
  {
    return cachable;
  }

  int writeHdr(SocketInfo socketD);
  void getLogInfo(const char **date, HttpStatus_t *status, int *legth);

private:
  httpResponse(const httpResponse &);
  HttpStatus_t status;
  int refresh;
  int conLen;
  Content_t conType;
  char *explicitConType;
  char *authRealm;
  char *refreshURL;
  char *locationURL;
  time_t lastMod;
  int cachable;
  char *dateResponse; // for logs
};

#endif
