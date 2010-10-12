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
 *  MgmtConverter.cc - Interface to convert XML <==> TS config files
 *
 ****************************************************************************/

#include "inktomi++.h"
#include "MgmtConverter.h"
#include "RecordsConfig.h"
#include "Main.h"
#include "WebMgmtUtils.h"
#include "CfgContextUtils.h"
#include "CoreAPI.h"            // for testing

#define SCHEMA_FILE "/home/lant/cnp/TrafficServer.xsd"

// each file entry in the table stores the info needed for that file in
// order to convert it to XML format
FileInfo file_info_entries[] = {
  {"proxy.config.cache.control.filename", INK_FNAME_CACHE_OBJ, &convertCacheRule_ts, &convertCacheRule_xml},
  {"proxy.config.icp.icp_configuration", INK_FNAME_ICP_PEER, &convertIcpRule_ts, &convertIcpRule_xml},
  {"proxy.config.url_remap.filename", INK_FNAME_REMAP, &convertRemapRule_ts, &convertRemapRule_xml},
  {"proxy.config.dns.splitdns.filename", INK_FNAME_SPLIT_DNS, &convertSplitDnsRule_ts, &convertSplitDnsRule_xml},
  {"proxy.config.cache.hosting_filename", INK_FNAME_HOSTING, &convertHostingRule_ts, &convertHostingRule_xml},
  {"proxy.config.cache.ip_allow.filename", INK_FNAME_IP_ALLOW, &convertIpAllowRule_ts, &convertIpAllowRule_xml},
  {"proxy.config.admin.ip_allow.filename", INK_FNAME_MGMT_ALLOW, &convertMgmtAllowRule_ts, &convertMgmtAllowRule_xml},
  {"proxy.config.http.parent_proxy.file", INK_FNAME_PARENT_PROXY, &convertParentRule_ts, &convertParentRule_xml},
  {"proxy.config.cache.partition_filename", INK_FNAME_PARTITION, &convertPartitionRule_ts, &convertPartitionRule_xml},
  {"proxy.config.socks.socks_config_file", INK_FNAME_SOCKS, &convertSocksRule_ts, &convertSocksRule_xml},
  {"proxy.config.update.update_configuration", INK_FNAME_UPDATE_URL, &convertUpdateRule_ts, &convertUpdateRule_xml},
  {"proxy.config.vmap.addr_file", INK_FNAME_VADDRS, &convertVaddrsRule_ts, &convertVaddrsRule_xml},
  {"proxy.config.http.congestion_control.filename", INK_FNAME_CONGESTION, &convertCongestionRule_ts,
   &convertCongestionRule_xml},
  {"proxy.config.admin.access_control_file", INK_FNAME_ADMIN_ACCESS, &convertAdminAccessRule_ts,
   &convertAdminAccessRule_xml},
  {"proxy.config.cache.storage_filename", INK_FNAME_STORAGE, &convertStorageRule_ts, &convertStorageRule_xml}
};

int num_file_entries = SIZE(file_info_entries);

static InkHashTable *file_info_ht = 0;

// This is for TESTING ONLY!! (used in testConvertFile_ts)
const char *config_files[] = {
  "admin_access.config",
  "bypass.config",
  "cache.config",
  "congestion.config",
  "hosting.config",
  "icp.config",
  "ip_allow.config",
  "ipnat.conf",
  "mgmt_allow.config",
  "parent.config",
  "partition.config",
  "remap.config",
  "socks.config",
  "splitdns.config",
  "storage.config",
  "update.config",
  "vaddrs.config",
  NULL
};


// ---------------------------------------------------------------------
// converterInit
// ---------------------------------------------------------------------
// Need to create hashtable that maps the record name used in the
// XML instance file with the file_info_entries.
void
converterInit()
{
  int i, j;
  InkHashTableValue hash_value;
  // This isn't used.
  //FileInfo *file_info;
  XMLDom schema;
  XMLNode *ts_node, *seq_node, *file_node;
  char *schema_name, *record_name;


  // Step 1:
  // Create a temporary hashtable from file_info_entries list where:
  // key = record_name (eg. proxy.config.cache.filename)
  // value = corresponding FileInfo struct
  InkHashTable *temp_info_ht = ink_hash_table_create(InkHashTableKeyType_String);
  for (i = 0; i < num_file_entries; i++) {
    ink_hash_table_insert(temp_info_ht, file_info_entries[i].record_name, &(file_info_entries[i]));
  }

  // Step 2: THIS IS DEPENDENT ON STRUCTURE OF XML SCHEMA
  // Parse the trafficServer schema tag which should specify the
  // file element name (eg. arm_security_file) and a record_name
  // attribute (eg. proxy.config.arm.security_filename)
  //    - create a new hashtable where:
  //      key = schema file element name
  //      value = corresponding FileInfo struct (which is located by
  //      looking up the hashtable in 1) using the record_name attribute
  file_info_ht = ink_hash_table_create(InkHashTableKeyType_String);
  schema.LoadFile(SCHEMA_FILE);
  for (i = 0; i < schema.getChildCount(); i++) {
    ts_node = schema.getChildNode(i);
    if (ts_node->getAttributeValueByName("name") &&
        strcmp(ts_node->getAttributeValueByName("name"), "trafficserver") == 0) {
      seq_node = ts_node->getNodeByPath("xs:complexType/xs:all");
      if (seq_node) {
        for (j = 0; j < seq_node->getChildCount(); j++) {
          file_node = seq_node->getChildNode(j);
          schema_name = file_node->getAttributeValueByName("name");
          record_name = file_node->getAttributeValueByName("type");
          if (record_name && *record_name) {
            record_name = strstr(record_name, "proxy"); // eliminate "ts:" prefix
          }
          if (record_name && ink_hash_table_lookup(temp_info_ht, (InkHashTableKey) record_name, &hash_value) != 0) {
            ink_hash_table_insert(file_info_ht, schema_name, hash_value);
          } else {
            Warning("[MgmtConverter::converterInit] There is no file info entry for the schema tag filename %s",
                    record_name);
          }
        }
      }
      break;
    }
  }

  // free temporary hashtable
  ink_hash_table_destroy(temp_info_ht);

}


// ---------------------------------------------------------------------
// convertFile_xml
// ---------------------------------------------------------------------
// Purpose: Converts the entire XML tree into the TS  file format;
//          does not directly write result to disk; stores the
//          converted text in "file" parameter.
// Input: file_node - xml root node for all the rules
//        file - buffer that converted text is stored
// Output:  returns INK_ERR_OKAY if all the rules converted correctly
//          If there is a problem converting a rule, the rule is simply
//          skipped, and INK_ERR_FAIL is returned
char *
convertFile_xml(XMLNode * file_node)
{
  XMLNode *child;
  textBuffer ts_file(1024);
  char *rule = NULL;
  char *filename = NULL;
  int i;

  if (!file_node) {
    Error("[MgmtConverter::convertFile_xml] invalid parameters");
    return 0;
  }

  RuleConverter_xml converter;
  FileInfo *info = NULL;
  InkHashTableValue lookup;
  // iterate through each of the "rule" nodes
  filename = file_node->getNodeName();
  for (i = 0; i < file_node->getChildCount(); i++) {
    child = file_node->getChildNode(i);

    // get file information by doing table lookup
    if (ink_hash_table_lookup(file_info_ht, filename, &lookup)) {
      info = (FileInfo *) lookup;
      converter = info->converter_xml;
      rule = converter(child);
    } else {
      Debug("convert", "[convertFile_xml] No converter function for %s", filename);
      return 0;
    }

    if (rule) {
      if (*rule) {
        ts_file.copyFrom(rule, strlen(rule));
        ts_file.copyFrom("\n", 1);
      }
      xfree(rule);
    } else {                    // ERROR: converting
      Debug("convert", "[convertFile_xml] Error converting XML rule %d", i);
      return 0;
    }
  }

  return xstrdup(ts_file.bufPtr());
}

// ---------------------------------------------------------------------
// convertAdminAccessRule_xml
// ---------------------------------------------------------------------
char *
convertAdminAccessRule_xml(XMLNode * rule_node)
{
  INKAdminAccessEle *ele = NULL;
  char *val;

  if (!rule_node)
    return NULL;

  ele = INKAdminAccessEleCreate();

  val = rule_node->getAttributeValueByName("access");
  if (val)
    ele->access = string_to_admin_acc_type(val);
  val = rule_node->getAttributeValueByName("user");
  if (val)
    ele->user = xstrdup(val);
  val = rule_node->getAttributeValueByName("password");
  if (val)
    ele->password = xstrdup(val);

  AdminAccessObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}



// ---------------------------------------------------------------------
// convertCacheRule_xml
// ---------------------------------------------------------------------
char *
convertCacheRule_xml(XMLNode * rule_node)
{
  XMLNode *child;
  INKCacheEle *ele = NULL;
  char *name = NULL, *type = NULL;

  if (!rule_node)
    return NULL;

  type = rule_node->getNodeName();
  ele = INKCacheEleCreate(INK_TYPE_UNDEFINED);
  if (strcmp(type, "never-cache") == 0) {
    ele->cfg_ele.type = INK_CACHE_NEVER;
  } else if (strcmp(type, "ignore-no-cache") == 0) {
    ele->cfg_ele.type = INK_CACHE_IGNORE_NO_CACHE;
  } else if (strcmp(type, "ignore-client-no-cache") == 0) {
    ele->cfg_ele.type = INK_CACHE_IGNORE_CLIENT_NO_CACHE;
  } else if (strcmp(type, "ignore-server-no-cache") == 0) {
    ele->cfg_ele.type = INK_CACHE_IGNORE_SERVER_NO_CACHE;
  } else if (strcmp(type, "pin-in-cache") == 0) {
    ele->cfg_ele.type = INK_CACHE_PIN_IN_CACHE;
  } else if (strcmp(type, "revalidate") == 0) {
    ele->cfg_ele.type = INK_CACHE_REVALIDATE;
  } else if (strcmp(type, "ttl-in-cache") == 0) {
    ele->cfg_ele.type = INK_CACHE_TTL_IN_CACHE;
  } else {
    if (ele)
      INKCacheEleDestroy(ele);
    return NULL;
  }

  // iterate through each subelement of a rule node
  for (int i = 0; i < rule_node->getChildCount(); i++) {
    child = rule_node->getChildNode(i);
    name = child->getNodeName();
    if (strcmp(name, "pdss") == 0) {
      convertPdssFormat_xml(child, &(ele->cache_info));
    } else if (strcmp(name, "time_period") == 0) {
      convertTimePeriod_xml(child, &(ele->time_period));
    }
  }

  // convert Ele into "one-liner" text format
  CacheObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}

