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

/***************************************/
/****************************************************************************
 *
 *  Module:
 *
 *
 ****************************************************************************/

#include "libts.h"
#include "ink_platform.h"
#include "ink_unused.h"        /* MAGIC_EDITING_TAG */

/* local includes */

#include "CLIeventHandler.h"
#include "WebMgmtUtils.h"
#include "FileManager.h"
#include "MgmtUtils.h"
#include "LocalManager.h"
#include "CliUtils.h"
#include "CLI.h"
#include "CLIconfigure.h"

// Table of Variable/Descriptions for the Configure section
//
// The variables come from 'proxy/config/records.conf' and
// the Descriptions from 'mgmt/html/protocols.stats.ink'
// It would be nice to cosolidate these so that both
// the ASCII and HTML output could use the same info.
//
// For now we hard code it here but more than likely the
// info will migrate to seperate file once the CLI
// functionality is there. This should all be
// internationalized with LOCALE stuff(gettext() or catgets()) at some point.
//
// It would also be nice if all display textual info is in file
// that can be read in for each level/mode so that changing
// the layout/format could be more easily controlled.
// Unfortunately this might lead to having to develop infrastruture
// for an ascii display engine :-(
//


//
// Variable/Description table for configure->server level
//
const
  CLI_globals::VarNameDesc
  CLI_configure::conf_server_desctable[NUM_SERVER_DESCS] = {
  // Traffic Server - 4
  {"proxy.config.proxy_name", NULL,
   "Traffic Server Name", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.http.server_port", NULL,
   "Traffic Server Port", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.dns.search_default_domains", NULL,
   "Local Domain Expansion(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.http.enable_url_expandomatic", NULL,
   ".com Domain Expansion(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Web Management - 2
  {"proxy.config.admin.web_interface_port", NULL,
   "Traffic Manager Port (takes effect at restart)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.admin.ui_refresh_rate", NULL,
   "Refresh rate in Monitor mode (secs)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Virtual IP addressing - 1
  {"proxy.config.vmap.enabled", NULL,
   "Virtual IP (1=On,0=Off: takes effect at restart)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Auto-Configuration of browsers - 1
  {"proxy.config.admin.autoconf_port", NULL,
   "Auto-configuration port (takes effect at restart)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Throttling of Network connections - 1
  {"proxy.config.net.connections_throttle", NULL,
   "Maximum Number of Connections", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // SNMP - 1
  {"proxy.config.snmp.master_agent_enabled", NULL,
   "SNMP Master Agent(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Customizable Response Pages - 4
  {"proxy.config.body_factory.response_suppression_mode", NULL,
   "Suppress generated response pages", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.body_factory.enable_customizations", NULL,
   "Enable Custom Response Pages", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.body_factory.enable_logging", NULL,
   "Log Customization Activity to Error Log", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.body_factory.template_sets_dir", NULL,
   "Custom Response Page Template Directory", "%*d) %-*s %*s\n",
   10, 10, 50, 3}
};


//
// Variable/Description table for configure->protocols level
//
const
  CLI_globals::VarNameDesc
  CLI_configure::conf_protocols_desctable[NUM_CONF_PROTOCOLS_DESCS] = {
  // HTTP - 15
  // ->Keep-alive timeouts - 2
  {"proxy.config.http.keep_alive_no_activity_timeout_in", NULL,
   "Keep-Alive Timeout: Inbound (secs)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.http.keep_alive_no_activity_timeout_out", NULL,
   "Keep-Alive Timeout: Outbound (secs)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // ->Inactivty timeouts - 2
  {"proxy.config.http.transaction_no_activity_timeout_in", NULL,
   "Inactivity Timeout: Inbound (secs)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.http.transaction_no_activity_timeout_out", NULL,
   "Inactivity Timeout: Outbound (secs)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // ->Activity timeouts - 2
  {"proxy.config.http.transaction_active_timeout_in", NULL,
   "Activity Timeout: Inbound (secs)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.http.transaction_active_timeout_out", NULL,
   "Activity Timeout: Outbound (secs)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // -> Remove headers - 6
  {"proxy.config.http.anonymize_remove_from", NULL,
   "From(1=Yes,0=No)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.http.anonymize_remove_referer", NULL,
   "Referer(1=Yes,0=No)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.http.anonymize_remove_user_agent", NULL,
   "User-Agent(1=Yes,0=No)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.http.anonymize_remove_cookie", NULL,
   "Cookie(1=Yes,0=No)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.http.anonymize_other_header_list", NULL,
   "Comma-separated list of headers to remove", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.http.global_user_agent_header", NULL,
   "User-Agent string to send to all origin servers", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // -> Insert/Remove Client IP - 2
  {"proxy.config.http.anonymize_insert_client_ip", NULL,
   "Insert Client-IP headers(1=Yes,0=No)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.http.anonymize_remove_client_ip", NULL,
   "Remove Client-IP headers(1=Yes,0=No)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // -> HTTPS - 1
  {"proxy.config.http.ssl_ports", NULL,
   "Restrict SSL connections to ports", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
};


//
// Variable/Description table for configure->cache level
//
const
  CLI_globals::VarNameDesc
  CLI_configure::conf_cache_desctable[NUM_CONF_CACHE_DESCS] = {
  // Cache Activation - 2
  {"proxy.config.http.cache.http", NULL,
   "Enable HTTP caching(1=On,0=Off)", "%*d) %-*s %*s\n",
   15, 15, 50, 3},
  {"proxy.config.http.cache.ignore_client_no_cache", NULL,
   "Ignore user requests to bypass cache(1=On,0=Off)", "%*d) %-*s %*s\n",
   15, 15, 50, 3},
  // Storage - 2
  {"proxy.config.cache.limits.http.max_doc_size", NULL,
   "Maximum HTTP document size to cache (bytes)", "%*d) %-*s %*s\n",
   15, 15, 50, 3},
  {"proxy.config.cache.limits.http.max_alts", NULL,
   "Maximum number of alternates allowed for a URL", "%*d) %-*s %*s\n",
   15, 15, 50, 3},
  // Freshness - 4
  {"proxy.config.http.cache.when_to_revalidate", NULL,
   "Verify freshness by checking", "%*d) %-*s %*s\n",
   15, 15, 50, 3},
  {"proxy.config.http.cache.required_headers", NULL,
   "Minimum information needed to cache document", "%*d) %-*s %*s\n",
   15, 15, 50, 3},
  {"proxy.config.http.cache.heuristic_min_lifetime", NULL,
   "minimum life time (secs)", "%*d) %-*s %*s\n",
   15, 15, 50, 3},
  {"proxy.config.http.cache.heuristic_max_lifetime", NULL,
   "maximum life time (secs)", "%*d) %-*s %*s\n",
   15, 15, 50, 3},
  // Variable Content,  Do not cache - 2
  {"proxy.config.http.cache.cache_urls_that_look_dynamic", NULL,
   "to URLs that contain '?' or '/cgi-bin'(1=Yes,0=No)", "%*d) %-*s %*s\n",
   15, 15, 50, 3},
  {"proxy.config.http.cache.cache_responses_to_cookies", NULL,
   "to requests that contain cookies(1=Yes,0=No)", "%*d) %-*s %*s\n",
   15, 15, 50, 3},
  // Variable Content,  Do not serve - 4
  // and Match these HTTP header fields
  {"proxy.config.http.cache.enable_default_vary_headers", NULL,
   "Enable Alternates(1=Yes,0=No)", "%*d) %-*s %*s\n",
   15, 15, 50, 3},
  {"proxy.config.http.cache.vary_default_text", NULL,
   "if the request is for text", "%*d) %-*s %*s\n",
   15, 15, 50, 3},
  {"proxy.config.http.cache.vary_default_images", NULL,
   "if the request is for images", "%*d) %-*s %*s\n",
   15, 15, 50, 3},
  {"proxy.config.http.cache.vary_default_other", NULL,
   "if the request is for anything else", "%*d) %-*s %*s\n",
   15, 15, 50, 3}
};

//
// Variable/Description table for configure->security level
//
const
  CLI_globals::VarNameDesc
  CLI_configure::conf_security_desctable[NUM_CONF_SECURITY_DESCS] = {
  // ACCESS - 3
  {"proxy.config.admin.basic_auth", NULL,
   "Authentication (basic: 1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.admin.admin_user", NULL,
   "Administrator's ID", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.admin.admin_password", NULL,
   "Administrator's Password", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Firewall Configuration - 4
  {"proxy.config.socks.socks_needed", NULL,
   "SOCKS(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.socks.socks_server_ip_str", NULL,
   "SOCKS server IP address", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.socks.socks_server_port", NULL,
   "SOCKS server port", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.socks.socks_timeout", NULL,
   "SOCKS timeout (seconds)", "%*d) %-*s %*s\n",
   10, 10, 50, 3}
};


//
// Variable/Description table for configure->routing level
//
const
  CLI_globals::VarNameDesc
  CLI_configure::conf_rout_desctable[NUM_CONF_ROUT_DESCS] = {
  // Parent Caching - 2
  {"proxy.config.http.parent_proxy_routing_enable", NULL,
   "Parent Caching(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.http.parent_proxies", NULL,
   "Parent Cache:", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // ICP - 4
  {"proxy.config.icp.enabled", NULL,
   "ICP mode:", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.icp.icp_port", NULL,
   "ICP Port", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.icp.multicast_enabled", NULL,
   "ICP multicast enabled(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.icp.query_timeout", NULL,
   "ICP Query Timeout", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Reverse Proxy - 3
  {"proxy.config.reverse_proxy.enabled", NULL,
   "Server Acceleration(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.url_remap.remap_required", NULL,
   "Require Document Route Rewriting(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.header.parse.no_host_url_redirect", NULL,
   "URL to redirect requests without Host header", "%*d) %-*s %*s\n",
   10, 10, 50, 3}
};

//
// Variable/Description table for configure->hostdb level
//
const
  CLI_globals::VarNameDesc
  CLI_configure::conf_hostdb_desctable[NUM_CONF_HOSTDB_DESCS] = {
  // Hostdb Management - 5
  {"proxy.config.hostdb.lookup_timeout", NULL,
   "Lookup timeout(secs)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.hostdb.timeout", NULL,
   "Foreground timeout(secs)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.hostdb.verify_after", NULL,
   "Background timeout(secs)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.hostdb.fail.timeout", NULL,
   "Invalid host timeout(minutes)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.hostdb.re_dns_on_reload", NULL,
   "Re-DNS on Reload(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // DNS configuration - 2
  {"proxy.config.dns.lookup_timeout", NULL,
   "Resolve attempt timeout(secs)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.dns.retries", NULL,
   "Number of retries", "%*d) %-*s %*s\n",
   10, 10, 50, 3}
};


//
// Variable/Description table for configure->logging level
//
const
  CLI_globals::VarNameDesc
  CLI_configure::conf_logging_desctable[NUM_CONF_LOGGING_DESCS] = {
  // Event Logging - 1
  {"proxy.config.log2.logging_enabled", NULL,
   "Event Logging(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Log Management - 3
  {"proxy.config.log2.logfile_dir", NULL,
   "Log directory", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.max_space_mb_for_logs", NULL,
   "Log space limit (MB)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.max_space_mb_headroom", NULL,
   "Log space Headroom(MB)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Log Collation - 5
  {"proxy.local.log2.collation_mode", NULL,
   "Log collation", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.collation_host", NULL,
   "Log collation host", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.collation_port", NULL,
   "Log collation port", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.collation_secret", NULL,
   "Log collation secret", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.max_space_mb_for_orphan_logs", NULL,
   "Log space limit for orphan log files (MB)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Standard Event Log Formats -> Squid - 4
  {"proxy.config.log2.squid_log_enabled", NULL,
   "Squid Enabled(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.squid_log_is_ascii", NULL,
   "Squid Log file type(1=ASCII,0=Binary)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.squid_log_name", NULL,
   "Squid Log file name", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.squid_log_header", NULL,
   "Log file header", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Standard Event Log Formats -> Netscape Common - 4
  {"proxy.config.log2.common_log_enabled", NULL,
   "Netscape Common Enabled(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.common_log_is_ascii", NULL,
   "Netscape Common Log file type(1=ASCII,0=Binary)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.common_log_name", NULL,
   "Netscape Common Log file name", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.common_log_header", NULL,
   "Netscape Common Log file header", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Standard Event Log Formats -> Netscape Extended - 4
  {"proxy.config.log2.extended_log_enabled", NULL,
   "Netscape Extended Enabled(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.extended_log_is_ascii", NULL,
   "Netscape Extended Log file type(1=ASCII,0=Binary)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.extended_log_name", NULL,
   "Netscape Extended Log file name", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.extended_log_header", NULL,
   "Netscape Extended Log file header", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Standard Event Log Formats -> Netscape Extended2 - 4
  {"proxy.config.log2.extended2_log_enabled", NULL,
   "Netscape Extended2 Enabled(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.extended2_log_is_ascii", NULL,
   "Netscape Extended2 Log file type(1=ASCII,0=Binary)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.extended2_log_name", NULL,
   "Netscape Extended2 Log file name", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.extended2_log_header", NULL,
   "Netscape Extended2 Log file header", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Custom logs - 1
  {"proxy.config.log2.custom_logs_enabled", NULL,
   "Custom logs enabled", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Log file rolling
  {"proxy.config.log2.rolling_enabled", NULL,
   "Rolling Enabled(1=On,0=Off)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.rolling_offset_hr", NULL,
   "Roll offset hour(24hr):", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.rolling_interval_sec", NULL,
   "Roll interval(sec)", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.config.log2.auto_delete_rolled_files", NULL,
   "Auto-delete rolled log files when space is low", "%*d) %-*s %*s\n",
   10, 10, 50, 3},
  // Log Splitting - 1
  {"proxy.config.log2.separate_host_logs", NULL,
   "Host Log Splitting", "%*d) %-*s %*s\n",
   10, 10, 50, 3}
};


//
// Variable/Description table for configure->snapshots level
//
// NOTE: currently not handled
//
const
  CLI_globals::VarNameDesc
  CLI_configure::conf_snapshots_desctable[NUM_CONF_SNAPSHOTS_DESCS] = {
  //
  {"proxy.config.", NULL,
   "", "%*d) %-*s %*s\n",
   10, 10, 50, 3}
};


//
// Handle displaying configure->server level
//
void
CLI_configure::doConfigureServer(CLI_DATA * c_data /* IN: client data */ )
{
  char buf[128];
  char tmpbuf[256];
  const char *line1 = " No     Attribute                                       Value\n";
  const char *line2 = "                              SERVER \n";
  const char *line3 = "                          WEB MANAGEMENT \n";
  const char *line4 = "                      VIRTUAL IP ADDRESSING \n";
  const char *line5 = "                        AUTO CONFIGURATION \n";
  const char *line6 = "                  THROTTLING OF NETWORK CONNECTIONS \n";
  const char *line7 = "     The Traffic Server name is the DNS round-robin \n" "     hostname of your cluster \n\n";
  const char *line8 = "\n     The following two options control how the Traffic Server \n"
    "     handles unqualified hostnames in a URL.  Setting both \n"
    "     options expands a hostname first into the local domain \n" "     and secondarily into the .com domain.\n\n";
  const char *line11 = "                              SNMP \n";
  const char *line12 = "     If SNMP Master Agent is turned off, you will not be able \n"
    "     to access MIB-2 host information.\n\n";
  const char *line13 = "                  CUSTOMIZABLE RESPONSE PAGES\n";
  const char *line14 = "     0=Never \n" "     1=Always \n" "     2=When Transparent \n";
  const char *line15 = "     0=Turn Off \n"
    "     1=Enable Default Custom Pages\n" "     2=Enable Language-Targeted Custom Pages\n";

  int highmark = 0;
  int i;

  Debug("cli_configure", "Entering doConfigureServer, c_data->cevent=%d\n", c_data->cevent);

  //  set response header
  c_data->output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
  CLI_globals::set_prompt(c_data->output, CL_CONF_SERVER);

  // output attribute/value header
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  c_data->output->copyFrom(line1, strlen(line1));

  // output SERVER header line
  highmark = NUM_SERVER_TRAFFIC_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_TWO == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line2, strlen(line2));
    c_data->output->copyFrom(line7, strlen(line7));

    // now we need to get all the configuration variables
    for (i = 0; i < highmark; i++) {
      if (2 == i) {             // this kind of stuff is an ugly hack, oh well
        c_data->output->copyFrom(line8, strlen(line8));
      }

      if (varStrFromName(conf_server_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_server_desctable[i].format,
                 conf_server_desctable[i].no_width, i,
                 conf_server_desctable[i].desc_width, conf_server_desctable[i].desc,
                 conf_server_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // Output Web Management header
  highmark += NUM_SERVER_WEB_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_THREE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line3, strlen(line3));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_SERVER_WEB_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_server_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_server_desctable[i].format,
                 conf_server_desctable[i].no_width, i,
                 conf_server_desctable[i].desc_width, conf_server_desctable[i].desc,
                 conf_server_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // Output Virtual IP  header
  highmark += NUM_SERVER_VIP_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_FOUR == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line4, strlen(line4));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_SERVER_VIP_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_server_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_server_desctable[i].format,
                 conf_server_desctable[i].no_width, i,
                 conf_server_desctable[i].desc_width, conf_server_desctable[i].desc,
                 conf_server_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // Output Auto Configuration header
  highmark += NUM_SERVER_AUTOC_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_FIVE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line5, strlen(line5));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_SERVER_AUTOC_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_server_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_server_desctable[i].format,
                 conf_server_desctable[i].no_width, i,
                 conf_server_desctable[i].desc_width, conf_server_desctable[i].desc,
                 conf_server_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // Output Throttle header
  highmark += NUM_SERVER_THROTTLE_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_SIX == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line6, strlen(line6));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_SERVER_THROTTLE_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_server_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_server_desctable[i].format,
                 conf_server_desctable[i].no_width, i,
                 conf_server_desctable[i].desc_width, conf_server_desctable[i].desc,
                 conf_server_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
  }
  // SNMP
  highmark += NUM_SERVER_SNMP_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_SEVEN == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line11, strlen(line11));
    c_data->output->copyFrom(line12, strlen(line12));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_SERVER_SNMP_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_server_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_server_desctable[i].format,
                 conf_server_desctable[i].no_width, i,
                 conf_server_desctable[i].desc_width, conf_server_desctable[i].desc,
                 conf_server_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
  }
  // Customizable Response pages
  highmark += NUM_SERVER_CRP_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_EIGHT == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line13, strlen(line13));

    // now we need to get all the descriptions
    for (i = highmark - NUM_SERVER_CRP_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_server_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_server_desctable[i].format,
                 conf_server_desctable[i].no_width, i,
                 conf_server_desctable[i].desc_width, conf_server_desctable[i].desc,
                 conf_server_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
      if ((highmark - NUM_SERVER_CRP_DESCS) == i) {     // this kind of stuff is an ugly hack, oh well
        c_data->output->copyFrom(line14, strlen(line14));
      }
      // another ugly hack
      if ((highmark - NUM_SERVER_CRP_DESCS + 1) == i) {
        c_data->output->copyFrom(line15, strlen(line15));
      }
    }
  }

  c_data->output->copyFrom("\n", strlen("\n"));
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  Debug("cli_configure", "Exiting doConfigureServer\n");
}                               // end doConfigureServer()

//
// Handle displaying configure->protocols level
//
void
CLI_configure::doConfigureProtocols(CLI_DATA * c_data /* IN: client data */ )
{
  char buf[128];
  char tmpbuf[256];
  const char *line1 = " No     Attribute                                          Value\n";
  const char *line2 = "                            HTTP \n";
  const char *line4 = "\n      Keep-alive time-outs set how long idle keep-alive \n"
    "      connections remain open.\n\n";
  const char *line5 = "\n      Inactivity timeouts set how long the Traffic Server \n"
    "      waits to abort stalled transactions.\n\n";
  const char *line6 = "\n      Activity timeouts limit the duration of transactions.\n\n";
  const char *line7 = "\n      Remove HTTP headers to increase the privacy of your \n"
    "      site and users. Remove the following headers:\n\n";
  const char *line9 = "\n                            HTTPS \n";
  const char *line22 = "\n     Traffic Server can insert Client-ip headers to retain the \n"
    "     user's IP address through proxies. \n\n";
  int highmark = 0;
  int i;

  Debug("cli_configure", "Entering doConfigureProtocols, c_data->cevent=%d\n", c_data->cevent);

  //  set response header
  c_data->output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
  CLI_globals::set_prompt(c_data->output, CL_CONF_PROTOCOLS);

  // output attribute/value header
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  c_data->output->copyFrom(line1, strlen(line1));

  // output HTTP header line
  highmark = NUM_CONF_PROTOCOLS_HTTP_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_TWO == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line2, strlen(line2));
    c_data->output->copyFrom(line4, strlen(line4));

    // now we need to get all the configuration variables
    for (i = 0; i < highmark; i++) {
      if (i == (2 + highmark - NUM_CONF_PROTOCOLS_HTTP_DESCS)) {        // these are ugly hacks for now
        c_data->output->copyFrom(line5, strlen(line5));
      } else if (i == (4 + highmark - NUM_CONF_PROTOCOLS_HTTP_DESCS)) {
        c_data->output->copyFrom(line6, strlen(line6));
      } else if (i == (6 + highmark - NUM_CONF_PROTOCOLS_HTTP_DESCS)) {
        c_data->output->copyFrom(line7, strlen(line7));
      } else if (i == (11 + highmark - NUM_CONF_PROTOCOLS_HTTP_DESCS)) {
        c_data->output->copyFrom(line22, strlen(line22));
      } else if (i == (13 + highmark - NUM_CONF_PROTOCOLS_HTTP_DESCS)) {
        c_data->output->copyFrom(line9, strlen(line9));
      }

      if (varStrFromName(conf_protocols_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_protocols_desctable[i].format,
                 conf_protocols_desctable[i].no_width, i,
                 conf_protocols_desctable[i].desc_width, conf_protocols_desctable[i].desc,
                 conf_protocols_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  /* Removing RNI stuff for now.  These two variables have been
     either dropped or renamed.  Eventually, this section needs
     to be revisited to encompass QT and WMT as well. */

  /*
     // Real Networks
     if (3 == c_data->advui || 2 == c_data->advui)
     { // yes, so show configuration
     highmark += NUM_CONF_PROTOCOLS_RNI_DESCS;
     if (CL_EV_ONE == c_data->cevent ||  CL_EV_FIVE == c_data->cevent) {
     c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
     c_data->output->copyFrom(line23, strlen(line23));

     // now we need to get all the configuration variables
     for (i = highmark - NUM_CONF_PROTOCOLS_RNI_DESCS; i < highmark; i++) {
     if (varStrFromName(conf_protocols_desctable[i].name, buf, sizeof(buf)) == true) {
     sprintf(tmpbuf,conf_protocols_desctable[i].format,
     conf_protocols_desctable[i].no_width,i,
     conf_protocols_desctable[i].desc_width, conf_protocols_desctable[i].desc,
     conf_protocols_desctable[i].name_value_width, buf);
     c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
     }
     }
     c_data->output->copyFrom("\n", strlen("\n"));
     }
     }
   */

  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  Debug("cli_configure", "Exiting doConfigureProtocols\n");
}                               // end doConfigureProtocols()

//
// Handle displaying configure->cache level
//
void
CLI_configure::doConfigureCache(CLI_DATA * c_data /* IN: client data */ )
{
  char buf[128];
  char tmpbuf[256];
  const char *line1 = " No     Attribute                                                  Value\n";
  const char *line2 = "                              ACTIVATION\n";
  const char *line3 = "        If Alternates Enabled Then Vary On These Headers \n";
  const char *line5 = "                              FRESHNESS\n";
  const char *line6 = "                           VARIABLE CONTENT\n";
  const char *line7 = "     Do not cache objects served in response :\n";
  const char *line14 = "\n     Before the Traffic Server serves an object from its cache,\n"
    "     it can ask the original content server to verify the object's \n" "     freshness.\n\n";
  const char *line15 = "\n     Some web servers do not stamp the objects they serve with an\n"
    "     expiration date, but you can control whether Traffic Server \n"
    "     considers these cacheable and limit how long these objects are \n" "     considered fresh.\n\n";
  const char *line16 = "\n     If an object has no expiration date, leave it in the cache \n"
    "     for at least 6) but no more than 7).\n";
  const char *line19 = "     0=when the object has expired \n"
    "     1=when the object has expired, or has no expiration date\n" "     2=always \n" "     3=never \n";
  const char *line20 = "     0=nothing \n" "     1=a last-modified time \n" "     2=an explict lifetime \n";
  const char *line21 = "                              STORAGE\n";

  int highmark = 0;
  int i;

  Debug("cli_configure", "Entering doConfigureCache, c_data->cevent=%d\n", c_data->cevent);

  //  set response header
  c_data->output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
  CLI_globals::set_prompt(c_data->output, CL_CONF_CACHE);

  // output attribute/value header
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  c_data->output->copyFrom(line1, strlen(line1));

  // output Cache Activation header line
  highmark += NUM_CONF_CACHE_ACT_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_TWO == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line2, strlen(line2));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_CACHE_ACT_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_cache_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_cache_desctable[i].format,
                 conf_cache_desctable[i].no_width, i,
                 conf_cache_desctable[i].desc_width, conf_cache_desctable[i].desc,
                 conf_cache_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output Cache Storage header line
  highmark += NUM_CONF_CACHE_STORAGE_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_THREE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line21, strlen(line21));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_CACHE_STORAGE_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_cache_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_cache_desctable[i].format,
                 conf_cache_desctable[i].no_width, i,
                 conf_cache_desctable[i].desc_width, conf_cache_desctable[i].desc,
                 conf_cache_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output Cache Freshness header line
  highmark += NUM_CONF_CACHE_FRESH_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_FOUR == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line5, strlen(line5));
    c_data->output->copyFrom(line14, strlen(line14));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_CACHE_FRESH_DESCS; i < highmark; i++) {
      // 12
      if (i == (1 + highmark - NUM_CONF_CACHE_FRESH_DESCS)) {
        c_data->output->copyFrom(line15, strlen(line15));
        // 13
      } else if (i == (2 + highmark - NUM_CONF_CACHE_FRESH_DESCS)) {
        c_data->output->copyFrom(line16, strlen(line16));
      }

      if (varStrFromName(conf_cache_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_cache_desctable[i].format,
                 conf_cache_desctable[i].no_width, i,
                 conf_cache_desctable[i].desc_width, conf_cache_desctable[i].desc,
                 conf_cache_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
      // 11
      if (i == (highmark - NUM_CONF_CACHE_FRESH_DESCS)) {
        c_data->output->copyFrom(line19, strlen(line19));
      } else if (i == (1 + highmark - NUM_CONF_CACHE_FRESH_DESCS)) {
        c_data->output->copyFrom(line20, strlen(line20));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output Cache Varible content section
  highmark += NUM_CONF_CACHE_VARC_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_FIVE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line6, strlen(line6));
    c_data->output->copyFrom(line7, strlen(line7));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_CACHE_VARC_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_cache_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_cache_desctable[i].format,
                 conf_cache_desctable[i].no_width, i,
                 conf_cache_desctable[i].desc_width, conf_cache_desctable[i].desc,
                 conf_cache_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
      if (i == (1 + highmark - NUM_CONF_CACHE_VARC_DESCS)) {
        c_data->output->copyFrom("\n", strlen("\n"));
      }
      if (i == (2 + highmark - NUM_CONF_CACHE_VARC_DESCS)) {
        c_data->output->copyFrom(line3, strlen(line3));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }

  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  Debug("cli_configure", "Exiting doConfigureCache\n");
}                               // end doConfigureCache()

//
// Handle displaying configure->security level
//
void
CLI_configure::doConfigureSecurity(CLI_DATA * c_data /* IN: client data */ )
{
  char buf[128];
  char tmpbuf[256];
  const char *line1 = " No     Attribute                                        Value\n";
  const char *line2 = "                              ACCESS \n";
  const char *line3 = "                      FIREWALL CONFIGURATION \n";
  int highmark = 0;
  int i;

  Debug("cli_configure", "Entering doConfigureSecurity, c_data->cevent=%d\n", c_data->cevent);

  //  set response header
  c_data->output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
  CLI_globals::set_prompt(c_data->output, CL_CONF_SECURITY);

  // output attribute/value header
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  c_data->output->copyFrom(line1, strlen(line1));

  // output Access header line
  highmark += NUM_CONF_SECURITY_ACCESS_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_TWO == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line2, strlen(line2));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_SECURITY_ACCESS_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_security_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_security_desctable[i].format,
                 conf_security_desctable[i].no_width, i,
                 conf_security_desctable[i].desc_width, conf_security_desctable[i].desc,
                 conf_security_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output Firewall header line
  highmark += NUM_CONF_SECURITY_FIREW_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_THREE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line3, strlen(line3));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_SECURITY_FIREW_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_security_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_security_desctable[i].format,
                 conf_security_desctable[i].no_width, i,
                 conf_security_desctable[i].desc_width, conf_security_desctable[i].desc,
                 conf_security_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }

  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  Debug("cli_configure", "Exiting doConfigureSecurity\n");
}                               // end doConfigureSecurity()


//
// Handle displaying configure->routing level
//
void
CLI_configure::doConfigureRouting(CLI_DATA * c_data /* IN: client data */ )
{
  char buf[128];
  char tmpbuf[256];
  const char *line1 = " No     Attribute                                        Value\n";
  const char *line2 = "                              PARENT PROXY \n";
  const char *line3 = "                                  ICP \n";
  const char *line4 = "                             REVERSE PROXY \n";
  const char *line5 = "\n      The Traffic Server can be configured as an accelerated,\n"
    "      virtualweb server in front of one or many slower, \n"
    "      traditional web servers.  The settings below allow you \n"
    "      to enable and disable web server acceleration, andcontrol \n"
    "      how Traffic Server routes document requests to the backing \n" "      webservers.\n\n";
  const char *line6 = "     0=Disabled \n" "     1=Only Receive Queries \n" "     2=Send/Receive Queries \n";
  int highmark = 0;
  int i;

  Debug("cli_configure", "Entering doConfigureRouting, c_data->cevent=%d\n", c_data->cevent);

  //  set response header
  c_data->output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
  CLI_globals::set_prompt(c_data->output, CL_CONF_ROUTING);

  // output attribute/value header
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  c_data->output->copyFrom(line1, strlen(line1));

  // output Access header line
  highmark += NUM_CONF_ROUT_PARENT_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_TWO == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line2, strlen(line2));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_ROUT_PARENT_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_rout_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_rout_desctable[i].format,
                 conf_rout_desctable[i].no_width, i,
                 conf_rout_desctable[i].desc_width, conf_rout_desctable[i].desc,
                 conf_rout_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output ICP header line
  highmark += NUM_CONF_ROUT_ICP_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_THREE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line3, strlen(line3));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_ROUT_ICP_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_rout_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_rout_desctable[i].format,
                 conf_rout_desctable[i].no_width, i,
                 conf_rout_desctable[i].desc_width, conf_rout_desctable[i].desc,
                 conf_rout_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
      if (i == (highmark - NUM_CONF_ROUT_ICP_DESCS)) {
        c_data->output->copyFrom(line6, strlen(line6));
        c_data->output->copyFrom("\n", strlen("\n"));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // Output Reverse Proxy header
  highmark += NUM_CONF_ROUT_REVP_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_FOUR == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line4, strlen(line4));
    c_data->output->copyFrom(line5, strlen(line5));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_ROUT_REVP_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_rout_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_rout_desctable[i].format,
                 conf_rout_desctable[i].no_width, i,
                 conf_rout_desctable[i].desc_width, conf_rout_desctable[i].desc,
                 conf_rout_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }

  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  Debug("cli_configure", "Exiting doConfigureRouting\n");
}                               // end doConfigureRouting()

//
// Handle displaying configure->hostdb level
//
void
CLI_configure::doConfigureHostDB(CLI_DATA * c_data /* IN: client data */ )
{
  char buf[128];
  char tmpbuf[256];
  const char *line1 = " No     Attribute                                           Value\n";
  const char *line2 = "                        HOST DATABASE MANAGEMENT\n";
  const char *line3 = "                          DNS CONFIGURATION \n";
  const char *line4 = "\n     Setting the foreground timeout to greater than or equal \n"
    "     to the background timeout disables background refresh\n\n";
  int highmark = 0;
  int i;

  Debug("cli_configure", "Entering doConfigureHostDB, c_data->cevent=%d\n", c_data->cevent);

  //  set response header
  c_data->output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
  CLI_globals::set_prompt(c_data->output, CL_CONF_HOSTDB);

  // output attribute/value header
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  c_data->output->copyFrom(line1, strlen(line1));

  // output Host DB managment header line
  highmark += NUM_CONF_HOSTDB_MG_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_TWO == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line2, strlen(line2));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_HOSTDB_MG_DESCS; i < highmark; i++) {
      if (i == (1 + highmark - NUM_CONF_HOSTDB_MG_DESCS)) {     // ugly hack again
        c_data->output->copyFrom(line4, strlen(line4));
      }

      if (varStrFromName(conf_hostdb_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_hostdb_desctable[i].format,
                 conf_hostdb_desctable[i].no_width, i,
                 conf_hostdb_desctable[i].desc_width, conf_hostdb_desctable[i].desc,
                 conf_hostdb_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output DNS configuration header line
  highmark += NUM_CONF_HOSTDB_DNS_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_THREE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line3, strlen(line3));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_HOSTDB_DNS_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_hostdb_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_hostdb_desctable[i].format,
                 conf_hostdb_desctable[i].no_width, i,
                 conf_hostdb_desctable[i].desc_width, conf_hostdb_desctable[i].desc,
                 conf_hostdb_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }

  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  Debug("cli_configure", "Exiting doConfigureHostDB\n");
}                               // end doConfigureHostDB()

//
// Handle displaying configure->logging level
//
void
CLI_configure::doConfigureLogging(CLI_DATA * c_data /* IN: client data */ )
{
  char buf[128];
  char tmpbuf[256];
  const char *line1 = " No     Attribute                                          Value\n";
  const char *line2 = "                           EVENT LOGGING \n";
  const char *line3 = "                           LOG MANAGEMENT \n";
  const char *line4 = "                           LOG COLLATION \n";
  const char *line5 = "                           SQUID FORMAT \n";
  const char *line6 = "                       NETSCAPE COMMON FORMAT \n";
  const char *line7 = "                       NETSCAPE EXTENDED FORMAT \n";
  const char *line8 = "                       NETSCAPE EXTENDED2 FORMAT \n";
  const char *line9 = "                          LOG FILE ROLLING\n";
  const char *line10 = "                          LOG SPLITTING\n";
  const char *line11 = "     0=Inactive \n"
    "     1=Be a collation host \n"
    "     2=Send standard formats \n"
    "     3=Send custom non-xml formats \n" "     4=Send standard and custom non-xml formats \n";
  int highmark = 0;
  int i;

  Debug("cli_configure", "Entering doConfigureLogging, c_data->cevent=%d\n", c_data->cevent);

  //  set response header
  c_data->output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
  CLI_globals::set_prompt(c_data->output, CL_CONF_LOGGING);

  // output attribute/value header
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  c_data->output->copyFrom(line1, strlen(line1));

  // output  Event header line
  highmark += NUM_CONF_LOGGING_EVENT_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_TWO == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line2, strlen(line2));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_LOGGING_EVENT_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_logging_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_logging_desctable[i].format,
                 conf_logging_desctable[i].no_width, i,
                 conf_logging_desctable[i].desc_width, conf_logging_desctable[i].desc,
                 conf_logging_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }


    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output  Log Management header line
  highmark += NUM_CONF_LOGGING_LMG_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_THREE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line3, strlen(line3));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_LOGGING_LMG_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_logging_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_logging_desctable[i].format,
                 conf_logging_desctable[i].no_width, i,
                 conf_logging_desctable[i].desc_width, conf_logging_desctable[i].desc,
                 conf_logging_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output  Log Collation header line
  highmark += NUM_CONF_LOGGING_LC_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_FOUR == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line4, strlen(line4));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_LOGGING_LC_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_logging_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_logging_desctable[i].format,
                 conf_logging_desctable[i].no_width, i,
                 conf_logging_desctable[i].desc_width, conf_logging_desctable[i].desc,
                 conf_logging_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
      if ((highmark - NUM_CONF_LOGGING_LC_DESCS) == i) {        // this kind of stuff is an ugly hack, oh well
        c_data->output->copyFrom(line11, strlen(line11));
      }

    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output  Squid format header line
  highmark += NUM_CONF_LOGGING_SQUID_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_FIVE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line5, strlen(line5));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_LOGGING_SQUID_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_logging_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_logging_desctable[i].format,
                 conf_logging_desctable[i].no_width, i,
                 conf_logging_desctable[i].desc_width, conf_logging_desctable[i].desc,
                 conf_logging_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output  Netscape common format header line
  highmark += NUM_CONF_LOGGING_NSCPC_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_SIX == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line6, strlen(line6));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_LOGGING_NSCPC_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_logging_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_logging_desctable[i].format,
                 conf_logging_desctable[i].no_width, i,
                 conf_logging_desctable[i].desc_width, conf_logging_desctable[i].desc,
                 conf_logging_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output  Netscape extended format header line
  highmark += NUM_CONF_LOGGING_NSCPE_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_SEVEN == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line7, strlen(line7));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_LOGGING_NSCPE_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_logging_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_logging_desctable[i].format,
                 conf_logging_desctable[i].no_width, i,
                 conf_logging_desctable[i].desc_width, conf_logging_desctable[i].desc,
                 conf_logging_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output  Netscape extended2 format header line
  // Custom logging is also included here
  highmark += NUM_CONF_LOGGING_NSCPE2_DESCS + NUM_CONF_LOGGING_CUSTOM_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_EIGHT == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line8, strlen(line8));

    // now we need to get all the configuration variables
    for (i = highmark - (NUM_CONF_LOGGING_NSCPE2_DESCS + NUM_CONF_LOGGING_CUSTOM_DESCS); i < highmark; i++) {
      if (varStrFromName(conf_logging_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_logging_desctable[i].format,
                 conf_logging_desctable[i].no_width, i,
                 conf_logging_desctable[i].desc_width, conf_logging_desctable[i].desc,
                 conf_logging_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output  Log rolling format header line
  highmark += NUM_CONF_LOGGING_ROLL_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_NINE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line9, strlen(line9));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_LOGGING_ROLL_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_logging_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_logging_desctable[i].format,
                 conf_logging_desctable[i].no_width, i,
                 conf_logging_desctable[i].desc_width, conf_logging_desctable[i].desc,
                 conf_logging_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output  Log Splitting format header line
  highmark += NUM_CONF_LOGGING_SPLIT_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_NINE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line10, strlen(line10));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_LOGGING_SPLIT_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_logging_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_logging_desctable[i].format, conf_logging_desctable[i].no_width, i, conf_logging_desctable[i].desc_width, conf_logging_desctable[i].desc, conf_logging_desctable[i].name_value_width, buf);       /* coverity[non_const_printf_format_string] */
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }

  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  Debug("cli_configure", "Exiting doConfigureLogging\n");
}                               // end doConfigureLogging()

//
// Handle displaying configure->snapshots level
//
// NOTE: currently not handled
//
void
CLI_configure::doConfigureSnapshots(CLI_DATA * c_data /* IN: client data */ )
{
  char buf[128];
  char tmpbuf[256];
  const char *line1 = " No     Attribute                                        Value\n";
  const char *line2 = "                            SNAPSHOTS \n";
  int highmark = 0;
  int i;

  Debug("cli_configure", "Entering doConfigureSnapshots, c_data->cevent=%d\n", c_data->cevent);

  //  set response header
  c_data->output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
  CLI_globals::set_prompt(c_data->output, CL_CONF_SNAPSHOTS);

  // output attribute/value header
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  c_data->output->copyFrom(line1, strlen(line1));

  // output  header line
  highmark += NUM_CONF_SNAPSHOTS_DESCS;
  if (CL_EV_ONE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line2, strlen(line2));

    // now we need to get all the configuration variables
    for (i = highmark - NUM_CONF_SNAPSHOTS_DESCS; i < highmark; i++) {
      if (varStrFromName(conf_snapshots_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), conf_snapshots_desctable[i].format,
                 conf_snapshots_desctable[i].no_width, i,
                 conf_snapshots_desctable[i].desc_width, conf_snapshots_desctable[i].desc,
                 conf_snapshots_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));       /* coverity[non_const_printf_format_string] */
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }

  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));

  Debug("cli_configure", "Exiting doConfigureSnapshots\n");
}                               // end doConfigureSnapshots()
