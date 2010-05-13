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
 *  WebHttpRener.h - html rendering and assembly
 *
 *
 ****************************************************************************/

#ifndef _WEB_HTTP_RENDER_H_
#define _WEB_HTTP_RENDER_H_

#include "TextBuffer.h"
#include "WebHttpContext.h"

#include "P_RecCore.h"

//-------------------------------------------------------------------------
// types
//-------------------------------------------------------------------------

#define HtmlId     char*
#define HtmlCss    char*
#define HtmlAlign  char*
#define HtmlValign char*
#define HtmlMethod char*
#define HtmlType   char*
#define HtmlWrap   char*

//-------------------------------------------------------------------------
// defines
//-------------------------------------------------------------------------

// These HTML_ID's must be defined in the language dictionary file,
// (CONFIG proxy.config.admin.lang_dict STRING <file>)
#define HTML_ID_CLEAR                   "s_text_000"
#define HTML_ID_ON                      "s_text_001"
#define HTML_ID_OFF                     "s_text_002"
#define HTML_ID_WARNING                 "s_text_003"
#define HTML_ID_OK                      "s_text_004"
#define HTML_ID_ALARM                   "s_text_005"
#define HTML_ID_CACHE_HIT_RATE          "s_text_006"
#define HTML_ID_FRESH                   "s_text_007"
#define HTML_ID_REFRESH                 "s_text_008"
#define HTML_ID_ERRORS                  "s_text_009"
#define HTML_ID_ABORTS                  "s_text_010"
#define HTML_ID_ACTIVE_CLIENTS          "s_text_011"
#define HTML_ID_ACTIVE_SERVERS          "s_text_012"
#define HTML_ID_NODE_IP_ADDRESS         "s_text_017"
#define HTML_ID_CACHE_FREE_SPACE        "s_text_018"
#define HTML_ID_HOSTDB_HIT_RATE         "s_text_019"
#define HTML_ID_STATUS_ACTIVE           "s_text_020"
#define HTML_ID_STATUS_INACTIVE         "s_text_021"
#define HTML_ID_CLUSTERING              "s_text_022"
#define HTML_ID_UP_SINCE                "s_text_023"
#define HTML_ID_ENABLED                 "s_text_024"
#define HTML_ID_MANAGEMENT_ONLY         "s_text_025"
#define HTML_ID_UNKNOWN                 "s_text_026"
#define HTML_ID_UNDEFINED               "s_text_027"
#define HTML_ID_PENDING                 "s_text_028"
#define HTML_ID_NO_ACTIVE_ALARMS        "s_text_029"
#define HTML_ID_VIP_DISABLED            "s_text_030"
#define HTML_ID_NO_ADDITIONAL_USERS     "s_text_031"
#define HTML_ID_AUTH_NO_ACCESS          "s_text_032"
#define HTML_ID_AUTH_MONITOR            "s_text_033"
#define HTML_ID_AUTH_MONITOR_VIEW       "s_text_034"
#define HTML_ID_AUTH_MONITOR_CHANGE     "s_text_035"
#define HTML_ID_USER                    "s_text_036"
#define HTML_ID_NO_PLUGINS              "s_text_037"
#define HTML_ID_NO_STATS                "s_text_039"

#define HTML_ID_SUBMIT_WARN_FLG         "s_text_100"
#define HTML_ID_INVALID_ENTRY           "s_text_101"
#define HTML_ID_OLD_PASSWD_INCORRECT    "s_text_102"
#define HTML_ID_NEW_PASSWD_MISTYPE      "s_text_103"
#define HTML_ID_NEW_USER_DUPLICATE      "s_text_104"
#define HTML_ID_OUT_OF_DATE             "s_text_105"
#define HTML_ID_UNABLE_TO_SUBMIT        "s_text_106"
#define HTML_ID_NEW_USERNAME_LENGTH     "s_text_107"
#define HTML_ID_MISSING_ENTRY           "s_text_108"
#define HTML_ID_LOG_SAVE_FAILED         "s_text_109"
#define HTML_ID_LOG_REMOVE_FAILED       "s_text_110"
#define HTML_ID_DUPLICATE_ENTRY         "s_text_111"
#define HTML_ID_PERMISSION_DENIED       "s_text_112"
#define HTML_ID_FILE_TRUNCATED          "s_text_113"
#define HTML_ID_SESSION_VALUE_LIMIT     "s_text_114"
#define HTML_ID_FLOPPY_UNMOUNT_ERR	"s_text_115"
// Currently unsused: 116
#define HTML_ID_FLOPPY_NO_SPACE		"s_text_117"

#define HTML_ID_SUBMIT_NOTE_FLG         "s_text_150"
#define HTML_ID_RESTART_REQUIRED        "s_text_151"
#define HTML_ID_NEW_ADMIN_PASSWD_SET    "s_text_152"
#define HTML_ID_RESTART_REQUIRED_FILE   "s_text_155"