// ---------------------------------------------------------------------
// convertCongestionRule_xml
// ---------------------------------------------------------------------
char *
convertCongestionRule_xml(XMLNode * rule_node)
{
  INKCongestionEle *ele = NULL;
  char *val, *name;

  ele = INKCongestionEleCreate();

  val = rule_node->getAttributeValueByName("prefix");
  if (val)
    ele->prefix = xstrdup(val);
  val = rule_node->getAttributeValueByName("port");
  if (val)
    ele->port = ink_atoi(val);
  val = rule_node->getAttributeValueByName("scheme");
  if (val)
    ele->scheme = string_to_congest_scheme_type(val);
  val = rule_node->getAttributeValueByName("max_connection_failures");
  if (val)
    ele->max_connection_failures = ink_atoi(val);
  val = rule_node->getAttributeValueByName("fail_window");
  if (val)
    ele->fail_window = ink_atoi(val);
  val = rule_node->getAttributeValueByName("proxy_retry_interval");
  if (val)
    ele->proxy_retry_interval = ink_atoi(val);
  val = rule_node->getAttributeValueByName("client_wait_interval");
  if (val)
    ele->client_wait_interval = ink_atoi(val);
  val = rule_node->getAttributeValueByName("live_os_conn_timeout");
  if (val)
    ele->live_os_conn_timeout = ink_atoi(val);
  val = rule_node->getAttributeValueByName("live_os_conn_retries");
  if (val)
    ele->live_os_conn_retries = ink_atoi(val);
  val = rule_node->getAttributeValueByName("dead_os_conn_timeout");
  if (val)
    ele->dead_os_conn_timeout = ink_atoi(val);
  val = rule_node->getAttributeValueByName("dead_os_conn_retries");
  if (val)
    ele->dead_os_conn_retries = ink_atoi(val);
  val = rule_node->getAttributeValueByName("max_connection");
  if (val)
    ele->max_connection = ink_atoi(val);
  val = rule_node->getAttributeValueByName("error_page_uri");
  if (val)
    ele->error_page_uri = xstrdup(val);

  name = rule_node->getChildNode(0)->getNodeName();
  val = rule_node->getChildNode(0)->getNodeValue();
  if (strcmp(name, "dest_domain") == 0) {
    ele->pd_type = INK_PD_DOMAIN;
  } else if (strcmp(name, "dest_host") == 0) {
    ele->pd_type = INK_PD_HOST;
  } else if (strcmp(name, "dest_ip") == 0) {
    ele->pd_type = INK_PD_IP;
  } else if (strcmp(name, "host_regex") == 0) {
    ele->pd_type = INK_PD_URL_REGEX;
  }
  ele->pd_val = xstrdup(val);

  // convert Ele into "one-liner" text format
  CongestionObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}


// ---------------------------------------------------------------------
// convertHostingRule_xml
// ---------------------------------------------------------------------
char *
convertHostingRule_xml(XMLNode * rule_node)
{
  INKHostingEle *ele = NULL;
  XMLNode *child;
  char *name, *val;

  if (!rule_node)
    return NULL;

  // iterate through each subelement of a rule node
  ele = INKHostingEleCreate();
  for (int i = 0; i < rule_node->getChildCount(); i++) {
    child = rule_node->getChildNode(i);
    name = child->getNodeName();
    val = child->getNodeValue();
    if (strcmp(name, "domain") == 0) {
      ele->pd_type = INK_PD_DOMAIN;
      ele->pd_val = xstrdup(val);
    } else if (strcmp(name, "host") == 0) {
      ele->pd_type = INK_PD_HOST;
      ele->pd_val = xstrdup(val);
    } else if (strcmp(name, "partitions") == 0) {
      ele->partitions = string_to_int_list(val, " ");
    }
  }

  // convert Ele into "one-liner" text format
  HostingObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}

// ---------------------------------------------------------------------
// convertIcpRule_xml
// ---------------------------------------------------------------------
char *
convertIcpRule_xml(XMLNode * rule_node)
{
  // This isn't used.
  //XMLNode *host_node;
  XMLNode *child;
  INKIcpEle *ele = NULL;
  char *name, *port_val, *child_val, *type = NULL;

  if (!rule_node)
    return NULL;

  // create the Ele and determine what type it is
  type = rule_node->getNodeName();
  ele = INKIcpEleCreate();
  if (strcmp(type, "parent") == 0) {
    ele->peer_type = INK_ICP_PARENT;
  } else if (strcmp(type, "sibling") == 0) {
    ele->peer_type = INK_ICP_SIBLING;
  } else {
    if (ele)
      INKIcpEleDestroy(ele);
    return NULL;
  }

  // process port attributes
  port_val = rule_node->getAttributeValueByName("proxy_port");
  if (port_val)
    ele->peer_proxy_port = ink_atoi(port_val);
  port_val = rule_node->getAttributeValueByName("icp_port");
  if (port_val) {
    ele->peer_icp_port = ink_atoi(port_val);
  }
  // iterate through each subelement of a rule node
  for (int i = 0; i < rule_node->getChildCount(); i++) {
    child = rule_node->getChildNode(i);
    name = child->getNodeName();
    child_val = child->getNodeValue();
    if (strcmp(name, "hostip") == 0) {
      ele->peer_host_ip_addr = string_to_ip_addr(child_val);
    } else if (strcmp(name, "hostname") == 0) {
      ele->peer_hostname = xstrdup(child_val);
    } else if (strcmp(name, "multicast") == 0) {
      ele->is_multicast = true;
      child_val = child->getAttributeValueByName("ip");
      if (child_val)
        ele->mc_ip_addr = string_to_ip_addr(child_val);
      child_val = child->getAttributeValueByName("time_to_live");
      if (child_val) {
        if (strcmp(child_val, "single_subnet") == 0)
          ele->mc_ttl = INK_MC_TTL_SINGLE_SUBNET;
        else
          ele->mc_ttl = INK_MC_TTL_MULT_SUBNET;
      }
    }
  }

  // convert Ele into "one-liner" text format
  IcpObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}

// ---------------------------------------------------------------------
// convertIpAllowRule_xml
// ---------------------------------------------------------------------
char *
convertIpAllowRule_xml(XMLNode * rule_node)
{
  INKIpAllowEle *ele = NULL;
  char *type = NULL;

  if (!rule_node)
    return NULL;

  ele = INKIpAllowEleCreate();
  type = rule_node->getNodeName();
  if (strcmp(type, "allow") == 0) {
    ele->action = INK_IP_ALLOW_ALLOW;
  } else if (strcmp(type, "deny") == 0) {
    ele->action = INK_IP_ALLOW_DENY;
  }

  ele->src_ip_addr = INKIpAddrEleCreate();
  convertIpAddrEle_xml(rule_node, ele->src_ip_addr);

  IpAllowObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}


// ---------------------------------------------------------------------
// convertMgmtAllowRule_xml
// ---------------------------------------------------------------------
char *
convertMgmtAllowRule_xml(XMLNode * rule_node)
{
  INKMgmtAllowEle *ele = NULL;
  char *type = NULL;

  if (!rule_node)
    return NULL;

  ele = INKMgmtAllowEleCreate();
  type = rule_node->getNodeName();
  if (strcmp(type, "allow") == 0) {
    ele->action = INK_MGMT_ALLOW_ALLOW;
  } else if (strcmp(type, "deny") == 0) {
    ele->action = INK_MGMT_ALLOW_DENY;
  }

  ele->src_ip_addr = INKIpAddrEleCreate();
  convertIpAddrEle_xml(rule_node, ele->src_ip_addr);

  MgmtAllowObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}

// ---------------------------------------------------------------------
// convertParentRule_xml
// ---------------------------------------------------------------------
char *
convertParentRule_xml(XMLNode * rule_node)
{
  XMLNode *child;
  INKParentProxyEle *ele = NULL;
  char *name = NULL, *attr_val;

  if (!rule_node)
    return NULL;

  ele = INKParentProxyEleCreate(INK_TYPE_UNDEFINED);

  // process attributes
  attr_val = rule_node->getAttributeValueByName("go_direct");
  if (attr_val && strcmp(attr_val, "true") == 0) {
    ele->direct = true;
    ele->cfg_ele.type = INK_PP_GO_DIRECT;
  } else {
    ele->cfg_ele.type = INK_PP_PARENT;
  }

  attr_val = rule_node->getAttributeValueByName("round_robin");
  if (attr_val) {
    ele->rr = string_to_round_robin_type(attr_val);
  }
  // process elements
  for (int i = 0; i < rule_node->getChildCount(); i++) {
    child = rule_node->getChildNode(i);
    name = child->getNodeName();
    if (strcmp(name, "pdss") == 0) {
      convertPdssFormat_xml(child, &(ele->parent_info));
    } else if (strcmp(name, "proxies") == 0) {
      char *strList = child->getNodeValue();
      ele->proxy_list = string_to_domain_list(strList, " ");
    }
  }

  ParentProxyObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}


// ---------------------------------------------------------------------
// convertPartitionRule_xml
// ---------------------------------------------------------------------
char *
convertPartitionRule_xml(XMLNode * rule_node)
{
  XMLNode *child;
  INKPartitionEle *ele = NULL;
  char *name = NULL, *val;


  ele = INKPartitionEleCreate();
  val = rule_node->getAttributeValueByName("number");
  if (val) {
    ele->partition_num = ink_atoi(val);
  }


  if (strcmp(rule_node->getNodeName(), "http") == 0) {
    ele->scheme = INK_PARTITION_HTTP;
  } else {
    INKPartitionEleDestroy(ele);
    return NULL;
  }

  child = rule_node->getChildNode(0);
  name = child->getNodeName();
  val = child->getNodeValue();
  if (strcmp(name, "absolute_size") == 0) {
    ele->size_format = INK_SIZE_FMT_ABSOLUTE;
  } else if (strcmp(name, "percent_size") == 0) {
    ele->size_format = INK_SIZE_FMT_PERCENT;
  }
  ele->partition_size = ink_atoi(val);

  PartitionObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}

