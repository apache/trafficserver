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

#include "inktomi++.h"
#include "ink_platform.h"
#include "ink_unused.h"    /* MAGIC_EDITING_TAG */

/* local includes */

#include "CLIeventHandler.h"
#include "WebMgmtUtils.h"
#include "WebOverview.h"
#include "FileManager.h"
#include "MgmtUtils.h"
#include "LocalManager.h"
#include "CliUtils.h"
#include "CLI.h"
#include "CLImonitor.h"
#include "CLIlineBuffer.h"

// Table of Variable/Descriptions for the monitor section
//
// The variables come from 'proxy/etc/trafficserver/records.conf' and
// the Descriptions from 'proxy/mgmt/html/protocols.stats.ink'
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
// Variable/Description table for monitor->protocols level
//
const
  CLI_globals::VarNameDesc
  CLI_monitor::mon_prot_desctable[NUM_PROT_DESCS] = {
  // HTTP User Agent - 4 pairs
  {"proxy.process.http.user_agent_response_document_total_size\\b", NULL,
   "Total Document Bytes", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.http.user_agent_response_header_total_size\\b", NULL,
   "Total Header Bytes", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.http.current_client_connections\\c", NULL,
   "Total Connections", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.http.current_client_transactions\\c", NULL,
   "Transcations In Progress", "%-*s %*s\n",
   10, 10, 50, 3},
  // HTTP Origin Server - 4 pairs
  {"proxy.process.http.origin_server_response_document_total_size\\b", NULL,
   "Total Document Bytes", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.http.origin_server_response_header_total_size\\b", NULL,
   "Total Header Bytes", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.http.current_server_connections\\c", NULL,
   "Total Connections", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.http.current_server_transactions\\c", NULL,
   "Transcations In Progress", "%-*s %*s\n",
   10, 10, 50, 3},
  // ICP - 11 pairs
  // Queries from this Node - 7
  {"proxy.process.icp.icp_query_requests\\c", NULL,
   "Query requests", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.icp.total_udp_send_queries\\c", NULL,
   "Query Messages Sent", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.icp.icp_query_hits\\c", NULL,
   "Peer Hit Messages Received", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.icp.icp_query_misses\\c", NULL,
   "Peer Miss Messages Received", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.icp.icp_remote_responses\\c", NULL,
   "Total Responses Received", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.icp.total_icp_response_time", NULL,
   "Average ICP Message Response time(ms)", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.icp.total_icp_request_time", NULL,
   "Average ICP Request Time(ms)", "%-*s %*s\n",
   10, 10, 50, 3},
  // Queries from ICP Peers - 4
  {"proxy.process.icp.icp_remote_query_requests\\c", NULL,
   "Query Messages Received", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.icp.cache_lookup_success\\c", NULL,
   "Remote Query Hits", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.icp.cache_lookup_fail\\c", NULL,
   "Remote Query Misses", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.icp.query_response_write\\c", NULL,
   "Sucessful Response Messges Sent to Peers", "%-*s %*s\n",
   10, 10, 50, 3},
// RNI Statitics - 13
  // RNI General - 5
  {"proxy.process.rni.object_count\\c", NULL,
   "Total Objects Served", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.rni.block_hit_count\\c", NULL,
   "Total Block Hits", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.rni.block_miss_count\\c", NULL,
   "Total Block Misses", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.rni.byte_hit_sum\\b", NULL,
   "Total Bytes Hit", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.rni.byte_miss_sum\\b", NULL,
   "Total Bytes Missed", "%-*s %*s\n",
   10, 10, 50, 3},
  // RNI client - 4
  {"proxy.process.rni.current_client_connections\\c", NULL,
   "Open Connections", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.rni.downstream_requests\\c", NULL,
   "Number of Requests", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.rni.downstream.request_bytes\\b", NULL,
   "Request Bytes", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.rni.downstream.response_bytes\\b", NULL,
   "Response Bytes", "%-*s %*s\n",
   10, 10, 50, 3},
  // RNI server - 4
  {"proxy.process.rni.current_server_connections\\c", NULL,
   "Open Connections", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.rni.upstream_requests\\c", NULL,
   "Number of Requests", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.rni.upstream.request_bytes\\b", NULL,
   "Request Bytes", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.rni.upstream.response_bytes\\b", NULL,
   "Response Bytes", "%-*s %*s\n",
   10, 10, 50, 3},
};

