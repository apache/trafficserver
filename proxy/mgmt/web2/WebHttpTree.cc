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
 *  WebHttpTree.cc - dynamic, javascript web-ui tree hierarchy and
 *                   web-ui link index
 *
 *
 ****************************************************************************/

#include "ink_platform.h"
#include "ink_hash_table.h"
#include "ink_rwlock.h"

#include "MgmtUtils.h"
#include "WebMgmtUtils.h"
#include "WebHttpRender.h"
#include "WebHttpTree.h"
#include "WebGlobals.h"
#include "WebCompatibility.h"
#include "expat.h"

//-------------------------------------------------------------------------
// defines
//-------------------------------------------------------------------------

#define WHT_MAX_MODES        5
#define WHT_MAX_MENUS        10
#define WHT_MAX_ITEMS        32
#define WHT_MAX_LINKS        5

#define WHT_MAX_BUF_LEN      128
#define WHT_MAX_TREE_JS_BUF  4096
#define WHT_MAX_PATH_LEN     1024

#define WHT_ENABLED          NULL
#define WHT_DISABLED         "disabled"

//-------------------------------------------------------------------------
// structs
//-------------------------------------------------------------------------

struct tree_node
{
  const char *name;             // name of this node
  char *enabled;                // config record to check if this node enabled (NULL default to enabled)
};

// link_node - The link_node describes a page in the web-ui.  The
// structure contains the following:
// the disk file for the page; where the page should appear in
// the web-ui (mode, menu, item, tab); and lastly, any additional
// query items required to make this page render correctly.

struct link_node
{
  tree_node node;
  char *file_name;              // file name for this web-ui page
  int mode_id;                  // query: mode_id = [0: monitor, 1: configure]
  int menu_id;                  // query: menu_id = [menu id]
  int item_id;                  // query: item_id = [item id]
  int tab_id;                   // query: tab_id  = [tab id]
  const char *query;            // query: additional query items for this link
  bool refresh;
  char *help_link;
};

struct item_node
{
  tree_node node;
  link_node links[WHT_MAX_LINKS + 1];
};

struct menu_node
{
  tree_node node;
  bool top_level_item;          // if true, the first item in the list will be
  // pulled into a link in the top level menu.
  item_node items[WHT_MAX_ITEMS + 1];
};

struct mode_node
{
  tree_node node;
  textBuffer *tree_js;          // text buffer for output javascript
  ink_rwlock tree_js_rwlock;    // lock to protect tree_js buffer
  menu_node menus[WHT_MAX_MENUS + 1];
};

//-------------------------------------------------------------------------
// globals
//-------------------------------------------------------------------------

static InkHashTable *g_mode_ht = NULL;
static InkHashTable *g_menu_ht = NULL;
static InkHashTable *g_item_ht = NULL;
static InkHashTable *g_link_ht = NULL;
static mode_node g_modes[WHT_MAX_MODES];

static const char *g_empty_string = "";
static int g_mode_id = 0;
static int g_menu_id = 0;
static int g_item_id = 0;
static int g_link_id = 0;

//-------------------------------------------------------------------------
// XML Element Handlers
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
// start_element_handler
//-------------------------------------------------------------------------
void
start_element_handler(void *userData, const char *name, const char **atts)
{
  NOWARN_UNUSED(userData);
  char **attrs = (char **) atts;
  if (strcmp(name, "MENU_TREE") == 0) {
    // Main entity tag - do nothing
  } else if (strcmp(name, "MODE") == 0) {
    generate_mode_node(attrs);
  } else if (strcmp(name, "MENU") == 0) {
    generate_menu_node(attrs);
  } else if (strcmp(name, "ITEM") == 0) {
    generate_item_node(attrs);
  } else if (strcmp(name, "LINK") == 0) {
    generate_link_node(attrs);
  } else {
    fprintf(stderr, "[WebHttpTree] Unknown element specified in xml: %s\n", name);
  }
}