// ---------------------------------------------------------------------
// convertRemapRule_xml
// ---------------------------------------------------------------------
char *
convertRemapRule_xml(XMLNode * rule_node)
{
  XMLNode *child;
  INKRemapEle *ele = NULL;
  char *name = NULL, *val, *type = NULL;

  if (!rule_node)
    return NULL;

  // create the Ele and determine what type it is
  type = rule_node->getNodeName();
  ele = INKRemapEleCreate(INK_TYPE_UNDEFINED);
  if (strcmp(type, "map") == 0) {
    ele->cfg_ele.type = INK_REMAP_MAP;
  } else if (strcmp(type, "reverse_map") == 0) {
    ele->cfg_ele.type = INK_REMAP_REVERSE_MAP;
  } else if (strcmp(type, "redirect") == 0) {
    ele->cfg_ele.type = INK_REMAP_REDIRECT;
  } else if (strcmp(type, "redirect_temporary") == 0) {
    ele->cfg_ele.type = INK_REMAP_REDIRECT_TEMP;
  } else {
    INKRemapEleDestroy(ele);
    return NULL;
  }

  // iterate through each subelement of a rule node
  for (int i = 0; i < rule_node->getChildCount(); i++) {
    child = rule_node->getChildNode(i);
    name = child->getNodeName();
    if (strcmp(name, "src_url") == 0) {
      val = child->getAttributeValueByName("scheme");
      if (val)
        ele->from_scheme = string_to_scheme_type(val);
      val = child->getAttributeValueByName("host");
      if (val)
        ele->from_host = xstrdup(val);
      val = child->getAttributeValueByName("port");
      if (val)
        ele->from_port = ink_atoi(val);
      val = child->getAttributeValueByName("path_prefix");
      if (val)
        ele->from_path_prefix = xstrdup(val);
    } else if (strcmp(name, "dest_url") == 0) {
      val = child->getAttributeValueByName("scheme");
      if (val)
        ele->to_scheme = string_to_scheme_type(val);
      val = child->getAttributeValueByName("host");
      if (val)
        ele->to_host = xstrdup(val);
      val = child->getAttributeValueByName("port");
      if (val)
        ele->to_port = ink_atoi(val);
      val = child->getAttributeValueByName("path_prefix");
      if (val)
        ele->to_path_prefix = xstrdup(val);
    }
  }

  // convert Ele into "one-liner" text format
  RemapObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}

// ---------------------------------------------------------------------
// convertSocksRule_xml
// ---------------------------------------------------------------------
char *
convertSocksRule_xml(XMLNode * rule_node)
{
  // This isn't used.
  //XMLNode *child;
  INKSocksEle *ele = NULL;
  char *type = NULL, *attr_val;

  if (!rule_node)
    return NULL;

  ele = INKSocksEleCreate(INK_TYPE_UNDEFINED);
  type = rule_node->getNodeName();
  if (strcmp(type, "auth") == 0) {
    ele->cfg_ele.type = INK_SOCKS_AUTH;
    attr_val = rule_node->getAttributeValueByName("username");
    if (attr_val)
      ele->username = xstrdup(attr_val);
    attr_val = rule_node->getAttributeValueByName("password");
    if (attr_val)
      ele->password = xstrdup(attr_val);

  } else if (strcmp(type, "multiple_socks") == 0) {
    ele->cfg_ele.type = INK_SOCKS_MULTIPLE;
    attr_val = rule_node->getAttributeValueByName("round_robin");
    if (attr_val)
      ele->rr = string_to_round_robin_type(attr_val);

    ele->dest_ip_addr = INKIpAddrEleCreate();
    ele->socks_servers = INKDomainListCreate();
    convertIpAddrEle_xml(rule_node->getChildNode(0), ele->dest_ip_addr);
    convertDomainList_xml(rule_node->getChildNode(1), ele->socks_servers);

  } else if (strcmp(type, "no_socks") == 0) {
    ele->cfg_ele.type = INK_SOCKS_BYPASS;
    ele->ip_addrs = INKIpAddrListCreate();
    convertIpAddrList_xml(rule_node, ele->ip_addrs);

  } else {
    INKSocksEleDestroy(ele);
    return NULL;
  }

  // convert Ele into "one-liner" text format
  SocksObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}

// ---------------------------------------------------------------------
// convertSplitDnsRule_xml
// ---------------------------------------------------------------------
char *
convertSplitDnsRule_xml(XMLNode * rule_node)
{
  XMLNode *child;
  INKSplitDnsEle *ele = NULL;
  char *name = NULL, *val;

  if (!rule_node)
    return NULL;

  ele = INKSplitDnsEleCreate();

  // process attributes, if any
  val = rule_node->getAttributeValueByName("default_domain");
  if (val)
    ele->def_domain = xstrdup(val);

  // iterate through each subelement of a rule node
  for (int i = 0; i < rule_node->getChildCount(); i++) {
    child = rule_node->getChildNode(i);
    name = child->getNodeName();
    val = child->getNodeValue();
    if (strcmp(name, "dest_domain") == 0) {
      ele->pd_type = INK_PD_DOMAIN;
      ele->pd_val = xstrdup(val);
    } else if (strcmp(name, "dest_host") == 0) {
      ele->pd_type = INK_PD_HOST;
      ele->pd_val = xstrdup(val);
    } else if (strcmp(name, "url_regex") == 0) {
      ele->pd_type = INK_PD_URL_REGEX;
      ele->pd_val = xstrdup(val);
    } else if (strcmp(name, "dns_servers") == 0) {
      ele->dns_servers_addrs = string_to_domain_list(val, " ");
    } else if (strcmp(name, "search_list") == 0) {
      ele->search_list = string_to_domain_list(val, " ");
    }
  }

  // convert Ele into "one-liner" text format
  SplitDnsObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}

// ---------------------------------------------------------------------
// convertStorageRule_xml
// ---------------------------------------------------------------------
char *
convertStorageRule_xml(XMLNode * rule_node)
{
  INKStorageEle *ele = NULL;
  char *val;

  if (!rule_node)
    return NULL;

  ele = INKStorageEleCreate();

  val = rule_node->getAttributeValueByName("pathname");
  if (val)
    ele->pathname = xstrdup(val);
  val = rule_node->getAttributeValueByName("size");
  if (val)
    ele->size = ink_atoi(val);

  StorageObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}

// ---------------------------------------------------------------------
// convertUpdateRule_xml
// ---------------------------------------------------------------------
char *
convertUpdateRule_xml(XMLNode * rule_node)
{
  XMLNode *child;
  INKUpdateEle *ele = NULL;
  char *val;

  if (!rule_node)
    return NULL;

  ele = INKUpdateEleCreate();

  // process attributes
  val = rule_node->getAttributeValueByName("url");
  if (val)
    ele->url = xstrdup(val);
  val = rule_node->getAttributeValueByName("offset_hour");
  if (val)
    ele->offset_hour = ink_atoi(val);
  val = rule_node->getAttributeValueByName("interval");
  if (val)
    ele->interval = ink_atoi(val);
  val = rule_node->getAttributeValueByName("recursion_depth");
  if (val)
    ele->recursion_depth = ink_atoi(val);

  // iterate through all header subelements
  if (rule_node->getChildCount() > 0) {
    ele->headers = INKStringListCreate();
    for (int i = 0; i < rule_node->getChildCount(); i++) {
      child = rule_node->getChildNode(i);
      val = child->getNodeValue();
      INKStringListEnqueue(ele->headers, xstrdup(val));
    }
  }
  // convert Ele into "one-liner" text format
  UpdateObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}

// ---------------------------------------------------------------------
// convertVaddrsRule_xml
// ---------------------------------------------------------------------
char *
convertVaddrsRule_xml(XMLNode * rule_node)
{
  // This isn't used.
  //XMLNode *child;
  INKVirtIpAddrEle *ele = NULL;
  char *val;

  if (!rule_node)
    return NULL;

  ele = INKVirtIpAddrEleCreate();

  // process attributes
  val = rule_node->getAttributeValueByName("ip");
  if (val)
    ele->ip_addr = xstrdup(val);
  val = rule_node->getAttributeValueByName("interface");
  if (val)
    ele->intr = xstrdup(val);
  val = rule_node->getAttributeValueByName("sub-interface");
  if (val)
    ele->sub_intr = ink_atoi(val);

  // convert Ele into "one-liner" text format
  VirtIpAddrObj ele_obj(ele);
  char *rule = ele_obj.formatEleToRule();

  return rule;
}


// ####################### HELPER FUNCTIONS ############################