#define HTML_ID_INSPECTOR_REGEX_MISSED  "s_text_300"
#define HTML_ID_INSPECTOR_CACHE_MISSED  "s_text_301"
#define HTML_ID_INSPECTOR_REGEX_MATCHED "s_text_302"
#define HTML_ID_INSPECTOR_DELETED       "s_text_303"
#define HTML_ID_INSPECTOR_INVALIDATED   "s_text_304"
#define HTML_ID_INSPECTOR_DOCUMENT      "s_text_305"
#define HTML_ID_INSPECTOR_ALTERNATE     "s_text_306"
#define HTML_ID_INSPECTOR_ALTERNATE_NUM "s_text_307"
#define HTML_ID_INSPECTOR_REQ_TIME      "s_text_308"
#define HTML_ID_INSPECTOR_REQ_HEADER    "s_text_309"
#define HTML_ID_INSPECTOR_RPN_TIME      "s_text_310"
#define HTML_ID_INSPECTOR_RPN_HEADER    "s_text_311"
#define HTML_ID_INSPECTOR_GENERAL_INFO  "s_text_312"
#define HTML_ID_INSPECTOR_REGEX_ERROR   "s_text_313"
#define HTML_ID_NETWORK_CONFIG_FAIL     "s_text_400"
#define HTML_ID_NETWORK_CONFIG_DISALLOW "s_text_401"
/*******************************************************/

/* general macro */
#define HTML_ID_CFG_NO_RULES              "s_text_550"
#define HTML_ID_CFG_EDIT_SECONDARY_SPEC   "s_text_551"
#define HTML_ID_CFG_EDIT_ADDITIONAL_SPEC  "s_text_552"

// mgmt_allow.config
#define HTML_ID_CFG_EDIT_IP_ACTION        "s_text_600"
#define HTML_ID_CFG_EDIT_IP_ACTION_HELP   "s_text_601"
#define HTML_ID_CFG_EDIT_SOURCE_IP        "s_text_602"
#define HTML_ID_CFG_EDIT_SOURCE_IP_HELP   "s_text_603"
#define HTML_ID_CFG_EDIT_SOURCE_IP_EG     "s_text_604"

// cache.config
#define HTML_ID_CFG_EDIT_RULE_TYPE        "s_text_610"
#define HTML_ID_CFG_EDIT_RULE_TYPE_HELP   "s_text_611"
#define HTML_ID_CFG_EDIT_PDEST_TYPE       "s_text_612"
#define HTML_ID_CFG_EDIT_PDEST_TYPE_HELP  "s_text_613"
#define HTML_ID_CFG_EDIT_PDEST_VALUE      "s_text_614"
#define HTML_ID_CFG_EDIT_PDEST_VALUE_HELP "s_text_615"
#define HTML_ID_CFG_EDIT_PDEST_VALUE_EG   "s_text_616"
#define HTML_ID_CFG_EDIT_TIME             "s_text_618"
#define HTML_ID_CFG_EDIT_TIME_HELP        "s_text_619"
#define HTML_ID_CFG_EDIT_TIME_EG          "s_text_620"
#define HTML_ID_CFG_EDIT_PREFIX           "s_text_621"
#define HTML_ID_CFG_EDIT_PREFIX_HELP      "s_text_622"
#define HTML_ID_CFG_EDIT_PREFIX_EG        "s_text_623"
#define HTML_ID_CFG_EDIT_SUFFIX           "s_text_624"
#define HTML_ID_CFG_EDIT_SUFFIX_HELP      "s_text_625"
#define HTML_ID_CFG_EDIT_SUFFIX_EG        "s_text_626"
#define HTML_ID_CFG_EDIT_SOURCE_IP_2      "s_text_627"
#define HTML_ID_CFG_EDIT_SOURCE_IP_2_HELP "s_text_628"
#define HTML_ID_CFG_EDIT_SOURCE_IP_2_EG   "s_text_629"
#define HTML_ID_CFG_EDIT_PORT             "s_text_630"
#define HTML_ID_CFG_EDIT_PORT_HELP        "s_text_631"
#define HTML_ID_CFG_EDIT_PORT_EG          "s_text_632"
#define HTML_ID_CFG_EDIT_METHOD           "s_text_633"
#define HTML_ID_CFG_EDIT_METHOD_HELP      "s_text_634"
#define HTML_ID_CFG_EDIT_SCHEME           "s_text_635"
#define HTML_ID_CFG_EDIT_SCHEME_HELP      "s_text_636"
#define HTML_ID_CFG_EDIT_MIXT_SCHEME      "s_text_637"
#define HTML_ID_CFG_EDIT_MIXT_SCHEME_HELP "s_text_638"
#define HTML_ID_CFG_EDIT_TIME_PERIOD      "s_text_640"
#define HTML_ID_CFG_EDIT_TIME_PERIOD_HELP "s_text_641"
#define HTML_ID_CFG_EDIT_TIME_PERIOD_EG   "s_text_642"

