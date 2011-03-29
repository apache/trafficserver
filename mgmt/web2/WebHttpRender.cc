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
#include "WebOverview.h"

#include "WebMgmtUtils.h"
#include "MgmtUtils.h"

#include "LocalManager.h"

#include "mgmtapi.h"
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
