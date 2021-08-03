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

  HttpBodyFactory.cc


 ****************************************************************************/

#include "tscore/ink_platform.h"
#include "tscore/ink_sprintf.h"
#include "tscore/ink_file.h"
#include "tscore/Filenames.h"
#include "HttpBodyFactory.h"
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "URL.h"
#include "logging/Log.h"
#include "logging/LogAccess.h"
#include "HttpCompat.h"
#include "tscore/I_Layout.h"

//////////////////////////////////////////////////////////////////////
// The HttpBodyFactory creates HTTP response page bodies, supported //
// configurable customization and language-targeting.               //
//                                                                  //
// The body factory can be reconfigured dynamically by a manager    //
// callback, so locking is required.  The callback takes a lock,    //
// and the user entry points take a lock.  These locks may limit    //
// the speed of error page generation.                              //
//////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
//
// User-Callable APIs --- locks will be taken internally
//
////////////////////////////////////////////////////////////////////////

char *
HttpBodyFactory::fabricate_with_old_api(const char *type, HttpTransact::State *context, int64_t max_buffer_length,
                                        int64_t *resulting_buffer_length, char *content_language_out_buf,
                                        size_t content_language_buf_size, char *content_type_out_buf, size_t content_type_buf_size,
                                        int format_size, const char *format)
{
  char *buffer            = nullptr;
  const char *lang_ptr    = nullptr;
  const char *charset_ptr = nullptr;
  char url[1024];
  const char *set               = nullptr;
  bool found_requested_template = false;
  bool plain_flag               = false;

  ///////////////////////////////////////////////
  // if suppressing this response, return NULL //
  ///////////////////////////////////////////////
  if (is_response_suppressed(context)) {
    if (enable_logging) {
      Log::error("BODY_FACTORY: suppressing '%s' response for url '%s'", type, url);
    }
    return nullptr;
  }

  lock();

  *resulting_buffer_length = 0;

  ink_strlcpy(content_language_out_buf, "en", content_language_buf_size);
  ink_strlcpy(content_type_out_buf, "text/html", content_type_buf_size);

  ///////////////////////////////////////////////////////////////////
  // if logging turned on, buffer up the URL string for simplicity //
  ///////////////////////////////////////////////////////////////////
  if (enable_logging) {
    url[0] = '\0';
    URL *u = context->hdr_info.client_request.url_get();

    if (u->valid()) { /* if url exists, copy the string into buffer */
      size_t i;
      char *s = u->string_get(&context->arena);
      for (i = 0; (i < sizeof(url) - 1) && s[i]; i++) {
        url[i] = s[i];
      }
      url[i] = '\0';
      if (s) {
        context->arena.str_free(s);
      }
    }
  }
  //////////////////////////////////////////////////////////////////////////////////
  // if language-targeting activated, get client Accept-Language & Accept-Charset //
  //////////////////////////////////////////////////////////////////////////////////

  StrList acpt_language_list(false);
  StrList acpt_charset_list(false);

  if (enable_customizations == 2) {
    context->hdr_info.client_request.value_get_comma_list(MIME_FIELD_ACCEPT_LANGUAGE, MIME_LEN_ACCEPT_LANGUAGE,
                                                          &acpt_language_list);
    context->hdr_info.client_request.value_get_comma_list(MIME_FIELD_ACCEPT_CHARSET, MIME_LEN_ACCEPT_CHARSET, &acpt_charset_list);
  }
  ///////////////////////////////////////////
  // check if we don't need to format body //
  ///////////////////////////////////////////

  buffer = (format == nullptr) ? nullptr : ats_strndup(format, format_size);
  if (buffer != nullptr && format_size > 0) {
    *resulting_buffer_length = format_size > max_buffer_length ? 0 : format_size;
    plain_flag               = true;
  }
  /////////////////////////////////////////////////////////
  // try to fabricate the desired type of error response //
  /////////////////////////////////////////////////////////
  if (buffer == nullptr) {
    buffer =
      fabricate(&acpt_language_list, &acpt_charset_list, type, context, resulting_buffer_length, &lang_ptr, &charset_ptr, &set);
    found_requested_template = (buffer != nullptr);
  }
  /////////////////////////////////////////////////////////////
  // if failed, try to fabricate the default custom response //
  /////////////////////////////////////////////////////////////
  if (buffer == nullptr) {
    if (is_response_body_precluded(context->http_return_code)) {
      *resulting_buffer_length = 0;
      unlock();
      return nullptr;
    }
    buffer = fabricate(&acpt_language_list, &acpt_charset_list, "default", context, resulting_buffer_length, &lang_ptr,
                       &charset_ptr, &set);
  }

  ///////////////////////////////////
  // enforce the max buffer length //
  ///////////////////////////////////
  if (buffer && (*resulting_buffer_length > max_buffer_length)) {
    if (enable_logging) {
      Log::error(("BODY_FACTORY: template '%s/%s' consumed %" PRId64 " bytes, "
                  "exceeding %" PRId64 " byte limit, using internal default"),
                 set, type, *resulting_buffer_length, max_buffer_length);
    }
    *resulting_buffer_length = 0;
    buffer                   = static_cast<char *>(ats_free_null(buffer));
  }
  /////////////////////////////////////////////////////////////////////
  // handle return of instantiated template and generate the content //
  // language and content type return values                         //
  /////////////////////////////////////////////////////////////////////
  if (buffer) { // got an instantiated template
    if (!plain_flag) {
      snprintf(content_language_out_buf, content_language_buf_size, "%s", lang_ptr);
      snprintf(content_type_out_buf, content_type_buf_size, "text/html; charset=%s", charset_ptr);
    }

    if (enable_logging) {
      if (found_requested_template) { // got exact template
        Log::error(("BODY_FACTORY: using custom template "
                    "'%s/%s' for url '%s' (language '%s', charset '%s')"),
                   set, type, url, lang_ptr, charset_ptr);
      } else { // got default template
        Log::error(("BODY_FACTORY: can't find custom template "
                    "'%s/%s', using '%s/%s' for url '%s' (language '%s', charset '%s')"),
                   set, type, set, "default", url, lang_ptr, charset_ptr);
      }
    }
  } else { // no template
    if (enable_logging) {
      Log::error(("BODY_FACTORY: can't find templates '%s' or '%s' for url `%s'"), type, "default", url);
    }
  }
  unlock();

  return buffer;
}