//
// Variable/Description table for monitor->node level
//
const
  CLI_globals::VarNameDesc
  CLI_monitor::mon_node_desctable[NUM_NODE_DESCS] = {
// Cache - 3
  {"proxy.node.cache_hit_ratio_avg_10s\\p", "proxy.cluster.cache_hit_ratio_avg_10s\\p",
   "Document Hit Rate(10 sec/avg)", "%-*s %*s %*s\n",
   15, 20, 30, 3},
  {"proxy.node.bandwidth_hit_ratio_avg_10s\\p", "proxy.cluster.bandwidth_hit_ratio_avg_10s\\p",
   "Bandwidth Savings(10 sec/avg)", "%-*s %*s %*s\n",
   15, 20, 30, 3},
  {"proxy.node.cache.percent_free\\p", "proxy.cluster.cache.percent_free\\p",
   "Cache Percent Free", "%-*s %*s %*s\n",
   15, 20, 30, 3},
// In Progress - 3
  {"proxy.node.current_server_connections\\c", "proxy.cluster.current_server_connections\\c",
   "Open Origin Server Connections", "%-*s %*s %*s\n",
   15, 20, 30, 3},
  {"proxy.node.current_client_connections\\c", "proxy.cluster.current_client_connections\\c",
   "Open Client Connections", "%-*s %*s %*s\n",
   15, 20, 30, 3},
  {"proxy.node.current_cache_connections\\c", "proxy.cluster.current_cache_connections\\c",
   "Cache Xfers In Progress", "%-*s %*s %*s\n",
   15, 20, 30, 3},
// Network - 2
  {"proxy.node.client_throughput_out", "proxy.cluster.client_throughput_out",
   "Client Throughput (MBits/sec)", "%-*s %*s %*s\n",
   15, 20, 30, 3},
  {"proxy.node.http.user_agent_xacts_per_second", "proxy.cluster.http.user_agent_xacts_per_second",
   "Transactions Per Second", "%-*s %*s %*s\n",
   15, 20, 30, 3},
// Name Resolution - 2
  {"proxy.node.dns.lookups_per_second", "proxy.cluster.dns.lookups_per_second",
   "DNS Lookups Per Second", "%-*s %*s %*s\n",
   15, 20, 30, 3},
  {"proxy.node.hostdb.hit_ratio_avg_10s\\p", "proxy.cluster.hostdb.hit_ratio_avg_10s\\p",
   "HostDB Hit Rate(10 sec/avg)", "%-*s %*s %*s\n",
   15, 20, 30, 3}
};