// ---------------------------------------------------------------------
// convertPdssFormat_xml
// ---------------------------------------------------------------------
// Convert the XML pdssFormatType complexType into INKPdSsFormat struct;
// this can be used by any file which has INKPdSsFormat
int
convertPdssFormat_xml(XMLNode * pdss_node, INKPdSsFormat * pdss)
{
  XMLNode *child;
  char *name, *value;

  for (int i = 0; i < pdss_node->getChildCount(); i++) {
    child = pdss_node->getChildNode(i);
    name = child->getNodeName();
    value = child->getNodeValue();
    // process the primary destination specifier
    if (strcmp(name, "dest_domain") == 0) {
      pdss->pd_type = INK_PD_DOMAIN;
      if (value)
        pdss->pd_val = xstrdup(value);
    } else if (strcmp(name, "dest_host") == 0) {
      pdss->pd_type = INK_PD_HOST;
      if (value)
        pdss->pd_val = xstrdup(value);
    } else if (strcmp(name, "dest_ip") == 0) {
      pdss->pd_type = INK_PD_IP;
      if (value)
        pdss->pd_val = xstrdup(value);
    } else if (strcmp(name, "url_regex") == 0) {
      pdss->pd_type = INK_PD_URL_REGEX;
      if (value)
        pdss->pd_val = xstrdup(value);
    } else                      // process secondary specifiers, if any
    if (strcmp(name, "sec_specs") == 0) {
      convertSecSpecs_xml(child, &(pdss->sec_spec));
    }
  }

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertTimePeriod_xml
// ---------------------------------------------------------------------
// Convert XML timePeriodType into INKHmsTime struct;
// <time_period> tag only has attribute values
int
convertTimePeriod_xml(XMLNode * time_node, INKHmsTime * time)
{
  char *day = time_node->getAttributeValueByName("day");
  char *hour = time_node->getAttributeValueByName("hour");
  char *min = time_node->getAttributeValueByName("min");
  char *sec = time_node->getAttributeValueByName("sec");
  if (day)
    time->d = ink_atoi(day);
  if (hour)
    time->h = ink_atoi(hour);
  if (min)
    time->m = ink_atoi(min);
  if (sec)
    time->s = ink_atoi(sec);

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertSecSpecs_xml
// ---------------------------------------------------------------------
// Convert XML secSpecsType into INKSspec struct
int
convertSecSpecs_xml(XMLNode * sspec_node, INKSspec * sspecs)
{
  XMLNode *child;
  char *sspec_val;

  // process the attributes
  sspec_val = sspec_node->getAttributeValueByName("src_ip");
  if (sspec_val)
    sspecs->src_ip = xstrdup(sspec_val);

  sspec_val = sspec_node->getAttributeValueByName("prefix");
  if (sspec_val)
    sspecs->prefix = xstrdup(sspec_val);

  sspec_val = sspec_node->getAttributeValueByName("suffix");
  if (sspec_val)
    sspecs->suffix = xstrdup(sspec_val);

  sspec_val = sspec_node->getAttributeValueByName("method");
  if (sspec_val)
    sspecs->method = string_to_method_type(sspec_val);

  sspec_val = sspec_node->getAttributeValueByName("scheme");
  if (sspec_val)
    sspecs->scheme = string_to_scheme_type(sspec_val);

  // could have a time_range or port element
  for (int i = 0; i < sspec_node->getChildCount(); i++) {
    child = sspec_node->getChildNode(i);
    if (strcmp(child->getNodeName(), "time_range") == 0) {
      sspec_val = child->getAttributeValueByName("hourA");
      if (sspec_val)
        sspecs->time.hour_a = ink_atoi(sspec_val);
      sspec_val = child->getAttributeValueByName("minA");
      if (sspec_val)
        sspecs->time.min_a = ink_atoi(sspec_val);
      sspec_val = child->getAttributeValueByName("hourB");
      if (sspec_val)
        sspecs->time.hour_b = ink_atoi(sspec_val);
      sspec_val = child->getAttributeValueByName("minB");
      if (sspec_val)
        sspecs->time.min_b = ink_atoi(sspec_val);
    } else if (strcmp(child->getNodeName(), "port") == 0) {
      sspecs->port = INKPortEleCreate();
      sspec_val = child->getAttributeValueByName("start");
      if (sspec_val)
        sspecs->port->port_a = ink_atoi(sspec_val);
      sspec_val = child->getAttributeValueByName("end");
      if (sspec_val)
        sspecs->port->port_b = ink_atoi(sspec_val);
    }
  }

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertPortList_xml
// ---------------------------------------------------------------------
int
convertPortList_xml(XMLNode * port_node)
{
  char *ports = port_node->getNodeValue();

  INKPortList portList = string_to_port_list(ports, " ");       // white space delimiter
  INKPortListDestroy(portList);
  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertIpAddrEle_xml
// ---------------------------------------------------------------------
// Converts ip_range type into INKIpAddrEle
int
convertIpAddrEle_xml(XMLNode * ip_node, INKIpAddrEle * ip)
{
  char *ip_val;
  XMLNode *ip1, *ip2;

  ip1 = ip_node->getChildNode(0);
  ip2 = ip_node->getChildNode(1);

  if (!ip1)                     // required
    return INK_ERR_FAIL;

  ip_val = ip1->getAttributeValueByName("ip");
  if (ip_val)
    ip->ip_a = xstrdup(ip_val);
  ip_val = ip1->getAttributeValueByName("cidr");
  if (ip_val)
    ip->cidr_a = ink_atoi(ip_val);
  ip_val = ip1->getAttributeValueByName("port");
  if (ip_val)
    ip->port_a = ink_atoi(ip_val);

  if (ip2) {                    // optional
    ip->type = INK_IP_RANGE;
    ip_val = ip2->getAttributeValueByName("ip");
    if (ip_val)
      ip->ip_b = xstrdup(ip_val);
    ip_val = ip2->getAttributeValueByName("cidr");
    if (ip_val)
      ip->cidr_b = ink_atoi(ip_val);
    ip_val = ip2->getAttributeValueByName("port");
    if (ip_val)
      ip->port_b = ink_atoi(ip_val);
  } else {
    ip->type = INK_IP_SINGLE;
  }

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertPortEle_xml
// ---------------------------------------------------------------------
int
convertPortEle_xml(XMLNode * port_node, INKPortEle * port)
{
  char *port_val;
  port_val = port_node->getAttributeValueByName("start");
  if (port_val)
    port->port_a = ink_atoi(port_val);
  port_val = port_node->getAttributeValueByName("end");
  if (port_val)
    port->port_b = ink_atoi(port_val);

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertIpAddrList_xml
// ---------------------------------------------------------------------
// Converts ipPortListType into INKIpAddrList
// Each child XML node should be an ip_range type
int
convertIpAddrList_xml(XMLNode * list_node, INKIpAddrList list)
{
  XMLNode *ip_node;
  INKIpAddrEle *ip_ele;

  for (int i = 0; i < list_node->getChildCount(); i++) {
    ip_node = list_node->getChildNode(i);
    ip_ele = INKIpAddrEleCreate();
    if (convertIpAddrEle_xml(ip_node, ip_ele) == INK_ERR_OKAY)
      INKIpAddrListEnqueue(list, ip_ele);
    else if (ip_ele) {
      xfree(ip_ele);
    }
  }

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertDomainList_xml
// ---------------------------------------------------------------------
// Converts a hostPortListType into an INKDomainList
int
convertDomainList_xml(XMLNode * list_node, INKDomainList list)
{
  INKDomain *dom;
  XMLNode *node;

  for (int i = 0; i < list_node->getChildCount(); i++) {
    node = list_node->getChildNode(i);
    dom = INKDomainCreate();
    if (convertDomain_xml(node, dom) == INK_ERR_OKAY)
      INKDomainListEnqueue(list, dom);
  }

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertDomain_xml
// ---------------------------------------------------------------------
// Converts a hostPortType into an INKDomain
int
convertDomain_xml(XMLNode * dom_node, INKDomain * dom)
{
  dom->domain_val = xstrdup(dom_node->getAttributeValueByName("host"));
  char *port_str = dom_node->getAttributeValueByName("port");
  if (port_str) {
    dom->port = ink_atoi(port_str);
  }

  return INK_ERR_OKAY;
}

//######################################################################
//######################################################################

// ---------------------------------------------------------------------
// convertFile_ts
// ---------------------------------------------------------------------
// Purpose: converts TS text file into XML file
// Input: ts_file  - the TS config file we need to convert
//        xml_file - results of the conversion (allocated buffer)
// Output:
int
convertFile_ts(const char *filename, char **xml_file)
{
  INKCfgContext ctx = NULL;
  INKCfgEle *ele;
  // This isn't used.
  //INKActionNeedT action_need;
  //INKError response;
  int i, ret = INK_ERR_OKAY, numRules;
  textBuffer xml(1024);
  textBuffer ruleBuf(512);

  if (!filename || !xml_file) {
    Error("[MgmtConverter::convertFile_ts] invalid parameters");
    return INK_ERR_PARAMS;
  }

  INKFileNameT type = INK_FNAME_UNDEFINED;
  RuleConverter_ts converter = NULL;
  FileInfo *info = NULL;
  InkHashTableValue lookup;
  // get file information by doing table lookup
  if (ink_hash_table_lookup(file_info_ht, filename, &lookup)) {
    info = (FileInfo *) lookup;
    type = info->type;
    converter = info->converter_ts;
  } else {
    Debug("convert", "[convertFile_tsl] No converter function for %s", filename);
    return INK_ERR_FAIL;        /* lv: file info does not exist, */
  }

  // read the file and convert each rule
  ctx = INKCfgContextCreate(type);
  if (!ctx) {
    return INK_ERR_FAIL;
  }
  // since we want to preserve comments, we need to read in the
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY) {
    return INK_ERR_FAIL;
  }
  // write the root file tag
  writeXmlStartTag(&xml, filename);

  // Convert each Ele into XML format and write it into the buffer
  // In general, there should be no problems converting the rule since
  // only valid rules will be in the CfgContext, but if there is a
  // problem converting the rule, the rule should not be put in the
  // final buffer
  numRules = INKCfgContextGetCount(ctx);
  for (i = 0; i < numRules; i++) {
    ele = INKCfgContextGetEleAt(ctx, i);
    ret = converter(ele, &ruleBuf);
    if (ret == INK_ERR_OKAY) {
      xml.copyFrom(ruleBuf.bufPtr(), strlen(ruleBuf.bufPtr()));
    } else {
      Debug("convert", "[convertFile_ts] Error converting %s ele %d", filename, i);
    }
    ruleBuf.reUse();
  }
  INKCfgContextDestroy(ctx);

  // close the root file tag
  writeXmlEndTag(&xml, filename);

  *xml_file = xstrdup(xml.bufPtr());

  return ret;
}

/***********************************************************************
 Make sure that each "convert...Rule_ts" functions only returns
 INK_ERR_OKAY if the Ele wass successfuly converted into XML.
 ***********************************************************************/

// ---------------------------------------------------------------------
// convertAdminAccessRule_ts
// ---------------------------------------------------------------------
int
convertAdminAccessRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  INKAdminAccessEle *ele = (INKAdminAccessEle *) cfg_ele;

  writeXmlAttrStartTag(xml_file, "rule");
  writeXmlAttribute(xml_file, "access", admin_acc_type_to_string(ele->access));
  if (ele->user)
    writeXmlAttribute(xml_file, "user", ele->user);
  if (ele->password)
    writeXmlAttribute(xml_file, "password", ele->password);
  writeXmlClose(xml_file);

  return INK_ERR_OKAY;
}





// ---------------------------------------------------------------------
// convertCacheRule_ts
// ---------------------------------------------------------------------
int
convertCacheRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  int err;

  INKCacheEle *ele = (INKCacheEle *) cfg_ele;
  switch (ele->cfg_ele.type) {
  case INK_CACHE_NEVER:
    writeXmlStartTag(xml_file, "never-cache");
    break;
  case INK_CACHE_IGNORE_NO_CACHE:
    writeXmlStartTag(xml_file, "ignore-no-cache");
    break;
  case INK_CACHE_IGNORE_CLIENT_NO_CACHE:
    writeXmlStartTag(xml_file, "ignore-client-no-cache");
    break;
  case INK_CACHE_IGNORE_SERVER_NO_CACHE:
    writeXmlStartTag(xml_file, "ignore-server-no-cache");
    break;
  case INK_CACHE_PIN_IN_CACHE:
    writeXmlStartTag(xml_file, "pin-in-cache");
    break;
  case INK_CACHE_REVALIDATE:
    writeXmlStartTag(xml_file, "revalidate");
    break;
  case INK_CACHE_TTL_IN_CACHE:
    writeXmlStartTag(xml_file, "ttl-in-cache");
    break;
  default:
    goto Lerror;
  }

  err = convertPdssFormat_ts(&(ele->cache_info), xml_file);
  if (err != INK_ERR_OKAY)
    goto Lerror;

  err = convertTimePeriod_ts(&(ele->time_period), xml_file);
  if (err != INK_ERR_OKAY)
    goto Lerror;

  switch (ele->cfg_ele.type) {
  case INK_CACHE_NEVER:
    writeXmlEndTag(xml_file, "never");
    break;
  case INK_CACHE_IGNORE_NO_CACHE:
    writeXmlEndTag(xml_file, "ignore-no-cache");
    break;
  case INK_CACHE_IGNORE_CLIENT_NO_CACHE:
    writeXmlEndTag(xml_file, "ignore-client-no-cache");
    break;
  case INK_CACHE_IGNORE_SERVER_NO_CACHE:
    writeXmlEndTag(xml_file, "ignore-server-no-cache");
    break;
  case INK_CACHE_PIN_IN_CACHE:
    writeXmlEndTag(xml_file, "pin-in-cache");
    break;
  case INK_CACHE_REVALIDATE:
    writeXmlEndTag(xml_file, "revalidate");
    break;
  case INK_CACHE_TTL_IN_CACHE:
    writeXmlEndTag(xml_file, "ttl-in-cache");
    break;
  default:
    goto Lerror;
  }

  return INK_ERR_OKAY;

Lerror:
  return INK_ERR_FAIL;
}

// ---------------------------------------------------------------------
// convertCongestionRule_ts
// ---------------------------------------------------------------------
int
convertCongestionRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  INKCongestionEle *ele = (INKCongestionEle *) cfg_ele;

  writeXmlAttrStartTag(xml_file, "rule");

  if (ele->prefix)
    writeXmlAttribute(xml_file, "prefix", ele->prefix);
  if (ele->port > 0)
    writeXmlAttribute_int(xml_file, "port", ele->port);
  writeXmlAttribute_int(xml_file, "max_connection_failures", ele->max_connection_failures);
  writeXmlAttribute_int(xml_file, "fail_window", ele->fail_window);
  writeXmlAttribute_int(xml_file, "proxy_retry_interval", ele->proxy_retry_interval);
  writeXmlAttribute_int(xml_file, "client_wait_interval", ele->client_wait_interval);
  writeXmlAttribute_int(xml_file, "wait_interval_alpha", ele->wait_interval_alpha);
  writeXmlAttribute_int(xml_file, "live_os_conn_timeout", ele->live_os_conn_timeout);
  writeXmlAttribute_int(xml_file, "live_os_conn_retries", ele->live_os_conn_retries);
  writeXmlAttribute_int(xml_file, "dead_os_conn_timeout", ele->dead_os_conn_timeout);
  writeXmlAttribute_int(xml_file, "dead_os_conn_retries", ele->dead_os_conn_retries);
  writeXmlAttribute_int(xml_file, "max_connection", ele->max_connection);
  if (ele->error_page_uri)
    writeXmlAttribute(xml_file, "error_page_uri", ele->error_page_uri);

  switch (ele->scheme) {
  case INK_HTTP_CONGEST_PER_IP:
    writeXmlAttribute(xml_file, "scheme", "per_ip");
    break;
  case INK_HTTP_CONGEST_PER_HOST:
    writeXmlAttribute(xml_file, "scheme", "per_host");
    break;
  default:
    goto Lerror;
  }

  xml_file->copyFrom(">", 1);

  switch (ele->pd_type) {
  case INK_PD_DOMAIN:
    writeXmlElement(xml_file, "dest_domain", ele->pd_val);
    break;
  case INK_PD_HOST:
    writeXmlElement(xml_file, "dest_host", ele->pd_val);
    break;
  case INK_PD_IP:
    writeXmlElement(xml_file, "dest_ip", ele->pd_val);
    break;
  case INK_PD_URL_REGEX:
    writeXmlElement(xml_file, "url_regex", ele->pd_val);
    break;
  default:
    goto Lerror;
  }

  writeXmlEndTag(xml_file, "rule");

  return INK_ERR_OKAY;

Lerror:
  return INK_ERR_FAIL;
}


// ---------------------------------------------------------------------
// convertHostingRule_ts
// ---------------------------------------------------------------------
int
convertHostingRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  char *strList;
  INKHostingEle *ele = (INKHostingEle *) cfg_ele;

  writeXmlStartTag(xml_file, "rule");

  switch (ele->pd_type) {
  case INK_PD_DOMAIN:
    writeXmlElement(xml_file, "domain", ele->pd_val);
    break;
  case INK_PD_HOST:
    writeXmlElement(xml_file, "host", ele->pd_val);
    break;
  default:
    goto Lerror;
  }

  strList = int_list_to_string(ele->partitions, " ");
  if (strList) {
    writeXmlElement(xml_file, "partitions", strList);
    xfree(strList);
  }

  writeXmlEndTag(xml_file, "rule");

  return INK_ERR_OKAY;

Lerror:
  return INK_ERR_FAIL;
}

// ---------------------------------------------------------------------
// convertIcpRule_ts
// ---------------------------------------------------------------------
int
convertIcpRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  char *strPtr;

  INKIcpEle *ele = (INKIcpEle *) cfg_ele;

  switch (ele->peer_type) {
  case INK_ICP_PARENT:
    writeXmlAttrStartTag(xml_file, "parent");
    break;
  case INK_ICP_SIBLING:
    writeXmlAttrStartTag(xml_file, "sibling");
    break;
  default:
    goto Lerror;
  }

  // required port attributes
  writeXmlAttribute_int(xml_file, "proxy_port", ele->peer_proxy_port);
  writeXmlAttribute_int(xml_file, "icp_port", ele->peer_icp_port);
  xml_file->copyFrom(">", 1);

  if (ele->peer_host_ip_addr)
    writeXmlElement(xml_file, "hostip", ele->peer_host_ip_addr);

  if (ele->peer_hostname)
    writeXmlElement(xml_file, "hostname", ele->peer_hostname);

  if (ele->is_multicast) {
    writeXmlAttrStartTag(xml_file, "multicast");
    if (ele->mc_ip_addr)
      writeXmlAttribute(xml_file, "ip", ele->mc_ip_addr);

    strPtr = multicast_type_to_string(ele->mc_ttl);
    if (strPtr) {
      writeXmlAttribute(xml_file, "time_to_live", strPtr);
      xfree(strPtr);
    }
    writeXmlClose(xml_file);
  }

  switch (ele->peer_type) {
  case INK_ICP_PARENT:
    writeXmlEndTag(xml_file, "parent");
    break;
  case INK_ICP_SIBLING:
    writeXmlEndTag(xml_file, "sibling");
    break;
  default:
    goto Lerror;
  }

  return INK_ERR_OKAY;

Lerror:
  return INK_ERR_FAIL;
}


// ---------------------------------------------------------------------
// convertIpAllowRule_ts
// ---------------------------------------------------------------------
int
convertIpAllowRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  INKIpAllowEle *ele = (INKIpAllowEle *) cfg_ele;

  switch (ele->action) {
  case INK_IP_ALLOW_ALLOW:
    convertIpAddrEle_ts(ele->src_ip_addr, xml_file, "allow");
    break;
  case INK_IP_ALLOW_DENY:
    convertIpAddrEle_ts(ele->src_ip_addr, xml_file, "deny");
    break;
  default:
    goto Lerror;
  }

  return INK_ERR_OKAY;

Lerror:
  return INK_ERR_FAIL;
}

// ---------------------------------------------------------------------
// convertMgmtAllowRule_ts
// ---------------------------------------------------------------------
int
convertMgmtAllowRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  INKMgmtAllowEle *ele = (INKMgmtAllowEle *) cfg_ele;

  switch (ele->action) {
  case INK_MGMT_ALLOW_ALLOW:
    convertIpAddrEle_ts(ele->src_ip_addr, xml_file, "allow");
    break;
  case INK_MGMT_ALLOW_DENY:
    convertIpAddrEle_ts(ele->src_ip_addr, xml_file, "deny");
    break;
  default:
    goto Lerror;
  }

  return INK_ERR_OKAY;

Lerror:
  return INK_ERR_FAIL;
}