void
HttpBodyFactory::dump_template_tables(FILE *fp)
{
  lock();
  if (table_of_sets) {
    for (const auto &it1 : *table_of_sets.get()) {
      HttpBodySet *body_set = static_cast<HttpBodySet *>(it1.second);
      if (body_set) {
        fprintf(fp, "set %s: name '%s', lang '%s', charset '%s'\n", it1.first.c_str(), body_set->set_name,
                body_set->content_language, body_set->content_charset);

        ///////////////////////////////////////////
        // loop over body-types->body hash table //
        ///////////////////////////////////////////

        ink_assert(body_set->is_sane());
        if (body_set->table_of_pages) {
          for (const auto &it2 : *body_set->table_of_pages.get()) {
            fprintf(fp, "  %-30s: %" PRId64 " bytes\n", it2.first.c_str(), it2.second->byte_count);
          }
        }
      }
    }
  }

  unlock();
}

////////////////////////////////////////////////////////////////////////
//
// Configuration Change Callback
//
////////////////////////////////////////////////////////////////////////

static int
config_callback(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData /* data ATS_UNUSED */,
                void *cookie)
{
  HttpBodyFactory *body_factory = static_cast<HttpBodyFactory *>(cookie);
  body_factory->reconfigure();
  return 0;
}

void
HttpBodyFactory::reconfigure()
{
  RecInt e;
  RecString s = nullptr;
  bool all_found;
  int rec_err;

  lock();
  sanity_check();

  if (!callbacks_established) {
    unlock();
    return;
  } // callbacks not setup right

  ////////////////////////////////////////////
  // extract relevant records.config values //
  ////////////////////////////////////////////

  Debug("body_factory", "config variables changed, reconfiguring...");

  all_found = true;

  // enable_customizations if records.config set
  rec_err               = RecGetRecordInt("proxy.config.body_factory.enable_customizations", &e);
  enable_customizations = ((rec_err == REC_ERR_OKAY) ? e : 0);
  all_found             = all_found && (rec_err == REC_ERR_OKAY);
  Debug("body_factory", "enable_customizations = %d (found = %" PRId64 ")", enable_customizations, e);

  rec_err        = RecGetRecordInt("proxy.config.body_factory.enable_logging", &e);
  enable_logging = ((rec_err == REC_ERR_OKAY) ? (e ? true : false) : false);
  all_found      = all_found && (rec_err == REC_ERR_OKAY);
  Debug("body_factory", "enable_logging = %d (found = %" PRId64 ")", enable_logging, e);

  rec_err                   = RecGetRecordInt("proxy.config.body_factory.response_suppression_mode", &e);
  response_suppression_mode = ((rec_err == REC_ERR_OKAY) ? e : 0);
  all_found                 = all_found && (rec_err == REC_ERR_OKAY);
  Debug("body_factory", "response_suppression_mode = %d (found = %" PRId64 ")", response_suppression_mode, e);

  ats_scoped_str directory_of_template_sets;

  rec_err   = RecGetRecordString_Xmalloc("proxy.config.body_factory.template_sets_dir", &s);
  all_found = all_found && (rec_err == REC_ERR_OKAY);
  if (rec_err == REC_ERR_OKAY) {
    directory_of_template_sets = Layout::get()->relative(s);
    if (access(directory_of_template_sets, R_OK) < 0) {
      Warning("Unable to access() directory '%s': %d, %s", (const char *)directory_of_template_sets, errno, strerror(errno));
      Warning(" Please set 'proxy.config.body_factory.template_sets_dir' ");
    }
  }

  Debug("body_factory", "directory_of_template_sets = '%s' ", (const char *)directory_of_template_sets);

  ats_free(s);

  if (!all_found) {
    Warning("config changed, but can't fetch all proxy.config.body_factory values");
  }

  /////////////////////////////////////////////
  // clear out previous template hash tables //
  /////////////////////////////////////////////

  nuke_template_tables();

  /////////////////////////////////////////////////////////////
  // at this point, the body hash table is gone, so we start //
  // building a new one, by scanning the template directory. //
  /////////////////////////////////////////////////////////////

  if (directory_of_template_sets) {
    table_of_sets = load_sets_from_directory(directory_of_template_sets);
  }

  unlock();
}