// update.config
#define HTML_ID_CFG_EDIT_URL              "s_text_650"
#define HTML_ID_CFG_EDIT_URL_HELP         "s_text_651"
#define HTML_ID_CFG_EDIT_REQUEST_HDR      "s_text_652"
#define HTML_ID_CFG_EDIT_REQUEST_HDR_HELP "s_text_653"
#define HTML_ID_CFG_EDIT_REQUEST_HDR_EG   "s_text_654"
#define HTML_ID_CFG_EDIT_OFFSET_HOUR      "s_text_655"
#define HTML_ID_CFG_EDIT_OFFSET_HOUR_HELP "s_text_656"
#define HTML_ID_CFG_EDIT_OFFSET_HOUR_EG   "s_text_657"
#define HTML_ID_CFG_EDIT_INTERVAL         "s_text_658"
#define HTML_ID_CFG_EDIT_INTERVAL_HELP    "s_text_659"
#define HTML_ID_CFG_EDIT_INTERVAL_EG      "s_text_660"
#define HTML_ID_CFG_EDIT_RECUR_DEPTH      "s_text_661"
#define HTML_ID_CFG_EDIT_RECUR_DEPTH_HELP "s_text_662"

// parent.config
#define HTML_ID_CFG_EDIT_PARENTS          "s_text_670"
#define HTML_ID_CFG_EDIT_PARENTS_HELP     "s_text_671"
#define HTML_ID_CFG_EDIT_PARENTS_EG       "s_text_672"
#define HTML_ID_CFG_EDIT_ROUND_ROBIN      "s_text_673"
#define HTML_ID_CFG_EDIT_ROUND_ROBIN_HELP "s_text_674"
#define HTML_ID_CFG_EDIT_GO_DIRECT        "s_text_675"
#define HTML_ID_CFG_EDIT_GO_DIRECT_HELP   "s_text_676"

// icp.config
#define HTML_ID_CFG_EDIT_PEER_HOST        "s_text_680"
#define HTML_ID_CFG_EDIT_PEER_HOST_HELP   "s_text_681"
#define HTML_ID_CFG_EDIT_PEER_IP          "s_text_682"
#define HTML_ID_CFG_EDIT_PEER_IP_HELP     "s_text_683"
#define HTML_ID_CFG_EDIT_PEER_TYPE        "s_text_684"
#define HTML_ID_CFG_EDIT_PEER_TYPE_HELP   "s_text_685"
#define HTML_ID_CFG_EDIT_PEER_PORT        "s_text_686"
#define HTML_ID_CFG_EDIT_PEER_PORT_HELP   "s_text_687"
#define HTML_ID_CFG_EDIT_ICP_PORT         "s_text_688"
#define HTML_ID_CFG_EDIT_ICP_PORT_HELP    "s_text_689"
#define HTML_ID_CFG_EDIT_MCAST_STATE      "s_text_690"
#define HTML_ID_CFG_EDIT_MCAST_STATE_HELP "s_text_691"
#define HTML_ID_CFG_EDIT_MCAST_IP         "s_text_692"
#define HTML_ID_CFG_EDIT_MCAST_IP_HELP    "s_text_693"
#define HTML_ID_CFG_EDIT_MCAST_TTL        "s_text_694"
#define HTML_ID_CFG_EDIT_MCAST_TTL_HELP   "s_text_695"

// remap.config
#define HTML_ID_CFG_EDIT_RULE_TYPE_HELP_2   "s_text_700"
#define HTML_ID_CFG_EDIT_SCHEME_HELP_2      "s_text_701"
#define HTML_ID_CFG_EDIT_FROM_HOST          "s_text_702"
#define HTML_ID_CFG_EDIT_FROM_HOST_HELP     "s_text_703"
#define HTML_ID_CFG_EDIT_FROM_PORT          "s_text_704"
#define HTML_ID_CFG_EDIT_FROM_PORT_HELP     "s_text_705"
#define HTML_ID_CFG_EDIT_FROM_PATH          "s_text_706"
#define HTML_ID_CFG_EDIT_FROM_PATH_HELP     "s_text_707"
#define HTML_ID_CFG_EDIT_TO_HOST            "s_text_708"
#define HTML_ID_CFG_EDIT_TO_HOST_HELP       "s_text_709"
#define HTML_ID_CFG_EDIT_TO_PORT            "s_text_710"
#define HTML_ID_CFG_EDIT_TO_PORT_HELP       "s_text_711"
#define HTML_ID_CFG_EDIT_TO_PATH            "s_text_712"
#define HTML_ID_CFG_EDIT_TO_PATH_HELP       "s_text_713"
#define HTML_ID_CFG_EDIT_MIXT_SCHEME_HELP_2 "s_text_714"
#define HTML_ID_CFG_EDIT_FROM_SCHEME        "s_text_715"
#define HTML_ID_CFG_EDIT_TO_SCHEME          "s_text_716"

