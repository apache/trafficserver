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

  StatPages.cc


 ****************************************************************************/

#include "tscore/ink_config.h"
#include "ProxyConfig.h"
#include "StatPages.h"
#include "HdrUtils.h"
#include "tscore/MatcherUtils.h"

#define MAX_STAT_PAGES 32

// Globals
StatPagesManager statPagesManager;

static struct {
  char *module;
  StatPagesFunc func;
} stat_pages[MAX_STAT_PAGES];

static int n_stat_pages;

void
StatPagesManager::init()
{
  ink_mutex_init(&stat_pages_mutex);
  REC_EstablishStaticConfigInt32(m_enabled, "proxy.config.http_ui_enabled");
}

void
StatPagesManager::register_http(const char *module, StatPagesFunc func)
{
  ink_mutex_acquire(&stat_pages_mutex);
  ink_release_assert(n_stat_pages < MAX_STAT_PAGES);

  stat_pages[n_stat_pages].module = (char *)ats_malloc(strlen(module) + 3);
  snprintf(stat_pages[n_stat_pages].module, strlen(module) + 3, "{%s}", module);
  stat_pages[n_stat_pages++].func = func;
  ink_mutex_release(&stat_pages_mutex);
}

Action *
StatPagesManager::handle_http(Continuation *cont, HTTPHdr *header)
{
  URL *url = header->url_get();

  if (((m_enabled == 1 || m_enabled == 3) && is_cache_inspector_page(url)) ||
      ((m_enabled == 2 || m_enabled == 3) && is_stat_page(url) && !is_cache_inspector_page(url))) {
    int host_len;
    char host[MAXDNAME + 1];
    const char *h;
    int i;

    h = url->host_get(&host_len);
    if (host_len > MAXDNAME) {
      host_len = MAXDNAME;
    }
    memcpy(host, h, host_len);
    host[host_len] = '\0';
    host_len       = unescapifyStr(host);

    for (i = 0; i < n_stat_pages; i++) {
      if (strlen(host) == strlen(stat_pages[i].module) && strncmp(host, stat_pages[i].module, host_len) == 0) {
        return stat_pages[i].func(cont, header);
      }
    }
  }

  cont->handleEvent(STAT_PAGE_FAILURE, nullptr);
  return ACTION_RESULT_DONE;
}

bool
StatPagesManager::is_stat_page(URL *url)
{
  // This gets called from the state machine, so we should optimize here and not in caller.
  if (m_enabled <= 0) {
    return false;
  }

  int length;
  const char *h = url->host_get(&length);
  char host[MAXDNAME + 1];

  if (h == nullptr || length < 2 || length > MAXDNAME) {
    return false;
  }

  memcpy(host, h, length);
  host[length] = '\0';
  length       = unescapifyStr(host);

  if ((host[0] == '{') && (host[length - 1] == '}')) {
    return true;
  }

  return false;
}

bool
StatPagesManager::is_cache_inspector_page(URL *url)
{
  int length;
  const char *h = url->host_get(&length);
  char host[MAXDNAME + 1];

  if (h == nullptr || length < 2 || length > MAXDNAME) {
    return false;
  }

  memcpy(host, h, length);
  host[length] = '\0';
  length       = unescapifyStr(host);

  if (strncmp(host, "{cache}", length) == 0) {
    return true;
  } else {
    return false;
  }
}

void
BaseStatPagesHandler::resp_clear()
{
  ats_free(response);
  response        = nullptr;
  response_size   = 0;
  response_length = 0;
}

void
BaseStatPagesHandler::resp_add(const char *fmt, ...)
{
  va_list args;
  char buf[16384];
  int length;
  int size;

  va_start(args, fmt);
  length = vsnprintf(buf, 16384, fmt, args);
  va_end(args);

  size = response_size;
  if (size == 0) {
    size = 1024;
  }
  while ((response_length + length + 1) > size) {
    size *= 2;
  }

  if (size != response_size) {
    if (!response) {
      response = (char *)ats_malloc(size);
    } else {
      response = (char *)ats_realloc(response, size);
    }
    response_size = size;
  }

  memcpy(&response[response_length], buf, length + 1);
  response_length += length;
}

void
BaseStatPagesHandler::resp_add_sep()
{
  resp_add("<hr width=\"100%%\">\n");
}

void
BaseStatPagesHandler::resp_begin(const char *title)
{
  resp_clear();
  resp_add("<html>\n"
           "<head><title>%s</title></head>\n"
           "<body text=\"#000000\" bgcolor=\"#ffffff\" link=\"#0000ee\" vlink=\"#551a8b\" alink=\"#ff0000\">\n",
           title);
}

void
BaseStatPagesHandler::resp_end()
{
  resp_add("</body>\n"
           "</html>\n");
}

void
BaseStatPagesHandler::resp_begin_numbered()
{
  resp_add("<ol>\n");
}

void
BaseStatPagesHandler::resp_end_numbered()
{
  resp_add("</ol>\n");
}

void
BaseStatPagesHandler::resp_begin_unnumbered()
{
  resp_add("<ul>\n");
}

void
BaseStatPagesHandler::resp_end_unnumbered()
{
  resp_add("</ul>\n");
}

void
BaseStatPagesHandler::resp_begin_item()
{
  resp_add("<li>\n");
}

void
BaseStatPagesHandler::resp_end_item()
{
  resp_add("</li>\n");
}

void
BaseStatPagesHandler::resp_begin_table(int border, int columns, int percent)
{
  resp_add("<table border=%d cols=%d width=\"%d%%\">\n", border, columns, percent);
}

void
BaseStatPagesHandler::resp_end_table()
{
  resp_add("</table>\n");
}

void
BaseStatPagesHandler::resp_begin_row()
{
  resp_add("<tr>\n");
}

void
BaseStatPagesHandler::resp_end_row()
{
  resp_add("</tr>\n");
}

void
BaseStatPagesHandler::resp_begin_column(int percent, const char *align)
{
  if (percent == -1) {
    resp_add("<td %s%s>\n", align ? "align=" : "", align ? align : "");
  } else {
    resp_add("<td width=\"%d%%\" %s%s>\n", percent, align ? "align=" : "", align ? align : "");
  }
}

void
BaseStatPagesHandler::resp_end_column()
{
  resp_add("</td>\n");
}