//-------------------------------------------------------------------------
// generate_mode_node
//-------------------------------------------------------------------------
void
generate_mode_node(char **atts)
{
  int i;
  mode_node *mode = &g_modes[g_mode_id];
  memset(mode, 0, sizeof(mode_node));

  ink_debug_assert(atts);
  mode->tree_js = new textBuffer(WHT_MAX_TREE_JS_BUF + 1);
  ink_rwlock_init(&mode->tree_js_rwlock);
  for (i = 0; atts[i]; i += 2) {
    ink_debug_assert(atts[i + 1]);
    if (strcmp(atts[i], "name") == 0) {
      mode->node.name = xstrdup(atts[i + 1]);
    } else if (strcmp(atts[i], "enable_record") == 0) {
      mode->node.enabled = xstrdup(atts[i + 1]);
    }
  }

  // reset g_item_id to start from 0
  g_menu_id = 0;
}

//-------------------------------------------------------------------------
// generate_menu_node
//-------------------------------------------------------------------------
void
generate_menu_node(char **atts)
{
  int i;
  mode_node *mode = &g_modes[g_mode_id];
  menu_node *menu = &mode->menus[g_menu_id];
  memset(menu, 0, sizeof(menu_node));

  ink_debug_assert(mode);
  ink_debug_assert(atts);
  ink_debug_assert(g_menu_id <= WHT_MAX_MENUS);

  for (i = 0; atts[i]; i += 2) {
    ink_debug_assert(atts[i + 1]);
    if (strcmp(atts[i], "name") == 0) {
      menu->node.name = xstrdup(atts[i + 1]);
    } else if (strcmp(atts[i], "enable_record") == 0) {
      menu->node.enabled = xstrdup(atts[i + 1]);
    } else if (strcmp(atts[i], "top_level_item") == 0) {
      menu->top_level_item = (strcmp(atts[i + 1], "true") == 0) ? true : false;
    }
  }

  // reset g_item_id to start from 0
  g_item_id = 0;
}

//-------------------------------------------------------------------------
// generate_item_node
//-------------------------------------------------------------------------
void
generate_item_node(char **atts)
{
  int i;
  mode_node *mode = &g_modes[g_mode_id];
  menu_node *menu = &mode->menus[g_menu_id];
  item_node *item = &menu->items[g_item_id];

  ink_debug_assert(g_item_id <= WHT_MAX_ITEMS);
  for (i = 0; atts[i]; i += 2) {
    ink_debug_assert(atts[i + 1]);
    if (strcmp(atts[i], "name") == 0) {
      item->node.name = xstrdup(atts[i + 1]);
    } else if (strcmp(atts[i], "enable_record") == 0) {
      item->node.enabled = xstrdup(atts[i + 1]);
    }
  }

  // reset g_link_id to start from 0
  g_link_id = 0;
}

//-------------------------------------------------------------------------
// generate_link_node
//-------------------------------------------------------------------------
void
generate_link_node(char **atts)
{
  int i;
  mode_node *mode = &g_modes[g_mode_id];
  menu_node *menu = &mode->menus[g_menu_id];
  item_node *item = &menu->items[g_item_id];
  link_node *link = &item->links[g_link_id];
  memset(link, 0, sizeof(link_node));
  link->node.name = g_empty_string;
  link->query = g_empty_string;

  ink_debug_assert(g_link_id <= WHT_MAX_LINKS);
  for (i = 0; atts[i]; i += 2) {
    ink_debug_assert(atts[i + 1]);
    if (strcmp(atts[i], "name") == 0) {
      link->node.name = xstrdup(atts[i + 1]);
    } else if (strcmp(atts[i], "enable_record") == 0) {
      link->node.enabled = xstrdup(atts[i + 1]);
    } else if (strcmp(atts[i], "refresh") == 0) {
      link->refresh = (strcmp(atts[i + 1], "true") == 0) ? true : false;
    } else if (strcmp(atts[i], "query") == 0) {
      link->query = xstrdup(atts[i + 1]);
    } else if (strcmp(atts[i], "file_link") == 0) {
      link->file_name = xstrdup(atts[i + 1]);
    } else if (strcmp(atts[i], "help_link") == 0) {
      link->help_link = xstrdup(atts[i + 1]);
    }
  }
}