// ipnat.conf
#define HTML_ID_CFG_EDIT_ETH_INTERFACE      "s_text_720"
#define HTML_ID_CFG_EDIT_ETH_INTERFACE_HELP "s_text_721"
#define HTML_ID_CFG_EDIT_CONN_TYPE_HELP_2   "s_text_722"
#define HTML_ID_CFG_EDIT_SOURCE_IP_HELP_3   "s_text_723"
#define HTML_ID_CFG_EDIT_SOURCE_IP_EG_3     "s_text_724"
#define HTML_ID_CFG_EDIT_SOURCE_PORT        "s_text_725"
#define HTML_ID_CFG_EDIT_SOURCE_PORT_HELP   "s_text_726"
#define HTML_ID_CFG_EDIT_DEST_IP            "s_text_727"
#define HTML_ID_CFG_EDIT_DEST_IP_HELP       "s_text_728"
#define HTML_ID_CFG_EDIT_DEST_PORT          "s_text_729"
#define HTML_ID_CFG_EDIT_DEST_PORT_HELP     "s_text_730"
#define HTML_ID_CFG_EDIT_USER_PROTOCOL      "s_text_907"
#define HTML_ID_CFG_EDIT_USER_PROTOCOL_HELP "s_text_908"
#define HTML_ID_CFG_EDIT_SOURCE_CIDR        "s_text_731"
#define HTML_ID_CFG_EDIT_SOURCE_CIDR_HELP   "s_text_732"

// arm_security.config
#define HTML_ID_CFG_EDIT_RULE_TYPE_HELP_3   "s_text_735"
#define HTML_ID_CFG_EDIT_CONN_TYPE          "s_text_736"
#define HTML_ID_CFG_EDIT_CONN_TYPE_HELP     "s_text_737"
#define HTML_ID_CFG_EDIT_SOURCE_IP_HELP_4   "s_text_738"
#define HTML_ID_CFG_EDIT_SOURCE_PORT_HELP_2 "s_text_739"
#define HTML_ID_CFG_EDIT_DEST_IP_HELP_2     "s_text_740"
#define HTML_ID_CFG_EDIT_DEST_PORT_HELP_2   "s_text_741"
#define HTML_ID_CFG_EDIT_OPEN_PORT          "s_text_742"
#define HTML_ID_CFG_EDIT_OPEN_PORT_HELP     "s_text_743"
#define HTML_ID_CFG_EDIT_PORT_LIST_EG       "s_text_744"

// bypass.config
#define HTML_ID_CFG_EDIT_RULE_TYPE_HELP_4   "s_text_750"
#define HTML_ID_CFG_EDIT_SOURCE_IP_HELP_5   "s_text_751"
#define HTML_ID_CFG_EDIT_SOURCE_IP_EG_5     "s_text_752"
#define HTML_ID_CFG_EDIT_DEST_IP_HELP_3     "s_text_753"
#define HTML_ID_CFG_EDIT_DEST_IP_EG_3       "s_text_754"

// hosting.config
#define HTML_ID_CFG_EDIT_PDEST_TYPE_HELP_2  "s_text_760"
#define HTML_ID_CFG_EDIT_PDEST_VALUE_HELP_2 "s_text_761"
#define HTML_ID_CFG_EDIT_PARTITIONS         "s_text_762"
#define HTML_ID_CFG_EDIT_PARTITIONS_HELP    "s_text_763"

// partition.config
#define HTML_ID_CFG_EDIT_PARTITION_NUM      "s_text_770"
#define HTML_ID_CFG_EDIT_PARTITION_NUM_HELP "s_text_771"
#define HTML_ID_CFG_EDIT_SCHEME_HELP_3      "s_text_772"
#define HTML_ID_CFG_EDIT_PSIZE              "s_text_773"
#define HTML_ID_CFG_EDIT_PSIZE_HELP         "s_text_774"
#define HTML_ID_CFG_EDIT_PSIZE_EG           "s_text_775"
#define HTML_ID_CFG_EDIT_PSIZE_FMT          "s_text_776"
#define HTML_ID_CFG_EDIT_PSIZE_FMT_HELP     "s_text_777"