////////////////////////////////////////////////////////////////////////
//
// class HttpBodyFactory
//
////////////////////////////////////////////////////////////////////////

HttpBodyFactory::HttpBodyFactory()
{
  int i;
  bool status, no_registrations_failed;

  ////////////////////////////////////
  // initialize first-time defaults //
  ////////////////////////////////////
  ink_mutex_init(&mutex);

  //////////////////////////////////////////////////////
  // set up management configuration-change callbacks //
  //////////////////////////////////////////////////////

  static const char *config_record_names[] = {
    "proxy.config.body_factory.enable_customizations", "proxy.config.body_factory.enable_logging",
    "proxy.config.body_factory.template_sets_dir", "proxy.config.body_factory.response_suppression_mode", nullptr};

  no_registrations_failed = true;
  for (i = 0; config_record_names[i] != nullptr; i++) {
    status = REC_RegisterConfigUpdateFunc(config_record_names[i], config_callback, (void *)this);
    if (status != REC_ERR_OKAY) {
      Warning("couldn't register variable '%s', is %s up to date?", config_record_names[i], ts::filename::RECORDS);
    }
    no_registrations_failed = no_registrations_failed && (status == REC_ERR_OKAY);
  }

  if (no_registrations_failed == false) {
    Warning("couldn't setup all body_factory callbacks, disabling body_factory");
  } else {
    Debug("body_factory", "all callbacks established successfully");
    callbacks_established = true;
    reconfigure();
  }
}

HttpBodyFactory::~HttpBodyFactory()
{
  // FIX: need to implement destructor
  table_of_sets.reset(nullptr);
}

// LOCKING: must be called with lock taken
char *
HttpBodyFactory::fabricate(StrList *acpt_language_list, StrList *acpt_charset_list, const char *type, HttpTransact::State *context,
                           int64_t *buffer_length_return, const char **content_language_return, const char **content_charset_return,
                           const char **set_return)
{
  char *buffer;
  const char *pType = context->txn_conf->body_factory_template_base;
  const char *set;
  char template_base[PATH_NAME_MAX];

  if (set_return) {
    *set_return = "???";
  }
  *content_language_return = nullptr;
  *content_charset_return  = nullptr;

  Debug("body_factory", "calling fabricate(type '%s')", type);
  *buffer_length_return = 0;

  // if error body suppressed, return NULL
  if (is_response_suppressed(context)) {
    Debug("body_factory", "  error suppression enabled, returning NULL template");
    return nullptr;
  }
  // if custom error pages are disabled, return NULL
  if (!enable_customizations) {
    Debug("body_factory", "  customization disabled, returning NULL template");
    return nullptr;
  }

  // what set should we use (language target if enable_customizations == 2)
  if (enable_customizations == 2) {
    set = determine_set_by_language(acpt_language_list, acpt_charset_list);
  } else if (enable_customizations == 3) {
    set = determine_set_by_host(context);
  } else if (is_response_body_precluded(context->http_return_code)) {
    return nullptr;
  } else {
    set = "default";
  }
  if (set_return) {
    *set_return = set;
  }

  HttpBodyTemplate *t   = nullptr;
  HttpBodySet *body_set = nullptr;
  if (pType != nullptr && 0 != *pType && 0 != strncmp(pType, "NONE", 4)) {
    sprintf(template_base, "%s_%s", pType, type);
    t = find_template(set, template_base, &body_set);
    // Check for default alternate.
    if (t == nullptr) {
      sprintf(template_base, "%s_default", pType);
      t = find_template(set, template_base, &body_set);
    }
  }

  // Check for base customizations if specializations didn't work.
  if (t == nullptr) {
    if (is_response_body_precluded(context->http_return_code)) {
      return nullptr;
    }
    t = find_template(set, type, &body_set); // this executes if the template_base is wrong and doesn't exist
  }

  if (t == nullptr) {
    Debug("body_factory", "  can't find template, returning NULL template");
    return nullptr;
  }

  *content_language_return = body_set->content_language;
  *content_charset_return  = body_set->content_charset;

  // build the custom error page
  buffer = t->build_instantiated_buffer(context, buffer_length_return);
  return buffer;
}

