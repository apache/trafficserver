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
 *
 *  WebHttpRender.cc - html rendering/assembly
 *
 *
 ****************************************************************************/

#include "ink_platform.h"

#include "ink_hash_table.h"
#include "I_Version.h"
#include "SimpleTokenizer.h"

#include "WebGlobals.h"
#include "WebCompatibility.h"
#include "WebHttp.h"
#include "WebHttpRender.h"
#include "WebHttpSession.h"
#include "WebHttpTree.h"
#include "WebOverview.h"

#include "WebMgmtUtils.h"
#include "MgmtUtils.h"

#include "LocalManager.h"

#include "INKMgmtAPI.h"
#include "CfgContextUtils.h"
#include "MgmtSocket.h"
#include "WebConfigRender.h"

#include "MultiFile.h"
#include "../tools/ConfigAPI.h"

//-------------------------------------------------------------------------
// defines
//-------------------------------------------------------------------------

#define MAX_TMP_BUF_LEN       1024
#define MAX_ARGS              10
#define NO_RECORD             "loading..."

//-------------------------------------------------------------------------
// types
//-------------------------------------------------------------------------

typedef int (*WebHttpDisplayHandler) (WebHttpContext * whc, char *tag, char *arg);

//-------------------------------------------------------------------------
// globals
//-------------------------------------------------------------------------

static InkHashTable *g_display_bindings_ht = 0;
extern InkHashTable *g_display_config_ht;

//-------------------------------------------------------------------------
// forward declarations
//-------------------------------------------------------------------------


//-------------------------------------------------------------------------
// substitute_language
//-------------------------------------------------------------------------

int
substitute_language(WebHttpContext * whc, char *tag)
{
  return HtmlRndrText(whc->response_bdy, whc->lang_dict_ht, (HtmlId) tag);
}

//-------------------------------------------------------------------------
// WebHttpGetTopLevelRndrFile_Xmalloc
//-------------------------------------------------------------------------
char *
WebHttpGetTopLevelRndrFile_Xmalloc(WebHttpContext * whc)
{
  char *file = NULL;

  if (whc->top_level_render_file) {
    file = xstrdup(whc->top_level_render_file);
  } else if (whc->request->getFile()) {
    file = xstrdup(whc->request->getFile());
  }
  return file;
}

//-------------------------------------------------------------------------
// WebHttpGetIntFromQuery
//-------------------------------------------------------------------------
void
WebHttpGetIntFromQuery(WebHttpContext * whc, const char *tag, int *active_id)
{
  char *active_str;

  if (whc->query_data_ht && ink_hash_table_lookup(whc->query_data_ht, tag, (void **) &active_str)) {
    *active_id = atoi(active_str);
  } else {
    *active_id = 0;
  }
}

#if TS_HAS_WEBUI
//-------------------------------------------------------------------------
// handle_alarm_object
//-------------------------------------------------------------------------

static int
handle_alarm_object(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  overviewGenerator->generateAlarmsTable(whc);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_alarm_summary_object
//-------------------------------------------------------------------------

static int
handle_alarm_summary_object(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  overviewGenerator->generateAlarmsSummary(whc);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_config_table_object
//-------------------------------------------------------------------------
// Displays rules of config file in table format. The arg specifies
// the "/configure/f_xx_config.ink" of the config file; this arg is used
// to determine which file table to render. Each of the
// writeXXConfigTable function writes the html for displaying all the rules
// by using an INKCfgContext to read all the rules and converting
// each rule into a row of the table.
static int
handle_config_table_object(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  INKFileNameT type;
  int err = WEB_HTTP_ERR_OKAY;

  // arg == f_xxx_config.ink (aka. HTML_XXX_CONFIG_FILE)
  if (ink_hash_table_lookup(g_display_config_ht, arg, (void **) &type)) {
    switch (type) {
    case INK_FNAME_CACHE_OBJ:
      err = writeCacheConfigTable(whc);
      break;
    case INK_FNAME_HOSTING:
      err = writeHostingConfigTable(whc);
      break;
    case INK_FNAME_ICP_PEER:
      err = writeIcpConfigTable(whc);
      break;
    case INK_FNAME_IP_ALLOW:
      err = writeIpAllowConfigTable(whc);
      break;
    case INK_FNAME_MGMT_ALLOW:
      err = writeMgmtAllowConfigTable(whc);
      break;
    case INK_FNAME_PARENT_PROXY:
      err = writeParentConfigTable(whc);
      break;
    case INK_FNAME_PARTITION:
      err = writePartitionConfigTable(whc);
      break;
    case INK_FNAME_REMAP:
      err = writeRemapConfigTable(whc);
      break;
    case INK_FNAME_SOCKS:
      err = writeSocksConfigTable(whc);
      break;
    case INK_FNAME_SPLIT_DNS:
      err = writeSplitDnsConfigTable(whc);
      break;
    case INK_FNAME_UPDATE_URL:
      err = writeUpdateConfigTable(whc);
      break;
    case INK_FNAME_VADDRS:
      err = writeVaddrsConfigTable(whc);
      break;
    default:
      break;
    }
  } else {
    mgmt_log(stderr, "[handle_config_table_object] invalid config file configurator %s\n", arg);
    err = WEB_HTTP_ERR_FAIL;
  }

  return err;
}

//-------------------------------------------------------------------------
// handle_help_config_link
//-------------------------------------------------------------------------
static int
handle_help_config_link(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  INKFileNameT type;
  char *ink_file = NULL;

  if (ink_hash_table_lookup(whc->query_data_ht, HTML_CONFIG_FILE_TAG, (void **) &ink_file) ||
      ink_hash_table_lookup(whc->post_data_ht, HTML_CONFIG_FILE_TAG, (void **) &ink_file)) {

    // look up the INKFileNameT type
    if (ink_hash_table_lookup(g_display_config_ht, ink_file, (void **) &type)) {
      switch (type) {
      case INK_FNAME_CACHE_OBJ:
        whc->response_bdy->copyFrom(HTML_HELP_LINK_CACHE, strlen(HTML_HELP_LINK_CACHE));
        break;
      case INK_FNAME_HOSTING:
        whc->response_bdy->copyFrom(HTML_HELP_LINK_HOSTING, strlen(HTML_HELP_LINK_HOSTING));
        break;
      case INK_FNAME_ICP_PEER:
        whc->response_bdy->copyFrom(HTML_HELP_LINK_ICP, strlen(HTML_HELP_LINK_ICP));
        break;
      case INK_FNAME_IP_ALLOW:
        whc->response_bdy->copyFrom(HTML_HELP_LINK_IP_ALLOW, strlen(HTML_HELP_LINK_IP_ALLOW));
        break;
      case INK_FNAME_MGMT_ALLOW:
        whc->response_bdy->copyFrom(HTML_HELP_LINK_MGMT_ALLOW, strlen(HTML_HELP_LINK_MGMT_ALLOW));
        break;
      case INK_FNAME_PARENT_PROXY:
        whc->response_bdy->copyFrom(HTML_HELP_LINK_PARENT, strlen(HTML_HELP_LINK_PARENT));
        break;
      case INK_FNAME_PARTITION:
        whc->response_bdy->copyFrom(HTML_HELP_LINK_PARTITION, strlen(HTML_HELP_LINK_PARTITION));
        break;
      case INK_FNAME_REMAP:
        whc->response_bdy->copyFrom(HTML_HELP_LINK_REMAP, strlen(HTML_HELP_LINK_REMAP));
        break;
      case INK_FNAME_SOCKS:
        whc->response_bdy->copyFrom(HTML_HELP_LINK_SOCKS, strlen(HTML_HELP_LINK_SOCKS));
        break;
      case INK_FNAME_SPLIT_DNS:
        whc->response_bdy->copyFrom(HTML_HELP_LINK_SPLIT_DNS, strlen(HTML_HELP_LINK_SPLIT_DNS));
        break;
      case INK_FNAME_UPDATE_URL:
        whc->response_bdy->copyFrom(HTML_HELP_LINK_UPDATE, strlen(HTML_HELP_LINK_UPDATE));
        break;
      case INK_FNAME_VADDRS:
        whc->response_bdy->copyFrom(HTML_HELP_LINK_VADDRS, strlen(HTML_HELP_LINK_VADDRS));
        break;
      default:
        break;
      }
    }
  } else {
    mgmt_log(stderr, "[handle_help_config_link] failed to get top_level_render_file");
  }

  return WEB_HTTP_ERR_OKAY;
}


//-------------------------------------------------------------------------
// handle_dynamic_javascript
//-------------------------------------------------------------------------
// This creates the Javascript Rule object and its properties;
// Must open the config file and see how many rules; create a javascript Rule
// object so that it can be inserted into the ruleList object; writes the
// functions which must interact between the config data form. All
// the config file specific javascript goes here. This javascript is actually
// stored in the "/configure/f_XXX_config.ink" file.
//
static int
handle_dynamic_javascript(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  int err = WEB_HTTP_ERR_OKAY;
  INKFileNameT type;
  char *ink_file;
  char *ink_file_path = NULL;
  char *file_buf = NULL;
  int file_size;

  // the configurator page can be invoked from two places so it
  // can retreive the "filename" information from 2 possible places:
  // 1) as a GET request (HTML_SUBMIT_CONFIG_DISPLAY) when click on "Edit file" button
  // 2) refreshing page after clicking  "Apply" button (HTML_SUBMIT_UPDATE_CONFIG)
  if (ink_hash_table_lookup(whc->query_data_ht, HTML_CONFIG_FILE_TAG, (void **) &ink_file) ||
      ink_hash_table_lookup(whc->post_data_ht, HTML_CONFIG_FILE_TAG, (void **) &ink_file)) {

    ink_file_path = WebHttpAddDocRoot_Xmalloc(whc, ink_file);
    file_buf = 0;
    if (WebFileImport_Xmalloc(ink_file_path, &file_buf, &file_size) != WEB_HTTP_ERR_OKAY) {
      goto Lnot_found;
    }
    // copy file's contents into html buffer
    whc->response_bdy->copyFrom(file_buf, strlen(file_buf));

    // look up the INKFileNameT type
    if (ink_hash_table_lookup(g_display_config_ht, ink_file, (void **) &type)) {
      switch (type) {
      case INK_FNAME_CACHE_OBJ:
        err = writeCacheRuleList(whc->response_bdy);
        break;
      case INK_FNAME_HOSTING:
        err = writeHostingRuleList(whc->response_bdy);
        break;
      case INK_FNAME_ICP_PEER:
        err = writeIcpRuleList(whc->response_bdy);
        break;
      case INK_FNAME_IP_ALLOW:
        err = writeIpAllowRuleList(whc->response_bdy);
        break;
      case INK_FNAME_MGMT_ALLOW:
        err = writeMgmtAllowRuleList(whc->response_bdy);
        break;
      case INK_FNAME_PARENT_PROXY:
        err = writeParentRuleList(whc->response_bdy);
        break;
      case INK_FNAME_PARTITION:
        err = writePartitionRuleList(whc->response_bdy);
        break;
      case INK_FNAME_REMAP:
        err = writeRemapRuleList(whc->response_bdy);
        break;
      case INK_FNAME_SOCKS:
        err = writeSocksRuleList(whc->response_bdy);
        break;
      case INK_FNAME_SPLIT_DNS:
        err = writeSplitDnsRuleList(whc->response_bdy);
        break;
      case INK_FNAME_UPDATE_URL:
        err = writeUpdateRuleList(whc->response_bdy);
        break;
      case INK_FNAME_VADDRS:
        err = writeVaddrsRuleList(whc->response_bdy);
        break;
      default:
        break;
      }

      goto Ldone;
    }
  }

Lnot_found:                    // missing file
  if (ink_file_path)
    mgmt_log(stderr, "[handle_dynamic_javascript] requested file not found (%s)", ink_file_path);
  whc->response_hdr->setStatus(STATUS_NOT_FOUND);
  WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
  err = WEB_HTTP_ERR_REQUEST_ERROR;
  goto Ldone;

Ldone:
  if (ink_file_path)
    xfree(ink_file_path);
  if (file_buf)
    xfree(file_buf);
  return err;
}


//-------------------------------------------------------------------------
// handle_config_input_form
//-------------------------------------------------------------------------
// Writes the html for the section of the Config File Editor that requires
// user input/modifications (eg. has the INSERT, MODIFY.... buttons).
// Corresponds to the "writeConfigForm" tag.
// Each config file has different fields so each form will have different
// fields on the form (refer to data in corresponding Ele structs).
static int
handle_config_input_form(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  char *ink_file;
  char *frecord;
  INKFileNameT type;
  int err = WEB_HTTP_ERR_OKAY;

  if (ink_hash_table_lookup(whc->query_data_ht, HTML_CONFIG_FILE_TAG, (void **) &ink_file) ||
      ink_hash_table_lookup(whc->post_data_ht, HTML_CONFIG_FILE_TAG, (void **) &ink_file)) {

    if (ink_hash_table_lookup(g_display_config_ht, ink_file, (void **) &type)) {

      // need to have the file's record name on the Config File Editor page
      // so we can check if restart is required when users "Apply" the change
      if (ink_hash_table_lookup(whc->query_data_ht, "frecord", (void **) &frecord)) {
        HtmlRndrInput(whc->response_bdy, HTML_CSS_NONE, HTML_TYPE_HIDDEN, "frecord", frecord, NULL, NULL);
        ink_hash_table_delete(whc->query_data_ht, "frecord");
        xfree(frecord);
      } else if (ink_hash_table_lookup(whc->post_data_ht, "frecord", (void **) &frecord)) {
        HtmlRndrInput(whc->response_bdy, HTML_CSS_NONE, HTML_TYPE_HIDDEN, "frecord", frecord, NULL, NULL);
        ink_hash_table_delete(whc->post_data_ht, "frecord");
        xfree(frecord);
      }

      switch (type) {
      case INK_FNAME_CACHE_OBJ:
        err = writeCacheConfigForm(whc);
        break;
      case INK_FNAME_HOSTING:
        err = writeHostingConfigForm(whc);
        break;
      case INK_FNAME_ICP_PEER:
        err = writeIcpConfigForm(whc);
        break;
      case INK_FNAME_IP_ALLOW:
        err = writeIpAllowConfigForm(whc);
        break;
      case INK_FNAME_MGMT_ALLOW:
        err = writeMgmtAllowConfigForm(whc);
        break;
      case INK_FNAME_PARENT_PROXY:
        err = writeParentConfigForm(whc);
        break;
      case INK_FNAME_PARTITION:
        err = writePartitionConfigForm(whc);
        break;
      case INK_FNAME_REMAP:
        err = writeRemapConfigForm(whc);
        break;
      case INK_FNAME_SOCKS:
        err = writeSocksConfigForm(whc);
        break;
      case INK_FNAME_SPLIT_DNS:
        err = writeSplitDnsConfigForm(whc);
        break;
      case INK_FNAME_UPDATE_URL:
        err = writeUpdateConfigForm(whc);
        break;
      case INK_FNAME_VADDRS:
        err = writeVaddrsConfigForm(whc);
        break;
      default:
        break;
      }
    } else {
      mgmt_log(stderr, "[handle_config_input_form] invalid config file configurator %s\n", ink_file);
      err = WEB_HTTP_ERR_FAIL;
    }
  }

  return err;
}



//-------------------------------------------------------------------------
// HtmlRndrSelectList
//-------------------------------------------------------------------------
// Creates a select list where the options are the strings passed in
// the options array. Assuming the value and text of the option are the same.
int
HtmlRndrSelectList(textBuffer * html, const char *listName, const char *options[], int numOpts)
{
  if (!listName || !options)
    return WEB_HTTP_ERR_FAIL;

  HtmlRndrSelectOpen(html, HTML_CSS_BODY_TEXT, listName, 1);

  for (int i = 0; i < numOpts; i++) {
    HtmlRndrOptionOpen(html, options[i], false);
    html->copyFrom(options[i], strlen(options[i]));     // this is option text
    HtmlRndrOptionClose(html);
  }

  HtmlRndrSelectClose(html);

  return WEB_HTTP_ERR_OKAY;
}



//-------------------------------------------------------------------------
// handle_file_edit
//-------------------------------------------------------------------------

static int
handle_file_edit(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  Rollback *rb;
  char target_file[FILE_NAME_MAX + 1];
  char *formatText;

  if (arg && varStrFromName(arg, target_file, FILE_NAME_MAX)) {
    if (configFiles->getRollbackObj(target_file, &rb)) {
      textBuffer *output = whc->response_bdy;
      textBuffer *file;
      version_t version;
      char version_str[MAX_VAR_LENGTH + 5];     // length of version + file record
      char checksum[MAX_CHECKSUM_LENGTH + 1];

      rb->acquireLock();
      version = rb->getCurrentVersion();
      if (rb->getVersion_ml(version, &file) != OK_ROLLBACK) {
        file = NULL;
      }
      rb->releaseLock();
      if (file) {
        snprintf(version_str, sizeof(version_str), "%d:%s", version, arg);
        HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, "file_version", version_str, NULL, NULL);
        fileCheckSum(file->bufPtr(), file->spaceUsed(), checksum, sizeof(checksum));
        HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, "file_checksum", checksum, NULL, NULL);
        HtmlRndrTextareaOpen(output, HTML_CSS_NONE, 70, 15, HTML_WRAP_OFF, "file_contents", false);
        formatText = substituteForHTMLChars(file->bufPtr());
        output->copyFrom(formatText, strlen(formatText));
        HtmlRndrTextareaClose(output);
        delete file;
        delete[]formatText;
      } else {
        mgmt_log(stderr, "[handle_file_edit] could not acquire/edit file [%s]", target_file);
        goto Lerror;
      }
    } else {
      mgmt_log(stderr, "[handle_file_edit] could not acquire/edit file [%s]", target_file);
      goto Lerror;
    }
  } else {
    mgmt_log(stderr, "[handle_file_edit] file record not found %s", arg);
    goto Lerror;
  }
  return WEB_HTTP_ERR_OKAY;
Lerror:
  whc->response_hdr->setStatus(STATUS_INTERNAL_SERVER_ERROR);
  WebHttpSetErrorResponse(whc, STATUS_INTERNAL_SERVER_ERROR);
  return WEB_HTTP_ERR_REQUEST_ERROR;
}

