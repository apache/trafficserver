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

#include <strings.h>
#include <ts/ts.h>
#include "common.h"
#include "list.h"

/* Used to determine if a request "looks" dynamic */
#define ASP_EXTENSION ".asp"
#define JSP_EXTENSION ".jsp"
#define CGI_BIN       "cgi"



/*-------------------------------------------------------------------------
  query_string_extract

  Extract query string from client's request and copy it in query_store
  If no query in client's request, query_store is set to NULL

  Returns 0 or -1 if an error occured
  -------------------------------------------------------------------------*/
int
query_string_extract(TxnData * txn_data, char **query_store)
{
  INKMBuffer bufp;
  INKMLoc url_loc;
  const char *query_string;
  int len;

  INKAssert(txn_data->magic == MAGIC_ALIVE);
  INKDebug(MED, "In query_string_extract");

  if (txn_data != NULL) {
    bufp = txn_data->request_url_buf;
    url_loc = txn_data->request_url_loc;
  } else {
    return -1;
  }

  query_string = INKUrlHttpQueryGet(bufp, url_loc, &len);
  if ((len != 0) && (query_string != NULL)) {
    *query_store = INKmalloc(len + 1);
    strncpy(*query_store, query_string, len);
    (*query_store)[len] = '\0';
    INKHandleStringRelease(bufp, url_loc, query_string);
  } else {
    *query_store = NULL;
  }

  INKDebug(LOW, "query string = |%s|", (*query_store != NULL) ? *query_store : "NULL");

  return 0;
}


/*-------------------------------------------------------------------------
  query_and_cookies_extract

  Extract query parameters and cookies from the request header

  query string syntax: url?param1=value1&param2=value2& ... paramN=valueN
  cookies syntax: Cookie: param1=value1; param2=value2; ... paramN=valueN

  Returns 0 or -1 if an error occured
  -------------------------------------------------------------------------*/
int
query_and_cookies_extract(INKHttpTxn txnp, TxnData * txn_data, PairList * query, PairList * cookies)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc, url_loc, cookies_loc;
  const char *query_string;
  const char *cookies_string;
  int len;

  INKAssert(txn_data->magic == MAGIC_ALIVE);
  INKDebug(MED, "In query_and_cookies_extract");

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    return -1;
  }

  /* Deal with query string */
  /* OLD IMPLEMENTATION
     if ((url_loc = INKHttpHdrUrlGet(bufp, hdr_loc)) == NULL) {
     INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
     return -1;
     }
     query_string = INKUrlHttpQueryGet(bufp, url_loc, &len); */

  if (txn_data != NULL) {
    query_string = INKUrlHttpQueryGet(txn_data->request_url_buf, txn_data->request_url_loc, &len);
  } else {
    query_string = NULL;
  }

  INKDebug(LOW, "query string = |%s|", query_string);

  if ((len != 0) && (query_string != NULL)) {
    char *str;
    char *ptr;
    char *equal;
    char *separator;

    /* life is easier with a null terminated string ... */
    str = (char *) INKmalloc(len + 1);
    memcpy(str, query_string, len);
    str[len] = '\0';

    ptr = str;

    do {
      char *name;
      char *value;

      equal = strchr(ptr, '=');
      if (equal != NULL) {
        *equal = '\0';
        name = strdup(ptr);
        ptr = (char *) (equal + 1);
      }

      separator = strchr(ptr, '&');
      if (separator != NULL) {
        *separator = '\0';
        value = strdup(ptr);
        ptr = (char *) (separator + 1);
      } else {
        /* Last value */
        value = strdup(ptr);
      }

      if ((name != NULL) && (value != NULL)) {
        INKDebug(LOW, "Adding query pair |%s| |%s|", name, value);
        pairListAdd(query, name, value);
      }

      free(name);
      free(value);
    } while (separator != NULL);

    INKfree(str);

    /* old implementation INKHandleStringRelease(bufp, url_loc, query_string); */
    INKHandleStringRelease(txn_data->request_url_buf, txn_data->request_url_loc, query_string);
  }
  /* old implementation INKHandleMLocRelease(bufp, hdr_loc, url_loc); */


  /* Extract cookies */
  cookies_loc = INKMimeHdrFieldRetrieve(bufp, hdr_loc, INK_MIME_FIELD_COOKIE);
  if (cookies_loc == NULL) {
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    return 0;
  }

  cookies_string = INKMimeHdrFieldValueGet(bufp, hdr_loc, cookies_loc, -1, &len);
  INKDebug(LOW, "Cookies = %s", cookies_string);

  if ((len != 0) && (cookies_string != NULL)) {
    char *str;
    char *ptr;
    char *equal;
    char *separator;

    /* life is easier with a null terminated string ... */
    str = (char *) INKmalloc(len + 1);
    memcpy(str, cookies_string, len);
    str[len] = '\0';

    ptr = str;

    do {
      char *name;
      char *value;

      /* skip heading spaces */
      while ((*ptr == ' ') && (ptr != '\0')) {
        ptr++;
      }

      equal = strchr(ptr, '=');
      if (equal != NULL) {
        *equal = '\0';
        name = strdup(ptr);
        ptr = (char *) (equal + 1);
      }

      separator = strchr(ptr, ';');
      if (separator != NULL) {
        *separator = '\0';
        value = strdup(ptr);
        ptr = (char *) (separator + 1);
      } else {
        /* Last value */
        value = strdup(ptr);
      }

      if ((name != NULL) && (value != NULL)) {
        INKDebug(LOW, "Adding cookie pair |%s| |%s|", name, value);
        pairListAdd(cookies, name, value);
      }

      free(name);
      free(value);
    } while (separator != NULL);

    INKfree(str);
    INKHandleMLocRelease(bufp, hdr_loc, cookies_loc);
  }

  INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);

  return 0;
}