//
// Variable/Description table for monitor->cache level
//
const
  CLI_globals::VarNameDesc
  CLI_monitor::mon_cache_desctable[NUM_CACHE_DESCS] = {
// Cache - 20
  {"proxy.process.cache.bytes_used\\m", NULL,
   "Bytes Used(MB)", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cache.bytes_total\\m", NULL,
   "Cache Size(MB)", "%-*s %*s\n",
   10, 10, 50, 3},
  // Lookups
  {"proxy.process.cache.lookup.active\\c", NULL,
   "Lookups in Progress", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cache.lookup.success\\c", NULL,
   "Lookups Completed", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cache.lookup.failure\\c", NULL,
   "Lookup Misses", "%-*s %*s\n",
   10, 10, 50, 3},
  // Reads
  {"proxy.process.cache.read.active\\c", NULL,
   "Reads in Progress", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cache.read.success\\c", NULL,
   "Reads Completed", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cache.read.miss\\c", NULL,
   "Read Misses", "%-*s %*s\n",
   10, 10, 50, 3},
  // Writes
  {"proxy.process.cache.write.active\\c", NULL,
   "Writes in Progress", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cache.write.success\\c", NULL,
   "Writes Completed", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cache.write.cancel\\c", NULL,
   "Write Failures", "%-*s %*s\n",
   10, 10, 50, 3},
  // Updates
  {"proxy.process.cache.update.active\\c", NULL,
   "Updates in Progress", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cache.update.success\\c", NULL,
   "Updates Completed", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cache.update.failure\\c", NULL,
   "Update Failures", "%-*s %*s\n",
   10, 10, 50, 3},
  // Links
#if 0
  {"proxy.process.cache.link.active\\c", NULL,
   "Links in Progress", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cache.link.success\\c", NULL,
   "Link Sucesses", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cache.link.failure\\c", NULL,
   "Link Failures", "%-*s %*s\n",
   10, 10, 50, 3},
#endif
  // Removes
  {"proxy.process.cache.remove.active\\c", NULL,
   "Removes in Progress", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cache.remove.success\\c", NULL,
   "Remove Sucesses", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cache.remove.failure\\c", NULL,
   "Remove Failures", "%-*s %*s\n",
   10, 10, 50, 3}
};

//
// Variable/Description table for monitor->other level
//
const
  CLI_globals::VarNameDesc
  CLI_monitor::mon_other_desctable[NUM_OTHER_DESCS] = {
  // HOSTDB - 3
  {"proxy.process.hostdb.total_lookups\\c", NULL,
   "Total Lookups", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.hostdb.total_hits\\c", NULL,
   "Total Hits", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.hostdb.ttl", NULL,
   "Time TTL(min)", "%-*s %*s\n",
   10, 10, 50, 3},
  // DNS - 3
  {"proxy.process.dns.total_dns_lookups\\c", NULL,
   "DNS Total Look Ups", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.dns.lookup_avg_time", NULL,
   "Average Lookup Up Time (ms)", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.dns.lookup_successes\\c", NULL,
   "DNS Successes", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.dns.in_flight\\c", NULL,
   "Queries in flight", "%-*s %*s\n",
   10, 10, 50, 3},
  // CLUSTER - 6
  {"proxy.process.cluster.read_bytes\\m", NULL,
   "Bytes Read", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cluster.write_bytes\\m", NULL,
   "Bytes Written", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cluster.connections_open\\c", NULL,
   "Connections Open", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cluster.connections_opened\\c", NULL,
   "Total Operations", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cluster.net_backup\\c", NULL,
   "Network Backups", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.cluster.nodes\\c", NULL,
   "Clustering Nodes", "%-*s %*s\n",
   10, 10, 50, 3},
  // SOCKS - 3
  {"proxy.process.socks.connections_unsuccessful\\c", NULL,
   "Connections Unsuccessful", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.socks.connections_successful\\c", NULL,
   "Successful Connections", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.socks.connections_currently_open\\c", NULL,
   "Connections in progress", "%-*s %*s\n",
   10, 10, 50, 3},
  // LOGGING - 5 
  {"proxy.process.log2.log_files_open\\c", NULL,
   "Currently Open Log Files", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.log2.log_files_space_used\\b", NULL,
   "Space Used For Log Files", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.log2.event_log_access\\c", NULL,
   "Number of Access Events Logged", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.log2.event_log_access_skip\\c", NULL,
   "Number of Access Events Skipped", "%-*s %*s\n",
   10, 10, 50, 3},
  {"proxy.process.log2.event_log_error\\c", NULL,
   "Number of Error Events Logged", "%-*s %*s\n",
   10, 10, 50, 3}
};