// ---------------------------------------------------------------------
// convertParentRule_ts
// ---------------------------------------------------------------------
int
convertParentRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  char *tempStr;
  INKParentProxyEle *ele = (INKParentProxyEle *) cfg_ele;

  writeXmlAttrStartTag(xml_file, "rule");

  tempStr = round_robin_type_to_string(ele->rr);
  if (tempStr) {
    writeXmlAttribute(xml_file, "round_robin", tempStr);
    xfree(tempStr);
  }
  if (ele->direct)
    writeXmlAttribute(xml_file, "go_direct", "true");
  else
    writeXmlAttribute(xml_file, "go_direct", "false");
  xml_file->copyFrom(">", 1);


  convertPdssFormat_ts(&(ele->parent_info), xml_file);

  tempStr = domain_list_to_string(ele->proxy_list, " ");
  if (tempStr) {                // optional field
    writeXmlElement(xml_file, "proxies", tempStr);
    xfree(tempStr);
  }

  writeXmlEndTag(xml_file, "rule");

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertPartitionRule_ts
// ---------------------------------------------------------------------
int
convertPartitionRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  INKPartitionEle *ele = (INKPartitionEle *) cfg_ele;

  switch (ele->scheme) {
  case INK_PARTITION_HTTP:
    writeXmlAttrStartTag(xml_file, "http");
    break;
  default:
    goto Lerror;
  }

  writeXmlAttribute_int(xml_file, "number", ele->partition_num);
  xml_file->copyFrom(">", 1);

  switch (ele->size_format) {
  case INK_SIZE_FMT_PERCENT:
    writeXmlElement_int(xml_file, "percent_size", ele->partition_size);
    break;
  case INK_SIZE_FMT_ABSOLUTE:
    writeXmlElement_int(xml_file, "absolute_size", ele->partition_size);
    break;
  default:
    goto Lerror;
  }

  switch (ele->scheme) {
  case INK_PARTITION_HTTP:
    writeXmlEndTag(xml_file, "http");
    break;
  default:
    break;
  }

  return INK_ERR_OKAY;

Lerror:
  return INK_ERR_FAIL;
}

// ---------------------------------------------------------------------
// convertRemapRule_ts
// ---------------------------------------------------------------------
int
convertRemapRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  char *strPtr;

  INKRemapEle *ele = (INKRemapEle *) cfg_ele;
  switch (ele->cfg_ele.type) {
  case INK_REMAP_MAP:
    writeXmlAttrStartTag(xml_file, "map");
    break;
  case INK_REMAP_REVERSE_MAP:
    writeXmlAttrStartTag(xml_file, "reverse_map");
    break;
  case INK_REMAP_REDIRECT:
    writeXmlAttrStartTag(xml_file, "redirect");
    break;
  case INK_REMAP_REDIRECT_TEMP:
    writeXmlAttrStartTag(xml_file, "redirect_temporary");
    break;
  default:
    goto Lerror;
  }

  // write Url's
  writeXmlAttrStartTag(xml_file, "src_url");
  strPtr = scheme_type_to_string(ele->from_scheme);
  if (!strPtr)
    goto Lerror;                // required attribute
  writeXmlAttribute(xml_file, "scheme", strPtr);
  xfree(strPtr);
  if (ele->from_host)
    writeXmlAttribute(xml_file, "host", ele->from_host);
  if (ele->from_port)
    writeXmlAttribute_int(xml_file, "port", ele->from_port);
  if (ele->from_path_prefix)
    writeXmlAttribute(xml_file, "path_prefix", ele->from_path_prefix);
  writeXmlClose(xml_file);

  writeXmlAttrStartTag(xml_file, "dest_url");
  strPtr = scheme_type_to_string(ele->to_scheme);
  if (!strPtr)
    goto Lerror;                // required attribute
  writeXmlAttribute(xml_file, "scheme", strPtr);
  xfree(strPtr);
  if (ele->to_host)
    writeXmlAttribute(xml_file, "host", ele->to_host);
  if (ele->to_port)
    writeXmlAttribute_int(xml_file, "port", ele->to_port);
  if (ele->to_path_prefix)
    writeXmlAttribute(xml_file, "path_prefix", ele->to_path_prefix);
  writeXmlClose(xml_file);

  switch (ele->cfg_ele.type) {
  case INK_REMAP_MAP:
    writeXmlEndTag(xml_file, "map");
    break;
  case INK_REMAP_REVERSE_MAP:
    writeXmlEndTag(xml_file, "reverse_map");
    break;
  case INK_REMAP_REDIRECT:
    writeXmlEndTag(xml_file, "redirect");
    break;
  case INK_REMAP_REDIRECT_TEMP:
    writeXmlEndTag(xml_file, "redirect_temporary");
    break;
  default:
    goto Lerror;
  }

  return INK_ERR_OKAY;