/*-------------------------------------------------------------------------
  is_template_header

  To be processed as a template page, its header MUST have:
    - "200 OK" or "304 not modified" response
    - content type = text/html
    - header X-Template: True

  Returns 1 if it's a template header 0 otherwise
  -------------------------------------------------------------------------*/
int
is_template_header(INKMBuffer bufp, INKMLoc hdr_loc)
{
  INKHttpStatus resp_status;
  INKMLoc field_loc;
  const char *str;
  int len;

  INKDebug(MED, "In is_template_header");

  /* Check out that status is 200 */
  resp_status = INKHttpHdrStatusGet(bufp, hdr_loc);
  if ((resp_status != INK_HTTP_STATUS_OK) && (resp_status != INK_HTTP_STATUS_NOT_MODIFIED)) {
    INKDebug(LOW, "Not a template: status is [%d], not 200 nor 304", resp_status);
    return 0;
  }

  /* Check out that content type is text/html */
  field_loc = INKMimeHdrFieldRetrieve(bufp, hdr_loc, INK_MIME_FIELD_CONTENT_TYPE);
  if (field_loc == INK_NULL_MLOC) {
    INKDebug(LOW, "Not a template: could not find header %s", INK_MIME_FIELD_CONTENT_TYPE);
    return 0;
  }
  str = INKMimeHdrFieldValueGet(bufp, hdr_loc, field_loc, 0, &len);
  if (str == NULL) {
    INKDebug(LOW, "Not a template: could not get value of header %s", INK_MIME_FIELD_CONTENT_TYPE);
    INKHandleMLocRelease(bufp, hdr_loc, field_loc);
    return 0;
  }

  if (strncasecmp(str, CONTENT_TYPE_TEXT_HTML, CONTENT_TYPE_TEXT_HTML_LEN) != 0) {
    INKDebug(LOW, "Not a template: could value of header %s is %s, not %s",
             INK_MIME_FIELD_CONTENT_TYPE, str, CONTENT_TYPE_TEXT_HTML);
    INKHandleStringRelease(bufp, hdr_loc, str);
    INKHandleMLocRelease(bufp, hdr_loc, field_loc);
    return 0;
  }
  INKHandleStringRelease(bufp, hdr_loc, str);
  INKHandleMLocRelease(bufp, hdr_loc, field_loc);

  /* Check out that header X-Include is present */
  field_loc = INKMimeHdrFieldFind(bufp, hdr_loc, HEADER_X_TEMPLATE, -1);
  if (field_loc == INK_NULL_MLOC) {
    INKDebug(LOW, "Not a template: could not find header %s", HEADER_X_TEMPLATE);
    return 0;
  }
  INKHandleMLocRelease(bufp, hdr_loc, field_loc);

  /* We're done with checks. This is a template page that has to be transformed */
  INKDebug(LOW, "This is a template, transform it !");
  return 1;
}


/*-------------------------------------------------------------------------
  has_nocache_header

  Return 1 if the header "X-NoCache" is present. O otherwise.
  This header can be sent by the OS along with a X-Template header
  to let the TS know that the template should not be cached.
  -------------------------------------------------------------------------*/
int
has_nocache_header(INKMLoc bufp, INKMLoc hdr_loc)
{
  INKMLoc field_loc;
  INKDebug(MED, "In has_no_cacheheader");

  field_loc = INKMimeHdrFieldFind(bufp, hdr_loc, HEADER_X_NOCACHE, -1);
  if (field_loc != NULL) {
    INKHandleMLocRelease(bufp, hdr_loc, field_loc);
    return 1;
  }

  return 0;
}


/*-------------------------------------------------------------------------
  request_looks_dynamic

  Look dynamic <=> extension ".asp", ".jsp"
                   "cgi" in pathname
		   query string non null
		   cookies in header

  Returns 1 if dynamic, 0 if not dynamic, -1 in case of error
  -------------------------------------------------------------------------*/