// splitdns.config
#define HTML_ID_CFG_EDIT_PDEST_TYPE_HELP_3  "s_text_780"
#define HTML_ID_CFG_EDIT_PDEST_VALUE_HELP_3 "s_text_781"
#define HTML_ID_CFG_EDIT_DNS_SERVER_IP      "s_text_782"
#define HTML_ID_CFG_EDIT_DNS_SERVER_IP_HELP "s_text_783"
#define HTML_ID_CFG_EDIT_DNS_SERVER_IP_EG   "s_text_784"
#define HTML_ID_CFG_EDIT_DOMAIN_NAME        "s_text_785"
#define HTML_ID_CFG_EDIT_DOMAIN_NAME_HELP   "s_text_786"
#define HTML_ID_CFG_EDIT_SEARCH_LIST        "s_text_787"
#define HTML_ID_CFG_EDIT_SEARCH_LIST_HELP   "s_text_788"
#define HTML_ID_CFG_EDIT_SEARCH_LIST_EG     "s_text_789"

// filter.config
#define HTML_ID_CFG_EDIT_AUTH_SPEC            "s_text_809"
#define HTML_ID_CFG_EDIT_RULE_TYPE_HELP_5     "s_text_810"
#define HTML_ID_CFG_EDIT_PDEST_TYPE_HELP_4    "s_text_811"
#define HTML_ID_CFG_EDIT_PDEST_VALUE_HELP_4   "s_text_812"
#define HTML_ID_CFG_EDIT_HEADER_TYPE          "s_text_813"
#define HTML_ID_CFG_EDIT_HEADER_TYPE_HELP     "s_text_814"
#define HTML_ID_CFG_EDIT_LDAP_SERVER          "s_text_815"
#define HTML_ID_CFG_EDIT_LDAP_SERVER_HELP     "s_text_816"
#define HTML_ID_CFG_EDIT_LDAP_SERVER_EG       "s_text_817"
#define HTML_ID_CFG_EDIT_LDAP_BASE_DN         "s_text_818"
#define HTML_ID_CFG_EDIT_LDAP_BASE_DN_HELP    "s_text_819"
#define HTML_ID_CFG_EDIT_LDAP_UID             "s_text_820"
#define HTML_ID_CFG_EDIT_LDAP_UID_HELP        "s_text_821"
#define HTML_ID_CFG_EDIT_LDAP_ATTR_NAME       "s_text_822"
#define HTML_ID_CFG_EDIT_LDAP_ATTR_NAME_HELP  "s_text_823"
#define HTML_ID_CFG_EDIT_LDAP_ATTR_VALUE      "s_text_824"
#define HTML_ID_CFG_EDIT_LDAP_ATTR_VALUE_HELP "s_text_825"
#define HTML_ID_CFG_EDIT_LDAP_REALM           "s_text_826"
#define HTML_ID_CFG_EDIT_LDAP_REALM_HELP      "s_text_827"
#define HTML_ID_CFG_EDIT_LDAP_OPTIONS         "s_text_828"
#define HTML_ID_CFG_EDIT_LDAP_BIND_DN         "s_text_829"
#define HTML_ID_CFG_EDIT_LDAP_BIND_DN_HELP    "s_text_830"
#define HTML_ID_CFG_EDIT_LDAP_BIND_PWD        "s_text_831"
#define HTML_ID_CFG_EDIT_LDAP_BIND_PWD_HELP   "s_text_832"
#define HTML_ID_CFG_EDIT_LDAP_BIND_PWD_FILE   "s_text_833"
#define HTML_ID_CFG_EDIT_LDAP_BIND_PWD_FILE_HELP  "s_text_834"
#define HTML_ID_CFG_EDIT_LDAP_RDR_URL         "s_text_835"
#define HTML_ID_CFG_EDIT_LDAP_RDR_URL_HELP    "s_text_836"

#define HTML_ID_CFG_EDIT_USER                 "s_text_844"
#define HTML_ID_CFG_EDIT_USER_HELP            "s_text_845"
#define HTML_ID_CFG_EDIT_PASSWORD             "s_text_846"

// ip_allow.config
#define HTML_ID_CFG_EDIT_IP_ACTION_HELP_2     "s_text_875"
#define HTML_ID_CFG_EDIT_SOURCE_IP_HELP_6     "s_text_876"
#define HTML_ID_CFG_EDIT_SOURCE_IP_EG_6       "s_text_877"

// socks.config
#define HTML_ID_CFG_EDIT_RULE_TYPE_HELP_6     "s_text_880"
#define HTML_ID_CFG_EDIT_ORIGIN_SERVER        "s_text_881"
#define HTML_ID_CFG_EDIT_ORIGIN_SERVER_HELP   "s_text_882"
#define HTML_ID_CFG_EDIT_ORIGIN_SERVER_EG     "s_text_883"
#define HTML_ID_CFG_EDIT_USER_HELP_2          "s_text_884"
#define HTML_ID_CFG_EDIT_SOCKS_PASSWORD       "s_text_885"
#define HTML_ID_CFG_EDIT_SOCKS_PASSWORD_HELP  "s_text_886"
#define HTML_ID_CFG_EDIT_DEST_IP_HELP_4       "s_text_887"
#define HTML_ID_CFG_EDIT_SOCKS_SERVER         "s_text_888"
#define HTML_ID_CFG_EDIT_SOCKS_SERVER_HELP    "s_text_889"
#define HTML_ID_CFG_EDIT_SOCKS_SERVER_EG      "s_text_890"
#define HTML_ID_CFG_EDIT_ROUND_ROBIN_HELP_2   "s_text_891"

