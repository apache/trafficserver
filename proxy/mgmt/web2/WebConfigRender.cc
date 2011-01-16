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
 *  WebConfigRender.cc - html rendering/assembly for Config File Editor
 *
 *
 ****************************************************************************/

#include "ink_platform.h"

#include "ink_hash_table.h"
#include "I_Version.h"
#include "SimpleTokenizer.h"

#include "WebConfigRender.h"
#include "WebHttpRender.h"

#include "MgmtUtils.h"
#include "LocalManager.h"

#include "INKMgmtAPI.h"
#include "CfgContextUtils.h"

//-------------------------------------------------------------------------
// Defines
//-------------------------------------------------------------------------

// Get rid of previous definition of MAX_RULE_SIZE.
#undef MAX_RULE_SIZE

#define MAX_RULE_SIZE         512
#define MAX_RULE_PART_SIZE    64
#define BORDER_COLOR          "#cccccc"

//-------------------- RULE LIST FUNCTIONS --------------------------------

//-------------------------------------------------------------------------
// writeSecondarySpecsTableElem
//-------------------------------------------------------------------------
// helper function that writes the following HTML: lists all the secondary
// specifiers in a table data element, one sec spec per line.
int
writeSecondarySpecsTableElem(textBuffer * output, char *time, char *src_ip, char *prefix, char *suffix, char *port,
                             char *method, char *scheme, char *mixt)
{
  char line[30];
  bool hasSspecs = false;

  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_LEFT, HTML_VALIGN_TOP, NULL, NULL, 0);
  if (strlen(time) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "time=%s", time);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }
  if (strlen(prefix) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "prefix=%s", prefix);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }
  if (strlen(suffix) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "suffix=%s", suffix);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }
  if (strlen(src_ip) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "src_ip=%s", src_ip);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }
  if (strlen(port) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "port=%s", port);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }
  if (strlen(method) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "method=%s", method);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }
  if (strlen(scheme) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "scheme=%s", scheme);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }
  if (strlen(mixt) > 0) {
    HtmlRndrSpace(output, 2);
    memset(line, 0, 30);
    snprintf(line, 30, "mixt tag=%s", mixt);
    output->copyFrom(line, strlen(line));
    HtmlRndrBr(output);
    hasSspecs = true;
  }

  if (!hasSspecs) {
    HtmlRndrSpace(output, 2);
  }

  HtmlRndrTdClose(output);

  return WEB_HTTP_ERR_OKAY;
}

//------------------------- SELECT FUNCTIONS ------------------------------

//-------------------------------------------------------------------------
// writeRuleTypeSelect
//-------------------------------------------------------------------------
void
writeRuleTypeSelect_cache(textBuffer * html, const char *listName)
{
  const char *options[7];
  options[0] = "never-cache";
  options[1] = "ignore-no-cache";
  options[2] = "ignore-client-no-cache";
  options[3] = "ignore-server-no-cache";
  options[4] = "pin-in-cache";
  options[5] = "revalidate";
  options[6] = "ttl-in-cache";

  HtmlRndrSelectList(html, listName, options, 7);
}
void
writeRuleTypeSelect_filter(textBuffer * html, const char *listName)
{
  const char *options[6];
  options[0] = "allow";
  options[1] = "deny";
  options[2] = "ldap";
  options[3] = "ntlm";
  options[4] = "radius";
  options[5] = "strip_hdr";

  HtmlRndrSelectList(html, listName, options, 6);
}

void
writeRuleTypeSelect_remap(textBuffer * html, const char *listName)
{
  const char *options[4];
  options[0] = "map";
  options[1] = "reverse_map";
  options[2] = "redirect";
  options[3] = "redirect_temporary";

  HtmlRndrSelectList(html, listName, options, 4);
}

void
writeRuleTypeSelect_socks(textBuffer * html, const char *listName)
{
  const char *options[3];
  options[0] = "no_socks";
  options[1] = "auth";
  options[2] = "multiple_socks";

  HtmlRndrSelectList(html, listName, options, 3);
}

void
writeRuleTypeSelect_bypass(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "bypass";
  options[1] = "deny_dyn_bypass";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeConnTypeSelect
//-------------------------------------------------------------------------
void
writeConnTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "tcp";
  options[1] = "udp";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeIpActionSelect
//-------------------------------------------------------------------------
void
writeIpActionSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "ip_allow";
  options[1] = "ip_deny";

  HtmlRndrSelectList(html, listName, options, 2);
}


//-------------------------------------------------------------------------
// writePdTypeSelect
//-------------------------------------------------------------------------
void
writePdTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[4];
  options[0] = "dest_domain";
  options[1] = "dest_host";
  options[2] = "dest_ip";
  options[3] = "url_regex";

  HtmlRndrSelectList(html, listName, options, 4);
}

void
writePdTypeSelect_splitdns(textBuffer * html, const char *listName)
{
  const char *options[3];
  options[0] = "dest_domain";
  options[1] = "dest_host";
  options[2] = "url_regex";

  HtmlRndrSelectList(html, listName, options, 3);
}