//
// Handle displaying monitor->node statistics
//
void
CLI_monitor::doMonitorNodeStats(CLI_DATA * c_data /* IN: client data */ )
{
  char buf[128];
  char buf2[128];
  char tmpbuf[256];
  const char *line1 = "      Attribute                     Node Value          Cluster Value\n";
  const char *line2 = "                          CACHE \n";
  const char *line3 = "                        IN PROGRESS \n";
  const char *line4 = "                          NETWORK \n";
  const char *line5 = "                      NAME RESOLUTION \n";
  int highmark = 0;
  int i;

  Debug("cli_monitor", "Entering doMonitorNodeStats, c_data->cevent=%d\n", c_data->cevent);

  //  set response header
  c_data->output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
  CLI_globals::set_prompt(c_data->output, CL_MON_NODE);

  // output attribute/value header
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  c_data->output->copyFrom(line1, strlen(line1));

  // output CACHE header line
  highmark = NUM_NODE_CACHE_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_TWO == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line2, strlen(line2));
    c_data->output->copyFrom("\n", strlen("\n"));

    // now we need to get all the stats
    for (i = 0; i < highmark; i++) {
      if (varStrFromName(mon_node_desctable[i].name, buf, sizeof(buf)) == true
          && varStrFromName(mon_node_desctable[i].cname, buf2, sizeof(buf2)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), mon_node_desctable[i].format,
                 mon_node_desctable[i].desc_width, mon_node_desctable[i].desc,
                 mon_node_desctable[i].name_value_width, buf, mon_node_desctable[i].cname_value_width, buf2);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output IN PROGRESS header
  highmark += NUM_NODE_INPROG_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_THREE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line3, strlen(line3));
    c_data->output->copyFrom("\n", strlen("\n"));

    for (i = highmark - NUM_NODE_INPROG_DESCS; i < highmark; i++) {
      if (varStrFromName(mon_node_desctable[i].name, buf, sizeof(buf)) == true
          && varStrFromName(mon_node_desctable[i].cname, buf2, sizeof(buf2)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), mon_node_desctable[i].format,
                 mon_node_desctable[i].desc_width, mon_node_desctable[i].desc,
                 mon_node_desctable[i].name_value_width, buf, mon_node_desctable[i].cname_value_width, buf2);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output NETWORK header
  highmark += NUM_NODE_NETWORK_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_FOUR == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line4, strlen(line4));
    c_data->output->copyFrom("\n", strlen("\n"));

    // output Network stats
    for (i = highmark - NUM_NODE_NETWORK_DESCS; i < highmark; i++) {
      if (varStrFromName(mon_node_desctable[i].name, buf, sizeof(buf)) == true
          && varStrFromName(mon_node_desctable[i].cname, buf2, sizeof(buf2)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), mon_node_desctable[i].format,
                 mon_node_desctable[i].desc_width, mon_node_desctable[i].desc,
                 mon_node_desctable[i].name_value_width, buf, mon_node_desctable[i].cname_value_width, buf2);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output NAME RESOLUTION header
  highmark += NUM_NODE_NAMERES_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_FIVE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line5, strlen(line5));
    c_data->output->copyFrom("\n", strlen("\n"));

    // output Network stats
    for (i = highmark - NUM_NODE_NAMERES_DESCS; i < highmark; i++) {
      if (varStrFromName(mon_node_desctable[i].name, buf, sizeof(buf)) == true
          && varStrFromName(mon_node_desctable[i].cname, buf2, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), mon_node_desctable[i].format,
                 mon_node_desctable[i].desc_width, mon_node_desctable[i].desc,
                 mon_node_desctable[i].name_value_width, buf, mon_node_desctable[i].cname_value_width, buf2);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
  }
  // output trailing header
  c_data->output->copyFrom("\n", strlen("\n"));
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));

  Debug("cli_monitor", "Exiting doMonitorNodeStats\n");
}                               // end doMonitorNodeStats()