// LOCKING: must be called with lock taken
const char *
HttpBodyFactory::determine_set_by_host(HttpTransact::State *context)
{
  const char *set;
  int host_len = context->hh_info.host_len;
  char host_buffer[host_len + 1];
  strncpy(host_buffer, context->hh_info.request_host, host_len);
  host_buffer[host_len] = '\0';
  if (auto it = table_of_sets->find(host_buffer); it != table_of_sets->end()) {
    set = it->first.c_str();
  } else {
    set = "default";
  }
  return set;
}

const char *
HttpBodyFactory::determine_set_by_language(std::unique_ptr<BodySetTable> &table_of_sets, StrList *acpt_language_list,
                                           StrList *acpt_charset_list, float *Q_best_ptr, int *La_best_ptr, int *Lc_best_ptr,
                                           int *I_best_ptr)
{
  float Q, Ql, Qc, Q_best;
  int I, Idummy, I_best;
  int La, Lc, La_best, Lc_best;
  int is_the_default_set;
  const char *set_best;

  set_best = "default";
  Q_best   = 0.00001;
  La_best  = 0;
  Lc_best  = INT_MAX;
  I_best   = INT_MAX;

  Debug("body_factory_determine_set", "  INITIAL: [ set_best='%s', Q=%g, La=%d, Lc=%d, I=%d ]", set_best, Q_best, La_best, Lc_best,
        I_best);

  // FIX: eliminate this special case (which doesn't work anyway), by properly
  //      handling empty lists and empty pieces in match_accept_XXX

  // if no Accept-Language or Accept-Charset, just return default
  if ((acpt_language_list->count == 0) && (acpt_charset_list->count == 0)) {
    Q_best = 1;
    Debug("body_factory_determine_set", "  no constraints => returning '%s'", set_best);
    goto done;
  }

  if (table_of_sets) {
    ///////////////////////////////////////////
    // loop over set->body-types hash table //
    ///////////////////////////////////////////
    for (const auto &it : *table_of_sets.get()) {
      const char *set_name         = it.first.c_str();
      HttpBodySetRawData *body_set = it.second;

      if ((it.first.empty()) || (body_set->table_of_pages == nullptr)) {
        continue;
      }

      //////////////////////////////////////////////////////////////////////
      // Take this error page language and match it against the           //
      // Accept-Language string passed in, to evaluate the match          //
      // quality.  Disable wildcard processing so we use "default"        //
      // if no set explicitly matches.  We also get back the index        //
      // of the match and the length of the match.                        //
      //                                                                  //
      // We optimize the match in a couple of ways:                       //
      //   (a) if Q is better ==> wins, else if tie,                      //
      //   (b) if accept tag length La is bigger ==> wins, else if tie,   //
      //   (c) if content tag length Lc is smaller ==> wins, else if tie, //
      //   (d) if index position I is smaller ==> wins                    //
      //////////////////////////////////////////////////////////////////////

      is_the_default_set = (strcmp(set_name, "default") == 0);

      Debug("body_factory_determine_set", "  --- SET: %-8s (Content-Language '%s', Content-Charset '%s')", set_name,
            body_set->content_language, body_set->content_charset);

      // if no Accept-Language hdr at all, treat as a wildcard that
      // slightly prefers "default".
      if (acpt_language_list->count == 0) {
        Ql = (is_the_default_set ? 1.0001 : 1.000);
        La = 0;
        Lc = INT_MAX;
        I  = 1;
        Debug("body_factory_determine_set", "      SET: [%-8s] A-L not present => [ Ql=%g, La=%d, Lc=%d, I=%d ]", set_name, Ql, La,
              Lc, I);
      } else {
        Lc = strlen(body_set->content_language);
        Ql = HttpCompat::match_accept_language(body_set->content_language, Lc, acpt_language_list, &La, &I, true);
        Debug("body_factory_determine_set", "      SET: [%-8s] A-L match value => [ Ql=%g, La=%d, Lc=%d, I=%d ]", set_name, Ql, La,
              Lc, I);
      }

      /////////////////////////////////////////////////////////////
      // Take this error page language and match it against the  //
      // Accept-Charset string passed in, to evaluate the match  //
      // quality.  Disable wildcard processing so that only      //
      // explicit values match.  (Many browsers will send along  //
      // "*" with all lists, and we really don't want to send    //
      // strange character sets for these people --- we'd rather //
      // use a more portable "default" set.  The index value we  //
      // get back isn't used, because it's a little hard to know //
      // how to tradeoff language indices vs. charset indices.   //
      // If someone cares, we could surely work charset indices  //
      // into the sorting computation below.                     //
      /////////////////////////////////////////////////////////////

      // if no Accept-Charset hdr at all, treat as a wildcard that
      // slightly prefers "default".
      if (acpt_charset_list->count == 0) {
        Qc     = (is_the_default_set ? 1.0001 : 1.000);
        Idummy = 1;
        Debug("body_factory_determine_set", "      SET: [%-8s] A-C not present => [ Qc=%g ]", set_name, Qc);
      } else {
        Qc = HttpCompat::match_accept_charset(body_set->content_charset, strlen(body_set->content_charset), acpt_charset_list,
                                              &Idummy, true);
        Debug("body_factory_determine_set", "      SET: [%-8s] A-C match value => [ Qc=%g ]", set_name, Qc);
      }

      /////////////////////////////////////////////////////////////////
      // We get back the Q value, the matching field length, and the //
      // matching field index.  We sort by largest Q value, but if   //
      // there is a Q tie, we sub sort on longer matching length,    //
      // and if there is a tie on Q and L, we sub sort on position   //
      // index, preferring values earlier in Accept-Language list.   //
      /////////////////////////////////////////////////////////////////

      Q = std::min(Ql, Qc);

      //////////////////////////////////////////////////////////
      // normally the Q for default pages should be slightly  //
      // less than for normal pages, but default pages should //
      // always match to a slight level, in case everything   //
      // else doesn't match (matches with Q=0).               //
      //////////////////////////////////////////////////////////

      if (is_the_default_set) {
        Q = Q + -0.00005;
        if (Q < 0.00001) {
          Q = 0.00001;
        }
      }

      Debug("body_factory_determine_set", "      NEW: [ set='%s', Q=%g, La=%d, Lc=%d, I=%d ]", set_name, Q, La, Lc, I);
      Debug("body_factory_determine_set", "      OLD: [ set='%s', Q=%g, La=%d, Lc=%d, I=%d ]", set_best, Q_best, La_best, Lc_best,
            I_best);

      if (((Q > Q_best)) || ((Q == Q_best) && (La > La_best)) || ((Q == Q_best) && (La == La_best) && (Lc < Lc_best)) ||
          ((Q == Q_best) && (La == La_best) && (Lc == Lc_best) && (I < I_best))) {
        Q_best   = Q;
        La_best  = La;
        Lc_best  = Lc;
        I_best   = I;
        set_best = set_name;

        Debug("body_factory_determine_set", "   WINNER: [ set_best='%s', Q=%g, La=%d, Lc=%d, I=%d ]", set_best, Q_best, La_best,
              Lc_best, I_best);
      } else {
        Debug("body_factory_determine_set", "    LOSER: [ set_best='%s', Q=%g, La=%d, Lc=%d, I=%d ]", set_best, Q_best, La_best,
              Lc_best, I_best);
      }
    }
  }

done:

  *Q_best_ptr  = Q_best;
  *La_best_ptr = La_best;
  *Lc_best_ptr = Lc_best;
  *I_best_ptr  = I_best;
  return set_best;
}