//-------------------------------------------------------------------------
// handle_include
//-------------------------------------------------------------------------

static int
handle_include(WebHttpContext * whc, char *tag, char *arg)
{
  if (arg) {
    return WebHttpRender(whc, arg);
  } else {
    mgmt_log(stderr, "[handle_include] no argument passed to <@%s ...>", tag);
    whc->response_hdr->setStatus(STATUS_NOT_FOUND);
    WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
    return WEB_HTTP_ERR_REQUEST_ERROR;
  }
}

//-------------------------------------------------------------------------
// handle_include_cgi
//-------------------------------------------------------------------------

static int
handle_include_cgi(WebHttpContext * whc, char *tag, char *arg)
{
  int err = WEB_HTTP_ERR_OKAY;
  char *cgi_path;

  if (arg) {
    whc->response_hdr->setCachable(0);
    whc->response_hdr->setStatus(STATUS_OK);
    whc->response_hdr->setContentType(TEXT_HTML);
    cgi_path = WebHttpAddDocRoot_Xmalloc(whc, arg);
    err = spawn_cgi(whc, cgi_path, NULL, false, false);
    xfree(cgi_path);
  } else {
    mgmt_log(stderr, "[handle_include_cgi] no argument passed to <@%s ...>", tag);
  }
  return err;
}

//-------------------------------------------------------------------------
// handle_overview_object
//-------------------------------------------------------------------------

static int
handle_overview_object(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  overviewGenerator->generateTable(whc);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_overview_details_object
//-------------------------------------------------------------------------

static int
handle_overview_details_object(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  int err;
  if (whc->request_state & WEB_HTTP_STATE_MORE_DETAIL)
    // if currently showing more detail, render link to show less
    err = WebHttpRender(whc, "/monitor/m_overview_details_less.ink");
  else
    err = WebHttpRender(whc, "/monitor/m_overview_details_more.ink");
  return err;
}

//-------------------------------------------------------------------------
// handle_post_data
//-------------------------------------------------------------------------

static int
handle_post_data(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  char *value;
  if (arg && whc->post_data_ht) {
    if (ink_hash_table_lookup(whc->post_data_ht, arg, (void **) &value)) {
      if (value) {
        whc->response_bdy->copyFrom(value, strlen(value));
      }
    }
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_query
//-------------------------------------------------------------------------

static int
handle_query(WebHttpContext * whc, char *tag, char *arg)
{
  char *value;
  if (arg && whc->query_data_ht) {
    if (ink_hash_table_lookup(whc->query_data_ht, arg, (void **) &value) && value) {
      whc->response_bdy->copyFrom(value, strlen(value));
    } else {
      mgmt_log(stderr, "[handle_query] invalid argument (%s) passed to <@%s ...>", arg, tag);
    }
  } else {
    mgmt_log(stderr, "[handle_query] no argument passed to <@%s ...>", tag);
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_record
//-------------------------------------------------------------------------

static int
handle_record(WebHttpContext * whc, char *tag, char *arg)
{
  char record_value[MAX_VAL_LENGTH];
  char *record_value_safe;
  char *dummy;

  if (arg != NULL) {
    if (whc->submit_warn_ht && ink_hash_table_lookup(whc->submit_warn_ht, arg, (void **) &dummy)) {
      if (whc->post_data_ht && ink_hash_table_lookup(whc->post_data_ht, arg, (void **) &record_value_safe)) {
        // copy in the value; use double quotes if there is nothing to copy
        if (record_value_safe)
          whc->response_bdy->copyFrom(record_value_safe, strlen(record_value_safe));
        else
          whc->response_bdy->copyFrom("\"\"", 2);
      }
    } else {
      if (!varStrFromName(arg, record_value, MAX_VAL_LENGTH)) {
        ink_strncpy(record_value, NO_RECORD, sizeof(record_value));
      }
      record_value_safe = substituteForHTMLChars(record_value);
      // copy in the value; use double quotes if there is nothing to copy
      if (*record_value_safe != '\0')
        whc->response_bdy->copyFrom(record_value_safe, strlen(record_value_safe));
      else
        whc->response_bdy->copyFrom("\"\"", 2);
      delete[]record_value_safe;
    }
  } else {
    mgmt_log(stderr, "[handle_record] no argument passed to <@%s ...>", tag);
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_record_version
//-------------------------------------------------------------------------

static int
handle_record_version(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  int id;
  char id_str[256];
  id = RecGetRecordUpdateCount(RECT_CONFIG);
  if (id < 0) {
    mgmt_log(stderr, "[handle_record_version] unable to CONFIG records update count");
    return WEB_HTTP_ERR_OKAY;
  }
  //fix me --> lmgmt->record_data->pid
  snprintf(id_str, sizeof(id_str), "%ld:%d", lmgmt->record_data->pid, id);
  whc->response_bdy->copyFrom(id_str, strlen(id_str));
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_summary_object
//-------------------------------------------------------------------------

static int
handle_summary_object(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  char dateBuf[40];
  char tmpBuf[256];
  time_t uptime_secs, d, h, m, s;

  time_t upTime;

  textBuffer *output = whc->response_bdy;
  MgmtHashTable *dict_ht = whc->lang_dict_ht;

  if (lmgmt->proxy_running == 1) {
    HtmlRndrText(output, dict_ht, HTML_ID_STATUS_ACTIVE);
    HtmlRndrBr(output);
    upTime = lmgmt->proxy_started_at;
    uptime_secs = time(NULL) - upTime;

    d = uptime_secs / (60 * 60 * 24);
    uptime_secs -= d * (60 * 60 * 24);
    h = uptime_secs / (60 * 60);
    uptime_secs -= h * (60 * 60);
    m = uptime_secs / (60);
    uptime_secs -= m * (60);
    s = uptime_secs;

    char *r = ink_ctime_r(&upTime, dateBuf);
    if (r != NULL) {
      HtmlRndrText(output, dict_ht, HTML_ID_UP_SINCE);
      snprintf(tmpBuf, sizeof(tmpBuf), ": %s (%d:%02d:%02d:%02d)", dateBuf, (int32_t)d, (int32_t)h, (int32_t)m, (int32_t)s);
      output->copyFrom(tmpBuf, strlen(tmpBuf));
      HtmlRndrBr(output);
    }
  } else {
    HtmlRndrText(output, dict_ht, HTML_ID_STATUS_INACTIVE);
    HtmlRndrBr(output);
  }

  HtmlRndrText(output, dict_ht, HTML_ID_CLUSTERING);
  output->copyFrom(": ", 2);
  switch (lmgmt->ccom->cluster_type) {
  case FULL_CLUSTER:
    HtmlRndrText(output, dict_ht, HTML_ID_ENABLED);
    break;
  case MGMT_CLUSTER:
    HtmlRndrText(output, dict_ht, HTML_ID_MANAGEMENT_ONLY);
    break;
  case NO_CLUSTER:
    HtmlRndrText(output, dict_ht, HTML_ID_OFF);
    break;
  case CLUSTER_INVALID:
  default:
    HtmlRndrText(output, dict_ht, HTML_ID_UNKNOWN);
    break;
  }
  HtmlRndrBr(output);

  return WEB_HTTP_ERR_OKAY;

}

//-------------------------------------------------------------------------
// handle_tab_object
//-------------------------------------------------------------------------

static int
handle_tab_object(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  int err = WEB_HTTP_ERR_OKAY;
  int active_mode;

  // render main tab object
  WebHttpGetIntFromQuery(whc, "mode", &active_mode);
  if ((err = WebHttpRenderTabs(whc->response_bdy, active_mode)) != WEB_HTTP_ERR_OKAY) {
    mgmt_log(stderr, "[handle_tab_object] failed to render mode tabs");
  }
  return err;
}

//-------------------------------------------------------------------------
// handle_html_tab_object
//-------------------------------------------------------------------------

static int
handle_html_tab_object(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  char *file = NULL;
  int err = WEB_HTTP_ERR_OKAY;
  int active_tab;

  // render item tab object
  if ((file = WebHttpGetTopLevelRndrFile_Xmalloc(whc))) {
    WebHttpGetIntFromQuery(whc, "tab", &active_tab);
    if ((err = WebHttpRenderHtmlTabs(whc->response_bdy, file, active_tab) != WEB_HTTP_ERR_OKAY)) {
      mgmt_log(stderr, "[handle_html_tab_object] failed to render link tabs");
    }
  } else {
    mgmt_log(stderr, "[handle_html_tab_object] failed to get top_level_render_file");
  }
  if (file)
    xfree(file);
  return err;
}

//-------------------------------------------------------------------------
// handle_mgmt_auth_object
//-------------------------------------------------------------------------

static int
handle_mgmt_auth_object(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  int user_count;
  INKCfgContext ctx;
  INKCfgIterState ctx_state;
  char *ctx_key;
  INKAdminAccessEle *ele;
  textBuffer *output = whc->response_bdy;
  char tmp[32];

  ctx = INKCfgContextCreate(INK_FNAME_ADMIN_ACCESS);

  if (INKCfgContextGet(ctx) != INK_ERR_OKAY)
    printf("ERROR READING FILE\n");
  INKCfgContextGetFirst(ctx, &ctx_state);

  user_count = 0;
  ele = (INKAdminAccessEle *) INKCfgContextGetFirst(ctx, &ctx_state);
  while (ele) {
    // render table row
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    snprintf(tmp, sizeof(tmp), "user:%d", user_count);
    HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, tmp, ele->user, NULL, NULL);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "33%", NULL, 0);
    output->copyFrom(ele->user, strlen(ele->user));
    HtmlRndrTdClose(output);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "33%", NULL, 0);
    snprintf(tmp, sizeof(tmp), "access:%d", user_count);
    HtmlRndrSelectOpen(output, HTML_CSS_BODY_TEXT, tmp, 1);
    snprintf(tmp, sizeof(tmp), "%d", INK_ACCESS_NONE);
    HtmlRndrOptionOpen(output, tmp, ele->access == INK_ACCESS_NONE);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_AUTH_NO_ACCESS);
    HtmlRndrOptionClose(output);
    snprintf(tmp, sizeof(tmp), "%d", INK_ACCESS_MONITOR);
    HtmlRndrOptionOpen(output, tmp, ele->access == INK_ACCESS_MONITOR);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_AUTH_MONITOR);
    HtmlRndrOptionClose(output);
    snprintf(tmp, sizeof(tmp), "%d", INK_ACCESS_MONITOR_VIEW);
    HtmlRndrOptionOpen(output, tmp, ele->access == INK_ACCESS_MONITOR_VIEW);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_AUTH_MONITOR_VIEW);
    HtmlRndrOptionClose(output);
    snprintf(tmp, sizeof(tmp), "%d", INK_ACCESS_MONITOR_CHANGE);
    HtmlRndrOptionOpen(output, tmp, ele->access == INK_ACCESS_MONITOR_CHANGE);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_AUTH_MONITOR_CHANGE);
    HtmlRndrOptionClose(output);
    HtmlRndrSelectClose(output);
    HtmlRndrTdClose(output);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "33%", NULL, 0);
    output->copyFrom(ele->password, strlen(ele->password));
    HtmlRndrTdClose(output);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
    snprintf(tmp, sizeof(tmp), "delete:%d", user_count);
    HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_CHECKBOX, tmp, ele->user, NULL, NULL);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
    // move on
    ele = (INKAdminAccessEle *) INKCfgContextGetNext(ctx, &ctx_state);
    user_count++;
  }
  // what? no users?
  if (user_count == 0) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 4);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_NO_ADDITIONAL_USERS);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }
  // store context
  ctx_key = WebHttpMakeSessionKey_Xmalloc();
  WebHttpSessionStore(ctx_key, (void *) ctx, InkMgmtApiCtxDeleter);
  // add hidden form tags
  snprintf(tmp, sizeof(tmp), "%d", user_count);
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, "user_count", tmp, NULL, NULL);
  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_HIDDEN, "session_id", ctx_key, NULL, NULL);
  xfree(ctx_key);

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_tree_object
//-------------------------------------------------------------------------

