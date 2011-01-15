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
 *  WebHttpTree.h - dynamic, javascript web-ui tree hierarchy and
 *                  web-ui link index
 *
 *
 ****************************************************************************/

#ifndef _WEB_HTTP_TREE_H_
#define _WEB_HTTP_TREE_H_

#include "WebHttpContext.h"

int WebHttpRenderJsTree(textBuffer * output, char *file_link);
int WebHttpRenderTabs(textBuffer * output, int active_mode);
int WebHttpRenderHtmlTabs(textBuffer * output, char *file_link, int active_tab);
bool WebHttpTreeReturnRefresh(char *file_link);
char *WebHttpTreeReturnHelpLink(char *file_link);
void WebHttpTreeRebuildJsTree();

char *WebHttpGetLink_Xmalloc(const char *file_link);
char *WebHttpGetLinkQuery_Xmalloc(char *file_link);

void start_element_handler(void *userData, const char *name, const char **atts);
void end_element_handler(void *userData, const char *name, const char **atts);
void generate_mode_node(char **atts);
void generate_menu_node(char **atts);
void generate_item_node(char **atts);
void generate_link_node(char **atts);
#endif // _WEB_HTTP_TREE_H_