void
writePdTypeSelect_hosting(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "domain";
  options[1] = "hostname";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeMethodSelect
//-------------------------------------------------------------------------
// some files may/may not include the PUSH option in their list.
void
writeMethodSelect_push(textBuffer * html, const char *listName)
{
  // PUSH option is enabledwith proxy.config.http.push_method_enabled
  bool found;
  RecInt rec_int;
  found = (RecGetRecordInt("proxy.config.http.push_method_enabled", &rec_int)
           == REC_ERR_OKAY);
  int push_enabled = (int) rec_int;
  if (found && push_enabled) {  // PUSH enabled
    const char *options[6];
    options[0] = "";
    options[1] = "get";
    options[2] = "post";
    options[3] = "put";
    options[4] = "trace";
    options[5] = "PUSH";
    HtmlRndrSelectList(html, listName, options, 6);
  } else {
    writeMethodSelect(html, listName);  // PUSH disabled
  }
}

void
writeMethodSelect(textBuffer * html, const char *listName)
{
  const char *options[5];
  options[0] = "";
  options[1] = "get";
  options[2] = "post";
  options[3] = "put";
  options[4] = "trace";
  HtmlRndrSelectList(html, listName, options, 5);
}


//-------------------------------------------------------------------------
// writeSchemeSelect
//-------------------------------------------------------------------------
void
writeSchemeSelect(textBuffer * html, const char *listName)
{
  const char *options[5];
  options[0] = "";
  options[1] = "http";
  options[2] = "https";
  options[3] = "rtsp";
  options[4] = "mms";

  HtmlRndrSelectList(html, listName, options, 6);
}

void
writeSchemeSelect_partition(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "http";
  options[1] = "mixt";

  HtmlRndrSelectList(html, listName, options, 2);
}

void
writeSchemeSelect_remap(textBuffer * html, const char *listName)
{
  const char *options[4];
  options[0] = "http";
  options[1] = "https";
  options[2] = "rtsp";
  options[3] = "mms";

  HtmlRndrSelectList(html, listName, options, 5);
}

//-------------------------------------------------------------------------
// writeHeaderTypeSelect
//-------------------------------------------------------------------------
void
writeHeaderTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[5];
  options[0] = "";
  options[1] = "date";
  options[2] = "host";
  options[3] = "cookie";
  options[4] = "client_ip";

  HtmlRndrSelectList(html, listName, options, 5);
}

//-------------------------------------------------------------------------
// writeCacheTypeSelect
//-------------------------------------------------------------------------
void
writeCacheTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "parent";
  options[1] = "sibling";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeMcTtlSelect
//-------------------------------------------------------------------------
void
writeMcTtlSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "single subnet";
  options[1] = "multiple subnets";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeOnOffSelect
//-------------------------------------------------------------------------
void
writeOnOffSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "off";
  options[1] = "on";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeDenySelect
//-------------------------------------------------------------------------
void
writeDenySelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "";
  options[1] = "deny";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeClientGroupTypeSelect
//-------------------------------------------------------------------------
void
writeClientGroupTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[3];
  options[0] = "ip";
  options[1] = "domain";
  options[2] = "hostname";

  HtmlRndrSelectList(html, listName, options, 3);
}

//-------------------------------------------------------------------------
// writeAccessTypeSelect
//-------------------------------------------------------------------------
void
writeAccessTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[5];
  options[0] = "allow";
  options[1] = "deny";
  options[2] = "basic";
  options[3] = "generic";
  options[4] = "custom";

  HtmlRndrSelectList(html, listName, options, 5);
}

//-------------------------------------------------------------------------
// writeTreatmentTypeSelect
//-------------------------------------------------------------------------
void
writeTreatmentTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[7];
  options[0] = "";
  options[1] = "feed";
  options[2] = "push";
  options[3] = "pull";
  options[4] = "pullover";
  options[5] = "dynamic";
  options[6] = "post";

  HtmlRndrSelectList(html, listName, options, 7);
}

//-------------------------------------------------------------------------
// writeRoundRobinTypeSelect
//-------------------------------------------------------------------------
void
writeRoundRobinTypeSelect(textBuffer * html, const char *listName)
{
  const char *options[4];
  options[0] = "";
  options[1] = "true";
  options[2] = "strict";
  options[3] = "false";

  HtmlRndrSelectList(html, listName, options, 4);
}

void
writeRoundRobinTypeSelect_notrue(textBuffer * html, const char *listName)
{
  const char *options[3];
  options[0] = "";
  options[1] = "strict";
  options[2] = "false";

  HtmlRndrSelectList(html, listName, options, 3);
}


//-------------------------------------------------------------------------
// writeTrueFalseSelect
//-------------------------------------------------------------------------
void
writeTrueFalseSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "false";
  options[1] = "true";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeSizeFormatSelect
//-------------------------------------------------------------------------
void
writeSizeFormatSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "absolute";
  options[1] = "percent";

  HtmlRndrSelectList(html, listName, options, 2);
}

//-------------------------------------------------------------------------
// writeProtocolSelect
//-------------------------------------------------------------------------
void
writeProtocolSelect(textBuffer * html, const char *listName)
{
  const char *options[2];
  options[0] = "";
  options[1] = "dns";

  HtmlRndrSelectList(html, listName, options, 2);
}