//-------------------------------------------------------------------------
// end_element_handler
//-------------------------------------------------------------------------
void
end_element_handler(void *userData, const char *name)
{
  NOWARN_UNUSED(userData);
  if (strcmp(name, "MENU_TREE") == 0) {
    // Main entity tag - do nothing
  } else if (strcmp(name, "MODE") == 0) {
    g_mode_id++;
  } else if (strcmp(name, "MENU") == 0) {
    g_menu_id++;
  } else if (strcmp(name, "ITEM") == 0) {
    g_item_id++;
  } else if (strcmp(name, "LINK") == 0) {
    g_link_id++;
  } else {
    fprintf(stderr, "[WebHttpTree] Unknown element specified in xml: %s\n", name);
  }
}

//-------------------------------------------------------------------------
// is_enabled
//-------------------------------------------------------------------------

static inline int
is_enabled(char *record)
{
  if (record == WHT_ENABLED) {
    return true;
  } else if (strcmp(record, WHT_DISABLED) == 0) {
    return false;
  } else {
    MgmtInt value;
    char *rec = xstrdup(record);
    char *ptr = rec;
    char *sep;
    while (ptr) {
      sep = strchr(ptr, '|');
      if (sep) {
        *sep++ = '\0';
      }
      if (varIntFromName(ptr, &value)) {
        if (value) {
          xfree(rec);
          return true;
        }
      } else {
        goto Ldone;
      }
      ptr = sep;
    }
  Ldone:
    xfree(rec);
    return false;
  }
}

//-------------------------------------------------------------------------
// build_and_index_tree
//-------------------------------------------------------------------------