static int
handle_tree_object(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  int err = WEB_HTTP_ERR_OKAY;
  char *file = NULL;

  if ((err = WebHttpRender(whc, HTML_TREE_HEADER_FILE)) != WEB_HTTP_ERR_OKAY)
    goto Ldone;

  if ((file = WebHttpGetTopLevelRndrFile_Xmalloc(whc))) {
    if ((err = WebHttpRenderJsTree(whc->response_bdy, file)) != WEB_HTTP_ERR_OKAY)
      goto Ldone;

  } else {
    mgmt_log(stderr, "[handle_tree_object] failed to get top_level_render_file");
  }
  err = WebHttpRender(whc, HTML_TREE_FOOTER_FILE);

Ldone:
  if (file)
    xfree(file);
  return err;
}

//-------------------------------------------------------------------------
// handle_vip_object
//-------------------------------------------------------------------------

static int
handle_vip_object(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  // Hash table iteration variables
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;
  char *tmp;
  char *tmpCopy;

  // Local binding variables
  char localHostName[256];
  int hostNameLen;

  // Variables for peer bindings
  ExpandingArray peerBindings(100, true);
  Tokenizer spaceTok(" ");
  char *peerHostName;
  bool hostnameFound;
  int numPeerBindings = 0;

  textBuffer *output = whc->response_bdy;

  // different behavior is vip is enabled or not
  if (lmgmt->virt_map->enabled > 0) {

    // Get the local hostname
    varStrFromName("proxy.node.hostname", localHostName, 256);
    hostNameLen = strlen(localHostName);

    ink_mutex_acquire(&(lmgmt->ccom->mutex));

    // First dump the local VIP map
    for (entry = ink_hash_table_iterator_first(lmgmt->virt_map->our_map, &iterator_state);
         entry != NULL; entry = ink_hash_table_iterator_next(lmgmt->virt_map->our_map, &iterator_state)) {

      tmp = (char *) ink_hash_table_entry_key(lmgmt->virt_map->our_map, entry);

      HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);
      HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
      output->copyFrom(localHostName, hostNameLen);
      HtmlRndrTdClose(output);
      HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
      output->copyFrom(tmp, strlen(tmp));
      HtmlRndrTdClose(output);
      HtmlRndrTrClose(output);

    }

    // Now, dump the peer map and make a copy of it
    for (entry = ink_hash_table_iterator_first(lmgmt->virt_map->ext_map, &iterator_state);
         entry != NULL; entry = ink_hash_table_iterator_next(lmgmt->virt_map->ext_map, &iterator_state)) {
      tmp = (char *) ink_hash_table_entry_key(lmgmt->virt_map->ext_map, entry);
      tmpCopy = xstrdup(tmp);
      peerBindings.addEntry(tmpCopy);
      numPeerBindings++;
    }
    ink_mutex_release(&(lmgmt->ccom->mutex));

    // Output the peer map
    for (int i = 0; i < numPeerBindings; i++) {
      tmp = (char *) peerBindings[i];
      if (spaceTok.Initialize(tmp, SHARE_TOKS) == 2) {

        // Resolve the peer hostname
        // FIXME: is this thread safe?? this whole function used to be
        // called under the overviewGenerator lock
        peerHostName = overviewGenerator->resolvePeerHostname(spaceTok[1]);

        if (peerHostName == NULL) {
          hostnameFound = false;
          peerHostName = (char *) spaceTok[1];
        } else {
          hostnameFound = true;
          // Chop off the domain name
          tmp = strchr(peerHostName, '.');
          if (tmp != NULL) {
            *tmp = '\0';
          }
        }

        HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);
        HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
        output->copyFrom(peerHostName, strlen(peerHostName));
        HtmlRndrTdClose(output);
        HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
        output->copyFrom(spaceTok[0], strlen(spaceTok[0]));
        HtmlRndrTdClose(output);
        HtmlRndrTrClose(output);

        if (hostnameFound == true) {
          xfree(peerHostName);
        }
      }
    }

  } else {

    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 2);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_VIP_DISABLED);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);

  }

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_checked
//-------------------------------------------------------------------------

