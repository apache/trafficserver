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

 /***************************************/
/****************************************************************************
 *
 *  WebHttp.cc - code to process requests, and create responses
 *
 *
 ****************************************************************************/

#include "libts.h"
#include "ink_platform.h"

#include "SimpleTokenizer.h"

#include "WebCompatibility.h"
#include "WebHttp.h"
#include "WebHttpContext.h"
#include "WebHttpMessage.h"
#include "WebHttpSession.h"
#include "WebOverview.h"

#include "mgmtapi.h"
#include "WebMgmtUtils.h"
#include "MgmtUtils.h"
#include "CfgContextUtils.h"

//-------------------------------------------------------------------------
// defines
//-------------------------------------------------------------------------

#define DIR_MODE S_IRWXU
#define FILE_MODE S_IRWXU

#define MAX_ARGS         10
#define MAX_TMP_BUF_LEN  1024

//-------------------------------------------------------------------------
// types
//-------------------------------------------------------------------------

typedef int (*WebHttpHandler) (WebHttpContext * whc, const char *file);

//-------------------------------------------------------------------------
// globals
//-------------------------------------------------------------------------

// only allow access to specific files on the autoconf port
static InkHashTable *g_autoconf_allow_ht = 0;

static InkHashTable *g_file_bindings_ht = 0;

//-------------------------------------------------------------------------
// handle_record_info
//
// Warning!!! This is really hacky since we should not be directly
// accessing the librecords data structures.  Just do this here
// tempoarily until we can have something better.
//-------------------------------------------------------------------------

#include "P_RecCore.h"

#define LINE_SIZE 512
#define BUF_SIZE 128
#define NULL_STR "NULL"

#undef LINE_SIZE
#undef BUF_SIZE
#undef NULL_STR

//-------------------------------------------------------------------------
// handle_synthetic
//-------------------------------------------------------------------------