static int
build_and_index_tree(InkHashTable * mode_ht, InkHashTable * menu_ht, InkHashTable * item_ht, InkHashTable * link_ht)
{

  char tmp[WHT_MAX_BUF_LEN + 1];
  int item_count = 0;

  int mode_id = 0;
  int menu_id = 0;
  int item_id = 0;
  int link_id = 0;
  mode_node *mode;
  menu_node *menu;
  item_node *item;
  link_node *link;

  mode = &g_modes[mode_id];
  while (mode->node.name != NULL) {
    ink_rwlock_wrlock(&mode->tree_js_rwlock);

    // reset js_tree buffer
    mode->tree_js->reUse();
    if (is_enabled(mode->node.enabled)) {
      // loop through tree until terminator
      menu_id = 0;
      menu = mode->menus;
      while (menu->node.name != NULL) {
        if (is_enabled(menu->node.enabled)) {
          // count number of items in menu
          item_count = 0;
          item = menu->items;
          while (item->node.name != NULL) {
            if (is_enabled(item->node.enabled))
              item_count++;
            item++;
          }
          ink_debug_assert(item_count <= WHT_MAX_ITEMS);
          // add 'menu_block' to output
          snprintf(tmp, WHT_MAX_BUF_LEN, "menu_block[%d]=", menu_id);
          mode->tree_js->copyFrom(tmp, strlen(tmp));
          // look at items
          item = menu->items;
          if (menu->top_level_item) {
            // special case: item combined with menu
            link_id = 0;
            link = item->links;
            while (link->file_name != NULL) {
              if (is_enabled(link->node.enabled)) {
                link->mode_id = mode_id;
                link->menu_id = menu_id;
                link->item_id = 0;
                link->tab_id = link_id;
                if (link_id == 0) {
                  // link item to first link in list
                  snprintf(tmp, WHT_MAX_BUF_LEN, "\"%s|%s?mode=%d&menu=%d&item=%d&tab=%d%s\"\n",
                               menu->node.name, link->file_name, link->mode_id, link->menu_id,
                               link->item_id, link->tab_id, link->query);
                  mode->tree_js->copyFrom(tmp, strlen(tmp));
                }
                // Take out the assert for MRTG: multiple entries are inserted for /mrtg/detailed.ink
                ink_hash_table_insert(link_ht, link->file_name, (void *) link);
                ink_hash_table_insert(item_ht, link->file_name, (void *) item);
                ink_hash_table_insert(menu_ht, link->file_name, (void *) menu);
                ink_hash_table_insert(mode_ht, link->file_name, (void *) mode);
                link_id++;
              }
              link++;
            }
            ink_debug_assert(link_id <= WHT_MAX_LINKS);
          } else {
            // handle menu items
            item_id = 0;
            item = menu->items;
            bool write_menu_name = false;
            while (item->node.name != NULL) {
              if (is_enabled(item->node.enabled)) {
                if (!write_menu_name) { // output menu name
                  snprintf(tmp, WHT_MAX_BUF_LEN, "\"%s;\" +\n", menu->node.name);
                  mode->tree_js->copyFrom(tmp, strlen(tmp));
                  write_menu_name = true;
                }
                link_id = 0;
                link = item->links;
                while (link->file_name != NULL) {
                  if (is_enabled(link->node.enabled)) {
                    link->mode_id = mode_id;
                    link->menu_id = menu_id;
                    link->item_id = item_id;
                    link->tab_id = link_id;
                    if (link_id == 0) {
                      snprintf(tmp, WHT_MAX_BUF_LEN, "  \"%s|%s?mode=%d&menu=%d&item=%d&tab=%d%s",
                                   item->node.name, link->file_name, link->mode_id, link->menu_id,
                                   link->item_id, link->tab_id, link->query);
                      mode->tree_js->copyFrom(tmp, strlen(tmp));
                      if (item_id < item_count - 1)
                        mode->tree_js->copyFrom(";\" +\n", 5);
                      else
                        mode->tree_js->copyFrom("\"\n", 2);
                    }
                    // Take out the assert for MRTG: multiple entries are inserted for /mrtg/detailed.ink
                    ink_hash_table_insert(link_ht, link->file_name, (void *) link);
                    ink_hash_table_insert(item_ht, link->file_name, (void *) item);
                    ink_hash_table_insert(menu_ht, link->file_name, (void *) menu);
                    ink_hash_table_insert(mode_ht, link->file_name, (void *) mode);
                    link_id++;
                  }
                  link++;
                }
                ink_debug_assert(link_id <= WHT_MAX_LINKS);
                item_id++;
              }
              item++;
            }
          }
          mode->tree_js->copyFrom("\n", 1);
          menu_id++;
        }
        menu++;
      }
      mode_id++;
    }
    ink_rwlock_unlock(&mode->tree_js_rwlock);
    mode++;
  }
  return WEB_HTTP_ERR_OKAY;
}

//-------------------------------------------------------------------------
// WebHttpRenderJsTree
//-------------------------------------------------------------------------

int
WebHttpRenderJsTree(textBuffer * output, char *file_link)
{
  mode_node *mode;
  if (!ink_hash_table_lookup(g_mode_ht, file_link, (void **) &mode)) {
    return WEB_HTTP_ERR_FAIL;
  }
  ink_rwlock_rdlock(&mode->tree_js_rwlock);
  output->copyFrom(mode->tree_js->bufPtr(), mode->tree_js->spaceUsed());
  ink_rwlock_unlock(&mode->tree_js_rwlock);

  return WEB_HTTP_ERR_OKAY;
}


//-------------------------------------------------------------------------
// WebHttpRenderHtmlTabs
//-------------------------------------------------------------------------