int
request_looks_dynamic(INKMBuffer bufp, INKMLoc hdr_loc)
{
  INKMLoc url_loc, cookie_loc;
  const char *path;
  const char *query;
  int len;

  INKDebug(MED, "In request_looks_dynamic");

  url_loc = INKHttpHdrUrlGet(bufp, hdr_loc);
  if (url_loc == NULL) {
    INKError("Could not retrieve Url");
    return -1;
  }

  path = INKUrlPathGet(bufp, url_loc, &len);
  if ((path != NULL) && (len > 0)) {
    char *str = INKmalloc(len + 1);
    strncpy(str, path, len);
    str[len] = '\0';

    if ((strstr(str, ASP_EXTENSION) != NULL) || (strstr(str, JSP_EXTENSION) != NULL) || (strstr(str, CGI_BIN) != NULL)) {
      INKHandleStringRelease(bufp, url_loc, path);
      INKHandleMLocRelease(bufp, hdr_loc, url_loc);
      return 1;
    }
    INKHandleStringRelease(bufp, url_loc, path);
  }

  query = INKUrlHttpQueryGet(bufp, url_loc, &len);
  if ((query != NULL) && (len > 0)) {
    INKHandleStringRelease(bufp, url_loc, query);
    INKHandleMLocRelease(bufp, hdr_loc, url_loc);
    return 1;
  }

  cookie_loc = INKMimeHdrFieldRetrieve(bufp, hdr_loc, INK_MIME_FIELD_COOKIE);
  if (cookie_loc != NULL) {
    INKHandleMLocRelease(bufp, hdr_loc, cookie_loc);
    INKHandleMLocRelease(bufp, hdr_loc, url_loc);
    return 1;
  }

  INKHandleMLocRelease(bufp, hdr_loc, url_loc);
  return 0;
}


/*-------------------------------------------------------------------------
  is_block_request

  Returns 1 if it's a request for a block, 0 if otherwise
  Look for header X-Block in request.
  -------------------------------------------------------------------------*/
int
is_block_request(INKMBuffer bufp, INKMLoc hdr_loc)
{
  INKMLoc field_loc;

  INKDebug(MED, "In is_block_request");

  field_loc = INKMimeHdrFieldFind(bufp, hdr_loc, HEADER_X_BLOCK, -1);
  if (field_loc != NULL) {
    INKHandleMLocRelease(bufp, hdr_loc, field_loc);
    return 1;
  }

  return 0;
}


/*-------------------------------------------------------------------------
  modify_request_url

  Change a header url into a template url:
     - remove query string
     - append ".template" to the path
     - store the original url in the txn_data structure
  -------------------------------------------------------------------------*/
void
modify_request_url(INKMBuffer bufp, INKMLoc url_loc, TxnData * txn_data)
{
  INKMBuffer template_url_buf = txn_data->template_url_buf;
  INKMLoc template_url_loc = txn_data->template_url_loc;
  const char *user;
  const char *password;
  const char *host;
  int port;
  const char *path;
  const char *fragment;
  const char *params;
  int len;

  INKAssert(txn_data->magic == MAGIC_ALIVE);
  INKDebug(LOW, "In modify_request_url");

  /* Note: We have to do all the copy manually due to a bug
     in INKHttpUrlQuerySet: if used to set query to NULL,
     the "?" is not removed. */

  INKUrlSchemeSet(template_url_buf, template_url_loc, INK_URL_SCHEME_HTTP, INK_URL_LEN_HTTP);

  user = INKUrlUserGet(bufp, url_loc, &len);
  if ((user != NULL) && (len > 0)) {
    INKUrlUserSet(template_url_buf, template_url_loc, user, len);
    INKHandleStringRelease(bufp, url_loc, user);
  }
  password = INKUrlPasswordGet(bufp, url_loc, &len);
  if ((password != NULL) && (len > 0)) {
    INKUrlPasswordSet(template_url_buf, template_url_loc, password, len);
    INKHandleStringRelease(bufp, url_loc, password);
  }
  host = INKUrlHostGet(bufp, url_loc, &len);
  if ((host != NULL) && (len > 0)) {
    INKUrlHostSet(template_url_buf, template_url_loc, host, len);
    INKHandleStringRelease(bufp, url_loc, host);
  }
  port = INKUrlPortGet(bufp, url_loc);
  if (port != HTTP_DEFAULT_PORT) {
    INKUrlPortSet(template_url_buf, template_url_loc, port);
  }
  path = INKUrlPathGet(bufp, url_loc, &len);
  if ((path != NULL) && (len > 0)) {
    int new_path_len = len + sizeof(TEMPLATE_CACHE_SUFFIX);
    char *new_path = INKmalloc(new_path_len);
    sprintf(new_path, "%s%s", path, TEMPLATE_CACHE_SUFFIX);
    INKUrlPathSet(template_url_buf, template_url_loc, new_path, -1);
    INKfree(new_path);
    INKHandleStringRelease(bufp, url_loc, path);
  }
  params = INKUrlHttpParamsGet(bufp, url_loc, &len);
  if ((params != NULL) && (len > 0)) {
    INKUrlHttpParamsSet(template_url_buf, template_url_loc, params, len);
    INKHandleStringRelease(bufp, url_loc, params);
  }
  fragment = INKUrlHttpFragmentGet(bufp, url_loc, &len);
  if ((fragment != NULL) && (len > 0)) {
    INKUrlHttpFragmentSet(template_url_buf, template_url_loc, fragment, len);
    INKHandleStringRelease(bufp, url_loc, fragment);
  }

  /* Replace the original url by the template url */
  INKUrlCopy(bufp, url_loc, template_url_buf, template_url_loc);
}
