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

/****************************************************************************

  HttpBodyFactory.h

  This implements a user-customizable response message generation system.

  The concept is simple.  Error/response messages are classified into
  several types, each given a name, such as "request/header_error".

  The HttpBodyFactory can build a message body for each response type.
  The user can create custom message body text for each type (stored
  in a text file directory), containing templates with space-holders for
  variables which are inline-substituted with curent values.  The resulting
  body is dynamically allocated and returned.

  The major data types implemented in this file are:

    HttpBodyFactory       The main data structure which is the machine
                          that maintains configuration information, reads
                          user error message template files, and performs
                          the substitution to generate the message bodies.

    HttpBodySet           The data structure representing a set of
                          templates, including the templates and metadata.

    HttpBodyTemplate      The template loaded from the directory to be
                          instantiated with variables, producing a body.


 ****************************************************************************/

#ifndef _HttpBodyFactory_h_
#define _HttpBodyFactory_h_

#include <strings.h>
#include <sys/types.h>
#include "ts/ink_platform.h"
#include "HTTP.h"
#include "HttpConfig.h"
#include "HttpCompat.h"
#include "HttpTransact.h"
#include "Error.h"
#include "Main.h"
#include "ts/RawHashTable.h"

#define HTTP_BODY_TEMPLATE_MAGIC 0xB0DFAC00
#define HTTP_BODY_SET_MAGIC 0xB0DFAC55
#define HTTP_BODY_FACTORY_MAGIC 0xB0DFACFF

////////////////////////////////////////////////////////////////////////
//
//      class HttpBodyTemplate
//
//      An HttpBodyTemplate object represents a template with HTML
//      text, and unexpanded log fields.  The object also has methods
//      to dump out the contents of the template, and to instantiate
//      the template into a buffer given a context.
//
////////////////////////////////////////////////////////////////////////

class HttpBodyTemplate
{
public:
  HttpBodyTemplate();
  ~HttpBodyTemplate();

  void reset();
  int load_from_file(char *dir, char *file);
  bool
  is_sane()
  {
    return (magic == HTTP_BODY_TEMPLATE_MAGIC);
  }
  char *build_instantiated_buffer(HttpTransact::State *context, int64_t *length_return);

  unsigned int magic;
  int64_t byte_count;
  char *template_buffer;
  char *template_pathname;
};

////////////////////////////////////////////////////////////////////////
//
//      class HttpBodySet
//
//      An HttpBodySet object represents a set of body factory
//      templates.  It includes operators to get the hash table of
//      templates, and the associated metadata for the set.
//
//      The raw data members come from HttpBodySetRawData, which
//      is defined in proxy/hdrs/HttpCompat.h
//
////////////////////////////////////////////////////////////////////////

class HttpBodySet : public HttpBodySetRawData
{
public:
  HttpBodySet();
  ~HttpBodySet();

  int init(char *set_name, char *dir);
  bool
  is_sane()
  {
    return (magic == HTTP_BODY_SET_MAGIC);
  }

  HttpBodyTemplate *get_template_by_name(const char *name);
  void set_template_by_name(const char *name, HttpBodyTemplate *t);
};

////////////////////////////////////////////////////////////////////////
//
//      class HttpBodyFactory
//
//      An HttpBodyFactory object is the main object which keeps track
//      of all the response body templates, and which provides the
//      methods to create response bodies.
//
//      Once an HttpBodyFactory object is initialized, and the template
//      data has been loaded, the HttpBodyFactory object allows the
//      caller to make error message bodies w/fabricate_with_old_api
//
////////////////////////////////////////////////////////////////////////

class HttpBodyFactory
{
public:
  HttpBodyFactory();
  ~HttpBodyFactory();

  ///////////////////////
  // primary user APIs //
  ///////////////////////
  char *fabricate_with_old_api(const char *type, HttpTransact::State *context, int64_t max_buffer_length,
                               int64_t *resulting_buffer_length, char *content_language_out_buf, size_t content_language_buf_size,
                               char *content_type_out_buf, size_t content_type_buf_size, const char *format, va_list ap);

  char *
  fabricate_with_old_api_build_va(const char *type, HttpTransact::State *context, int64_t max_buffer_length,
                                  int64_t *resulting_buffer_length, char *content_language_out_buf,
                                  size_t content_language_buf_size, char *content_type_out_buf, size_t content_type_buf_size,
                                  const char *format, ...)
  {
    char *msg;
    va_list ap;

    va_start(ap, format);
    msg = fabricate_with_old_api(type, context, max_buffer_length, resulting_buffer_length, content_language_out_buf,
                                 content_language_buf_size, content_type_out_buf, content_type_buf_size, format, ap);
    va_end(ap);
    return msg;
  }

  void dump_template_tables(FILE *fp = stderr);
  void reconfigure();

private:
  char *fabricate(StrList *acpt_language_list, StrList *acpt_charset_list, const char *type, HttpTransact::State *context,
                  int64_t *resulting_buffer_length, const char **content_language_return, const char **content_charset_return,
                  const char **set_return = NULL);

  const char *determine_set_by_language(StrList *acpt_language_list, StrList *acpt_charset_list);
  const char *determine_set_by_host(HttpTransact::State *context);
  HttpBodyTemplate *find_template(const char *set, const char *type, HttpBodySet **body_set_return);
  bool is_response_suppressed(HttpTransact::State *context);
  bool
  is_sane()
  {
    return (magic == HTTP_BODY_FACTORY_MAGIC);
  }
  void
  sanity_check()
  {
    ink_assert(is_sane());
  }

private:
  ////////////////////////////
  // initialization methods //
  ////////////////////////////
  void nuke_template_tables();
  RawHashTable *load_sets_from_directory(char *set_dir);
  HttpBodySet *load_body_set_from_directory(char *set_name, char *tmpl_dir);

  /////////////////////////////////////////////////
  // internal data structure concurrency control //
  /////////////////////////////////////////////////
  void
  lock()
  {
    ink_mutex_acquire(&mutex);
  }
  void
  unlock()
  {
    ink_mutex_release(&mutex);
  }

  /////////////////////////////////////
  // manager configuration variables //
  /////////////////////////////////////
  int enable_customizations;     // 0:no custom,1:custom,2:language-targeted
  bool enable_logging;           // the user wants body factory logging
  int response_suppression_mode; // when to suppress responses

  ////////////////////
  // internal state //
  ////////////////////
  unsigned int magic;          // magic for sanity checks/debugging
  ink_mutex mutex;             // prevents reconfig/read races
  bool callbacks_established;  // all config variables present
  RawHashTable *table_of_sets; // sets of template hash tables
};

#endif