Lerror:
  return INK_ERR_FAIL;
}

// ---------------------------------------------------------------------
// convertSocksRule_ts
// ---------------------------------------------------------------------
int
convertSocksRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  INKSocksEle *ele = (INKSocksEle *) cfg_ele;

  switch (ele->cfg_ele.type) {
  case INK_SOCKS_AUTH:
    writeXmlAttrStartTag(xml_file, "auth");
    writeXmlAttribute(xml_file, "username", ele->username);
    writeXmlAttribute(xml_file, "password", ele->password);
    writeXmlClose(xml_file);
    break;

  case INK_SOCKS_MULTIPLE:
    writeXmlAttrStartTag(xml_file, "multiple_socks");
    writeXmlAttribute(xml_file, "round_robin", round_robin_type_to_string(ele->rr));
    xml_file->copyFrom(">", 1);
    convertIpAddrEle_ts(ele->dest_ip_addr, xml_file, "dest_ip");
    convertDomainList_ts(ele->socks_servers, xml_file, "socks_servers");
    writeXmlEndTag(xml_file, "multiple_socks");
    break;

  case INK_SOCKS_BYPASS:
    convertIpAddrList_ts(ele->ip_addrs, xml_file, "no_socks");
    break;

  default:
    return INK_ERR_FAIL;
  }

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertSplitDnsRule_ts
// ---------------------------------------------------------------------
int
convertSplitDnsRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  char *strPtr;

  INKSplitDnsEle *ele = (INKSplitDnsEle *) cfg_ele;
  if (ele->def_domain) {
    writeXmlAttrStartTag(xml_file, "rule");
    writeXmlAttribute(xml_file, "default_domain", ele->def_domain);
    xml_file->copyFrom(">", 1);
  } else {
    writeXmlStartTag(xml_file, "rule");
  }

  switch (ele->pd_type) {
  case INK_PD_DOMAIN:
    writeXmlElement(xml_file, "dest_domain", ele->pd_val);
    break;
  case INK_PD_HOST:
    writeXmlElement(xml_file, "dest_host", ele->pd_val);
    break;
  case INK_PD_URL_REGEX:
    writeXmlElement(xml_file, "url_regex", ele->pd_val);
    break;
  default:
    goto Lerror;
  }

  // INKDomainList dns_servers_addrs required
  strPtr = domain_list_to_string(ele->dns_servers_addrs, " ");
  if (!strPtr)
    goto Lerror;
  writeXmlElement(xml_file, "dns_servers", strPtr);
  xfree(strPtr);

  // INKDomainList search_list optional
  strPtr = domain_list_to_string(ele->search_list, " ");
  if (strPtr) {
    writeXmlElement(xml_file, "search_list", strPtr);
    xfree(strPtr);
  }

  writeXmlEndTag(xml_file, "rule");

  return INK_ERR_OKAY;

Lerror:
  return INK_ERR_FAIL;
}

// ---------------------------------------------------------------------
// convertStorageRule_ts
// ---------------------------------------------------------------------
int
convertStorageRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  INKStorageEle *ele = (INKStorageEle *) cfg_ele;

  writeXmlAttrStartTag(xml_file, "rule");
  if (ele->pathname)
    writeXmlAttribute(xml_file, "pathname", ele->pathname);
  if (ele->size > 0)
    writeXmlAttribute_int(xml_file, "size", ele->size);
  writeXmlClose(xml_file);

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertUpdateRule_ts
// ---------------------------------------------------------------------
int
convertUpdateRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  INKUpdateEle *ele = (INKUpdateEle *) cfg_ele;

  writeXmlAttrStartTag(xml_file, "rule");
  writeXmlAttribute(xml_file, "url", ele->url);
  writeXmlAttribute_int(xml_file, "offset_hour", ele->offset_hour);
  writeXmlAttribute_int(xml_file, "interval", ele->interval);
  if (ele->recursion_depth > 0)
    writeXmlAttribute_int(xml_file, "recursion_depth", ele->recursion_depth);
  xml_file->copyFrom(">", 1);

  int len = INKStringListLen(ele->headers);
  char *elem;
  for (int i = 0; i < len; i++) {
    elem = INKStringListDequeue(ele->headers);
    writeXmlElement(xml_file, "header", elem);
    INKStringListEnqueue(ele->headers, elem);
  }

  writeXmlEndTag(xml_file, "rule");

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertVaddrsRule_ts
// ---------------------------------------------------------------------
int
convertVaddrsRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file)
{
  INKVirtIpAddrEle *ele = (INKVirtIpAddrEle *) cfg_ele;

  writeXmlAttrStartTag(xml_file, "rule");
  writeXmlAttribute(xml_file, "ip", ele->ip_addr);
  writeXmlAttribute(xml_file, "interface", ele->intr);
  writeXmlAttribute_int(xml_file, "sub-interface", ele->sub_intr);
  writeXmlClose(xml_file);

  return INK_ERR_OKAY;
}


//######################################################################