// vaddrs.config
#define HTML_ID_CFG_EDIT_VIRTUAL_IP           "s_text_900"
#define HTML_ID_CFG_EDIT_VIRTUAL_IP_HELP      "s_text_901"
#define HTML_ID_CFG_EDIT_ETH_INTERFACE_HELP_3 "s_text_902"
#define HTML_ID_CFG_EDIT_SUB_INTERFACE        "s_text_903"
#define HTML_ID_CFG_EDIT_SUB_INTERFACE_HELP   "s_text_904"

// Config File Editor error messages
#define HTML_ID_CFG_COMMIT_ERROR              "s_text_905"
#define HTML_ID_CFG_INVALID_RULE              "s_text_906"


#define HTML_ID_CLEAR_CLUSTER_STAT            "s_text_2020"
#define HTML_ID_CLEAR_CLUSTER_STAT_HELP       "s_text_2021"


/*********************/

#define HTML_CSS_NONE                   NULL
#define HTML_CSS_ALARM_COLOR            "alarmColor"
#define HTML_CSS_HILIGHT_COLOR          "hilightColor"
#define HTML_CSS_UNHILIGHT_COLOR        "unhilightColor"
#define HTML_CSS_PRIMARY_COLOR          "primaryColor"
#define HTML_CSS_SECONDARY_COLOR        "secondaryColor"
#define HTML_CSS_TERTIARY_COLOR         "tertiaryColor"
#define HTML_CSS_WARNING_COLOR          "warningColor"
#define HTML_CSS_GREY_LINKS             "greyLinks"
#define HTML_CSS_RED_LINKS              "redLinks"
#define HTML_CSS_BLUE_LINKS             "blueLinks"
#define HTML_CSS_BLACK_LABEL            "blackLabel"
#define HTML_CSS_RED_LABEL              "redLabel"
#define HTML_CSS_BLUE_LABEL             "blueLabel"
#define HTML_CSS_CONFIGURE_LABEL        "configureLabel"
#define HTML_CSS_CONFIGURE_LABEL_SMALL  "configureLabelSmall"
#define HTML_CSS_BLACK_ITEM             "blackItem"
#define HTML_CSS_WHITE_TEXT             "whiteText"
#define HTML_CSS_BODY_TEXT              "bodyText"
#define HTML_CSS_BODY_READONLY_TEXT     "bodyReadonlyText"
#define HTML_CSS_ALARM_BUTTON           "alarmButton"
#define HTML_CSS_CONFIGURE_BUTTON       "configureButton"
#define HTML_CSS_CONFIGURE_HELP         "configureHelp"
#define HTML_CSS_GRAPH                  "graph"
#define HTML_CSS_HELPBG                 "helpBg"

#define HTML_ALIGN_NONE                 NULL
#define HTML_ALIGN_LEFT                 "left"
#define HTML_ALIGN_CENTER               "center"
#define HTML_ALIGN_RIGHT                "right"

#define HTML_VALIGN_NONE                NULL
#define HTML_VALIGN_TOP                 "top"
#define HTML_VALIGN_BOTTOM              "bottom"

#define HTML_METHOD_POST                "POST"
#define HTML_METHOD_GET                 "GET"

#define HTML_TYPE_HIDDEN                "hidden"
#define HTML_TYPE_SUBMIT                "submit"
#define HTML_TYPE_CHECKBOX              "checkbox"
#define HTML_TYPE_BUTTON                "button"

#define HTML_WRAP_OFF                   "off"

#define HTML_ALARM_FILE                 "/monitor/m_alarm.ink"
#define HTML_MGMT_GENERAL_FILE          "/configure/c_mgmt_general.ink"
#define HTML_MGMT_LOGIN_FILE            "/configure/c_mgmt_login.ink"
#define HTML_INSPECTOR_DISPLAY_FILE     "/configure/c_inspector_display.ink"
#define HTML_CONFIG_DISPLAY_FILE        "/configure/c_config_display.ink"
#define HTML_TREE_HEADER_FILE           "/include/tree_header.ink"
#define HTML_TREE_FOOTER_FILE           "/include/tree_footer.ink"
#define HTML_DEFAULT_MONITOR_FILE       "/monitor/m_overview.ink"
#define HTML_DEFAULT_CONFIGURE_FILE     "/configure/c_basic.ink"
#define HTML_OTW_UPGRADE_FILE           "/configure/c_otw_upgrade.ink"
#define HTML_OTW_UPGRADE_CGI_FILE       "/configure/helper/traffic_shell.cgi"
#define HTML_FEATURE_ON_OFF_FILE        "/configure/c_basic.ink"
#define HTML_DEFAULT_HELP_FILE          "/help/ts.ink"