static int
handle_checked(WebHttpContext * whc, char *tag, char *arg)
{
  const char checkStr[] = "checked";
  char record_value[MAX_VAL_LENGTH];

  Tokenizer backslashTok("\\");
  if (backslashTok.Initialize(arg, SHARE_TOKS) == 2) {
    if (varStrFromName(backslashTok[0], record_value, MAX_VAL_LENGTH - 1)) {
      if (strncmp(backslashTok[1], record_value, strlen(backslashTok[1])) == 0) {
        // replace the tag with "checked" string
        whc->response_bdy->copyFrom(checkStr, strlen(checkStr));
      }
    } else {
      mgmt_log(stderr, "[handle_checked] cannot find record %s", backslashTok[0]);
    }
  } else {
    mgmt_log(stderr, "[handle_checked] invalid number of arguments passed to " "<@%s ...>", tag);
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_action_checked
//-------------------------------------------------------------------------

static int
handle_action_checked(WebHttpContext * whc, char *tag, char *arg)
{
  const char checkStr[] = "checked";
  char *action;

  Tokenizer backslashTok("\\");
  if (backslashTok.Initialize(arg, SHARE_TOKS) == 2) {
    if (whc->post_data_ht) {
      if (ink_hash_table_lookup(whc->post_data_ht, "action", (void **) &action)) {
        if (strncmp(backslashTok[1], action, strlen(backslashTok[1])) == 0) {
          // replace the tag with "checked" string
          whc->response_bdy->copyFrom(checkStr, strlen(checkStr));
        }
      }
    } else {
      if (strncmp(backslashTok[1], "view_last", strlen(backslashTok[1])) == 0) {
        // default "checked" option
        whc->response_bdy->copyFrom(checkStr, strlen(checkStr));
      }
    }
  } else {
    mgmt_log(stderr, "[handle_checked] invalid number of arguments passed to " "<@%s ...>", tag);
  }
  return WEB_HTTP_ERR_OKAY;
}




//-------------------------------------------------------------------------
// handle_select
//-------------------------------------------------------------------------
static int
handle_select(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  if (strcmp(arg, "snapshot") == 0) {
    configFiles->displaySnapOption(whc->response_bdy);
  }
  return WEB_HTTP_ERR_OKAY;
}


//-------------------------------------------------------------------------
// handle_password_object
//-------------------------------------------------------------------------
static int
handle_password_object(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  RecString pwd_file;
  RecGetRecordString_Xmalloc(arg, &pwd_file);

  if (pwd_file) {
    HtmlRndrInput(whc->response_bdy, HTML_CSS_BODY_TEXT, "password", arg, FAKE_PASSWORD, NULL, NULL);
  } else {
    HtmlRndrInput(whc->response_bdy, HTML_CSS_BODY_TEXT, "password", arg, NULL, NULL, NULL);
  }

  return WEB_HTTP_ERR_OKAY;
}


//-------------------------------------------------------------------------
// handle_select_*_logs
//-------------------------------------------------------------------------
static bool
readable(const char *file, MgmtInt * size)
{
  WebHandle h_file;
  if ((h_file = WebFileOpenR(file)) == WEB_HANDLE_INVALID) {
    return false;
  }
  *size = WebFileGetSize(h_file);
  WebFileClose(h_file);
  return true;
}

static bool
selected_log(WebHttpContext * whc, const char *file)
{
  char *selected_file;
  InkHashTable *ht = whc->post_data_ht;
  if (ht) {
    if (ink_hash_table_lookup(ht, "logfile", (void **) &selected_file)) {
      if (strcmp(selected_file, file) == 0) {
        return true;
      }
    }
  }
  return false;
}

static void
render_option(textBuffer * output, const char *value, char *display, bool selected)
{
  HtmlRndrOptionOpen(output, value, selected);
  output->copyFrom(display, strlen(display));
  HtmlRndrOptionClose(output);
}

//-------------------------------------------------------------------------
// handle_select_system_logs
//-------------------------------------------------------------------------
static int
handle_select_system_logs(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  const char *syslog_path = NULL;
  const char *syslog = NULL;
  char tmp[MAX_TMP_BUF_LEN + 1];
  char tmp2[MAX_TMP_BUF_LEN + 1];
  char tmp3[MAX_TMP_BUF_LEN + 1];
  int i;
  bool selected = false;
  textBuffer *output = whc->response_bdy;
  MgmtInt fsize;;

  // define the name of syslog in different OS
#if defined(linux)
  syslog = "messages";
  syslog_path = "/var/log/";
#endif

  // display all syslog in the select box
  if (syslog_path) {
    // check if 'message' is readable
    snprintf(tmp, MAX_TMP_BUF_LEN, "%s%s", syslog_path, syslog);
    if (readable(tmp, &fsize)) {
      selected = selected_log(whc, tmp);
      bytesFromInt(fsize, tmp3);
      snprintf(tmp2, MAX_TMP_BUF_LEN, "%s  [%s]", syslog, tmp3);
      render_option(output, tmp, tmp2, selected);
    }
    // check if 'message.n' are exist
    for (i = 0; i < 10; i++) {
      snprintf(tmp, MAX_TMP_BUF_LEN, "%s%s.%d", syslog_path, syslog, i);
      if (readable(tmp, &fsize)) {
        selected = selected_log(whc, tmp);
        bytesFromInt(fsize, tmp3);
        snprintf(tmp2, MAX_TMP_BUF_LEN, "%s.%d  [%s]", syslog, i, tmp3);
        render_option(output, tmp, tmp2, selected);
      }
    }
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_select_access_logs
//-------------------------------------------------------------------------
static int
handle_select_access_logs(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  char *logdir;
  char *logfile;
  char tmp[MAX_TMP_BUF_LEN + 1];
  char tmp2[MAX_TMP_BUF_LEN + 1];
  char tmp3[MAX_TMP_BUF_LEN + 1];
  RecInt fsize;
  bool selected = false;
  textBuffer *output = whc->response_bdy;

  struct dirent *dent;
  DIR *dirp;
  DIR *dirp2;
  struct stat s;
  int err;

  // open all files in the log directory except traffic.out
  ink_assert(RecGetRecordString_Xmalloc("proxy.config.output.logfile", &logfile)
             == REC_ERR_OKAY);

  if ((err = stat(system_log_dir, &s)) < 0) {
    ink_assert(RecGetRecordString_Xmalloc("proxy.config.log.logfile_dir", &logdir)
	       == REC_ERR_OKAY);
    if ((err = stat(logdir, &s)) < 0) {
      // Try 'system_root_dir/var/log/trafficserver' directory
      snprintf(system_log_dir, sizeof(system_log_dir), "%s%s%s%s%s%s%s",
               system_root_dir, DIR_SEP,"var",DIR_SEP,"log",DIR_SEP,"trafficserver");
      if ((err = stat(system_log_dir, &s)) < 0) {
        mgmt_elog("unable to stat() log dir'%s': %d %d, %s\n",
                system_log_dir, err, errno, strerror(errno));
        mgmt_elog("please set 'proxy.config.log.logfile_dir'\n");
        //_exit(1);
      }
    } else {
      ink_strncpy(system_log_dir,logdir,sizeof(system_log_dir));
    }
  }

  if ((dirp = opendir(system_log_dir))) {
    while ((dent = readdir(dirp)) != NULL) {
      // exclude traffic.out*
      if (strncmp(logfile, dent->d_name, strlen(logfile)) != 0) {
        snprintf(tmp, MAX_TMP_BUF_LEN, "%s%s%s", system_log_dir, DIR_SEP, dent->d_name);
        if ((dirp2 = opendir(tmp))) {
          // exclude directory
          closedir(dirp2);

        } else if (strncmp(dent->d_name, ".", 1) == 0 &&
                   strncmp(dent->d_name + strlen(dent->d_name) - 5, ".meta", 5) == 0) {
          // exclude .*.meta files

        } else if (strncmp(dent->d_name, "traffic_server.core", 19) == 0) {
          // exclude traffic_server.core*

        } else {
          if (readable(tmp, &fsize)) {
            selected = selected_log(whc, tmp);
            bytesFromInt(fsize, tmp3);
            snprintf(tmp2, MAX_TMP_BUF_LEN, "%s  [%s]", dent->d_name, tmp3);
            render_option(output, tmp, tmp2, selected);
          }
        }
      }
    }
    closedir(dirp);
  }

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_select_debug_logs
//-------------------------------------------------------------------------
static int
handle_select_debug_logs(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  char tmp[MAX_TMP_BUF_LEN + 1];
  char tmp2[MAX_TMP_BUF_LEN + 1];
  char tmp3[MAX_TMP_BUF_LEN + 1];
  MgmtInt fsize;
  textBuffer *output = whc->response_bdy;
  bool selected = false;
  char *logdir;
  char *logfile;
  int i;

  struct dirent *dent;
  DIR *dirp;
  const char *debug_logs[] = {
    "diags.log",
    "manager.log",
    "lm.log"
  };
  struct stat s;
  int err;


  ink_assert(RecGetRecordString_Xmalloc("proxy.config.output.logfile", &logfile)
             == REC_ERR_OKAY);

  if ((err = stat(system_log_dir, &s)) < 0) {
    ink_assert(RecGetRecordString_Xmalloc("proxy.config.log.logfile_dir", &logdir)
	       == REC_ERR_OKAY);
    if ((err = stat(logdir, &s)) < 0) {
      // Try 'system_root_dir/var/log/trafficserver' directory
      snprintf(system_log_dir, sizeof(system_log_dir), "%s%s%s%s%s%s%s",
               system_root_dir, DIR_SEP,"var",DIR_SEP,"log",DIR_SEP,"trafficserver");
      if ((err = stat(system_log_dir, &s)) < 0) {
        mgmt_elog("unable to stat() log dir'%s': %d %d, %s\n",
                system_log_dir, err, errno, strerror(errno));
        mgmt_elog("please set 'proxy.config.log.logfile_dir'\n");
        //_exit(1);
      }
    } else {
      ink_strncpy(system_log_dir,logdir,sizeof(system_log_dir));
    }
  }

  // traffic.out*
  if ((dirp = opendir(system_log_dir))) {
    while ((dent = readdir(dirp)) != NULL) {
      if (strncmp(logfile, dent->d_name, strlen(logfile)) == 0) {
        snprintf(tmp, MAX_TMP_BUF_LEN, "%s%s%s", system_log_dir, DIR_SEP, dent->d_name);
        if (readable(tmp, &fsize)) {
          selected = selected_log(whc, tmp);
          bytesFromInt(fsize, tmp3);
          snprintf(tmp2, MAX_TMP_BUF_LEN, "%s  [%s]", dent->d_name, tmp3);
          render_option(output, tmp, tmp2, selected);
        }
      }
    }
    closedir(dirp);
  }
  // others
  for (i = 0; i < 3; i++) {
    if (readable(debug_logs[i], &fsize)) {
      selected = selected_log(whc, debug_logs[i]);
      bytesFromInt(fsize, tmp3);
      snprintf(tmp2, MAX_TMP_BUF_LEN, "%s  [%s]", debug_logs[i], tmp3);
      render_option(output, debug_logs[i], tmp2, selected);
    }
  }

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_log_action
//-------------------------------------------------------------------------

static int
handle_log_action(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  textBuffer *output = whc->response_bdy;
  InkHashTable *ht = whc->post_data_ht;
  char *logfile;
  char *action;
  char *nlines;
  char *substring;
  char *action_arg;
  char *script_path;
  bool truncated;

  if (arg) {
    const char *args[MAX_ARGS + 1];
    for (int i = 0; i < MAX_ARGS + 1; i++)
      args[i] = NULL;

    if (!ht)
      goto Ldone;               // render not from submission
    if (!ink_hash_table_lookup(ht, "logfile", (void **) &logfile))
      goto Ldone;
    if (!ink_hash_table_lookup(ht, "action", (void **) &action))
      goto Ldone;
    if (!logfile || !action)
      goto Ldone;
    if (strcmp(logfile, "default") == 0)
      goto Ldone;

    if (strcmp(action, "view_all") == 0) {
      action_arg = NULL;

    } else if (strcmp(action, "view_last") == 0) {
      if (!ink_hash_table_lookup(ht, "nlines", (void **) &nlines))
        goto Ldone;
      if (!nlines)
        goto Ldone;
      action_arg = nlines;

    } else if (strcmp(action, "view_subset") == 0) {
      if (!ink_hash_table_lookup(ht, "substring", (void **) &substring))
        goto Ldone;
      if (!substring)
        goto Ldone;
      action_arg = substring;

    } else {
      Debug("web2", "[handle_log_action] unknown action: %s", action);
      goto Ldone;
    }
    script_path = WebHttpAddDocRoot_Xmalloc(whc, arg);
    args[0] = script_path;
    args[1] = logfile;
    args[2] = action;
    args[3] = action_arg;

    // grey bar
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, "2", 0);
    HtmlRndrSpace(output, 1);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);

    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
    HtmlRndrTableOpen(output, "100%", 0, 0, 1, NULL);
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_READONLY_TEXT, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, "100%", NULL, 0);
    HtmlRndrTextareaOpen(output, HTML_CSS_BODY_READONLY_TEXT, 75, 15, HTML_WRAP_OFF, NULL, true);
    processSpawn(&args[0], NULL, NULL, whc->response_bdy, false, true, &truncated);
    HtmlRndrTextareaClose(output);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
    if (truncated) {
      HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
      HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
      HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_FILE_TRUNCATED);
      HtmlRndrTdClose(output);
      HtmlRndrTrClose(output);
    }
    HtmlRndrTableClose(output);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
    xfree(script_path);

  } else {
    Debug("web2", "[handle_log_action] no argument passed.");
  }

Ldone:
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_version
//-------------------------------------------------------------------------

static int
handle_version(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  whc->response_bdy->copyFrom(PACKAGE_VERSION, strlen(PACKAGE_VERSION));
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_clear_cluster_stats
//-------------------------------------------------------------------------
static int
handle_clear_cluster_stats(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  textBuffer *output = whc->response_bdy;

  RecInt cluster_type = 0;      // current SSL value, enabled/disabled

  if (RecGetRecordInt("proxy.local.cluster.type", &cluster_type) != REC_ERR_OKAY)
    mgmt_log(stderr, "[handle_clear_cluster_stat] Error: Unable to get cluster type config variable\n");

  // only display button for full or mgmt clustering
  if (cluster_type == 1 || cluster_type == 2) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, "2", 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CLEAR_CLUSTER_STAT);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrInput(output, HTML_CSS_CONFIGURE_BUTTON, "submit", "clear_cluster_stats", " Clear ", NULL, NULL);
    HtmlRndrTdClose(output);
    HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_HELP, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
    HtmlRndrUlOpen(output);
    HtmlRndrLi(output);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_CLEAR_CLUSTER_STAT_HELP);
    HtmlRndrUlClose(output);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_submit_error_msg
//-------------------------------------------------------------------------

static int
handle_submit_error_msg(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  if (whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN || whc->request_state & WEB_HTTP_STATE_SUBMIT_NOTE) {
    textBuffer *output = whc->response_bdy;
    HtmlRndrTableOpen(output, "100%", 0, 0, 10);
    HtmlRndrTrOpen(output, HTML_CSS_WARNING_COLOR, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, NULL, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, "30", 0);

    if (whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN) {
      HtmlRndrSpanOpen(output, HTML_CSS_RED_LINKS);
      output->copyFrom(whc->submit_warn->bufPtr(), whc->submit_warn->spaceUsed());
      HtmlRndrSpanClose(output);
    }
    if (whc->request_state & WEB_HTTP_STATE_SUBMIT_NOTE) {
      HtmlRndrSpanOpen(output, HTML_CSS_BLUE_LINKS);
      output->copyFrom(whc->submit_note->bufPtr(), whc->submit_note->spaceUsed());
      HtmlRndrSpanClose(output);
    }
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
    HtmlRndrTableClose(output);
  }

  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_help_link
//-------------------------------------------------------------------------
static int
handle_help_link(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  char *file = NULL;
  char *link;

  if ((file = WebHttpGetTopLevelRndrFile_Xmalloc(whc))) {
    link = (char *) WebHttpTreeReturnHelpLink(file);
    if (link != NULL) {
      whc->response_bdy->copyFrom(link, strlen(link));
    } else {
      whc->response_bdy->copyFrom(HTML_DEFAULT_HELP_FILE, strlen(HTML_DEFAULT_HELP_FILE));
    }
  } else {
    mgmt_log(stderr, "[handle_help_link] failed to get top_level_render_file");
  }

  if (file)
    xfree(file);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_submit_error_flg
//-------------------------------------------------------------------------

static int
handle_submit_error_flg(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  if (whc->request_state & WEB_HTTP_STATE_SUBMIT_WARN) {
    char *dummy;
    if (arg && ink_hash_table_lookup(whc->submit_warn_ht, arg, (void **) &dummy)) {
      textBuffer *output = whc->response_bdy;
      HtmlRndrSpanOpen(output, HTML_CSS_RED_LABEL);
      HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_SUBMIT_WARN_FLG);
      HtmlRndrSpace(output, 1);
      HtmlRndrSpanClose(output);
    }
  }
  if (whc->request_state & WEB_HTTP_STATE_SUBMIT_NOTE) {
    char *dummy;
    if (arg && ink_hash_table_lookup(whc->submit_note_ht, arg, (void **) &dummy)) {
      textBuffer *output = whc->response_bdy;
      HtmlRndrSpanOpen(output, HTML_CSS_BLUE_LABEL);
      HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_SUBMIT_NOTE_FLG);
      HtmlRndrSpace(output, 1);
      HtmlRndrSpanClose(output);
    }
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_link
//-------------------------------------------------------------------------

static int
handle_link(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);

  if (arg) {
    char* link = WebHttpGetLink_Xmalloc(arg);
    whc->response_bdy->copyFrom(link, strlen(link));
    xfree(link);
  } else {
    mgmt_log(stderr, "[handle_link] no arg specified");
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_link_file
//-------------------------------------------------------------------------

static int
handle_link_file(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  char *file = NULL;

  if ((file = WebHttpGetTopLevelRndrFile_Xmalloc(whc))) {
    whc->response_bdy->copyFrom(file, strlen(file));
  } else {
    mgmt_log(stderr, "[handle_link_file] failed to get top_level_render_file");
  }
  if (file)
    xfree(file);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_link_query
//-------------------------------------------------------------------------

static int
handle_link_query(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  char *file = NULL;
  char *query;

  if ((file = WebHttpGetTopLevelRndrFile_Xmalloc(whc))) {
    query = WebHttpGetLinkQuery_Xmalloc(file);
    whc->response_bdy->copyFrom(query, strlen(query));
    xfree(query);
  } else {
    mgmt_log(stderr, "[handle_link_query] failed to get top_level_render_file");
  }
  if (file)
    xfree(file);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_cache_query
//-------------------------------------------------------------------------

static int
handle_cache_query(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  textBuffer *output = whc->response_bdy;
  char *cache_op;
  char *url;
  char *cqr = whc->cache_query_result;
  char *cqr_tmp1;
  char *cqr_tmp2;
  char tmp[MAX_TMP_BUF_LEN + 1];
  int alt_count = 0;
  int size = 0;
  InkHashTable *ht;

  ht = whc->query_data_ht;
  if (ht) {
    if (ink_hash_table_lookup(ht, "url_op", (void **) &cache_op) && ink_hash_table_lookup(ht, "url", (void **) &url)) {

      // blue bar
      HtmlRndrTableOpen(output, "100%", 0, 0, 3, NULL);
      HtmlRndrTrOpen(output, HTML_CSS_SECONDARY_COLOR, HTML_ALIGN_NONE);
      HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "100%", NULL, 0);
      HtmlRndrSpace(output, 1);
      HtmlRndrTdClose(output);
      HtmlRndrTrClose(output);
      HtmlRndrTableClose(output);

      if (url && cqr) {
        if ((cqr = strstr(cqr, "<CACHE_INFO status=\"")) && (cqr_tmp1 = strchr(cqr + 20, '"'))) {
          cqr += 20;
          if (strncmp(cqr, "succeeded", cqr_tmp1 - cqr) == 0) {
            // cache hit
            if (strcmp(cache_op, "Lookup") == 0) {

              // found out number of alternates
              if ((cqr = strstr(cqr, "count=\""))) {
                cqr += 7;
                alt_count = ink_atoi(cqr);
              }

              HtmlRndrTableOpen(output, "100%", 0, 0, 10, NULL);
              HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
              HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
              HtmlRndrTableOpen(output, "100%", 1, 0, 0, "#CCCCCC");
              HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
              HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);

              HtmlRndrTableOpen(output, "100%", 0, 0, 5, NULL);
              // document general information
              HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
              HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, "2", 2);
              HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_GENERAL_INFO);
              HtmlRndrTdClose(output);
              HtmlRndrTrClose(output);
              // document URL
              HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
              HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_TOP, NULL, NULL, 0);
              HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_DOCUMENT);
              HtmlRndrTdClose(output);
              HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
              output->copyFrom(url, strlen(url));
              HtmlRndrTdClose(output);
              HtmlRndrTrClose(output);
              // number of alternates
              HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
              HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_TOP, NULL, NULL, 0);
              HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_ALTERNATE_NUM);
              HtmlRndrTdClose(output);
              HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
              snprintf(tmp, MAX_TMP_BUF_LEN, "%d", alt_count);
              output->copyFrom(tmp, strlen(tmp));
              HtmlRndrTdClose(output);
              HtmlRndrTrClose(output);

              for (int i = 0; i < alt_count; i++) {
                // alternate information
                HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
                HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL_SMALL, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, "2", 2);
                HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_ALTERNATE);
                snprintf(tmp, MAX_TMP_BUF_LEN, " %d", i);
                output->copyFrom(tmp, strlen(tmp));
                HtmlRndrTdClose(output);
                HtmlRndrTrClose(output);
                // request sent time
                if ((cqr_tmp1 = strstr(cqr, "<REQ_SENT_TIME size=\"")) && (cqr_tmp2 = strstr(cqr, "</REQ_SENT_TIME>"))) {
                  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
                  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_TOP, NULL, NULL, 0);
                  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_REQ_TIME);
                  HtmlRndrTdClose(output);
                  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
                  size = atoi(cqr_tmp1 + 21);
                  output->copyFrom(cqr_tmp2 - size, size);
                  HtmlRndrTdClose(output);
                  HtmlRndrTrClose(output);
                }
                // request header
                if ((cqr_tmp1 = strstr(cqr, "<REQUEST_HDR size=\"")) && (cqr_tmp2 = strstr(cqr, "</REQUEST_HDR>"))) {
                  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
                  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_TOP, NULL, NULL, 0);
                  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_REQ_HEADER);
                  HtmlRndrTdClose(output);
                  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
                  HtmlRndrPreOpen(output, HTML_CSS_BODY_TEXT, NULL);
                  size = atoi(cqr_tmp1 + 19);
                  output->copyFrom(cqr_tmp2 - size, size);
                  HtmlRndrPreClose(output);
                  HtmlRndrTdClose(output);
                  HtmlRndrTrClose(output);
                }
                // response receive time
                if ((cqr_tmp1 = strstr(cqr, "<RES_RECV_TIME size=\"")) && (cqr_tmp2 = strstr(cqr, "</RES_RECV_TIME>"))) {
                  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
                  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_TOP, NULL, NULL, 0);
                  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_RPN_TIME);
                  HtmlRndrTdClose(output);
                  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
                  size = atoi(cqr_tmp1 + 21);
                  output->copyFrom(cqr_tmp2 - size, size);
                  HtmlRndrTdClose(output);
                  HtmlRndrTrClose(output);
                }
                // response header
                if ((cqr_tmp1 = strstr(cqr, "<RESPONSE_HDR size=\"")) && (cqr_tmp2 = strstr(cqr, "</RESPONSE_HDR>"))) {
                  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
                  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_TOP, NULL, NULL, 0);
                  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_RPN_HEADER);
                  HtmlRndrTdClose(output);
                  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
                  HtmlRndrPreOpen(output, HTML_CSS_BODY_TEXT, NULL);
                  size = atoi(cqr_tmp1 + 20);
                  output->copyFrom(cqr_tmp2 - size, size);
                  HtmlRndrPreClose(output);
                  HtmlRndrTdClose(output);
                  HtmlRndrTrClose(output);
                }
              }
              HtmlRndrTableClose(output);
              HtmlRndrTdClose(output);
              HtmlRndrTrClose(output);
              HtmlRndrTableClose(output);
              HtmlRndrTdClose(output);
              HtmlRndrTrClose(output);
              HtmlRndrTableClose(output);

              // blue bar with delete button
              HtmlRndrTableOpen(output, "100%", 0, 0, 3, NULL);
              HtmlRndrTrOpen(output, HTML_CSS_SECONDARY_COLOR, HTML_ALIGN_NONE);
              HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "100%", NULL, 0);
              HtmlRndrSpace(output, 1);
              HtmlRndrTdClose(output);
              HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
              HtmlRndrInput(output, HTML_CSS_CONFIGURE_BUTTON, "submit", "url_op", "Delete", NULL, NULL);
              HtmlRndrTdClose(output);
              HtmlRndrTrClose(output);
              HtmlRndrTableClose(output);
            } else if (strcmp(cache_op, "Delete") == 0) {

              HtmlRndrTableOpen(output, "100%", 0, 0, 10, NULL);
              HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
              HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
              HtmlRndrTableOpen(output, "100%", 1, 0, 3, "#CCCCCC");

              // table of deleted urls & status
              while ((cqr = strstr(cqr, "<URL name=\"")) && (cqr_tmp1 = strchr(cqr + 11, '"'))) {
                cqr += 11;

                // document url
                HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
                HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
                output->copyFrom(cqr, cqr_tmp1 - cqr);
                HtmlRndrTdClose(output);

                // deletion status
                cqr = cqr_tmp1 + 2;
                HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
                HtmlRndrSpanOpen(output, HTML_CSS_BLACK_ITEM);
                if ((cqr_tmp1 = strstr(cqr, "</URL>")) && (strncmp(cqr, "succeeded", cqr_tmp1 - cqr) == 0)) {
                  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_DELETED);
                } else {
                  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_CACHE_MISSED);
                }
                HtmlRndrSpanClose(output);
                HtmlRndrTdClose(output);
                HtmlRndrTrClose(output);
              }

              HtmlRndrTableClose(output);
              HtmlRndrTdClose(output);
              HtmlRndrTrClose(output);
              HtmlRndrTableClose(output);

              // blue bar
              HtmlRndrTableOpen(output, "100%", 0, 0, 3, NULL);
              HtmlRndrTrOpen(output, HTML_CSS_SECONDARY_COLOR, HTML_ALIGN_NONE);
              HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "100%", NULL, 0);
              HtmlRndrSpace(output, 1);
              HtmlRndrTdClose(output);
              HtmlRndrTrClose(output);
              HtmlRndrTableClose(output);
            }
          } else {
            // cache miss
            HtmlRndrTableOpen(output, "100%", 0, 0, 10, NULL);
            HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
            HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
            HtmlRndrTableOpen(output, "100%", 1, 0, 3, "#CCCCCC");
            HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);

            // document url
            HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 0);
            snprintf(tmp, MAX_TMP_BUF_LEN, "%s", url);
            output->copyFrom(tmp, strlen(tmp));
            HtmlRndrTdClose(output);
            // cache miss message
            HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
            HtmlRndrSpanOpen(output, HTML_CSS_BLACK_ITEM);
            HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_CACHE_MISSED);
            HtmlRndrSpanClose(output);
            HtmlRndrTdClose(output);

            HtmlRndrTrClose(output);
            HtmlRndrTableClose(output);
            HtmlRndrTdClose(output);
            HtmlRndrTrClose(output);
            HtmlRndrTableClose(output);

            // blue bar
            HtmlRndrTableOpen(output, "100%", 0, 0, 3, NULL);
            HtmlRndrTrOpen(output, HTML_CSS_SECONDARY_COLOR, HTML_ALIGN_NONE);
            HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "100%", NULL, 0);
            HtmlRndrSpace(output, 1);
            HtmlRndrTdClose(output);
            HtmlRndrTrClose(output);
            HtmlRndrTableClose(output);
          }
        }
      }
    }
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_cache_regex_query
//-------------------------------------------------------------------------