// LOCKING: must be called with lock taken
const char *
HttpBodyFactory::determine_set_by_language(StrList *acpt_language_list, StrList *acpt_charset_list)
{
  float Q_best;
  const char *set_best;
  int La_best, Lc_best, I_best;

  set_best = determine_set_by_language(table_of_sets, acpt_language_list, acpt_charset_list, &Q_best, &La_best, &Lc_best, &I_best);

  return set_best;
}

// LOCKING: must be called with lock taken
HttpBodyTemplate *
HttpBodyFactory::find_template(const char *set, const char *type, HttpBodySet **body_set_return)
{
  Debug("body_factory", "calling find_template(%s,%s)", set, type);

  *body_set_return = nullptr;

  if (table_of_sets == nullptr || !set || !type) {
    return nullptr;
  }
  if (auto it = table_of_sets->find(set); it != table_of_sets->end()) {
    HttpBodySet *body_set = static_cast<HttpBodySet *>(it->second);
    if (body_set->table_of_pages == nullptr) {
      return nullptr;
    }

    if (auto it_page = body_set->table_of_pages->find(type); it_page != body_set->table_of_pages->end()) {
      HttpBodyTemplate *t = it_page->second;
      if ((t == nullptr) || (!t->is_sane())) {
        return nullptr;
      }
      *body_set_return = body_set;

      Debug("body_factory", "find_template(%s,%s) -> (file %s, length %" PRId64 ", lang '%s', charset '%s')", set, type,
            t->template_pathname, t->byte_count, body_set->content_language, body_set->content_charset);

      return t;
    }
  }
  Debug("body_factory", "find_template(%s,%s) -> NULL", set, type);

  return nullptr;
}