//
// Handle displaying monitor->protocol statistics
//
void
CLI_monitor::doMonitorProtocolStats(CLI_DATA * c_data /* IN: client data */ )
{
  char buf[128];
  char tmpbuf[256];
  const char *line1 = "      Attribute                                 Current Value\n";
  const char *line2 = "                              HTTP \n";
  const char *line3 = "                             Client \n";
  const char *line4 = "                             Server \n";
  const char *line6 = "                              ICP \n";
  const char *line10 = "              Transaction Frequency and Speeds \n";
  const char *line11 = "Transaction Type              Frequency        Speed(ms)\n";
  const char *line12 = "                 Queries Originating From This Node \n";
  const char *line13 = "                 Queries Originating From ICP Peers\n";
  // const char *line14 = "                           Operations\n";
#if 0
  const char *line15 = "                             WCCP\n";
  const char *line16 = "                      Router Statistics\n";
  const char *line17 = "                      Node Statistics\n";
  const char *line18 = "                      Protocol Statistics\n";
#endif
  const char *line19 = "                              RTSP\n";
  const char *line20 = "                      Client Statistics\n";
  const char *line21 = "                      Server Statistics\n";
  int highmark = 0;
  int i;

  NOWARN_UNUSED(line10);
  NOWARN_UNUSED(line11);
  Debug("cli_monitor", "Entering doMonitorProtocolStats, c_data->cevent=%d\n", c_data->cevent);

  //  set response header
  c_data->output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
  CLI_globals::set_prompt(c_data->output, CL_MON_PROTOCOLS);

  // output attribute/value header
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  c_data->output->copyFrom(line1, strlen(line1));

  // output HTTP header line and User Agent section
  highmark = NUM_PROT_HTTP_UA_DESCS;

  if (CL_EV_ONE == c_data->cevent || CL_EV_TWO == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line2, strlen(line2));
    c_data->output->copyFrom(" \n", strlen(" \n"));
    c_data->output->copyFrom(line3, strlen(line3));
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));

    // now we need to get all the stats
    for (i = 0; i < highmark; i++) {
      if (varStrFromName(mon_prot_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), mon_prot_desctable[i].format,
                 mon_prot_desctable[i].desc_width, mon_prot_desctable[i].desc,
                 mon_prot_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // Origin Server section
  highmark += NUM_PROT_HTTP_OS_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_TWO == c_data->cevent) {
    c_data->output->copyFrom(" \n", strlen(" \n"));
    c_data->output->copyFrom(line4, strlen(line4));
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));

    // now we need to get all the stats
    for (i = highmark - NUM_PROT_HTTP_OS_DESCS; i < highmark; i++) {
      if (varStrFromName(mon_prot_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), mon_prot_desctable[i].format,
                 mon_prot_desctable[i].desc_width, mon_prot_desctable[i].desc,
                 mon_prot_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }

    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output ICP header
  highmark += NUM_PROT_ICP_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_FOUR == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line6, strlen(line6));
    c_data->output->copyFrom(line12, strlen(line12));
    c_data->output->copyFrom("\n", strlen("\n"));

    // output ICP stats
    for (i = highmark - NUM_PROT_ICP_DESCS; i < highmark; i++) {
      if (varStrFromName(mon_prot_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), mon_prot_desctable[i].format,
                 mon_prot_desctable[i].desc_width, mon_prot_desctable[i].desc,
                 mon_prot_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
      if ((highmark - NUM_PROT_ICP_DESCS + 6) == i) {
        c_data->output->copyFrom("\n", strlen("\n"));
        c_data->output->copyFrom(line13, strlen(line13));
        c_data->output->copyFrom("\n", strlen("\n"));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // Check if RNI enabled 
  if (3 == c_data->advui || 2 == c_data->advui) {       // yes, so show stats
    highmark += NUM_PROT_RNI_DESCS;
    if (CL_EV_ONE == c_data->cevent || CL_EV_SEVEN == c_data->cevent) {
      c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
      c_data->output->copyFrom(line19, strlen(line19));

      // output RNI stats
      for (i = highmark - NUM_PROT_RNI_DESCS; i < highmark; i++) {
        if (varStrFromName(mon_prot_desctable[i].name, buf, sizeof(buf)) == true) {
          snprintf(tmpbuf, sizeof(tmpbuf), mon_prot_desctable[i].format,
                   mon_prot_desctable[i].desc_width, mon_prot_desctable[i].desc,
                   mon_prot_desctable[i].name_value_width, buf);
          c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
        }
        if ((highmark - NUM_PROT_RNI_DESCS + 4) == i) {
          c_data->output->copyFrom(line20, strlen(line20));
        } else if ((highmark - NUM_PROT_RNI_DESCS + 8) == i) {
          c_data->output->copyFrom(line21, strlen(line21));
        }
      }
    }

  }
  // output trailing header
  c_data->output->copyFrom("\n", strlen("\n"));
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));

  Debug("cli_monitor", "Exiting doMonitorProtocolStats\n");
}                               // end doMonitorProtocolStats()