static int
handle_cache_regex_query(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  textBuffer *output = whc->response_bdy;
  char *cache_op;
  char *regex;
  char *cqr = whc->cache_query_result;
  char *cqr_tmp1;
  char *cqr_tmp2;
  char tmp[MAX_TMP_BUF_LEN + 1];
  char *url;
  int url_size;
  InkHashTable *ht;

  ht = whc->post_data_ht;
  if (ht) {
    if (ink_hash_table_lookup(ht, "regex_op", (void **) &cache_op) &&
        ink_hash_table_lookup(ht, "regex", (void **) &regex)) {

      // Result label
      HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
      HtmlRndrTdOpen(output, HTML_CSS_CONFIGURE_LABEL, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, "2", 2);
      HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_REGEX_MATCHED);
      HtmlRndrTdClose(output);
      HtmlRndrTrClose(output);

      if (regex && cqr) {
        if ((cqr = strstr(cqr, "<CACHE_INFO status=\"")) && (cqr_tmp1 = strchr(cqr + 20, '"'))) {
          cqr += 20;
          if (strncmp(cqr, "succeeded", cqr_tmp1 - cqr) == 0) {
            // cache hit
            if (strcmp(cache_op, "Lookup") == 0 ||
                strcmp(cache_op, "Delete") == 0 || strcmp(cache_op, "Invalidate") == 0) {

              HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
              HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 2);
              HtmlRndrTableOpen(output, "100%", 0, 0, 10, NULL);
              HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
              HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
              HtmlRndrTableOpen(output, "100%", 1, 0, 3, "#CCCCCC");

              if (strcmp(cache_op, "Lookup") == 0) {
                HtmlRndrFormOpen(output, "regex_form", HTML_METHOD_GET, HTML_SUBMIT_INSPECTOR_DPY_FILE);
              }
              // Table of Documents
              cqr_tmp1 = cqr;
              while ((cqr_tmp1 = strstr(cqr_tmp1, "<URL>")) && (cqr_tmp2 = strstr(cqr_tmp1, "</URL>"))) {
                cqr_tmp1 += 5;
                HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
                HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 2);

                // calculate url length
                url_size = cqr_tmp2 - cqr_tmp1;
                url = xstrndup(cqr_tmp1, url_size);
                if (strcmp(cache_op, "Lookup") == 0) {
                  // display document lookup link
                  snprintf(tmp, MAX_TMP_BUF_LEN, "%s?url_op=%s&url=%s", HTML_SUBMIT_INSPECTOR_DPY_FILE, cache_op,
                               url);
                  HtmlRndrAOpen(output, HTML_CSS_GRAPH, tmp, "display", "window.open('display', 'width=350, height=400')");
                  output->copyFrom(url, url_size);
                  HtmlRndrAClose(output);
                } else {
                  // display document URL only
                  output->copyFrom(url, url_size);
                }
                HtmlRndrTdClose(output);

                if (strcmp(cache_op, "Lookup") == 0) {
                  HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
                  HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_CHECKBOX, url, NULL, NULL, "addToUrlList(this)");
                  HtmlRndrTdClose(output);
                } else if (strcmp(cache_op, "Delete") == 0) {
                  HtmlRndrTdOpen(output, HTML_CSS_BLACK_ITEM, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
                  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_DELETED);
                  HtmlRndrTdClose(output);
                } else if (strcmp(cache_op, "Invalidate") == 0) {
                  HtmlRndrTdOpen(output, HTML_CSS_BLACK_ITEM, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
                  HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_INVALIDATED);
                  HtmlRndrTdClose(output);
                }
                xfree(url);
                HtmlRndrTrClose(output);
                cqr_tmp1 = cqr_tmp2 + 6;
              }
              HtmlRndrTableClose(output);
              HtmlRndrTdClose(output);
              HtmlRndrTrClose(output);

              // delete button for lookup regex
              if (strcmp(cache_op, "Lookup") == 0) {
                HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
                HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_RIGHT, HTML_VALIGN_NONE, NULL, NULL, 2);
                HtmlRndrInput(output, HTML_CSS_CONFIGURE_BUTTON, HTML_TYPE_BUTTON, NULL, "Delete", "display",
                              "setUrls(window.document.regex_form)");
                HtmlRndrTdClose(output);
                HtmlRndrTrClose(output);
                HtmlRndrFormClose(output);
              }

              HtmlRndrTableClose(output);
              HtmlRndrTdClose(output);
              HtmlRndrTrClose(output);
            }
          } else if (strncmp(cqr, "failed", cqr_tmp1 - cqr) == 0) {
            HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
            HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 2);
            HtmlRndrTableOpen(output, "100%", 0, 0, 10, NULL);
            HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
            HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
            HtmlRndrTableOpen(output, "100%", 1, 0, 3, "#CCCCCC");
            HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
            HtmlRndrTdOpen(output, HTML_CSS_BLACK_ITEM, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 2);
            HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_REGEX_MISSED);
            HtmlRndrTdClose(output);
            HtmlRndrTrClose(output);
            HtmlRndrTableClose(output);
            HtmlRndrTdClose(output);
            HtmlRndrTrClose(output);
            HtmlRndrTableClose(output);
            HtmlRndrTdClose(output);
            HtmlRndrTrClose(output);

          } else if (strncmp(cqr, "error", cqr_tmp1 - cqr) == 0) {
            HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
            HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 2);
            HtmlRndrTableOpen(output, "100%", 0, 0, 10, NULL);
            HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
            HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
            HtmlRndrTableOpen(output, "100%", 1, 0, 3, "#CCCCCC");
            HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
            HtmlRndrTdOpen(output, HTML_CSS_BLACK_ITEM, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, NULL, NULL, 2);
            HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_INSPECTOR_REGEX_ERROR);
            HtmlRndrTdClose(output);
            HtmlRndrTrClose(output);
            HtmlRndrTableClose(output);
            HtmlRndrTdClose(output);
            HtmlRndrTrClose(output);
            HtmlRndrTableClose(output);
            HtmlRndrTdClose(output);
            HtmlRndrTrClose(output);
          }
        }
      }
    }
  }
  return WEB_HTTP_ERR_OKAY;
}