static int
handle_synthetic(WebHttpContext * whc, const char * /* file ATS_UNUSED */)
{
  char buffer[28];
  char cur = 'a';
  whc->response_hdr->setContentType(TEXT_PLAIN);
  whc->response_hdr->setStatus(STATUS_OK);
  buffer[26] = '\n';
  buffer[27] = '\0';
  for (int i = 0; i < 26; i++) {
    *(buffer + i) = cur;
    cur++;
  }
  for (int j = 0; j < 60; j++) {
    whc->response_bdy->copyFrom(buffer, 27);
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_default
//-------------------------------------------------------------------------

static int
handle_default(WebHttpContext * whc, const char *file)
{
  char *doc_root_file;
  int file_size;
  time_t file_date_gmt;
  WebHandle h_file;

  httpMessage *request = whc->request;
  httpResponse *response_hdr = whc->response_hdr;
  textBuffer *response_bdy = whc->response_bdy;

  const char *request_file = file;
  time_t request_file_ims;
  int request_file_len;

  // requests are supposed to begin with a "/"
  if (*request_file != '/') {
    response_hdr->setStatus(STATUS_NOT_FOUND);
    WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }
  // first, make sure there are no ..'s in path or root directory
  // access in name for security reasons
  if (strstr(request_file, "..") != NULL || strncmp(request_file, "//", 2) == 0) {
    response_hdr->setStatus(STATUS_FORBIDDEN);
    WebHttpSetErrorResponse(whc, STATUS_FORBIDDEN);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }

  if (strcmp("/", request_file) == 0) {
    request_file = whc->default_file;
  }
  // check file type and set document type if appropiate
  request_file_len = strlen(request_file);
  if (strcmp(request_file + (request_file_len - 4), ".htm") == 0) {
    response_hdr->setContentType(TEXT_HTML);
  } else if (strcmp(request_file + (request_file_len - 5), ".html") == 0) {
    response_hdr->setContentType(TEXT_HTML);
  } else if (strcmp(request_file + (request_file_len - 4), ".css") == 0) {
    response_hdr->setContentType(TEXT_CSS);
  } else if (strcmp(request_file + (request_file_len - 4), ".gif") == 0) {
    response_hdr->setContentType(IMAGE_GIF);
  } else if (strcmp(request_file + (request_file_len - 4), ".jpg") == 0) {
    response_hdr->setContentType(IMAGE_JPEG);
  } else if (strcmp(request_file + (request_file_len - 5), ".jpeg") == 0) {
    response_hdr->setContentType(IMAGE_JPEG);
  } else if (strcmp(request_file + (request_file_len - 4), ".png") == 0) {
    response_hdr->setContentType(IMAGE_PNG);
  } else if (strcmp(request_file + (request_file_len - 4), ".jar") == 0) {
    response_hdr->setContentType(APP_JAVA);
  } else if (strcmp(request_file + (request_file_len - 3), ".js") == 0) {
    response_hdr->setContentType(APP_JAVASCRIPT);
  } else if (strcmp(request_file + (request_file_len - 4), ".der") == 0) {
    response_hdr->setContentType(APP_X509);
  } else if (strcmp(request_file + (request_file_len - 4), ".dat") == 0) {
    response_hdr->setContentType(APP_AUTOCONFIG);
    response_hdr->setCachable(0);
  } else if (strcmp(request_file + (request_file_len - 4), ".pac") == 0) {
    response_hdr->setContentType(APP_AUTOCONFIG);
    // Fixed INKqa04312 - 02/21/1999 elam
    // We don't want anyone to cache .pac files.
    response_hdr->setCachable(0);
  } else if (strcmp(request_file + (request_file_len - 4), ".zip") == 0) {
    response_hdr->setContentType(APP_ZIP);
  } else {
    // don't serve file types that we don't know about; helps to lock
    // down the webserver.  for example, when serving files out the
    // etc/trafficserver/plugins directory, we don't want to allow the users to
    // access the .so plugin files.
    response_hdr->setStatus(STATUS_NOT_FOUND);
    WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }

  // append the appropriate doc_root on to the file
  doc_root_file = WebHttpAddDocRoot_Xmalloc(whc, request_file, request_file_len);

  // open the requested file
  if ((h_file = WebFileOpenR(doc_root_file)) == WEB_HANDLE_INVALID) {
    //could not find file
    ats_free(doc_root_file);
    response_hdr->setStatus(STATUS_NOT_FOUND);
    WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }
  // get the file
  file_size = WebFileGetSize(h_file);
  file_date_gmt = WebFileGetDateGmt(h_file);
  request_file_ims = request->getModTime();

  // special logic for the autoconf port
  if ((whc->server_state & WEB_HTTP_SERVER_STATE_AUTOCONF) && (file_size == 0)) {
    response_hdr->setStatus(STATUS_NOT_FOUND);
    WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
    WebFileClose(h_file);
    ats_free(doc_root_file);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }
  // Check to see if the clients copy is up to date.  Ignore the
  // stupid content length that Netscape Navigator sends on the
  // If-Modified-Since line since it not in the HTTP 1.0 standard

  // Since the client sends If-Modified-Since in GMT, make sure that
  // we transform mtime to GMT
  if (request_file_ims != -1 && request_file_ims >= file_date_gmt) {
    response_hdr->setStatus(STATUS_NOT_MODIFIED);
  } else {
    // fetch the file from disk to memory
    response_hdr->setStatus(STATUS_OK);
    response_hdr->setLength(file_size);
    while (response_bdy->rawReadFromFile(h_file) > 0);
  }
  // set the document last-modified header
  response_hdr->setLastMod(file_date_gmt);

  WebFileClose(h_file);
  ats_free(doc_root_file);

  return WEB_HTTP_ERR_OKAY;

}


//-------------------------------------------------------------------------
// read_request
//-------------------------------------------------------------------------
int
read_request(WebHttpContext * whc)
{
  const int buffer_size = 2048;
  char *buffer = (char *) alloca(buffer_size);

  httpMessage *request = whc->request;
  httpResponse *response_hdr = whc->response_hdr;

  // first get the request line
  if (sigfdrdln(whc->si, buffer, buffer_size) < 0) {
    // if we can not get the request line, update the status code so
    // it can get logged correctly but do not bother trying to send a
    // response
    response_hdr->setStatus(STATUS_BAD_REQUEST);
    return WEB_HTTP_ERR_REQUEST_FATAL;
  }


  if (request->addRequestLine(buffer) != 0) {
    response_hdr->setStatus(STATUS_BAD_REQUEST);
    WebHttpSetErrorResponse(whc, STATUS_BAD_REQUEST);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }

  // Check for a scheme we do not understand
  //
  //  If we undertand the scheme, it has
  //   to be HTTP
  if (request->getScheme() == SCHEME_UNKNOWN) {
    response_hdr->setStatus(STATUS_NOT_IMPLEMENTED);
    WebHttpSetErrorResponse(whc, STATUS_NOT_IMPLEMENTED);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }

  if (request->getMethod() != METHOD_GET && request->getMethod() != METHOD_POST && request->getMethod() != METHOD_HEAD) {
    response_hdr->setStatus(STATUS_NOT_IMPLEMENTED);
    WebHttpSetErrorResponse(whc, STATUS_NOT_IMPLEMENTED);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }
  // Read the headers of http request line by line until
  //   we get a line that is solely composed of "\r" (or
  //   just "" since not everyone follows the HTTP standard
  //
  do {
    if (sigfdrdln(whc->si, buffer, buffer_size) < 0) {
      response_hdr->setStatus(STATUS_BAD_REQUEST);
      return WEB_HTTP_ERR_REQUEST_FATAL;
    }
    request->addHeader(buffer);
  } while (strcmp(buffer, "\r") != 0 && *buffer != '\0');

  // If there is a content body, read it in
  if (request->addRequestBody(whc->si) < 0) {
    // There was error on reading the response body
    response_hdr->setStatus(STATUS_BAD_REQUEST);
    WebHttpSetErrorResponse(whc, STATUS_NOT_IMPLEMENTED);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }

  // Drain read channel: In the case of Linux, OS sends reset to the
  // socket if we close it when there is data left on it ot be read
  // (in compliance with TCP). This causes problems with the "POST"
  // method. (for example with update.html). With IE, we found ending
  // "\r\n" were not read.  The following work around is to read all
  // that is left in the socket before closing it.
#define MAX_DRAIN_BYTES 32
  // INKqa11524: If the user is malicious and keeps sending us data,
  // we'll go into an infinite spin here.  Fix is to only drain up
  // to 32 bytes to allow for funny browser behavior but to also
  // prevent reading forever.
  int drain_bytes = 0;
  if (fcntl(whc->si.fd, F_SETFL, O_NONBLOCK) >= 0) {
    char ch;
    while ((read(whc->si.fd, &ch, 1) > 0) && (drain_bytes < MAX_DRAIN_BYTES)) {
      drain_bytes++;
    }
  }

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// write_response
//-------------------------------------------------------------------------

int
write_response(WebHttpContext * whc)
{
  char *buf_p;
  int bytes_to_write;
  int bytes_written;

  // Make sure that we have a content length
  if (whc->response_hdr->getLength() < 0) {
    whc->response_hdr->setLength(whc->response_bdy->spaceUsed());
  }
  whc->response_hdr->writeHdr(whc->si);
  if (whc->request->getMethod() != METHOD_HEAD) {
    buf_p = whc->response_bdy->bufPtr();
    bytes_to_write = whc->response_bdy->spaceUsed();
    while (bytes_to_write) {
      bytes_written = socket_write(whc->si, buf_p, bytes_to_write);
      if (bytes_written < 0) {
        if (errno == EINTR || errno == EAGAIN)
          continue;
        else
          return WEB_HTTP_ERR_FAIL;
      } else {
        bytes_to_write -= bytes_written;
        buf_p += bytes_written;
      }
    }
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// process_query
//-------------------------------------------------------------------------

int
process_query(WebHttpContext * whc)
{
  int err;
  InkHashTable *ht;
  char *value;
  // processFormSubmission will substituteUnsafeChars()
  if ((ht = processFormSubmission((char *) (whc->request->getQuery()))) != NULL) {
    whc->query_data_ht = ht;
    // extract some basic info for easier access later
    if (ink_hash_table_lookup(ht, "mode", (void **) &value)) {
      if (strcmp(value, "1") == 0)
        whc->request_state |= WEB_HTTP_STATE_CONFIGURE;
    }
    if (ink_hash_table_lookup(ht, "detail", (void **) &value)) {
      if (strcmp(value, "more") == 0)
        whc->request_state |= WEB_HTTP_STATE_MORE_DETAIL;
    }
    err = WEB_HTTP_ERR_OKAY;
  } else {
    err = WEB_HTTP_ERR_FAIL;
  }
  return err;
}

//-------------------------------------------------------------------------
// process_post
//-------------------------------------------------------------------------
int
process_post(WebHttpContext * whc)
{
  int err;
  InkHashTable *ht;
  // processFormSubmission will substituteUnsafeChars()
  if ((ht = processFormSubmission(whc->request->getBody())) != NULL) {
    whc->post_data_ht = ht;
    err = WEB_HTTP_ERR_OKAY;
  } else {
    err = WEB_HTTP_ERR_FAIL;
  }
  return err;
}

//-------------------------------------------------------------------------
// signal_handler_init
//-------------------------------------------------------------------------

void
signal_handler_do_nothing(int /* x ATS_UNUSED */)
{
  //  A small function thats whole purpose is to give the signal
  //  handler for breaking out of a network read, somethng to call
}

int
signal_handler_init()
{
  // Setup signal handling.  We want to able to unstick stuck socket
  // connections.  This is accomplished by a watcher thread doing a
  // half close on the incoming socket after a timeout.  To break, out
  // the current read which is likely stuck we have a signal handler
  // on SIGUSR1 which does nothing except by side effect of break the
  // read.  All future reads from the socket should fail since
  // incoming traffic is shutdown on the connection and thread should
  // exit normally
  sigset_t sigsToBlock;
  // FreeBSD and Linux use SIGUSR1 internally in the threads library
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  // Set up the handler for SIGUSR1
  struct sigaction sigHandler;
  sigHandler.sa_handler = signal_handler_do_nothing;
  sigemptyset(&sigHandler.sa_mask);
  sigHandler.sa_flags = 0;
  sigaction(SIGUSR1, &sigHandler, NULL);
#endif
  // Block all other signals
  sigfillset(&sigsToBlock);
  sigdelset(&sigsToBlock, SIGUSR1);
  ink_thread_sigsetmask(SIG_SETMASK, &sigsToBlock, NULL);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// WebHttpInit
//-------------------------------------------------------------------------

void
WebHttpInit()
{
  static int initialized = 0;

  if (initialized != 0) {
    mgmt_log(stderr, "[WebHttpInit] error, initialized twice (%d)", initialized);
  }
  initialized++;

  // initialize allow files
  g_autoconf_allow_ht = ink_hash_table_create(InkHashTableKeyType_String);
  ink_hash_table_insert(g_autoconf_allow_ht, "/proxy.pac", NULL);
  ink_hash_table_insert(g_autoconf_allow_ht, "/public_key.der", NULL);
  ink_hash_table_insert(g_autoconf_allow_ht, "/synthetic.txt", NULL);

  // initialize file bindings
  g_file_bindings_ht = ink_hash_table_create(InkHashTableKeyType_String);
  ink_hash_table_insert(g_file_bindings_ht, "/synthetic.txt", (void *) handle_synthetic);

  // initialize other modules
  WebHttpSessionInit();
}

//-------------------------------------------------------------------------
// WebHttpHandleConnection
//
// Handles http requests across the web management port
//-------------------------------------------------------------------------
void
WebHttpHandleConnection(WebHttpConInfo * whci)
{
  int err = WEB_HTTP_ERR_OKAY;
  WebHttpContext *whc;
  WebHttpHandler handler;
  char *file;
  char *extn;
  int drain_bytes;
  char ch;

  // initialization
  if ((whc = WebHttpContextCreate(whci)) == NULL)
    goto Ltransaction_close;
  if (signal_handler_init() != WEB_HTTP_ERR_OKAY)
    goto Ltransaction_close;

  // read request
  if ((err = read_request(whc)) != WEB_HTTP_ERR_OKAY)
    goto Lerror_switch;

  // get our file information
  file = (char *) (whc->request->getFile());
  if (strcmp("/", file) == 0) {
    file = (char *) (whc->default_file);
  }

  Debug("web2", "[WebHttpHandleConnection] request file: %s", file);

  if (whc->server_state & WEB_HTTP_SERVER_STATE_AUTOCONF) {

    // security concern: special treatment if we're handling a request
    // on the autoconf port.  can't have users downloading arbitrary
    // files under the config directory!
    if (!ink_hash_table_isbound(g_autoconf_allow_ht, file)) {
      mgmt_elog(stderr, 0, "[WebHttpHandleConnection] %s not valid autoconf file", file);
      whc->response_hdr->setStatus(STATUS_NOT_FOUND);
      WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
      goto Ltransaction_send;
    }
  }

  // process query
  process_query(whc);

  // Lookup file handler
  if (!ink_hash_table_lookup(g_file_bindings_ht, file, (void **) &handler)) {
    extn = file;
    while (*extn != '\0')
      extn++;
    while ((extn > file) && (*extn != '.'))
      extn--;
    handler = handle_default;
  }
  err = handler(whc, file);

Lerror_switch:

  switch (err) {
  case WEB_HTTP_ERR_OKAY:
  case WEB_HTTP_ERR_REQUEST_ERROR:
    goto Ltransaction_send;
  case WEB_HTTP_ERR_FAIL:
  case WEB_HTTP_ERR_REQUEST_FATAL:
  default:
    goto Ltransaction_close;
  }

Ltransaction_send:

  // write response
  if ((err = write_response(whc)) != WEB_HTTP_ERR_OKAY)
    goto Ltransaction_close;

  // close the connection before logging it to reduce latency
  shutdown(whc->si.fd, 1);
  drain_bytes = 0;
  if (fcntl(whc->si.fd, F_SETFL, O_NONBLOCK) >= 0) {
    while ((read(whc->si.fd, &ch, 1) > 0) && (drain_bytes < MAX_DRAIN_BYTES)) {
      drain_bytes++;
    }
  }
  close_socket(whc->si.fd);
  whc->si.fd = -1;

Ltransaction_close:

  // if we didn't close already, close connection
  if (whc->si.fd != -1) {
    shutdown(whc->si.fd, 1);
    drain_bytes = 0;
    if (fcntl(whc->si.fd, F_SETFL, O_NONBLOCK) >= 0) {
      while ((read(whc->si.fd, &ch, 1) > 0) && (drain_bytes < MAX_DRAIN_BYTES)) {
        drain_bytes++;
      }
    }
    close_socket(whc->si.fd);
  }

  // clean up memory
  WebHttpContextDestroy(whc);
}

//-------------------------------------------------------------------------
// WebHttpSetErrorResponse
//
// Formulates a page to return on an HttpStatus condition
//-------------------------------------------------------------------------

void
WebHttpSetErrorResponse(WebHttpContext * whc, HttpStatus_t error)
{
  //-----------------------------------------------------------------------
  // FIXME: HARD-CODED HTML HELL!!!
  //-----------------------------------------------------------------------

  static const char a[] = "<HTML>\n<Head>\n<TITLE>";
  static const char b[] = "</TITLE>\n</HEAD>\n<BODY bgcolor=\"#FFFFFF\"><h1>\n";
  static const char c[] = "</h1>\n</BODY>\n</HTML>\n";
  int errorMsgLen = strlen(httpStatStr[error]);

  // reset the buffer
  whc->response_bdy->reUse();

  // fill in the buffer
  whc->response_bdy->copyFrom(a, strlen(a));
  whc->response_bdy->copyFrom(httpStatStr[error], errorMsgLen);
  whc->response_bdy->copyFrom(b, strlen(b));
  whc->response_bdy->copyFrom(httpStatStr[error], errorMsgLen);
  whc->response_bdy->copyFrom(c, strlen(c));
}

//-------------------------------------------------------------------------
// WebHttpAddDocRoot_Xmalloc
//-------------------------------------------------------------------------

char *
WebHttpAddDocRoot_Xmalloc(WebHttpContext * whc, const char *file, int file_len)
{
  char *doc_root_file = (char *)ats_malloc(file_len + whc->doc_root_len + 1);

  memcpy(doc_root_file, whc->doc_root, whc->doc_root_len);
  memcpy(doc_root_file + whc->doc_root_len, file, file_len);
  *(doc_root_file + whc->doc_root_len + file_len) = '\0';

  Debug("web2", "DocRoot request file: %s", doc_root_file);

  return doc_root_file;
}