//
// Handle displaying monitor->cache statistics
//
void
CLI_monitor::doMonitorCacheStats(CLI_DATA * c_data /* IN: client data */ )
{
  char buf[128];
  char tmpbuf[256];
  const char *line1 = "      Attribute                                         Value\n";
  const char *line2 = "                             CACHE \n";
  int highmark = 0;
  int i;

  Debug("cli_monitor", "Entering doMonitorCacheStats, c_data->cevent=%d\n", c_data->cevent);

  //  set response header
  c_data->output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
  CLI_globals::set_prompt(c_data->output, CL_MON_CACHE);

  // output attribute/value header
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  c_data->output->copyFrom(line1, strlen(line1));

  // output CACHE header line
  highmark = NUM_CACHE_DESCS;
  if (CL_EV_ONE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line2, strlen(line2));
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));

    // now we need to get all the stats
    for (i = 0; i < highmark; i++) {
      if (varStrFromName(mon_cache_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), mon_cache_desctable[i].format,
                 mon_cache_desctable[i].desc_width, mon_cache_desctable[i].desc,
                 mon_cache_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
  }

  c_data->output->copyFrom("\n", strlen("\n"));
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));

  Debug("cli_monitor", "Exiting doMonitorCacheStats\n");
}                               // end doMonitorCacheStats()