// ---------------------------------------------------------------------
// convertPortEle_ts
// ---------------------------------------------------------------------
// corresponds to complex type "port_range"
int
convertPortEle_ts(INKPortEle * ele, textBuffer * xml_file, char *tag_name)
{
  if (!ele || !xml_file || !tag_name)
    return INK_ERR_FAIL;

  writeXmlAttrStartTag(xml_file, tag_name);
  writeXmlAttribute_int(xml_file, "start", ele->port_a);
  if (ele->port_b > 0)
    writeXmlAttribute_int(xml_file, "end", ele->port_b);
  writeXmlClose(xml_file);

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertIpAddrEle_ts
// ---------------------------------------------------------------------
int
convertIpAddrEle_ts(INKIpAddrEle * ele, textBuffer * xml_file, const char *tag_name)
{
  if (!ele || !xml_file || !tag_name)
    return INK_ERR_FAIL;

  writeXmlStartTag(xml_file, tag_name);

  writeXmlAttrStartTag(xml_file, "start");
  writeXmlAttribute(xml_file, "ip", ele->ip_a);
  if (ele->cidr_a > 0)
    writeXmlAttribute_int(xml_file, "cidr", ele->cidr_a);
  if (ele->port_a > 0)
    writeXmlAttribute_int(xml_file, "port", ele->port_a);
  writeXmlClose(xml_file);

  if (ele->ip_b > 0) {
    writeXmlAttrStartTag(xml_file, "end");
    writeXmlAttribute(xml_file, "ip", ele->ip_b);
    if (ele->cidr_b > 0)
      writeXmlAttribute_int(xml_file, "cidr", ele->cidr_b);
    if (ele->port_b > 0)
      writeXmlAttribute_int(xml_file, "port", ele->port_b);
    writeXmlClose(xml_file);
  }

  writeXmlEndTag(xml_file, tag_name);

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertPdssFormat_ts
// ---------------------------------------------------------------------
// converts the INKPdssFormat struct into XML format and writes
// the XML directly into xml_file
int
convertPdssFormat_ts(INKPdSsFormat * pdss, textBuffer * xml_file)
{
  INKSspec sspec = pdss->sec_spec;
  char *strPtr;

  writeXmlStartTag(xml_file, "pdss");

  switch (pdss->pd_type) {
  case INK_PD_DOMAIN:
    writeXmlElement(xml_file, "dest_domain", pdss->pd_val);
    break;
  case INK_PD_HOST:
    writeXmlElement(xml_file, "dest_host", pdss->pd_val);
    break;
  case INK_PD_IP:
    writeXmlElement(xml_file, "dest_ip", pdss->pd_val);
    break;
  case INK_PD_URL_REGEX:
    writeXmlElement(xml_file, "url_regex", pdss->pd_val);
    break;
  default:                     /* error */
    break;
  }


  // only write the sec specs tag if there are secondary specifiers
  if (sspec.src_ip || sspec.prefix || sspec.suffix || sspec.port > 0 ||
      sspec.method != INK_METHOD_UNDEFINED ||
      sspec.scheme != INK_SCHEME_UNDEFINED ||
      sspec.time.hour_a != 0 || sspec.time.hour_b != 0 || sspec.time.min_a != 0 || sspec.time.min_b != 0) {

    writeXmlAttrStartTag(xml_file, "sec_specs");

    // write sec specs attributes
    if (sspec.src_ip) {
      writeXmlAttribute(xml_file, "src_ip", sspec.src_ip);
    }
    if (sspec.prefix) {
      writeXmlAttribute(xml_file, "prefix", sspec.prefix);
    }
    if (sspec.suffix) {
      writeXmlAttribute(xml_file, "suffix", sspec.suffix);
    }
    if (sspec.method != INK_METHOD_UNDEFINED) {
      strPtr = method_type_to_string(sspec.method);
      if (strPtr) {
        writeXmlAttribute(xml_file, "method", strPtr);
        xfree(strPtr);
      }
    }
    if (sspec.scheme != INK_SCHEME_UNDEFINED) {
      strPtr = scheme_type_to_string(sspec.scheme);
      if (strPtr) {
        writeXmlAttribute(xml_file, "scheme", strPtr);
        xfree(strPtr);
      }
    }

    if (sspec.time.hour_a != 0 || sspec.time.hour_b != 0 ||
        sspec.time.min_a != 0 || sspec.time.min_b != 0 || sspec.port) {
      xml_file->copyFrom(">", 1);       //close sec_specs tag first

      if (sspec.time.hour_a != 0 || sspec.time.hour_b != 0 || sspec.time.min_a != 0 || sspec.time.min_b != 0) {
        writeXmlAttrStartTag(xml_file, "time_range");
        if (sspec.time.hour_a != 0) {
          writeXmlAttribute_int(xml_file, "hourA", sspec.time.hour_a);
        }
        if (sspec.time.min_a != 0) {
          writeXmlAttribute_int(xml_file, "minA", sspec.time.min_a);
        }
        if (sspec.time.hour_b != 0) {
          writeXmlAttribute_int(xml_file, "hourB", sspec.time.hour_b);
        }
        if (sspec.time.min_b != 0) {
          writeXmlAttribute_int(xml_file, "minB", sspec.time.min_b);
        }
        writeXmlClose(xml_file);
      }

      if (sspec.port) {
        writeXmlAttrStartTag(xml_file, "port");
        if (sspec.port->port_a != 0) {
          writeXmlAttribute_int(xml_file, "start", sspec.port->port_a);
        }
        if (sspec.port->port_b != 0) {
          writeXmlAttribute_int(xml_file, "end", sspec.port->port_b);
        }
        writeXmlClose(xml_file);
      }

      writeXmlEndTag(xml_file, "sec_specs");
    } else {
      // close sec spec tag
      writeXmlClose(xml_file);
    }
  }

  writeXmlEndTag(xml_file, "pdss");

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertTimePeriod_ts
// ---------------------------------------------------------------------
// Before converting, first checks that there are valid values for
// time period
int
convertTimePeriod_ts(INKHmsTime * time, textBuffer * xml_file)
{
  if (time->d > 0 || time->h > 0 || time->m > 0 || time->s > 0) {
    writeXmlAttrStartTag(xml_file, "time_period");
    if (time->d != 0) {
      writeXmlAttribute_int(xml_file, "day", time->d);
    }
    if (time->h != 0) {
      writeXmlAttribute_int(xml_file, "hour", time->h);
    }
    if (time->m != 0) {
      writeXmlAttribute_int(xml_file, "min", time->m);
    }
    if (time->s != 0) {
      writeXmlAttribute_int(xml_file, "sec", time->s);
    }
    writeXmlClose(xml_file);
  }

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertIpAddrList_ts
// ---------------------------------------------------------------------
// converts into ipPortListType with the given "tag_name"
int
convertIpAddrList_ts(INKIpAddrList list, textBuffer * xml_file, const char *tag_name)
{
  INKIpAddrEle *ele;

  writeXmlStartTag(xml_file, tag_name);

  int count = INKIpAddrListLen(list);
  for (int i = 0; i < count; i++) {
    ele = INKIpAddrListDequeue(list);
    convertIpAddrEle_ts(ele, xml_file, "ip");
    INKIpAddrListEnqueue(list, ele);
  }

  writeXmlEndTag(xml_file, tag_name);

  return INK_ERR_OKAY;
}

// ---------------------------------------------------------------------
// convertDomainList_ts
// ---------------------------------------------------------------------
// INKDomainList ==> hostPortListType
int
convertDomainList_ts(INKDomainList list, textBuffer * xml_file, const char *tag_name)
{
  writeXmlStartTag(xml_file, tag_name);

  INKDomain *dom;
  int len = INKDomainListLen(list);
  for (int i = 0; i < len; i++) {
    dom = (INKDomain *) dequeue((LLQ *) list);
    if (dom) {
      writeXmlAttrStartTag(xml_file, "host");
      writeXmlAttribute(xml_file, "host", dom->domain_val);
      writeXmlAttribute_int(xml_file, "port", dom->port);
      writeXmlClose(xml_file);
      enqueue((LLQ *) list, dom);
    }
  }

  writeXmlEndTag(xml_file, tag_name);

  return INK_ERR_OKAY;
}


//#####################################################################
//#####################################################################

// ---------------------------------------------------------------------
// writeXmlStartTag
// ---------------------------------------------------------------------
// if namespace specified, writes into xml buffer "<namespace:name>"
void
writeXmlStartTag(textBuffer * xml, const char *name, const char *nsp)
{
  if (nsp) {
    xml->copyFrom("<", 1);
    xml->copyFrom(nsp, strlen(nsp));
    xml->copyFrom(":", 1);
    xml->copyFrom(name, strlen(name));
    xml->copyFrom(">", 1);
  } else {
    xml->copyFrom("<", 1);
    xml->copyFrom(name, strlen(name));
    xml->copyFrom(">", 1);
  }
}

// ---------------------------------------------------------------------
// writeXmlAttrStartTag
// ---------------------------------------------------------------------
// If namespace specified, writes into xml buffer "<namespace:name "
// Instead of closing the start tag with close bracket, writes whitespace
void
writeXmlAttrStartTag(textBuffer * xml, const char *name, char *nsp)
{
  xml->copyFrom("<", 1);
  if (nsp) {
    xml->copyFrom(nsp, strlen(nsp));
    xml->copyFrom(":", 1);
    xml->copyFrom(name, strlen(name));
  } else {
    xml->copyFrom(name, strlen(name));
  }
}

// ---------------------------------------------------------------------
// writeXmlEndTag
// ---------------------------------------------------------------------
// if namespace specified, writes into xml buffer "</namespace:name>"
void
writeXmlEndTag(textBuffer * xml, const char *name, const char *nsp)
{

  xml->copyFrom("</", 2);
  if (nsp) {
    xml->copyFrom(nsp, strlen(nsp));
    xml->copyFrom(":", 1);
    xml->copyFrom(name, strlen(name));
  } else {
    xml->copyFrom(name, strlen(name));
  }
  xml->copyFrom(">", 1);
}

// ---------------------------------------------------------------------
// writeXmlElement
// ---------------------------------------------------------------------
// writes into the file "xml": "<elemName>value</elemName>"
// the nsp is optional
void
writeXmlElement(textBuffer * xml, const char *elemName, const char *value, const char *nsp)
{
  writeXmlStartTag(xml, elemName, nsp);
  xml->copyFrom(value, strlen(value));
  writeXmlEndTag(xml, elemName, nsp);
}

// ---------------------------------------------------------------------
// writeXmlElement_int
// ---------------------------------------------------------------------
// writes into the file "xml": "<elemName>value</elemName>"
// the nsp is optional
void
writeXmlElement_int(textBuffer * xml, const char *elemName, int value, const char *nsp)
{
  char tempStr[128];
  memset(tempStr, 0, 128);
  snprintf(tempStr, 128, "%d", value);

  writeXmlStartTag(xml, elemName, nsp);
  xml->copyFrom(tempStr, strlen(tempStr));
  writeXmlEndTag(xml, elemName, nsp);
}

// ---------------------------------------------------------------------
// writeXmlAttribute
// ---------------------------------------------------------------------
// will write the attribute name value pair, padded with white space
void
writeXmlAttribute(textBuffer * xml, const char *attrName, const char *value)
{
  xml->copyFrom(" ", 1);
  xml->copyFrom(attrName, strlen(attrName));
  xml->copyFrom("=\"", 2);
  xml->copyFrom(value, strlen(value));
  xml->copyFrom("\" ", 2);
}

// ---------------------------------------------------------------------
// writeXmlAttribute_int
// ---------------------------------------------------------------------
// will write the attribute name value pair, padded with white space
void
writeXmlAttribute_int(textBuffer * xml, const char *attrName, int value)
{
  char tempStr[128];
  memset(tempStr, 0, 128);
  snprintf(tempStr, 128, "%d", value);

  xml->copyFrom(" ", 1);
  xml->copyFrom(attrName, strlen(attrName));
  xml->copyFrom("=\"", 2);
  xml->copyFrom(tempStr, strlen(tempStr));
  xml->copyFrom("\" ", 2);
}

// ---------------------------------------------------------------------
// writeXmlClose
// ---------------------------------------------------------------------
// will write "/>" with a newline
void
writeXmlClose(textBuffer * xml)
{
  xml->copyFrom("/>", 2);
}

// ---------------------------------------------------------------------
//  strcmptag
// ---------------------------------------------------------------------
// namespace is optional argument, by default it is null; if null, then
// function is just like a strcmp
// returns: 0 if "fulltag" == "namespace:name"
//         <0 if "fulltag" < "namespace:name"
//         >0 if "fulltag" > "namespace:name"
int
strcmptag(char *fulltag, char *name, char *nsp)
{
  char new_tag[128];

  if (nsp) {
    memset(new_tag, 0, 128);
    snprintf(new_tag, 128, "%s:%s", nsp, name);
    return strcmp(fulltag, new_tag);
  }

  return strcmp(fulltag, name);
}

//#####################################################################
//#####################################################################

// ---------------------------------------------------------------------
// convertRecordsFile_ts
// ---------------------------------------------------------------------
// Unlike the other config files, each "rule" is actually an
// attribute name-value pair. Instead of using the *.config file
// to retrieve the values (no INKCfgContext) the XML
// values are retrieved from TM's internal record arrays
void
convertRecordsFile_ts(char **xml_file)
{
  char value[256];
  char record[1024];
  textBuffer xml(2048);

  if (!xml_file) {
    Error("[MgmtConverter::convertRecordsFile_ts] invalid parameters");
    return;
  }
  // write as list of attributes; will fit under trafficserver root tag probably?
  for (int r = 0; RecordsConfig[r].value_type != INVALID; r++) {
    if (RecordsConfig[r].required == RR_REQUIRED) {     // need to write to xml
      // get the value of the record
      memset(record, 0, 1024);
      memset(value, 0, 256);
      xml.copyFrom("  ", 2);
      varStrFromName(RecordsConfig[r].name, value, 256);
      snprintf(record, 1024, "%s=\"%s\"", RecordsConfig[r].name, value);
      xml.copyFrom(record, strlen(record));
      xml.copyFrom("\n", 1);
    }
  }

  *xml_file = xstrdup(xml.bufPtr());
  return;
}

// ---------------------------------------------------------------------
// convertRecordsFile_xml
// ---------------------------------------------------------------------
// Unlike other *.config files, this does not directly write a new
// records.config. Instead, it updates TM's internal records arrays.
// (a new records.config file is created by TM when records are changed)
void
convertRecordsFile_xml(XMLNode * file_node)
{
  // This isn't used.
  //XMLNode *child;
  char *rec_val;

  if (!file_node) {
    Error("[MgmtConverter::convertRecordsFile_xml] invalid parameters");
    return;
  }

  for (int r = 0; RecordsConfig[r].value_type != INVALID; r++) {
    rec_val = file_node->getAttributeValueByName(RecordsConfig[r].name);
    if (rec_val) {
      if (!varSetFromStr(RecordsConfig[r].name, rec_val))
        Error("[MgmtConverter::convertRecordsFile_xml] set record %s failed", RecordsConfig[r].name);
    }
  }

  return;
}

// ---------------------------------------------------------------------
// createXmlSchemaRecords
// ---------------------------------------------------------------------
// all the records are subdivided into various arrays; use these
// arrays to look up the RecordElement data in RecordsElement table
// and create the Record XML schema obj;
// A record will be an attribute of the root trafficserver tag!
void
createXmlSchemaRecords(char **output)
{
  char record[MAX_BUF_SIZE];
  textBuffer newFile(2048);

  // create the list of attributes for each record; only list required ones
  // <xs:attribute name="value" type="xs:string" default="NULL"/>
  for (int r = 0; RecordsConfig[r].value_type != INVALID; r++) {
    if (RecordsConfig[r].required == RR_REQUIRED) {
      memset(record, 0, MAX_BUF_SIZE);
      getXmlRecType(RecordsConfig[r], record, MAX_BUF_SIZE);
      newFile.copyFrom("      ", 6);
      newFile.copyFrom(record, strlen(record));
      newFile.copyFrom("\n", 1);
    }
  }

  *output = xstrdup(newFile.bufPtr());
  return;
}

// ---------------------------------------------------------------------
// getXmlRecType
// ---------------------------------------------------------------------
// helper function that helps determine what the XML simple type
// should be used for the given RecordElement
void
getXmlRecType(struct RecordElement rec, char *buf, int buf_size)
{
  memset(buf, 0, buf_size);
  switch (rec.check) {
  case RC_NULL:                // no check type defined, use field type
    switch (rec.value_type) {
    case INK_STRING:
      sprintf(buf, "<xs:attribute name=\"%s\" type=\"xs:string\" default=\"%s\"/>", rec.name, rec.value);
      break;
    case INK_FLOAT:
      sprintf(buf, "<xs:attribute name=\"%s\" type=\"xs:float\" default=\"%s\"/>", rec.name, rec.value);
      break;
    case INK_INT:
    case INK_COUNTER:
    default:
      // Handled here:
      // INVALID, INK_STAT_CONST,INK_STAT_FX, MAX_MGMT_TYPE
      break;
    }
    break;

  case RC_IP:
    sprintf(buf, "<xs:attribute name=\"%s\" type=\"cnp:ipaddr\" default=\"%s\"/>", rec.name, rec.value);
    break;

  case RC_INT:
    if (rec.regex) {
      if (strcmp(rec.regex, "[0-1]") == 0) {    // for [0-1] range, make into boolean type
        sprintf(buf, "<xs:attribute name=\"%s\" type=\"xs:boolean\" default=\"%s\"/>", rec.name, rec.value);
      } else {                  // break up [x-y] into ts:range_x_y type
        Tokenizer range("[]-");
        if (range.Initialize(rec.regex) == 2) {
          sprintf(buf, "<xs:attribute name=\"%s\" type=\"ts:range_%s_%s\" default=\"%s\"/>",
                      rec.name, range[0], range[1], rec.value);
        }
      }
    }
    break;

  case RC_STR:
    if (rec.regex) {
      if (strcmp(rec.regex, ".*") == 0) {
        sprintf(buf, "<xs:attribute name=\"%s\" type=\"xs:string\" default=\"%s\"/>", rec.name, rec.value);
      } else if (strcmp(rec.regex, "^[0-9]+$") == 0) {
        sprintf(buf, "<xs:attribute name=\"%s\" type=\"xs:integer\" default=\"%s\"/>", rec.name, rec.value);
      } else if (strcmp(rec.regex, "^[^[:space:]]*") == 0) {
        sprintf(buf, "<xs:attribute name=\"%s\" type=\"xs:pattern_no_space\" default=\"%s\"/>",
                    rec.name, rec.value);
      } else if (strcmp(rec.regex, ".+") == 0) {
        sprintf(buf, "<xs:attribute name=\"%s\" type=\"xs:pattern_not_empty\" default=\"%s\"/>",
                    rec.name, rec.value);
      }
    }
    break;

  default:                     // uses special simpleType in schema
    sprintf(buf, "<xs:attribute name=\"%s\" type=\"FIXME\" default=\"%s\"/>", rec.name, rec.value);
    break;
  }
}


//#######################################################################
// For Integration with CNP
//#######################################################################

// ---------------------------------------------------------------------
// TrafficServer_xml
// ---------------------------------------------------------------------
// The trafficserver xml tag should be stored in a location which is
// passed to this function. This function will read in the trafficserver
// xml, parse it, and convert each subsection of the xml configuration into
// TS config files and write them to disk. Must invoke a rereadConfig
// to indicate that a reread of the configuration files is needed.
void
TrafficServer_xml(char *filepath)
{
  XMLDom xtree;
  XMLNode *file_node;
  char *ts_file = NULL;
  FileInfo *info;
  InkHashTableValue lookup;

  ink_assert(file_info_ht);
  if (!file_info_ht) {
    Error("[MgmtConverter::TrafficServer_ts] need to initialize converter module ");
    return;
  }

  if (!filepath) {
    Error("[MgmtConverter::TrafficServer_xml] XML filepath not specified");
    return;
  }

  xtree.LoadFile(filepath);
  Debug("convert", "[TrafficServer_xml] convert %s to *.config files", filepath);

  // process attributes for records.config (internally updated, not to disk)
  convertRecordsFile_xml((XMLNode *) & xtree);

  // process the other *.config files
  for (int i = 0; i < xtree.getChildCount(); i++) {
    file_node = xtree.getChildNode(i);
    file_node->getNodeName();
    ts_file = convertFile_xml(file_node);
    if (ink_hash_table_lookup(file_info_ht, file_node->getNodeName(), &lookup)) {
      info = (FileInfo *) lookup;
      if (ts_file) {
        if (*ts_file) {
          if (WriteFile(info->type, ts_file, strlen(ts_file), -1) != INK_ERR_OKAY) {
            Error("[MgmtConverter::TrafficServer_xml] failed to commit: %s", file_node->getNodeName());
          }
        }
        xfree(ts_file);
        ts_file = NULL;
      }
    } else {
      Error("[MgmtConverter::TrafficServer_xml] invalid file lookup: %s", file_node->getNodeName());
    }
    if (ts_file) {
      xfree(ts_file);
      ts_file = NULL;
    }
  }


  // notify Traffic Server that config files changed
  configFiles->rereadConfig();
}

// ---------------------------------------------------------------------
// TrafficServer_ts
// ---------------------------------------------------------------------
// Need to iterate through all the TS config files and convert them
// into XML format in order to assemble the entire TrafficServer xml tag
void
TrafficServer_ts(char **xml_file)
{
  const char xml_hdr[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  const char start_ts_tag[] = "<trafficserver xmlns=\"http://www.inktomi.com/CNP/trafficserver\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:schemaLocation=\"http://www.inktomi.com/CNP/trafficserver cfg_sample.xsd\" \n";    // do not close the tag
  const char end_ts_tag[] = "</trafficserver>";
  char *filename;
  char *cfile = NULL;
  textBuffer xml(2048);

  ink_assert(file_info_ht);
  if (!file_info_ht) {
    Error("[MgmtConverter::TrafficServer_ts] need to initialize converter module ");
    return;
  }

  if (!xml_file) {
    Error("[MgmtConverter::TrafficServer_ts] invalid buffer pointer parameter");
    return;
  }

  Debug("convert", "[TrafficServer_ts] create new XML trafficserver tag");

  xml.copyFrom(xml_hdr, strlen(xml_hdr));
  xml.copyFrom(start_ts_tag, strlen(start_ts_tag));

  // records.config conversion is special case
  convertRecordsFile_ts(&cfile);
  if (cfile) {
    if (*cfile) {
      xml.copyFrom(cfile, strlen(cfile));
    }
    xfree(cfile);
    cfile = NULL;
  }

  xml.copyFrom(">\n", 2);       // close trafficserver start tag


  // order of how the config files organized in trafficserver tag is
  // determined by order listed in hashtable (build from the schema)
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;
  for (entry = ink_hash_table_iterator_first(file_info_ht, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(file_info_ht, &iterator_state)) {
    filename = (char *) ink_hash_table_entry_key(file_info_ht, entry);
    convertFile_ts(filename, &cfile);
    if (cfile) {
      xml.copyFrom(cfile, strlen(cfile));
      xfree(cfile);
      cfile = NULL;
    }
  }

  xml.copyFrom(end_ts_tag, strlen(end_ts_tag));

  *xml_file = xstrdup(xml.bufPtr());
}


//#######################################################################
// FOR TESTING ONLY
//#######################################################################

// If it hits a certain config file,it will convert the file and
// print the results to output file xml-ts.log
int
testConvertFile_xml(XMLNode * file_node, char *file)
{
  XMLNode *child;
  FILE *fp;                     // output file for conversion results

  if (!file_info_ht)
    return INK_ERR_FAIL;

  if (!file_node || !file) {
    Debug("convert", "[MgmtConverter::testConvertFile_xml] invalid parameters");
    return INK_ERR_PARAMS;
  }

  fp = fopen("xml-ts.log", "w");
  file = NULL;

  fprintf(fp, "\n<!-- CONVERT records.config: -->\n");
  convertRecordsFile_xml(file_node);

  for (int i = 0; i < file_node->getChildCount(); i++) {
    child = file_node->getChildNode(i);
    file = convertFile_xml(child);
    if (file) {
      fprintf(fp, "\n<!-- CONVERT %s: -->\n%s\n", child->getNodeName(), file);
      xfree(file);
      file = NULL;
    } else {
      fprintf(fp, "\n\n<!-- CONVERT %s: ERROR -->\n", child->getNodeName());
    }
  }

  fclose(fp);
  return INK_ERR_OKAY;
}

// Converts the specified TS config file specified by "file" and outputs
// the xml result in file.xml. If file = "all", then all the config files
// are converted to xml.
int
testConvertFile_ts(char *file)
{
  char *xml_file = NULL;
  const char *filename;
  // Not used here.
  //INKError err;
  FILE *fp;                     // output file for conversion results

  if (!file_info_ht)
    return INK_ERR_FAIL;

  if (!file) {
    Debug("convert", "[MgmtConverter::testConvertFile_ts] invalid parameters");
    return INK_ERR_PARAMS;
  }

  char name[128];
  memset(name, 0, 128);
  snprintf(name, 128, "%s.xml", file);
  fp = fopen(name, "w");

  if (strcmp(file, "all") == 0) {
    convertRecordsFile_ts(&xml_file);   // convert records.config
    if (xml_file) {
      fprintf(fp, "\n\n<!-- CONVERT records.config: -->\n%s\n\n", xml_file);
      xfree(xml_file);
      xml_file = NULL;
    }
    // convert files in alphabetical order so easy to compare
    // to template "correct" file
    int i = 0;
    while (config_files[i]) {
      filename = config_files[i];
      convertFile_ts(filename, &xml_file);
      if (xml_file) {
        fprintf(fp, "\n\n<!-- CONVERT %s: -->\n%s\n\n", filename, xml_file);
        xfree(xml_file);
        xml_file = NULL;
      } else {
        fprintf(fp, "\n\n<!-- CONVERT %s: ERROR -->\n\n", filename);
      }
      i++;
    }

  } else {
    convertFile_ts(file, &xml_file);
    if (xml_file) {
      fprintf(fp, "\n\n<!-- CONVERT %s:-->\n%s\n\n", file, xml_file);
      xfree(xml_file);
      xml_file = NULL;
    } else {
      fprintf(fp, "\n\n<!-- CONVERT %s: ERROR --> \n\n", file);
    }
  }

  fclose(fp);
  return INK_ERR_OKAY;
}