//-------------------------------------------------------------------------
// handle_time
//-------------------------------------------------------------------------

static int
handle_time(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  time_t my_time_t;
  char my_ctime_str[32];
  time(&my_time_t);
  ink_ctime_r(&my_time_t, my_ctime_str);
  char *p = my_ctime_str;
  while (*p != '\n' && *p != '\0')
    p++;
  if (*p == '\n')
    *p = '\0';
  whc->response_bdy->copyFrom(my_ctime_str, strlen(my_ctime_str));
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_user
//-------------------------------------------------------------------------

static int
handle_user(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  MgmtInt basic_auth_enabled;
  if (!varIntFromName("proxy.config.admin.basic_auth", &basic_auth_enabled)) {
    return WEB_HTTP_ERR_FAIL;
  }
  if (basic_auth_enabled) {
    char *user = whc->current_user.user;
    HtmlRndrText(whc->response_bdy, whc->lang_dict_ht, HTML_ID_USER);
    HtmlRndrSpace(whc->response_bdy, 1);
    whc->response_bdy->copyFrom(user, strlen(user));
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_plugin_object
//-------------------------------------------------------------------------

static int
handle_plugin_object(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  textBuffer *output = whc->response_bdy;
  WebPluginConfig *wpc = lmgmt->plugin_list.getFirst();
  char *config_link;
  int config_link_len;

  if (wpc != NULL) {
    while (wpc) {
      HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_LEFT);
      HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_TOP, NULL, NULL, 0);
      config_link_len = strlen(wpc->config_path) + 16;
      config_link = (char *) alloca(config_link_len + 1);
      snprintf(config_link, config_link_len, "/plugins/%s", wpc->config_path);
      HtmlRndrAOpen(output, HTML_CSS_GRAPH, config_link, "_blank");
      output->copyFrom(wpc->name, strlen(wpc->name));
      HtmlRndrAClose(output);
      HtmlRndrTdClose(output);
      HtmlRndrTrClose(output);
      wpc = lmgmt->plugin_list.getNext(wpc);
    }
  } else {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 3);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_NO_PLUGINS);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

  return WEB_HTTP_ERR_OKAY;

}


//-------------------------------------------------------------------------
// handle_ssl_redirect_url
//-------------------------------------------------------------------------

static int
handle_ssl_redirect_url(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  RecInt ssl_value = 0;         // current SSL value, enabled/disabled
  char *hostname_FQ = NULL;

  // get current SSL value and fully qualified local hostname
  if (RecGetRecordInt("proxy.config.admin.use_ssl", &ssl_value) != REC_ERR_OKAY)
    mgmt_log(stderr, "[handle_ssl_redirect_url] Error: Unable to get SSL enabled config variable\n");
  if (RecGetRecordString_Xmalloc("proxy.node.hostname_FQ", &hostname_FQ) != REC_ERR_OKAY)
    mgmt_log(stderr, "[handle_ssl_redirect_url] Error: Unable to get local hostname \n");

  char ssl_redirect_url[256] = "";
  char* link = WebHttpGetLink_Xmalloc(HTML_MGMT_GENERAL_FILE);

  // construct proper redirect url
  snprintf(ssl_redirect_url, sizeof(ssl_redirect_url), "%s://%s:%d%s",
               ssl_value ? "https" : "http", hostname_FQ, wGlobals.webPort, link);

  whc->response_bdy->copyFrom(ssl_redirect_url, strlen(ssl_redirect_url));

  // free allocated space
  xfree(link);
  if (hostname_FQ)
    xfree(hostname_FQ);

  return WEB_HTTP_ERR_OKAY;

}

//-------------------------------------------------------------------------
// handle_host_redirect_url
//-------------------------------------------------------------------------