//
// Handle displaying monitor->other statistics
//
void
CLI_monitor::doMonitorOtherStats(CLI_DATA * c_data /* IN: client data */ )
{
  char buf[128];
  char tmpbuf[256];
  const char *line1 = "      Attribute                                         Value\n";
  const char *line2 = "                          HOSTDB \n";
  const char *line3 = "                           DNS \n";
  const char *line4 = "                         CLUSTER \n";
  const char *line5 = "                          SOCKS \n";
  const char *line6 = "                         LOGGING \n";
  int highmark = 0;
  int i;

  Debug("cli_monitor", "Entering doMonitorOtherStats, c_data->cevent=%d\n", c_data->cevent);

  //  set response header
  c_data->output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
  CLI_globals::set_prompt(c_data->output, CL_MON_OTHER);

  // output attribute/value header
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  c_data->output->copyFrom(line1, strlen(line1));

  // output HOSTDB header line
  highmark = NUM_OTHER_HOSTDB_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_TWO == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line2, strlen(line2));
    c_data->output->copyFrom("\n", strlen("\n"));

    // now we need to get all the stats
    for (i = 0; i < highmark; i++) {
      if (varStrFromName(mon_other_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), mon_other_desctable[i].format,
                 mon_other_desctable[i].desc_width, mon_other_desctable[i].desc,
                 mon_other_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output DNS header line
  highmark += NUM_OTHER_DNS_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_THREE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line3, strlen(line3));
    c_data->output->copyFrom("\n", strlen("\n"));

    // now we need to get all the stats
    for (i = highmark - NUM_OTHER_DNS_DESCS; i < highmark; i++) {
      if (varStrFromName(mon_other_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), mon_other_desctable[i].format,
                 mon_other_desctable[i].desc_width, mon_other_desctable[i].desc,
                 mon_other_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output CLUSTER header line
  highmark += NUM_OTHER_CLUSTER_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_FOUR == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line4, strlen(line4));
    c_data->output->copyFrom("\n", strlen("\n"));

    // now we need to get all the stats
    for (i = highmark - NUM_OTHER_CLUSTER_DESCS; i < highmark; i++) {
      if (varStrFromName(mon_other_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), mon_other_desctable[i].format,
                 mon_other_desctable[i].desc_width, mon_other_desctable[i].desc,
                 mon_other_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output SOCKS header line
  highmark += NUM_OTHER_SOCKS_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_FIVE == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line5, strlen(line5));
    c_data->output->copyFrom("\n", strlen("\n"));

    // now we need to get all the stats
    for (i = highmark - NUM_OTHER_SOCKS_DESCS; i < highmark; i++) {
      if (varStrFromName(mon_other_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), mon_other_desctable[i].format,
                 mon_other_desctable[i].desc_width, mon_other_desctable[i].desc,
                 mon_other_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }
  // output LOGGING header line
  highmark += NUM_OTHER_LOG_DESCS;
  if (CL_EV_ONE == c_data->cevent || CL_EV_SIX == c_data->cevent) {
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));
    c_data->output->copyFrom(line6, strlen(line6));
    c_data->output->copyFrom("\n", strlen("\n"));

    // now we need to get all the stats
    for (i = highmark - NUM_OTHER_LOG_DESCS; i < highmark; i++) {
      if (varStrFromName(mon_other_desctable[i].name, buf, sizeof(buf)) == true) {
        snprintf(tmpbuf, sizeof(tmpbuf), mon_other_desctable[i].format,
                 mon_other_desctable[i].desc_width, mon_other_desctable[i].desc,
                 mon_other_desctable[i].name_value_width, buf);
        c_data->output->copyFrom(tmpbuf, strlen(tmpbuf));
      }
    }
    c_data->output->copyFrom("\n", strlen("\n"));
  }

  c_data->output->copyFrom("\n", strlen("\n"));
  c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));

  Debug("cli_monitor", "Exiting doMonitorOtherStats\n");
}                               // end doMonitorOtherStats()

//
// Handle displaying monitor->dashboard 
//
void
CLI_monitor::doMonitorDashboard(CLI_DATA * c_data /* IN: client data */ )
{
  const char *line1 = "No   Node           Node     Alarms       Objects      Transactions\n";
  const char *line2 = "     Name           Status                Served          per sec \n";
  // CLIlineBuffer* Obuf = NULL;

  Debug("cli_monitor", "Entering doMonitorDashboard, c_data->cevent=%d\n", c_data->cevent);

  //  set response header
  c_data->output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
  CLI_globals::set_prompt(c_data->output, CL_MON_DASHBOARD);

  // Need to change outputing of headers to use CLIlineBuffer()
  // Obuf = new CLIlineBuffer(6);

  if (CL_EV_ONE == c_data->cevent) {    // dashboard header 
    // output seperator
    c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
    c_data->output->copyFrom(line1, strlen(line1));
    c_data->output->copyFrom(line2, strlen(line2));
    c_data->output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));

    // show dashboard
    overviewGenerator->generateTableCLI(c_data->output);

    c_data->output->copyFrom("\n", strlen("\n"));
    c_data->output->copyFrom(CLI_globals::sep2, strlen(CLI_globals::sep2));
  } else if (CL_EV_DISPLAY == c_data->cevent) { // display list of alarms
    overviewGenerator->generateAlarmsTableCLI(c_data->output);
  } else if (CL_EV_CHANGE == c_data->cevent) {  // resolve an alarm
    resolveAlarmCLI(c_data->output, c_data->args);
  }
  //  if (Obuf)
  //    delete Obuf;

  Debug("cli_monitor", "Exiting doMonitorDashboar\n");
}                               // end doMonitorDashboard()