#define HTML_CHART_FILE                 "/charting/chart.cgi"
#define HTML_SUBMIT_ALARM_FILE          "/submit_alarm.cgi"
#define HTML_SUBMIT_MGMT_AUTH_FILE      "/submit_mgmt_auth.cgi"
//#define HTML_SUBMIT_SNAPSHOT_FILE       "/submit_snapshot.cgi"
#define HTML_SUBMIT_SNAPSHOT_FILESYSTEM "/submit_snapshot_filesystem.cgi"
#define HTML_SUBMIT_SNAPSHOT_FTPSERVER  "/submit_snapshot_ftpserver.cgi"
#define HTML_SUBMIT_SNAPSHOT_FLOPPY     "/submit_snapshot_floppy.cgi"
#define HTML_SUBMIT_INSPECTOR_FILE      "/submit_inspector.cgi"
#define HTML_SUBMIT_INSPECTOR_DPY_FILE  "/configure/submit_inspector_display.cgi"
#define HTML_SUBMIT_VIEW_LOGS_FILE      "/log.cgi"
#define HTML_VIEW_DEBUG_LOGS_FILE       "/configure/c_view_debug_logs.ink"
#define HTML_SUBMIT_UPDATE_FILE         "/submit_update.cgi"
#define HTML_SUBMIT_UPDATE_CONFIG       "/submit_update_config.cgi"
#define HTML_SUBMIT_CONFIG_DISPLAY      "/configure/submit_config_display.cgi"
#define HTML_SUBMIT_NET_CONFIG          "/submit_net_config.cgi"
#define HTML_SUBMIT_OTW_UPGRADE_FILE    "/submit_otw_upgrade.cgi"
#define HTML_BACKDOOR_STATS             "/monitor/m_records.cgi"
#define HTML_BACKDOOR_CONFIGS           "/configure/c_records.cgi"
#define HTML_BACKDOOR_STATS_REC         "/monitor/m_records_rec.cgi"
#define HTML_BACKDOOR_CONFIGS_REC       "/configure/c_records_rec.cgi"
#define HTML_BACKDOOR_CONFIG_FILES      "/configure/f_configs.cgi"
#define HTML_BACKDOOR_DEBUG_LOGS        "/configure/d_logs.cgi"
#define HTML_SYNTHETIC_FILE             "/synthetic.txt"

#define HTML_CONFIG_FILE_TAG            "filename"
#define HTML_FILE_ALL_CONFIG            "/configure/f_configs.ink"
#define HTML_FILE_ARM_SECURITY_CONFIG   "/configure/f_arm_security_config.ink"
#define HTML_FILE_BYPASS_CONFIG         "/configure/f_bypass_config.ink"
#define HTML_FILE_CACHE_CONFIG          "/configure/f_cache_config.ink"
#define HTML_FILE_FILTER_CONFIG         "/configure/f_filter_config.ink"
#define HTML_FILE_HOSTING_CONFIG        "/configure/f_hosting_config.ink"
#define HTML_FILE_ICP_CONFIG            "/configure/f_icp_config.ink"
#define HTML_FILE_IP_ALLOW_CONFIG       "/configure/f_ip_allow_config.ink"
#define HTML_FILE_IPNAT_CONFIG          "/configure/f_ipnat_config.ink"
#define HTML_FILE_MGMT_ALLOW_CONFIG     "/configure/f_mgmt_allow_config.ink"
#define HTML_FILE_PARENT_CONFIG         "/configure/f_parent_config.ink"
#define HTML_FILE_PARTITION_CONFIG      "/configure/f_partition_config.ink"
#define HTML_FILE_REMAP_CONFIG          "/configure/f_remap_config.ink"
#define HTML_FILE_SOCKS_CONFIG          "/configure/f_socks_config.ink"
#define HTML_FILE_SPLIT_DNS_CONFIG      "/configure/f_split_dns_config.ink"
#define HTML_FILE_UPDATE_CONFIG         "/configure/f_update_config.ink"
#define HTML_FILE_VADDRS_CONFIG         "/configure/f_vaddrs_config.ink"