int
WebHttpRenderHtmlTabs(textBuffer * output, char *file_link, int active_tab)
{

  int i, j;
  char width_pcnt[8 + 1];
  char buf[WHT_MAX_BUF_LEN + 1];
  int link_count;
  item_node *item;
  link_node *link;
  link_node *link_array[WHT_MAX_LINKS];
  link_node **link_array_p;

  // get item from g_item_ht
  if (!ink_hash_table_lookup(g_item_ht, file_link, (void **) &item)) {
    return WEB_HTTP_ERR_FAIL;
  }
  // count links and construct link_array
  memset(link_array, 0, sizeof(link_array));
  link_count = 0;
  link = item->links;
  link_array_p = link_array;
  while (link->file_name) {
    if (is_enabled(link->node.enabled)) {
      link_count++;
      *link_array_p = link;
      link_array_p++;
    }
    link++;
  }

  // error check
  if (active_tab < 0)
    active_tab = 0;
  if (active_tab >= link_count)
    active_tab = link_count - 1;

  // compute the width percentage
  snprintf(width_pcnt, 8, "%d%%", 100 / WHT_MAX_LINKS);

  // render items
  HtmlRndrTableOpen(output, "95%", 0, 0, 0);

  // top lines
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_TERTIARY_COLOR, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "2", "2", 0);
  HtmlRndrDotClear(output, 2, 2);
  HtmlRndrTdClose(output);
  for (i = 0; i < link_count; i++) {
    for (j = 0; j < 2; j++) {
      HtmlRndrTdOpen(output, HTML_CSS_TERTIARY_COLOR, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "2", "2", 0);
      HtmlRndrDotClear(output, 2, 2);
      HtmlRndrTdClose(output);
    }
  }
  for (i = 0; i < WHT_MAX_LINKS - link_count; i++) {
    for (j = 0; j < 2; j++) {
      HtmlRndrTdOpen(output, HTML_CSS_PRIMARY_COLOR, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "2", "2", 0);
      HtmlRndrDotClear(output, 2, 2);
      HtmlRndrTdClose(output);
    }
  }
  HtmlRndrTrClose(output);

  // tab content (the fun part)
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_TERTIARY_COLOR, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "2", "2", 0);
  HtmlRndrDotClear(output, 2, 2);
  HtmlRndrTdClose(output);
  for (i = 0; i < link_count; i++) {
    if (i == active_tab) {
      HtmlRndrTdOpen(output, HTML_CSS_HILIGHT_COLOR, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, width_pcnt, "20", 0);
      HtmlRndrSpanOpen(output, HTML_CSS_BLACK_ITEM);
      HtmlRndrImg(output, HTML_BLANK_ICON, "0", "10", "10", "5");
      output->copyFrom(link_array[i]->node.name, strlen(link_array[i]->node.name));
      HtmlRndrSpanClose(output);
      HtmlRndrTdClose(output);
    } else {
      snprintf(buf, WHT_MAX_BUF_LEN, "%s?mode=%d&menu=%d&item=%d&tab=%d%s",
                   link_array[i]->file_name, link_array[i]->mode_id, link_array[i]->menu_id,
                   link_array[i]->item_id, link_array[i]->tab_id, link_array[i]->query);
      HtmlRndrTdOpen(output, HTML_CSS_UNHILIGHT_COLOR, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, width_pcnt, "20", 0);
      HtmlRndrAOpen(output, HTML_CSS_NONE, buf, NULL);
      HtmlRndrImg(output, HTML_BLANK_ICON, "0", "10", "10", "5");
      output->copyFrom(link_array[i]->node.name, strlen(link_array[i]->node.name));
      HtmlRndrAClose(output);
      HtmlRndrTdClose(output);
    }
    HtmlRndrTdOpen(output, HTML_CSS_TERTIARY_COLOR, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "2", "2", 0);
    HtmlRndrDotClear(output, 2, 2);
    HtmlRndrTdClose(output);
  }
  for (i = 0; i < WHT_MAX_LINKS - link_count; i++) {
    HtmlRndrTdOpen(output, HTML_CSS_PRIMARY_COLOR, HTML_ALIGN_LEFT, HTML_VALIGN_NONE, width_pcnt, "20", 0);
    HtmlRndrSpace(output, 1);
    HtmlRndrTdClose(output);
    HtmlRndrTdOpen(output, HTML_CSS_PRIMARY_COLOR, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "2", NULL, 0);
    HtmlRndrDotClear(output, 2, 2);
    HtmlRndrTdClose(output);
  }
  HtmlRndrTrClose(output);

  // bottom lines
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
  HtmlRndrTdOpen(output, HTML_CSS_TERTIARY_COLOR, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "2", "2", 0);
  HtmlRndrDotClear(output, 2, 2);
  HtmlRndrTdClose(output);
  for (i = 0; i < WHT_MAX_LINKS; i++) {
    if (i == active_tab) {
      HtmlRndrTdOpen(output, HTML_CSS_HILIGHT_COLOR, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "2", "2", 0);
      HtmlRndrDotClear(output, 2, 2);
      HtmlRndrTdClose(output);
      HtmlRndrTdOpen(output, HTML_CSS_TERTIARY_COLOR, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "2", "2", 0);
      HtmlRndrDotClear(output, 2, 2);
      HtmlRndrTdClose(output);
    } else {
      for (j = 0; j < 2; j++) {
        HtmlRndrTdOpen(output, HTML_CSS_TERTIARY_COLOR, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "2", "2", 0);
        HtmlRndrDotClear(output, 2, 2);
        HtmlRndrTdClose(output);
      }
    }
  }
  HtmlRndrTrClose(output);

  HtmlRndrTableClose(output);

  return WEB_HTTP_ERR_OKAY;

}

