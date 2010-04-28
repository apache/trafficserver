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

#ifndef _MGMT_CONVERTER_
#define _MGMT_CONVERTER_

/****************************************************************************
 *  MgmtConverter.h - Interface to convert XML <==> TS config files
 *
 ****************************************************************************/

#include "XmlUtils.h"
#include "INKMgmtAPI.h"
#include "TextBuffer.h"

typedef int (*RuleConverter_ts) (INKCfgEle * ele, textBuffer * xml_file);       /* TS ==> XML conversion */
typedef char *(*RuleConverter_xml) (XMLNode * rule_node);       /* XML ==> TS conversion */

struct FileInfo
{
  const char *record_name;
  INKFileNameT type;
  RuleConverter_ts converter_ts;
  RuleConverter_xml converter_xml;
};

/* 
  .._xml means the function will convert the file from xml  to text format
  .._ts means the function will convert from TS's text file format to XML 
*/

void converterInit();

int convertFile_xml(XMLNode * file_node, char **file, char *nsp = NULL);
char *convertAdminAccessRule_xml(XMLNode * rule_node);
char *convertArmSecurityRule_xml(XMLNode * rule_node);
char *convertBypassRule_xml(XMLNode * rule_node);
char *convertCacheRule_xml(XMLNode * rule_node);
char *convertCongestionRule_xml(XMLNode * rule_node);
char *convertFtpRemapRule_xml(XMLNode * rule_node);
char *convertHostingRule_xml(XMLNode * rule_node);
char *convertIcpRule_xml(XMLNode * rule_node);
char *convertIpAllowRule_xml(XMLNode * rule_node);
char *convertIpnatRule_xml(XMLNode * rule_node);
char *convertMgmtAllowRule_xml(XMLNode * rule_node);
char *convertParentRule_xml(XMLNode * rule_node);
char *convertPartitionRule_xml(XMLNode * rule_node);
char *convertRemapRule_xml(XMLNode * rule_node);
char *convertSocksRule_xml(XMLNode * rule_node);
char *convertSplitDnsRule_xml(XMLNode * rule_node);
char *convertStorageRule_xml(XMLNode * rule_node);
char *convertUpdateRule_xml(XMLNode * rule_node);
char *convertVaddrsRule_xml(XMLNode * rule_node);

/* helper functions to convert common structures */
int convertPdssFormat_xml(XMLNode * pdss_node, INKPdSsFormat * pdss);
int convertTimePeriod_xml(XMLNode * time_node, INKHmsTime * time);
int convertSecSpecs_xml(XMLNode * sspec_node, INKSspec * sspecs);
int convertPortList_xml(XMLNode * port_node, INKPortList * list);
int convertIpAddrEle_xml(XMLNode * ip_node, INKIpAddrEle * ip);
int convertPortEle_xml(XMLNode * port_node, INKPortEle * port);
int convertIpAddrList_xml(XMLNode * list_node, INKIpAddrList list);
int convertDomainList_xml(XMLNode * list_node, INKDomainList list);
int convertDomain_xml(XMLNode * dom_node, INKDomain * dom);


/* TS ==> XML conversion */
int convertFile_ts(const char *filename, char **xml_file);
/* RuleConverter function pointers */
int convertAdminAccessRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertArmSecurityRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertBypassRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertCacheRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertCongestionRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertFtpRemapRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertHostingRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertIcpRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertIpAllowRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertIpnatRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertMgmtAllowRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertParentRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertPartitionRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertRemapRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertSocksRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertSplitDnsRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertStorageRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertUpdateRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);
int convertVaddrsRule_ts(INKCfgEle * cfg_ele, textBuffer * xml_file);

/* helper functions to convert common structures */
int convertPortEle_ts(INKPortEle * ele, textBuffer * xml_file, char *tag_name);
int convertIpAddrEle_ts(INKIpAddrEle * ele, textBuffer * xml_file, const char *tag_name);
int convertPdssFormat_ts(INKPdSsFormat * pdss, textBuffer * xml_file);
int convertTimePeriod_ts(INKHmsTime * time, textBuffer * xml_file);
int convertIpAddrList_ts(INKIpAddrList list, textBuffer * xml_file, const char *tag_name);
int convertDomainList_ts(INKDomainList list, textBuffer * xml_file, const char *tag_name);

/* helper functions to write common xml */
void writeXmlStartTag(textBuffer * xml, const char *name, const char *nsp = NULL);
void writeXmlAttrStartTag(textBuffer * xml, const char *name, char *nsp = NULL);
void writeXmlEndTag(textBuffer * xml, const char *name, const char *nsp = NULL);
void writeXmlElement(textBuffer * xml, const char *elemName, const char *value, const char *nsp = NULL);
void writeXmlElement_int(textBuffer * xml, const char *elemName, int value, const char *nsp = NULL);
void writeXmlAttribute(textBuffer * xml, const char *attrName, const char *value);
void writeXmlAttribute_int(textBuffer * xml, const char *attrName, int value);
void writeXmlClose(textBuffer * xml);

int strcmptag(char *fulltag, char *name, char *nsp);

/* records.config functions */
void convertRecordsFile_ts(char **xml_file);
void convertRecordsFile_xml(XMLNode * file_node);

void createXmlSchemaRecords(char **output);
void getXmlRecType(struct RecordElement rec, char *buf, int buf_size);


void TrafficServer_xml(char *filepath);
void TrafficServer_ts(char **xml_file);

int testConvertFile_xml(XMLNode * file_node, char **file);
int testConvertFile_ts(char *file);


#endif // _MGMT_CONVERTER