// LOCKING: must be called with lock taken
bool
HttpBodyFactory::is_response_suppressed(HttpTransact::State *context)
{
  // Since a tunnel may not always be an SSL connection,
  // we may want to return an error message.
  // Even if it's an SSL connection, it won't cause any harm
  // as the connection is going to be closed anyway.
  /*
     if (context->client_info.port_attribute == SERVER_PORT_BLIND_TUNNEL) {
     // Blind SSL tunnels always suppress error messages
     return true;
     } else
   */
  if (response_suppression_mode == 0) {
    return false;
  } else if (response_suppression_mode == 1) {
    return true;
  } else if (response_suppression_mode == 2) {
    return context->request_data.internal_txn;
  } else {
    return false;
  }
}

// LOCKING: must be called with lock taken
void
HttpBodyFactory::nuke_template_tables()
{
  if (table_of_sets) {
    Debug("body_factory", "deleting pre-existing template tables");
  } else {
    Debug("body_factory", "no pre-existing template tables");
  }
  if (table_of_sets) {
    ///////////////////////////////////////////
    // loop over set->body-types hash table //
    ///////////////////////////////////////////
    for (const auto &it : *table_of_sets.get()) {
      HttpBodySet *body_set = static_cast<HttpBodySet *>(it.second);
      ink_assert(body_set->is_sane());
      if (body_set->table_of_pages) {
        ///////////////////////////////////////////
        // loop over body-types->body hash table //
        ///////////////////////////////////////////
        for (const auto &it_page : *body_set->table_of_pages.get()) {
          delete it_page.second;
        }
        body_set->table_of_pages.reset(nullptr);
      }
      delete body_set;
    }
    table_of_sets.reset(nullptr);
  }
}

// LOCKING: must be called with lock taken
std::unique_ptr<HttpBodyFactory::BodySetTable>
HttpBodyFactory::load_sets_from_directory(char *set_dir)
{
  DIR *dir;
  struct dirent *dirEntry;
  std::unique_ptr<HttpBodyFactory::BodySetTable> new_table_of_sets;

  if (set_dir == nullptr) {
    return nullptr;
  }

  Debug("body_factory", "load_sets_from_directory(%s)", set_dir);

  //////////////////////////////////////////////////
  // try to open the requested template directory //
  //////////////////////////////////////////////////

  dir = opendir(set_dir);
  if (dir == nullptr) {
    Warning("can't open response template directory '%s' (%s)", set_dir, (strerror(errno) ? strerror(errno) : "unknown reason"));
    Warning("no response templates --- using default error pages");
    return nullptr;
  }

  new_table_of_sets.reset(new HttpBodyFactory::BodySetTable);

  //////////////////////////////////////////
  // loop over each language subdirectory //
  //////////////////////////////////////////

  while ((dirEntry = readdir(dir))) {
    int status;
    struct stat stat_buf;
    char subdir[MAXPATHLEN + 1];

    //////////////////////////////////////////////////////
    // ensure a subdirectory, and not starting with '.' //
    //////////////////////////////////////////////////////

    if ((dirEntry->d_name)[0] == '.') {
      continue;
    }

    ink_filepath_make(subdir, sizeof(subdir), set_dir, dirEntry->d_name);
    status = stat(subdir, &stat_buf);
    if (status != 0) {
      continue; // can't stat
    }

    if (!S_ISDIR(stat_buf.st_mode)) {
      continue; // not a dir
    }

    ///////////////////////////////////////////////////////////
    // at this point, 'subdir' might be a valid template dir //
    ///////////////////////////////////////////////////////////

    HttpBodySet *body_set = load_body_set_from_directory(dirEntry->d_name, subdir);
    if (body_set != nullptr) {
      Debug("body_factory", "  %s -> %p", dirEntry->d_name, body_set);
      new_table_of_sets->emplace(dirEntry->d_name, body_set);
    }
  }

  closedir(dir);

  return new_table_of_sets;
}