//-------------------------------------------------------------------------
// WebHttpGetLink_Xmalloc
//-------------------------------------------------------------------------

char *
WebHttpGetLink_Xmalloc(const char *file_link)
{
  link_node *link;
  char *buf = (char *) xmalloc(WHT_MAX_BUF_LEN + 1);
  if (ink_hash_table_lookup(g_link_ht, file_link, (void **) &link)) {
    snprintf(buf, WHT_MAX_BUF_LEN, "%s?mode=%d&menu=%d&item=%d&tab=%d%s",
                 link->file_name, link->mode_id, link->menu_id, link->item_id, link->tab_id, link->query);
  } else {
    *buf = '\0';
  }
  return buf;
}

//-------------------------------------------------------------------------
// WebHttpGetLinkQuery_Xmalloc
//-------------------------------------------------------------------------

char *
WebHttpGetLinkQuery_Xmalloc(char *file_link)
{
  link_node *link;
  char *buf = (char *) xmalloc(WHT_MAX_BUF_LEN + 1);
  if (ink_hash_table_lookup(g_link_ht, file_link, (void **) &link)) {
    snprintf(buf, WHT_MAX_BUF_LEN, "mode=%d&menu=%d&item=%d&tab=%d%s",
                 link->mode_id, link->menu_id, link->item_id, link->tab_id, link->query);
  } else {
    *buf = '\0';
  }
  return buf;
}

//------------------------------------------------------------------------
// WebHttpTreeReturnRefresh
//------------------------------------------------------------------------

bool
WebHttpTreeReturnRefresh(char *file_link)
{
  link_node *link;

  // returns whether this link needs to be refreshed.
  if (!ink_hash_table_lookup(g_link_ht, file_link, (void **) &link)) {
    return false;
  }
  return link->refresh;
}

//------------------------------------------------------------------------
// WebHttpTreeReturnHelpLink
//------------------------------------------------------------------------
char *
WebHttpTreeReturnHelpLink(char *file_link)
{
  link_node *link;

  // returns the input file's be help link.
  if (!ink_hash_table_lookup(g_link_ht, file_link, (void **) &link)) {
    return NULL;
  } else {
    return link->help_link;
  }
}

//------------------------------------------------------------------------
// WebHttpTreeRebuildJsTree
//------------------------------------------------------------------------
void
WebHttpTreeRebuildJsTree()
{
  build_and_index_tree(g_mode_ht, g_menu_ht, g_item_ht, g_link_ht);
}
