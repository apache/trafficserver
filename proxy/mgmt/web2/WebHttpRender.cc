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
#include "WebHttpRender.h"
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
// globals
//-------------------------------------------------------------------------

extern InkHashTable *g_display_config_ht;

//-------------------------------------------------------------------------
// forward declarations
//-------------------------------------------------------------------------


#if TS_HAS_WEBUI




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

static void
render_option(textBuffer * output, const char *value, char *display, bool selected)
{
  HtmlRndrOptionOpen(output, value, selected);
  output->copyFrom(display, strlen(display));
  HtmlRndrOptionClose(output);
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
  ink_hash_table_insert(g_display_bindings_ht, "html_tab_object", (void *) handle_html_tab_object);
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