// LOCKING: must be called with lock taken
HttpBodySet *
HttpBodyFactory::load_body_set_from_directory(char *set_name, char *tmpl_dir)
{
  DIR *dir;
  int status;
  struct stat stat_buf;
  struct dirent *dirEntry;
  char path[MAXPATHLEN + 1];
  static const char BASED_DEFAULT[] = "_default";

  ////////////////////////////////////////////////
  // ensure we can open tmpl_dir as a directory //
  ////////////////////////////////////////////////

  Debug("body_factory", "  load_body_set_from_directory(%s)", tmpl_dir);
  dir = opendir(tmpl_dir);
  if (dir == nullptr) {
    return nullptr;
  }

  /////////////////////////////////////////////
  // ensure a .body_factory_info file exists //
  /////////////////////////////////////////////

  ink_filepath_make(path, sizeof(path), tmpl_dir, ".body_factory_info");
  status = stat(path, &stat_buf);
  if ((status < 0) || !S_ISREG(stat_buf.st_mode)) {
    Warning("Missing .body_factory_info in %s.  Not loading body_factory templates", tmpl_dir);
    closedir(dir);
    return nullptr;
  }
  Debug("body_factory", "    found '%s'", path);

  /////////////////////////////////////////////////////////////////
  // create body set, and loop over template files, loading them //
  /////////////////////////////////////////////////////////////////

  HttpBodySet *body_set = new HttpBodySet;
  body_set->init(set_name, tmpl_dir);

  Debug("body_factory", "  body_set = %p (set_name '%s', lang '%s', charset '%s')", body_set, body_set->set_name,
        body_set->content_language, body_set->content_charset);

  while ((dirEntry = readdir(dir))) {
    HttpBodyTemplate *tmpl;
    size_t d_len = strlen(dirEntry->d_name);

    ///////////////////////////////////////////////////////////////
    // all template files must have a file name of the form      //
    // - <type>#<subtype>                                        //
    // - <base>_<type>#<subtype>                                 //
    // - <base>_default   [based default]                        //
    // - default          [global default]                       //
    ///////////////////////////////////////////////////////////////

    if (!(nullptr != strchr(dirEntry->d_name, '#') || (0 == strcmp(dirEntry->d_name, "default")) ||
          (d_len >= sizeof(BASED_DEFAULT) && 0 == strcmp(dirEntry->d_name + d_len - (sizeof(BASED_DEFAULT) - 1), BASED_DEFAULT)))) {
      continue;
    }

    snprintf(path, sizeof(path), "%s/%s", tmpl_dir, dirEntry->d_name);
    status = stat(path, &stat_buf);
    if (status != 0) {
      continue; // can't stat
    }

    if (!S_ISREG(stat_buf.st_mode)) {
      continue; // not a file
    }

    ////////////////////////////////
    // read in this template file //
    ////////////////////////////////

    tmpl = new HttpBodyTemplate();
    if (!tmpl->load_from_file(tmpl_dir, dirEntry->d_name)) {
      delete tmpl;
    } else {
      Debug("body_factory", "      %s -> %p", dirEntry->d_name, tmpl);
      body_set->set_template_by_name(dirEntry->d_name, tmpl);
    }
  }

  closedir(dir);
  return body_set;
}

////////////////////////////////////////////////////////////////////////
//
// class HttpBodySet
//
////////////////////////////////////////////////////////////////////////

HttpBodySet::HttpBodySet()
{
  magic = HTTP_BODY_SET_MAGIC;

  set_name         = nullptr;
  content_language = nullptr;
  content_charset  = nullptr;

  table_of_pages = nullptr;
}

HttpBodySet::~HttpBodySet()
{
  ats_free(set_name);
  ats_free(content_language);
  ats_free(content_charset);
  table_of_pages.reset(nullptr);
}

int
HttpBodySet::init(char *set, char *dir)
{
  int fd, lineno, lines_added = 0;
  char info_path[MAXPATHLEN];

  char buffer[1024], name[1025], value[1024];

  ink_filepath_make(info_path, sizeof(info_path), dir, ".body_factory_info");
  fd = open(info_path, O_RDONLY);
  if (fd < 0) {
    return -1;
  }

  this->set_name = ats_strdup(set);
  this->table_of_pages.reset(new TemplateTable);

  lineno = 0;

  while (true) {
    char *name_s, *name_e, *value_s, *value_e, *hash;

    ++lineno;
    int bytes = ink_file_fd_readline(fd, sizeof(buffer), buffer);
    if (bytes <= 0) {
      break;
    }

    ///////////////////////////////////////////////
    // chop anything on and after first '#' sign //
    ///////////////////////////////////////////////

    hash = index(buffer, '#');
    if (hash) {
      *hash = '\0';

      ////////////////////////////////
      // find start and end of name //
      ////////////////////////////////
    }
    name_s = buffer;
    while (*name_s && ParseRules::is_wslfcr(*name_s)) {
      ++name_s;
    }

    name_e = name_s;
    while (*name_e && ParseRules::is_http_field_name(*name_e)) {
      ++name_e;
    }

    if (name_s == name_e) {
      continue; // blank line
    }

    /////////////////////////////////
    // find start and end of value //
    /////////////////////////////////

    value_s = name_e;
    while (*value_s && (ParseRules::is_wslfcr(*value_s))) {
      ++value_s;
    }
    if (*value_s != ':') {
      Warning("ignoring invalid body factory info line #%d in %s", lineno, info_path);
      continue;
    }
    ++value_s; // skip the colon
    while (*value_s && (ParseRules::is_wslfcr(*value_s))) {
      ++value_s;
    }
    value_e = buffer + strlen(buffer) - 1;
    while ((value_e > value_s) && ParseRules::is_wslfcr(*(value_e - 1))) {
      --value_e;
    }

    /////////////////////////////////
    // insert line into hash table //
    /////////////////////////////////

    memcpy(name, name_s, name_e - name_s);
    name[name_e - name_s] = '\0';

    memcpy(value, value_s, value_e - value_s);
    value[value_e - value_s] = '\0';

    //////////////////////////////////////////////////
    // so far, we only support 2 pieces of metadata //
    //////////////////////////////////////////////////

    if (strcasecmp(name, "Content-Language") == 0) {
      ats_free(this->content_language);
      this->content_language = ats_strdup(value);
    } else if (strcasecmp(name, "Content-Charset") == 0) {
      ats_free(this->content_charset);
      this->content_charset = ats_strdup(value);
    }
  }

  ////////////////////////////////////////////////////
  // fill in default language & charset, if not set //
  ////////////////////////////////////////////////////

  if (!this->content_language) {
    if (strcmp(set, "default") == 0) {
      this->content_language = ats_strdup("en");
    } else {
      this->content_language = ats_strdup(set);
    }
  }
  if (!this->content_charset) {
    this->content_charset = ats_strdup("utf-8");
  }

  close(fd);
  return lines_added;
}