#define HTML_HELP_LINK_ARM              "/help/ts.ink?help=c_arm.htm"
#define HTML_HELP_LINK_BYPASS           "/help/ts.ink?help=c_bypass.htm"
#define HTML_HELP_LINK_CACHE            "/help/ts.ink?help=ccache.htm"
#define HTML_HELP_LINK_FILTER           "/help/ts.ink?help=c_filter.htm"
#define HTML_HELP_LINK_HOSTING          "/help/ts.ink?help=c_host.htm"
#define HTML_HELP_LINK_ICP              "/help/ts.ink?help=c_icp.htm"
#define HTML_HELP_LINK_IP_ALLOW         "/help/ts.ink?help=ipallow.htm"
#define HTML_HELP_LINK_IPNAT            "/help/ts.ink?help=ipnat.htm"
#define HTML_HELP_LINK_MGMT_ALLOW       "/help/ts.ink?help=C_mgm.htm"
#define HTML_HELP_LINK_PARENT           "/help/ts.ink?help=c_parent.htm"
#define HTML_HELP_LINK_PARTITION        "/help/ts.ink?help=c_part.htm"
#define HTML_HELP_LINK_REMAP            "/help/ts.ink?help=c_remap.htm"
#define HTML_HELP_LINK_SOCKS            "/help/ts.ink?help=c_socks.htm"
#define HTML_HELP_LINK_SPLIT_DNS        "/help/ts.ink?help=c_split.htm"
#define HTML_HELP_LINK_UPDATE           "/help/ts.ink?help=update.htm"
#define HTML_HELP_LINK_VADDRS           "/help/ts.ink?help=c_vipo.htm"

#define HTML_BLANK_ICON                 "/images/blankIcon.gif"
#define HTML_DOT_CLEAR                  "/images/dot_clear.gif"

#define FAKE_PASSWORD                   "dummy$password**"

//-------------------------------------------------------------------------
// main rendering functions
//-------------------------------------------------------------------------

void WebHttpRenderInit();
int WebHttpRender(WebHttpContext * whc, const char *file);
int WebHttpRender(WebHttpContext * whc, char *file_buf, int file_size);

//-------------------------------------------------------------------------
// html rendering
//-------------------------------------------------------------------------

int HtmlRndrTrOpen(textBuffer * html, const HtmlCss css, const HtmlAlign align);
int HtmlRndrTdOpen(textBuffer * html, const HtmlCss css, const HtmlAlign align, const HtmlValign valign, const char *width, const char *height,
                   int colspan, const char *bg = NULL);
int HtmlRndrAOpen(textBuffer * html, const HtmlCss css, const char *href, const char *target, const char *onclick = NULL);
int HtmlRndrFormOpen(textBuffer * html, const char *name, const HtmlMethod method, const char *action);
int HtmlRndrTextareaOpen(textBuffer * html, const HtmlCss css, int cols, int rows, const HtmlWrap wrap, const char *name, bool readonly);
int HtmlRndrTableOpen(textBuffer * html, const char *width, int border, int cellspacing, int cellpadding,
                      const char *bordercolor = NULL);
int HtmlRndrSpanOpen(textBuffer * html, const HtmlCss css);
int HtmlRndrSelectOpen(textBuffer * html, const HtmlCss css, const char *name, int size);
int HtmlRndrOptionOpen(textBuffer * html, const char *value, bool selected);
int HtmlRndrPreOpen(textBuffer * html, const HtmlCss css, const char *width);
int HtmlRndrUlOpen(textBuffer * html);

int HtmlRndrTrClose(textBuffer * html);
int HtmlRndrTdClose(textBuffer * html);
int HtmlRndrAClose(textBuffer * html);
int HtmlRndrFormClose(textBuffer * html);
int HtmlRndrTextareaClose(textBuffer * html);
int HtmlRndrTableClose(textBuffer * html);
int HtmlRndrSpanClose(textBuffer * html);
int HtmlRndrSelectClose(textBuffer * html);
int HtmlRndrOptionClose(textBuffer * html);
int HtmlRndrPreClose(textBuffer * html);
int HtmlRndrUlClose(textBuffer * html);

int HtmlRndrInput(textBuffer * html, const HtmlCss css, const HtmlType type, const char *name, const char *value, const char *target, const char *onclick);

int HtmlRndrInput(textBuffer * html, MgmtHashTable * dict_ht, HtmlCss css, HtmlType type, char *name, HtmlId value_id);
int HtmlRndrBr(textBuffer * html);
int HtmlRndrLi(textBuffer * html);

int HtmlRndrSpace(textBuffer * html, int num_spaces);
int HtmlRndrText(textBuffer * html, MgmtHashTable * dict_ht, const HtmlId text_id);

int HtmlRndrImg(textBuffer * html, const char *src, const char *border, const char *width, const char *height, const char *hspace);
int HtmlRndrDotClear(textBuffer * html, int width, int height);
int HtmlRndrSelectList(textBuffer * html, const char *listName, const char *options[], int numOpts);

#endif // _WEB_HTTP_RENDER_H_