static int
handle_host_redirect_url(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  NOWARN_UNUSED(arg);
  RecInt ssl_value = 0;         // current SSL value, enabled/disabled
  char hostname[1024];

  // get current SSL value and fully qualified local hostname
  if (RecGetRecordInt("proxy.config.admin.use_ssl", &ssl_value) != REC_ERR_OKAY)
    mgmt_log(stderr, "[handle_ssl_redirect_url] Error: Unable to get SSL enabled config variable\n");
  gethostname(hostname, 1024);
  char host_redirect_url[256] = "";
  char* link = WebHttpGetLink_Xmalloc("/configure/c_net_config.ink");

  // construct proper redirect url
  snprintf(host_redirect_url, sizeof(host_redirect_url), "%s://%s:%d%s",
               ssl_value ? "https" : "http", hostname, wGlobals.webPort, link);

  whc->response_bdy->copyFrom(host_redirect_url, strlen(host_redirect_url));

  // free allocated space
  xfree(link);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// handle_network
//-------------------------------------------------------------------------
static int
handle_network(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
  int err = WEB_HTTP_ERR_OKAY;
#if defined(linux) || defined(solaris)
  char value[1024];
  char *value_safe, *old_value, *dummy;
  char *pos;
  char *interface;

  if (ink_hash_table_lookup(whc->submit_warn_ht, arg, (void **) &dummy)) {
    if (ink_hash_table_lookup(whc->post_data_ht, arg, (void **) &old_value)) {
      // coverity[var_assign]
      value_safe = substituteForHTMLChars(old_value);
      // coverity[noescape]
      whc->response_bdy->copyFrom(value_safe, strlen(value_safe));
    }
    goto Ldone;
  }

  if (strcmp(arg, "HOSTNAME") == 0) {
    Config_GetHostname(value, sizeof(value));
  } else if (strcmp(arg, "GATEWAY") == 0) {
    Config_GetDefaultRouter(value, sizeof(value));
  } else if (strstr(arg, "DNS") != NULL) {
    if (strstr(arg, "1") != NULL) {
      Config_GetDNS_Server(value, sizeof(value), 0);
    } else if (strstr(arg, "2") != NULL) {
      Config_GetDNS_Server(value, sizeof(value), 1);
    } else if (strstr(arg, "3") != NULL) {
      Config_GetDNS_Server(value, sizeof(value), 2);
    }
  } else if (strcmp(arg, "domain") == 0) {
    Config_GetDomain(value, sizeof(value));
  } else {
    interface = strchr(arg, '_') + 1;
    pos = strchr(interface, '_');
    *pos = '\0';
    arg = pos + 1;
    if (strcmp(arg, "up") == 0) {
      Config_GetNIC_Status(interface, value, sizeof(value));
      if (strcmp(value, "up") == 0) {
        ink_strncpy(value, "checked", sizeof(value));
      } else {
        ink_strncpy(value, "", sizeof(value));
      }
    } else if (strcmp(arg, "down") == 0) {
      Config_GetNIC_Status(interface, value, sizeof(value));
      if (interface[strlen(interface) - 1] == '0') {
        ink_strncpy(value, "disabled", sizeof(value));
      } else if (strcmp(value, "down") == 0) {
        ink_strncpy(value, "checked", sizeof(value));
      } else {
        ink_strncpy(value, "", sizeof(value));
      }
    } else if (strcmp(arg, "boot_enable") == 0) {
      Config_GetNIC_Start(interface, value, sizeof(value));
      if (strcmp(value, "onboot") == 0) {
        ink_strncpy(value, "checked", sizeof(value));
      } else {
        ink_strncpy(value, "", sizeof(value));
      }
    } else if (strcmp(arg, "boot_disable") == 0) {
      Config_GetNIC_Start(interface, value, sizeof(value));
      if (interface[strlen(interface) - 1] == '0') {
        ink_strncpy(value, "disabled", sizeof(value));
      } else if (strcmp(value, "not-onboot") == 0) {
        ink_strncpy(value, "checked", sizeof(value));
      } else {
        ink_strncpy(value, "", sizeof(value));
      }
    } else if (strcmp(arg, "boot_static") == 0) {
      Config_GetNIC_Protocol(interface, value, sizeof(value));
      if (strcmp(value, "none") == 0 || strcmp(value, "static") == 0) {
        ink_strncpy(value, "checked", sizeof(value));
      } else {
        ink_strncpy(value, "", sizeof(value));
      }
    } else if (strcmp(arg, "boot_dynamic") == 0) {
      Config_GetNIC_Protocol(interface, value, sizeof(value));
      if (strcmp(value, "dhcp") == 0) {
        ink_strncpy(value, "checked", sizeof(value));
      } else {
        ink_strncpy(value, "", sizeof(value));
      }
    } else if (strcmp(arg, "updown") == 0) {
      Config_GetNIC_Status(interface, value, sizeof(value));
      if (strcmp(value, "up") == 0) {
        char protocol[80];
        Config_GetNIC_Protocol(interface, protocol, sizeof(value));
        if (strcmp(protocol, "dhcp") == 0) {
          strncat(value, " (DHCP)", sizeof(value) - strlen(value));
        }
      }
    } else if (strcmp(arg, "yesno") == 0) {
      Config_GetNIC_Start(interface, value, sizeof(value));
      if (strcmp(value, "onboot") == 0) {
        ink_strncpy(value, "yes", sizeof(value));
        char protocol[80];
        Config_GetNIC_Protocol(interface, protocol, sizeof(protocol));
        if (strcmp(protocol, "dhcp") == 0) {
          strncat(value, " (DHCP)", sizeof(value) - strlen(value) - 1);
        }
      } else {
        ink_strncpy(value, "no", sizeof(value));
      }
    } else if (strcmp(arg, "staticdynamic") == 0) {
      Config_GetNIC_Protocol(interface, value, sizeof(value));
      if (strcmp(value, "dhcp") == 0) {
        ink_strncpy(value, "dynamic", sizeof(value));
      } else {
        ink_strncpy(value, "static", sizeof(value));
      }
    } else if (strcmp(arg, "IPADDR") == 0) {
      if (Config_GetNIC_IP(interface, value, sizeof(value)) == 0) {
        char protocol[80];
        Config_GetNIC_Protocol(interface, protocol, sizeof(protocol));
        if (strcmp(protocol, "dhcp") == 0) {
          strncat(value, " (DHCP)", sizeof(value) - strlen(value) - 1);
        }
      }
    } else if (strcmp(arg, "NETMASK") == 0) {
      if (Config_GetNIC_Netmask(interface, value, sizeof(value)) == 0) {
        char protocol[80];
        Config_GetNIC_Protocol(interface, protocol, sizeof(protocol));
        if (strcmp(protocol, "dhcp") == 0) {
          strncat(value, " (DHCP)", sizeof(value) - strlen(value) - 1);
        }
      }
    } else if (strcmp(arg, "GATEWAY") == 0) {
      Config_GetNIC_Gateway(interface, value, sizeof(value));
    }
  }

  // coverity[var_assign]
  value_safe = substituteForHTMLChars(value);
  // coverity[noescape]
  whc->response_bdy->copyFrom(value_safe, strlen(value_safe));
  xfree(value_safe);
Ldone:
  return err;
#else
  return err;
#endif
}


//-------------------------------------------------------------------------
// handle_network_object
//-------------------------------------------------------------------------

static int
handle_network_object(WebHttpContext * whc, char *tag, char *arg)
{
  NOWARN_UNUSED(tag);
#if defined(linux)
  char *device_ink_path, *template_ink_path;
  char command[200], tmpname[80], interface[80];

  if (strcmp(arg, "configure") == 0) {
    template_ink_path = WebHttpAddDocRoot_Xmalloc(whc, "/configure/c_net_device.ink");
  } else {
    template_ink_path = WebHttpAddDocRoot_Xmalloc(whc, "/monitor/m_net_device.ink");
  }

  int count = Config_GetNetworkIntCount();
  for (int i = 0; i < count; i++) {
    Config_GetNetworkInt(i, interface, sizeof(interface));
    ink_strncpy(tmpname, "/", sizeof(tmpname));
    strncat(tmpname, arg, sizeof(tmpname) - strlen(tmpname) - 1);
    strncat(tmpname, "/", sizeof(tmpname) - strlen(tmpname) - 1);
    strncat(tmpname, interface, sizeof(tmpname) - strlen(tmpname) - 1);
    strncat(tmpname, ".ink", sizeof(tmpname) - strlen(tmpname) - 1);

    device_ink_path = WebHttpAddDocRoot_Xmalloc(whc, tmpname);
    remove(device_ink_path);
    ink_strncpy(command, "cat ", sizeof(command));
    strncat(command, template_ink_path, sizeof(command) - strlen(command) - 1);
    strncat(command, "| sed 's/netdev/", sizeof(command) - strlen(command) - 1);
    strncat(command, interface, sizeof(command) - strlen(command) - 1);
    strncat(command, "/g' >", sizeof(command) - strlen(command) - 1);
    strncat(command, device_ink_path, sizeof(command) - strlen(command) - 1);
    strncat(command, " 2>/dev/null", sizeof(command) - strlen(command) - 1);
    NOWARN_UNUSED_RETURN(system(command));
    WebHttpRender(whc, tmpname);
    remove(device_ink_path);

    xfree(device_ink_path);
    xfree(template_ink_path);
  }
  if (template_ink_path)
    xfree(template_ink_path);
#endif
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// WebHttpRenderInit
//-------------------------------------------------------------------------
void
WebHttpRenderInit()
{
  // bind display tags to their display handlers (e.g. <@tag ...> maps
  // to handle_tag())
  g_display_bindings_ht = ink_hash_table_create(InkHashTableKeyType_String);
  ink_hash_table_insert(g_display_bindings_ht, "alarm_object", (void *) handle_alarm_object);
  ink_hash_table_insert(g_display_bindings_ht, "alarm_summary_object", (void *) handle_alarm_summary_object);
  ink_hash_table_insert(g_display_bindings_ht, "file_edit", (void *) handle_file_edit);
  ink_hash_table_insert(g_display_bindings_ht, "include", (void *) handle_include);
  ink_hash_table_insert(g_display_bindings_ht, "overview_object", (void *) handle_overview_object);
  ink_hash_table_insert(g_display_bindings_ht, "overview_details_object", (void *) handle_overview_details_object);
  ink_hash_table_insert(g_display_bindings_ht, "query", (void *) handle_query);
  ink_hash_table_insert(g_display_bindings_ht, "post_data", (void *) handle_post_data);
  ink_hash_table_insert(g_display_bindings_ht, "record", (void *) handle_record);
  ink_hash_table_insert(g_display_bindings_ht, "record_version", (void *) handle_record_version);
  ink_hash_table_insert(g_display_bindings_ht, "summary_object", (void *) handle_summary_object);
  ink_hash_table_insert(g_display_bindings_ht, "tab_object", (void *) handle_tab_object);
  ink_hash_table_insert(g_display_bindings_ht, "html_tab_object", (void *) handle_html_tab_object);
  ink_hash_table_insert(g_display_bindings_ht, "mgmt_auth_object", (void *) handle_mgmt_auth_object);
  ink_hash_table_insert(g_display_bindings_ht, "tree_object", (void *) handle_tree_object);
  ink_hash_table_insert(g_display_bindings_ht, "vip_object", (void *) handle_vip_object);
  ink_hash_table_insert(g_display_bindings_ht, "checked", (void *) handle_checked);
  ink_hash_table_insert(g_display_bindings_ht, "action_checked", (void *) handle_action_checked);
  ink_hash_table_insert(g_display_bindings_ht, "select", (void *) handle_select);
  ink_hash_table_insert(g_display_bindings_ht, "password_object", (void *) handle_password_object);
  ink_hash_table_insert(g_display_bindings_ht, "select_system_logs", (void *) handle_select_system_logs);
  ink_hash_table_insert(g_display_bindings_ht, "select_access_logs", (void *) handle_select_access_logs);
  ink_hash_table_insert(g_display_bindings_ht, "select_debug_logs", (void *) handle_select_debug_logs);
  ink_hash_table_insert(g_display_bindings_ht, "log_action", (void *) handle_log_action);
  ink_hash_table_insert(g_display_bindings_ht, "version", (void *) handle_version);
  // FIXME: submit_error_msg and submit_error_flg is kind of a bad
  // name, should pick something more like 'submit_diags_*'.  ^_^
  ink_hash_table_insert(g_display_bindings_ht, "submit_error_msg", (void *) handle_submit_error_msg);
  ink_hash_table_insert(g_display_bindings_ht, "submit_error_flg", (void *) handle_submit_error_flg);
  ink_hash_table_insert(g_display_bindings_ht, "link", (void *) handle_link);
  ink_hash_table_insert(g_display_bindings_ht, "link_file", (void *) handle_link_file);
  ink_hash_table_insert(g_display_bindings_ht, "link_query", (void *) handle_link_query);
  ink_hash_table_insert(g_display_bindings_ht, "cache_query", (void *) handle_cache_query);
  ink_hash_table_insert(g_display_bindings_ht, "cache_regex_query", (void *) handle_cache_regex_query);
  ink_hash_table_insert(g_display_bindings_ht, "time", (void *) handle_time);
  ink_hash_table_insert(g_display_bindings_ht, "user", (void *) handle_user);
  ink_hash_table_insert(g_display_bindings_ht, "plugin_object", (void *) handle_plugin_object);
  ink_hash_table_insert(g_display_bindings_ht, "ssl_redirect_url", (void *) handle_ssl_redirect_url);
  ink_hash_table_insert(g_display_bindings_ht, "host_redirect_url", (void *) handle_host_redirect_url);
  ink_hash_table_insert(g_display_bindings_ht, "help_link", (void *) handle_help_link);
  ink_hash_table_insert(g_display_bindings_ht, "include_cgi", (void *) handle_include_cgi);

  ink_hash_table_insert(g_display_bindings_ht, "help_config_link", (void *) handle_help_config_link);
  ink_hash_table_insert(g_display_bindings_ht, "config_input_form", (void *) handle_config_input_form);
  ink_hash_table_insert(g_display_bindings_ht, "dynamic_javascript", (void *) handle_dynamic_javascript);
  ink_hash_table_insert(g_display_bindings_ht, "config_table_object", (void *) handle_config_table_object);
  ink_hash_table_insert(g_display_bindings_ht, "network", (void *) handle_network);
  ink_hash_table_insert(g_display_bindings_ht, "network_object", (void *) handle_network_object);
  ink_hash_table_insert(g_display_bindings_ht, "clear_cluster_stats", (void *) handle_clear_cluster_stats);
  return;
}
#endif

//-------------------------------------------------------------------------
// WebHttpRender
//-------------------------------------------------------------------------

int
WebHttpRender(WebHttpContext * whc, const char *file)
{
  int err;
  char *file_buf;
  int file_size;
  char *doc_root_file;
  ink_debug_assert(file != NULL);
#if defined(linux) || defined(solaris)
//Bug 49922, for those .ink files which may meet the root-only system files,
//upgrade the uid to root.
  int old_euid;
  bool change_uid = false;
  if (strstr(file, "m_net.ink") != NULL ||
      strstr(file, "c_net_") != NULL || strstr(file, "c_time.ink") != NULL || strstr(file, "c_ntp.ink") != NULL) {

    change_uid = true;
    Config_User_Root(&old_euid);
  }
#endif

  doc_root_file = WebHttpAddDocRoot_Xmalloc(whc, file);
  // FIXME: probably should mmap here for better performance
  file_buf = 0;
  if (WebFileImport_Xmalloc(doc_root_file, &file_buf, &file_size) != WEB_HTTP_ERR_OKAY) {
    goto Lnot_found;
  }
  err = WebHttpRender(whc, file_buf, file_size);
  goto Ldone;

Lnot_found:

  // missing file
  mgmt_log(stderr, "[WebHttpRender] requested file not found (%s)", file);
  whc->response_hdr->setStatus(STATUS_NOT_FOUND);
  WebHttpSetErrorResponse(whc, STATUS_NOT_FOUND);
  err = WEB_HTTP_ERR_REQUEST_ERROR;
  goto Ldone;

Ldone:

#if defined(linux) || defined(solaris)
  if (change_uid) {
    Config_User_Inktomi(old_euid);
  }
#endif

  xfree(doc_root_file);
  if (file_buf)
    xfree(file_buf);
  return err;
}

int
WebHttpRender(WebHttpContext * whc, char *file_buf, int file_size)
{

  int err;
  char *cpy_p, *cur_p, *end_p;

  char *display_tag;
  char *display_arg;
  WebHttpDisplayHandler display_handler;

  // parse the file and call handlers
  cur_p = cpy_p = file_buf;
  end_p = file_buf + file_size;
  while (cur_p < end_p) {
    if (*cur_p == '<') {
      if (*(cur_p + 1) == '@' || *(cur_p + 1) == '#') {
        // copy the data from cpy_p to cur_p into resposne_bdy
        whc->response_bdy->copyFrom(cpy_p, cur_p - cpy_p);
        // set cpy_p to end of "<?...>" and zero '>'
        cpy_p = strstr(cur_p, ">");
        if (cpy_p == NULL) {
          goto Lserver_error;
        }
        *cpy_p = '\0';
        cpy_p++;
        switch (*(cur_p + 1)) {
        case '@':
          // tokenize arguments
          cur_p += 2;           // skip past '<@'
          display_tag = cur_p;
          display_arg = cur_p;
          while (*display_arg != ' ' && *display_arg != '\0')
            display_arg++;
          if (*display_arg == ' ') {
            *display_arg = '\0';
            display_arg++;
            while (*display_arg == ' ')
              display_arg++;
            if (*display_arg == '\0')
              display_arg = NULL;
          } else {
            display_arg = NULL;
          }
          // call the display handler
          if (display_tag != NULL) {
            if (ink_hash_table_lookup(g_display_bindings_ht, display_tag, (void **) &display_handler)) {
              if ((err = display_handler(whc, display_tag, display_arg)) != WEB_HTTP_ERR_OKAY) {
                goto Ldone;
              }
            } else {
              mgmt_log(stderr, "[WebHttpRender] invalid display tag (%s) ", display_tag);
            }
          } else {
            mgmt_log(stderr, "[WebHttpRender] missing display tag ");
          }
          break;
        case '#':
          cur_p += 2;           // skip past '<#'
          substitute_language(whc, cur_p);
          break;
        }
        // advance to one past the closing '>'
        cur_p = cpy_p;
      } else {
        // move along
        cur_p++;
      }
    } else {
      // move along
      cur_p++;
    }
  }

  // copy data from cpy_p to cur_p into resposne_bdy
  whc->response_bdy->copyFrom(cpy_p, cur_p - cpy_p);

  whc->response_hdr->setStatus(STATUS_OK);
  err = WEB_HTTP_ERR_OKAY;
  goto Ldone;

Lserver_error:

  // corrupt or truncated file
  mgmt_log(stderr, "[WebHttpRender] partial file detected");
  whc->response_hdr->setStatus(STATUS_INTERNAL_SERVER_ERROR);
  WebHttpSetErrorResponse(whc, STATUS_INTERNAL_SERVER_ERROR);
  err = WEB_HTTP_ERR_REQUEST_ERROR;
  goto Ldone;

Ldone:

  return err;

}

//-------------------------------------------------------------------------
// HtmlRndrTrOpen
//-------------------------------------------------------------------------

int
HtmlRndrTrOpen(textBuffer * html, const HtmlCss css, const HtmlAlign align)
{
  char tmp[MAX_TMP_BUF_LEN + 1];
  html->copyFrom("<tr", 3);
  if (css) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " class=\"%s\"", css);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (align) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " align=\"%s\"", align);
    html->copyFrom(tmp, strlen(tmp));
  }
  html->copyFrom(">\n", 2);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrTdOpen
//-------------------------------------------------------------------------

int
HtmlRndrTdOpen(textBuffer * html,
               const HtmlCss css, const HtmlAlign align, const HtmlValign valign, const char *width, const char *height, int colspan, const char *bg)
{
  char tmp[MAX_TMP_BUF_LEN + 1];
  html->copyFrom("<td", 3);
  if (css) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " class=\"%s\"", css);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (align) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " align=\"%s\"", align);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (valign) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " valign=\"%s\"", valign);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (width) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " width=\"%s\"", width);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (height) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " height=\"%s\"", height);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (colspan > 0) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " colspan=\"%d\"", colspan);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (bg) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " background=\"%s\"", bg);
    html->copyFrom(tmp, strlen(tmp));
  }
  html->copyFrom(">", 1);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrAOpen
//-------------------------------------------------------------------------

int
HtmlRndrAOpen(textBuffer * html, const HtmlCss css, const char *href, const char *target, const char *onclick)
{
  char tmp[MAX_TMP_BUF_LEN + 1];            // larger, since href's can be lengthy
  html->copyFrom("<a", 2);
  if (css) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " class=\"%s\"", css);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (href) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " href=\"%s\"", href);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (target) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " target=\"%s\"", target);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (onclick) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " onclick=\"%s\"", onclick);
    html->copyFrom(tmp, strlen(tmp));
  }
  html->copyFrom(">", 1);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrFormOpen
//-------------------------------------------------------------------------

int
HtmlRndrFormOpen(textBuffer * html, const char *name, const HtmlMethod method, const char *action)
{
  char tmp[MAX_TMP_BUF_LEN + 1];
  html->copyFrom("<form", 5);
  if (name) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " name=\"%s\"", name);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (method) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " method=\"%s\"", method);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (action) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " action=\"%s\"", action);
    html->copyFrom(tmp, strlen(tmp));
  }
  html->copyFrom(">\n", 2);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrTextareaOpen
//-------------------------------------------------------------------------

int
HtmlRndrTextareaOpen(textBuffer * html, const HtmlCss css, int cols, int rows, const HtmlWrap wrap, const char *name, bool readonly)
{
  char tmp[MAX_TMP_BUF_LEN + 1];
  html->copyFrom("<textarea", 9);
  if (css) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " class=\"%s\"", css);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (cols > 0) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " cols=\"%d\"", cols);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (rows > 0) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " rows=\"%d\"", rows);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (wrap) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " wrap=\"%s\"", wrap);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (name) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " name=\"%s\"", name);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (readonly) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " readonly");
    html->copyFrom(tmp, strlen(tmp));
  }
  html->copyFrom(">\n", 2);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrTableOpen
//-------------------------------------------------------------------------

int
HtmlRndrTableOpen(textBuffer * html, const char *width, int border, int cellspacing, int cellpadding, const char *bordercolor)
{
  char tmp[MAX_TMP_BUF_LEN + 1];
  html->copyFrom("<table", 6);
  if (width > 0) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " width=\"%s\"", width);
    html->copyFrom(tmp, strlen(tmp));
  }
  snprintf(tmp, MAX_TMP_BUF_LEN, " border=\"%d\"", border);
  html->copyFrom(tmp, strlen(tmp));
  snprintf(tmp, MAX_TMP_BUF_LEN, " cellspacing=\"%d\"", cellspacing);
  html->copyFrom(tmp, strlen(tmp));
  snprintf(tmp, MAX_TMP_BUF_LEN, " cellpadding=\"%d\"", cellpadding);
  html->copyFrom(tmp, strlen(tmp));
  if (bordercolor) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " bordercolor=\"%s\"", bordercolor);
    html->copyFrom(tmp, strlen(tmp));
  }
  html->copyFrom(">\n", 2);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrSpanOpen
//-------------------------------------------------------------------------

int
HtmlRndrSpanOpen(textBuffer * html, const HtmlCss css)
{
  char tmp[MAX_TMP_BUF_LEN + 1];
  html->copyFrom("<span", 5);
  if (css) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " class=\"%s\"", css);
    html->copyFrom(tmp, strlen(tmp));
  }
  html->copyFrom(">", 1);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrSelectOpen
//-------------------------------------------------------------------------

int
HtmlRndrSelectOpen(textBuffer * html, const HtmlCss css, const char *name, int size)
{
  char tmp[MAX_TMP_BUF_LEN + 1];
  html->copyFrom("<select", 7);
  if (css) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " class=\"%s\"", css);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (name) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " name=\"%s\"", name);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (size > 0) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " size=\"%d\"", size);
    html->copyFrom(tmp, strlen(tmp));
  }
  html->copyFrom(">\n", 2);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrOptionOpen
//-------------------------------------------------------------------------

int
HtmlRndrOptionOpen(textBuffer * html, const char *value, bool selected)
{
  char tmp[MAX_TMP_BUF_LEN + 1];
  html->copyFrom("<option", 7);
  if (value) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " value=\"%s\"", value);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (selected) {
    html->copyFrom(" selected", 10);
  }
  html->copyFrom(">", 1);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrPreOpen
//-------------------------------------------------------------------------

int
HtmlRndrPreOpen(textBuffer * html, const HtmlCss css, const char *width)
{
  char tmp[MAX_TMP_BUF_LEN + 1];
  html->copyFrom("<PRE", 4);
  if (css) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " class=\"%s\"", css);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (width) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " width=\"%s\"", width);
    html->copyFrom(tmp, strlen(tmp));
  }
  html->copyFrom(">", 1);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrUlOpen
//-------------------------------------------------------------------------

int
HtmlRndrUlOpen(textBuffer * html)
{
  html->copyFrom("<ul>", 4);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrTrClose
//-------------------------------------------------------------------------

int
HtmlRndrTrClose(textBuffer * html)
{
  html->copyFrom("</tr>\n", 6);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrTdClose
//-------------------------------------------------------------------------

int
HtmlRndrTdClose(textBuffer * html)
{
  html->copyFrom("</td>\n", 6);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrAClose
//-------------------------------------------------------------------------

int
HtmlRndrAClose(textBuffer * html)
{
  html->copyFrom("</a>", 4);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrFormClose
//-------------------------------------------------------------------------

int
HtmlRndrFormClose(textBuffer * html)
{
  html->copyFrom("</form>\n", 8);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrTextareaClose
//-------------------------------------------------------------------------

int
HtmlRndrTextareaClose(textBuffer * html)
{
  html->copyFrom("</textarea>\n", 12);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrTableClose
//-------------------------------------------------------------------------

int
HtmlRndrTableClose(textBuffer * html)
{
  html->copyFrom("</table>\n", 9);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrSpanClose
//-------------------------------------------------------------------------

int
HtmlRndrSpanClose(textBuffer * html)
{
  html->copyFrom("</span>", 7);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrSelectClose
//-------------------------------------------------------------------------

int
HtmlRndrSelectClose(textBuffer * html)
{
  html->copyFrom("</select>\n", 10);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrOptionClose
//-------------------------------------------------------------------------

int
HtmlRndrOptionClose(textBuffer * html)
{
  html->copyFrom("</option>\n", 10);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrPreClose
//-------------------------------------------------------------------------

int
HtmlRndrPreClose(textBuffer * html)
{
  html->copyFrom("</pre>\n", 7);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrUlClose
//-------------------------------------------------------------------------

int
HtmlRndrUlClose(textBuffer * html)
{
  html->copyFrom("</ul>\n", 7);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrInput
//-------------------------------------------------------------------------
int
HtmlRndrInput(textBuffer * html, const HtmlCss css, const HtmlType type, const char *name, const char *value, const char *target, const char *onclick)
{
  char tmp[MAX_TMP_BUF_LEN + 1];
  html->copyFrom("<input", 6);
  if (css) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " class=\"%s\"", css);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (type) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " type=\"%s\"", type);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (name) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " name=\"%s\"", name);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (value) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " value=\"%s\"", value);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (target) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " target=\"%s\"", target);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (onclick) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " onclick=\"%s\"", onclick);
    html->copyFrom(tmp, strlen(tmp));
  }
  html->copyFrom(">\n", 2);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrInput
//-------------------------------------------------------------------------

int
HtmlRndrInput(textBuffer * html, MgmtHashTable * dict_ht, HtmlCss css, HtmlType type, char *name, HtmlId value_id)
{

  char tmp[MAX_TMP_BUF_LEN + 1];
  html->copyFrom("<input", 6);
  if (css) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " class=\"%s\"", css);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (type) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " type=\"%s\"", type);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (name) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " name=\"%s\"", name);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (value_id) {
    html->copyFrom(" value=\"", 8);
    HtmlRndrText(html, dict_ht, value_id);
    html->copyFrom("\"", 1);
  }
  html->copyFrom(">", 1);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrBr
//-------------------------------------------------------------------------

int
HtmlRndrBr(textBuffer * html)
{
  html->copyFrom("<br>\n", 5);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrLi
//-------------------------------------------------------------------------

int
HtmlRndrLi(textBuffer * html)
{
  html->copyFrom("<li>", 4);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrSpace
//-------------------------------------------------------------------------

int
HtmlRndrSpace(textBuffer * html, int num_spaces)
{
  while (num_spaces > 0) {
    html->copyFrom("&nbsp;", 6);
    num_spaces--;
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrText
//-------------------------------------------------------------------------

int
HtmlRndrText(textBuffer * html, MgmtHashTable * dict_ht, const HtmlId text_id)
{
  char *value;
  if (dict_ht->mgmt_hash_table_lookup(text_id, (void **) &value)) {
    html->copyFrom(value, strlen(value));
  } else {
    if (dict_ht->mgmt_hash_table_lookup(HTML_ID_UNDEFINED, (void **) &value)) {
      html->copyFrom(value, strlen(value));
    }
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrImg
//-------------------------------------------------------------------------

int
HtmlRndrImg(textBuffer * html, const char *src, const char *border, const char *width, const char *height, const char *hspace)
{
  char tmp[MAX_TMP_BUF_LEN + 1];
  html->copyFrom("<img", 4);
  if (src) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " src=\"%s\"", src);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (border) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " border=\"%s\"", border);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (width) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " width=\"%s\"", width);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (height) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " height=\"%s\"", height);
    html->copyFrom(tmp, strlen(tmp));
  }
  if (hspace) {
    snprintf(tmp, MAX_TMP_BUF_LEN, " HSPACE='%s'", hspace);
    html->copyFrom(tmp, strlen(tmp));
  }
  html->copyFrom(">", 1);
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// HtmlRndrDotClear
//-------------------------------------------------------------------------

int
HtmlRndrDotClear(textBuffer * html, int width, int height)
{
  char tmp[MAX_TMP_BUF_LEN + 1];
  snprintf(tmp, MAX_TMP_BUF_LEN, "<img src=\"" HTML_DOT_CLEAR "\" " "width=\"%d\" height=\"%d\">", width, height);
  html->copyFrom(tmp, strlen(tmp));
  return WEB_HTTP_ERR_OKAY;
}