HttpBodyTemplate *
HttpBodySet::get_template_by_name(const char *name)
{
  Debug("body_factory", "    calling get_template_by_name(%s)", name);

  if (table_of_pages == nullptr) {
    return nullptr;
  }

  if (auto it = table_of_pages->find(name); it != table_of_pages->end()) {
    HttpBodyTemplate *t = it->second;
    if ((t == nullptr) || (!t->is_sane())) {
      return nullptr;
    }
    Debug("body_factory", "    get_template_by_name(%s) -> (file %s, length %" PRId64 ")", name, t->template_pathname,
          t->byte_count);
    return t;
  }
  Debug("body_factory", "    get_template_by_name(%s) -> NULL", name);
  return nullptr;
}

void
HttpBodySet::set_template_by_name(const char *name, HttpBodyTemplate *t)
{
  if (name) {
    table_of_pages->emplace(name, t);
  }
}

////////////////////////////////////////////////////////////////////////
//
// class HttpBodyTemplate
//
////////////////////////////////////////////////////////////////////////

HttpBodyTemplate::HttpBodyTemplate()
{
  magic             = HTTP_BODY_TEMPLATE_MAGIC;
  byte_count        = 0;
  template_buffer   = nullptr;
  template_pathname = nullptr;
}

HttpBodyTemplate::~HttpBodyTemplate()
{
  reset();
}

void
HttpBodyTemplate::reset()
{
  ats_free(template_buffer);
  template_buffer = nullptr;
  byte_count      = 0;
  ats_free(template_pathname);
}

int
HttpBodyTemplate::load_from_file(char *dir, char *file)
{
  int fd, status;
  int64_t bytes_read;
  struct stat stat_buf;
  char path[MAXPATHLEN + 1];
  char *new_template_buffer;
  int64_t new_byte_count;

  ////////////////////////////////////
  // ensure this is actually a file //
  ////////////////////////////////////

  snprintf(path, sizeof(path), "%s/%s", dir, file);
  // coverity[fs_check_call]
  status = stat(path, &stat_buf);
  if (status != 0) {
    return 0;
  }
  if (!S_ISREG(stat_buf.st_mode)) {
    return 0;
  }

  ///////////////////
  // open the file //
  ///////////////////

  // coverity[toctou]
  fd = open(path, O_RDONLY);
  if (fd < 0) {
    return 0;
  }

  ////////////////////////////////////////
  // read in the template file contents //
  ////////////////////////////////////////

  new_byte_count                      = stat_buf.st_size;
  new_template_buffer                 = static_cast<char *>(ats_malloc(new_byte_count + 1));
  bytes_read                          = read(fd, new_template_buffer, new_byte_count);
  new_template_buffer[new_byte_count] = '\0';
  close(fd);

  ///////////////////////////
  // check for read errors //
  ///////////////////////////

  if (bytes_read != new_byte_count) {
    Warning("reading template file '%s', got %" PRId64 " bytes instead of %" PRId64 " (%s)", path, bytes_read, new_byte_count,
            (strerror(errno) ? strerror(errno) : "unknown error"));
    ats_free(new_template_buffer);
    return 0;
  }

  Debug("body_factory", "    read %" PRId64 " bytes from '%s'", new_byte_count, path);

  /////////////////////////////////
  // actually commit the changes //
  /////////////////////////////////

  reset();
  template_buffer   = new_template_buffer;
  byte_count        = new_byte_count;
  template_pathname = ats_strdup(path);

  return 1;
}

char *
HttpBodyTemplate::build_instantiated_buffer(HttpTransact::State *context, int64_t *buflen_return)
{
  char *buffer = nullptr;

  Debug("body_factory_instantiation", "    before instantiation: [%s]", template_buffer);

  LogAccess la(context->state_machine);

  buffer = resolve_logfield_string(&la, template_buffer);

  *buflen_return = ((buffer == nullptr) ? 0 : strlen(buffer));
  Debug("body_factory_instantiation", "    after instantiation: [%s]", buffer);
  Debug("body_factory", "  returning %" PRId64 " byte instantiated buffer", *buflen_return);

  return buffer;
}
