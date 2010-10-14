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

/*****************************************************************************
 * Filename: INKMgmtAPI.h
 * Purpose: This file contains all API wrapper functions in one class. In
 * order to eliminate the interdependencies of other library calls, new
 * types and structs will be defined and used in the wrapper function calls.
 *
 *
 ***************************************************************************/

#ifndef __INK_MGMT_API_H__
#define __INK_MGMT_API_H__

#include "ink_port.h"

/***************************************************************************
 * System Specific Items
 ***************************************************************************/

#if defined (_WIN32) && defined (_INK_EXPORT)
#define inkapi __declspec( dllexport )
#elif defined (_WIN32)
#define inkapi __declspec( dllimport )
#else
#define inkapi
#endif

#if defined (_WIN32)
#define inkexp __declspec( dllexport )
#define inkimp __declspec( dllimport )
#else
#define inkexp
#define inkimp
#endif

#if !defined(linux)
#if defined (__SUNPRO_CC) || (defined (__GNUC__) || ! defined(__cplusplus))
#if !defined (bool)
#if !defined(darwin) && !defined(freebsd) && !defined(solaris)
// XXX: What other platforms are there?
#define bool int
#endif
#endif

#if !defined (true)
#define true 1
#endif

#if !defined (false)
#define false 0
#endif

#endif
#endif  // not linux

#if !defined (NULL)
#define NULL 0
#endif

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

#define __INK_RES_PATH(x)   #x
#define _INK_RES_PATH(x)    __INK_RES_PATH (x)
#define INK_RES_PATH(x)     x __FILE__ ":" _INK_RES_PATH (__LINE__)
#define INK_RES_MEM_PATH    INK_RES_PATH ("memory/")

/***************************************************************************
 * Error and Return Values
 ***************************************************************************/

  typedef enum
  {
    INK_ERR_OKAY = 0,

    INK_ERR_READ_FILE,          /* Error occur in reading file */
    INK_ERR_WRITE_FILE,         /* Error occur in writing file */
    INK_ERR_PARSE_CONFIG_RULE,  /* Error in parsing configuration file */
    INK_ERR_INVALID_CONFIG_RULE,        /* Invalid Configuration Rule */

    INK_ERR_NET_ESTABLISH,      /* Problem in establishing a TCP socket */
    INK_ERR_NET_READ,           /* Problem reading from socket */
    INK_ERR_NET_WRITE,          /* Problem writing to socket */
    INK_ERR_NET_EOF,            /* Hit socket EOF */
    INK_ERR_NET_TIMEOUT,        /* Timed out while waiting for socket read */

    INK_ERR_SYS_CALL,           /* Error in basic system call, eg. malloc */
    INK_ERR_PARAMS,             /* Invalid parameters for a fn */

    INK_ERR_FAIL
  } INKError;

/***************************************************************************
 * Constants
 ***************************************************************************/

#define INK_INVALID_HANDLE       NULL
#define INK_INVALID_LIST         INK_INVALID_HANDLE
#define INK_INVALID_CFG_CONTEXT  INK_INVALID_HANDLE
#define INK_INVALID_THREAD       INK_INVALID_HANDLE
#define INK_INVALID_MUTEX        INK_INVALID_HANDLE

#define INK_INVALID_IP_ADDR      NULL
#define INK_INVALID_IP_CIDR      -1
#define INK_INVALID_PORT         0

#define INK_SSPEC_TIME           0x1
#define INK_SSPEC_SRC_IP         0x2
#define INK_SSPEC_PREFIX         0x4
#define INK_SSPEC_SUFFIX         0x8
#define INK_SSPEC_PORT           0x10
#define INK_SSPEC_METHOD         0x20
#define INK_SSPEC_SCHEME         0x40

#define INK_ENCRYPT_PASSWD_LEN   23

/***************************************************************************
 * Types
 ***************************************************************************/

  typedef int64 INKInt;
  typedef int64 INKCounter;
  typedef float INKFloat;
  typedef char *INKString;
  typedef char *INKIpAddr;

  typedef void *INKHandle;
  typedef INKHandle INKList;
  typedef INKHandle INKIpAddrList;      /* contains INKIpAddrEle *'s */
  typedef INKHandle INKPortList;        /* conatins INKPortEle *'s   */
  typedef INKHandle INKDomainList;      /* contains INKDomain *'s    */
  typedef INKHandle INKStringList;      /* contains char* 's         */
  typedef INKHandle INKIntList; /* contains int* 's          */

  typedef INKHandle INKCfgContext;
  typedef INKHandle INKCfgIterState;

/*--- basic control operations --------------------------------------------*/

  typedef enum
  {
    INK_ACTION_SHUTDOWN,        /* change requires user to stop then start the Traffic Server and Manager (restart Traffic Cop) */
    INK_ACTION_RESTART,         /* change requires restart Traffic Server and Traffic Manager */
    INK_ACTION_DYNAMIC,         /* change is already made in function call */
    INK_ACTION_RECONFIGURE,     /* change requires TS to reread configuration files */
    INK_ACTION_UNDEFINED
  } INKActionNeedT;

  typedef enum
  {
    INK_PROXY_ON,
    INK_PROXY_OFF,
    INK_PROXY_UNDEFINED
  } INKProxyStateT;

/* used when starting Traffic Server process */
  typedef enum
  {
    INK_CACHE_CLEAR_ON,         /* run TS in  "clear entire cache" mode */
    INK_CACHE_CLEAR_HOSTDB,     /* run TS in "only clear the host db cache" mode */
    INK_CACHE_CLEAR_OFF         /* starts TS in regualr mode w/o any options */
  } INKCacheClearT;

/*--- diagnostic output operations ----------------------------------------*/

  typedef enum
  {
    INK_DIAG_DIAG,
    INK_DIAG_DEBUG,
    INK_DIAG_STATUS,
    INK_DIAG_NOTE,
    INK_DIAG_WARNING,
    INK_DIAG_ERROR,
    INK_DIAG_FATAL,             /* >= FATAL severity causes process termination */
    INK_DIAG_ALERT,
    INK_DIAG_EMERGENCY,
    INK_DIAG_UNDEFINED
  } INKDiagsT;

/*--- event operations ----------------------------------------------------*/
/*
typedef enum
{
  INK_EVENT_TYPE_PREDEFINED,
  INK_EVENT_TYPE_CONDITIONAL,
  INK_EVENT_TYPE_UNDEFINED
} INKEventTypeT;
*/

  typedef enum
  {
    INK_EVENT_PRIORITY_WARNING,
    INK_EVENT_PRIORITY_ERROR,
    INK_EVENT_PRIORITY_FATAL,
    INK_EVENT_PRIORITY_UNDEFINED
  } INKEventPriorityT;

/*--- abstract file operations --------------------------------------------*/

  typedef enum
  {
    INK_ACCESS_NONE,            /* no access */
    INK_ACCESS_MONITOR,         /* monitor only access */
    INK_ACCESS_MONITOR_VIEW,    /* monitor and view configuration access */
    INK_ACCESS_MONITOR_CHANGE,  /* monitor and change configuration access */
    INK_ACCESS_UNDEFINED
  } INKAccessT;

  typedef enum
  {
    INK_REC_INT,
    INK_REC_COUNTER,
    INK_REC_FLOAT,
    INK_REC_STRING,
    INK_REC_UNDEFINED
  } INKRecordT;

  typedef enum
  {
    INK_IP_SINGLE,              /* single ip address */
    INK_IP_RANGE,               /* range ip address, eg. 1.1.1.1-2.2.2.2 */
    INK_IP_UNDEFINED
  } INKIpAddrT;

  typedef enum
  {
    INK_CON_TCP,                /* TCP connection */
    INK_CON_UDP,                /* UDP connection */
    INK_CON_UNDEFINED
  } INKConnectT;

  typedef enum                  /* primary destination types */
  {
    INK_PD_DOMAIN,              /* domain name */
    INK_PD_HOST,                /* hostname */
    INK_PD_IP,                  /* ip address */
    INK_PD_URL_REGEX,           /* regular expression in url */
    INK_PD_UNDEFINED
  } INKPrimeDestT;

  typedef enum                  /* header information types */
  {
    INK_HDR_DATE,
    INK_HDR_HOST,
    INK_HDR_COOKIE,
    INK_HDR_CLIENT_IP,
    INK_HDR_UNDEFINED
  } INKHdrT;

  typedef enum                  /* indicate if ICP parent cache or ICP sibling cache */
  {
    INK_ICP_PARENT,
    INK_ICP_SIBLING,
    INK_ICP_UNDEFINED
  } INKIcpT;

  typedef enum                  /* access privileges to news articles cached by Traffic Server  */
  {
    INK_IP_ALLOW_ALLOW,
    INK_IP_ALLOW_DENY,
    INK_IP_ALLOW_UNDEFINED
  } INKIpAllowT;

  typedef enum                  /* multicast time to live options */
  {
    INK_MC_TTL_SINGLE_SUBNET,   /* forward multicast datagrams to single subnet */
    INK_MC_TTL_MULT_SUBNET,     /* deliver multicast to more than one subnet */
    INK_MC_TTL_UNDEFINED
  } INKMcTtlT;

  typedef enum                  /* tells Traffic Server to accept or reject records satisfying filter condition */
  {
    INK_LOG_FILT_ACCEPT,
    INK_LOG_FILT_REJECT,
    INK_LOG_FILT_UNDEFINED
  } INKLogFilterActionT;

  typedef enum                  /* possible conditional operators used in filters */
  {
    INK_LOG_COND_MATCH,         /* true if filter's field and value are idential; case-sensitive */
    INK_LOG_COND_CASE_INSENSITIVE_MATCH,
    INK_LOG_COND_CONTAIN,       /* true if field contains the value; case-sensitive */
    INK_LOG_COND_CASE_INSENSITIVE_CONTAIN,
    INK_LOG_COND_UNDEFINED
  } INKLogConditionOpT;

  typedef enum                  /* valid logging modes for LogObject's */
  {
    INK_LOG_MODE_ASCII,
    INK_LOG_MODE_BINARY,
    INK_LOG_ASCII_PIPE,
    INK_LOG_MODE_UNDEFINED
  } INKLogModeT;

  typedef enum                  /* access privileges to news articles cached by Traffic Server  */
  {
    INK_MGMT_ALLOW_ALLOW,
    INK_MGMT_ALLOW_DENY,
    INK_MGMT_ALLOW_UNDEFINED
  } INKMgmtAllowT;

  typedef enum                  /* methods of specifying groups of clients */
  {
    INK_CLIENT_GRP_IP,          /* ip range */
    INK_CLIENT_GRP_DOMAIN,      /* domain */
    INK_CLIENT_GRP_HOSTNAME,    /* hostname */
    INK_CLIENT_GRP_UNDEFINED
  } INKClientGroupT;

  typedef enum
  {
    INK_RR_TRUE,                /* go through parent cache list in round robin */
    INK_RR_STRICT,              /* Traffic Server machines serve requests striclty in turn */
    INK_RR_FALSE,               /* no round robin selection */
    INK_RR_NONE,                /* no round-robin action tag specified */
    INK_RR_UNDEFINED
  } INKRrT;

  typedef enum                  /* a request URL method; used in Secondary Specifiers */
  {
    INK_METHOD_NONE,
    INK_METHOD_GET,
    INK_METHOD_POST,
    INK_METHOD_PUT,
    INK_METHOD_TRACE,
    INK_METHOD_PUSH,            /* only valid with filter.config */
    INK_METHOD_UNDEFINED
  } INKMethodT;

  typedef enum                  /*  possible URL schemes */
  {
    INK_SCHEME_NONE,
    INK_SCHEME_HTTP,
    INK_SCHEME_HTTPS,
    INK_SCHEME_RTSP,
    INK_SCHEME_MMS,
    INK_SCHEME_UNDEFINED
  } INKSchemeT;

  typedef enum                  /* possible schemes to divide partition by */
  {
    INK_PARTITION_HTTP,
    INK_PARTITION_UNDEFINED
  } INKPartitionSchemeT;

  typedef enum                  /* specifies how size is specified */
  {
    INK_SIZE_FMT_PERCENT,       /* as a percentage */
    INK_SIZE_FMT_ABSOLUTE,      /* as an absolute value */
    INK_SIZE_FMT_UNDEFINED
  } INKSizeFormatT;

  typedef enum
  {
    INK_HTTP_CONGEST_PER_IP,
    INK_HTTP_CONGEST_PER_HOST,
    INK_HTTP_CONGEST_UNDEFINED
  } INKCongestionSchemeT;

  typedef enum
  {
    INK_PROTOCOL_DNS,
    INK_PROTOCOL_UNDEFINED
  } INKProtocolT;

  typedef enum
  {
    INK_FNAME_ADMIN_ACCESS,     /* admin_access.config */
    INK_FNAME_CACHE_OBJ,        /* cache.config */
    INK_FNAME_CONGESTION,       /* congestion.config */
    INK_FNAME_HOSTING,          /* hosting.config */
    INK_FNAME_ICP_PEER,         /* icp.config */
    INK_FNAME_IP_ALLOW,         /* ip_allow.config */
    INK_FNAME_LOGS_XML,         /* logs_xml.config */
    INK_FNAME_MGMT_ALLOW,       /* mgmt_allow.config */
    INK_FNAME_PARENT_PROXY,     /* parent.config */
    INK_FNAME_PARTITION,        /* partition.config */
    INK_FNAME_PLUGIN,           /* plugin.config */
    INK_FNAME_REMAP,            /* remap.config */
    INK_FNAME_SOCKS,            /* socks.config */
    INK_FNAME_SPLIT_DNS,        /* splitdns.config */
    INK_FNAME_STORAGE,          /* storage.config */
    INK_FNAME_UPDATE_URL,       /* update.config */
    INK_FNAME_VADDRS,           /* vaddrs.config */
    INK_FNAME_RMSERVER,         /* rmserver.cfg */
    INK_FNAME_VSCAN,            /* vscan.config */
    INK_FNAME_VS_TRUSTED_HOST,  /* trusted-host.config */
    INK_FNAME_VS_EXTENSION,     /* extensions.config */
    INK_FNAME_UNDEFINED
  } INKFileNameT;


/* Each rule type within a file has its own enumeration.
 * Need this enumeration because it's possible there are different Ele's used
 * for rule types within the same file
 */
  typedef enum
  {
    INK_ADMIN_ACCESS,           /* admin_access.config */
    INK_CACHE_NEVER,            /* cache.config */
    INK_CACHE_IGNORE_NO_CACHE,
    INK_CACHE_IGNORE_CLIENT_NO_CACHE,
    INK_CACHE_IGNORE_SERVER_NO_CACHE,
    INK_CACHE_PIN_IN_CACHE,
    INK_CACHE_REVALIDATE,
    INK_CACHE_TTL_IN_CACHE,
    INK_CACHE_AUTH_CONTENT,
    INK_CONGESTION,             /* congestion.config */
    INK_HOSTING,                /* hosting.config */
    INK_ICP,                    /* icp.config */
    INK_IP_ALLOW,               /* ip_allow.config */
    INK_LOG_FILTER,             /* logs_xml.config */
    INK_LOG_OBJECT,
    INK_LOG_FORMAT,
    INK_MGMT_ALLOW,             /* mgmt_allow.config */
    INK_PP_PARENT,              /* parent.config */
    INK_PP_GO_DIRECT,
    INK_PARTITION,              /* partition.config */
    INK_PLUGIN,                 /* plugin.config */
    INK_REMAP_MAP,              /* remap.config */
    INK_REMAP_REVERSE_MAP,
    INK_REMAP_REDIRECT,
    INK_REMAP_REDIRECT_TEMP,
    INK_SOCKS_BYPASS,           /* socks.config */
    INK_SOCKS_AUTH,
    INK_SOCKS_MULTIPLE,
    INK_SPLIT_DNS,              /* splitdns.config */
    INK_STORAGE,                /* storage.config */
    INK_UPDATE_URL,             /* update.config */
    INK_VADDRS,                 /* vaddrs.config */
    INK_TYPE_UNDEFINED,
    INK_TYPE_COMMENT            /* for internal use only */
  } INKRuleTypeT;


/***************************************************************************
 * Structures
 ***************************************************************************/

/*--- general -------------------------------------------------------------*/

  typedef struct
  {
    int d;                      /* days */
    int h;                      /* hours */
    int m;                      /* minutes */
    int s;                      /* seconds */
  } INKHmsTime;

/*--- records -------------------------------------------------------------*/

  typedef struct
  {
    char *rec_name;             /* record name */
    INKRecordT rec_type;        /* record type {INK_REC_INT...} */
    union
    {                           /* record value */
      INKInt int_val;
      INKCounter counter_val;
      INKFloat float_val;
      INKString string_val;
    };
  } INKRecordEle;

/*--- events --------------------------------------------------------------*/

/* Note: Each event has a format String associated with it from which the
 *       description is constructed when an event is signalled. This format
 *       string though can be retrieved from the event-mapping table which
 *       is stored both locally and remotely.
 */

  typedef struct
  {
    /*INKEventTypeT type; *//* Predefined or Conditional event */
    int id;
    char *name;                 /* pre-set, immutable for PREDEFINED events */
    char *description;          /* predefined events have default */
    /*char *condition; *//* pre-set, immutable for PREDEFINED events */
    INKEventPriorityT priority; /* WARNING, ERROR, FATAL */
    /*bool local; */
    /*unsigned long inet_address; *//* for remote peer events */
    /*bool seen; *//* for remote peer events */
  } INKEvent;

/* Will not be used until new Cougar Event Processor */
  typedef struct
  {
    char *name;
    /*int signalCount; *//* 0 is inactive, >= 1 is active event */
    /*unsigned long timestamp; *//* only applies to active events */
  } INKActiveEvent;


/*--- abstract file operations --------------------------------------------*/

  typedef struct
  {
    INKIpAddrT type;            /* single ip or an ip-range */
    INKIpAddr ip_a;             /* first ip */
    int cidr_a;                 /* CIDR value, 0 if not defined */
    int port_a;                 /* port, 0 if not defined */
    INKIpAddr ip_b;             /* second ip (if ip-range) */
    int cidr_b;                 /* CIDR value, 0 if not defined */
    int port_b;                 /* port, 0 if not defined */
  } INKIpAddrEle;

  typedef struct
  {
    int port_a;                 /* first port */
    int port_b;                 /* second port (0 if not a port range) */
  } INKPortEle;

  typedef struct
  {
    char *domain_val;           /* a server name can be specified by name or IP address */
    /* used for www.host.com:8080 or 11.22.33.44:8000 */
    int port;                   /* (optional) */
  } INKDomain;

/* there are a variety of secondary specifiers that can be used in a rule; more than
 * one secondary specifier can be used per rule, but a secondary specifier can only
 * be used once per rule (eg. time, src_ip, prefix, suffix, port, method, scheme)
 */
  typedef struct
  {
    uint32 active;              /* valid field: INK_SSPEC_xxx */
    struct
    {                           /* time range */
      int hour_a;
      int min_a;
      int hour_b;
      int min_b;
    } time;
    INKIpAddr src_ip;           /* client/source ip */
    char *prefix;               /* prefix in path part of URL */
    char *suffix;               /* suffix in the URL */
    INKPortEle *port;           /* requested URL port */
    INKMethodT method;          /* get, post, put, trace */
    INKSchemeT scheme;          /* HTTP */
  } INKSspec;                   /* Sspec = Secondary Specifier */

  typedef struct
  {
    INKPrimeDestT pd_type;      /* primary destination type: INK_PD_xxx */
    char *pd_val;               /* primary destination value; refers to the requested domain name,
                                   host name, ip address, or regular expression to
                                   be found in a URL  */
    INKSspec sec_spec;          /* secondary specifier */
  } INKPdSsFormat;              /* PdSs = Primary Destination Secondary Specifier */


/* Generic Ele struct which is used as first member in all other Ele structs.
 * The INKCfgContext operations deal with INKCfgEle* type, so must typecast
 * all Ele's to an INKCfgEle*
 */
  typedef struct
  {
    INKRuleTypeT type;
    INKError error;
  } INKCfgEle;

/* admin_access.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    char *user;                 /* username */
    char *password;             /* MD5 encrypted */
    INKAccessT access;          /* type of access allowed for user */
  } INKAdminAccessEle;

/* arm_security.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    INKConnectT type_con;       /* determines if ports will be opened for TCP or UDP */
    INKIpAddrEle *src_ip_addr;  /* arm-deny rule: the ip address or range of ip addresses that
                                   will be denied access to ports (can be NULL);
                                   arm-allow rule: the ip address or range of ip addressess
                                   specifying the source of communication (can be NULL) */
    INKIpAddrEle *dest_ip_addr; /* destination ip address (can be NULL) */
    INKPortEle *open_ports;     /* open source ports (can be INVALID) */
    INKPortEle *src_ports;      /* source ports (can be INVALID) */
    /* arm-open rule: list of ports/port-ranges to open by default */
    INKPortEle *dest_ports;     /* the destination port(s) that TCP traffic will be
                                   allowed/denied access to (can be INVALID)  */
    INKIntList src_port_list;   /* alternative for src_ports */
    INKIntList dest_port_list;  /* alternative for dest_ports */
  } INKArmSecurityEle;          /* + at least of src_ip_addr, dest_ip_addr, dest_ports,
                                   src_ports is specified for arm allow rules;
                                   + at least of dest_ports or src_ip_addr is specified for arm
                                   deny rules */

/* bypass.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    INKIpAddrList src_ip_addr;  /* source ip address (single or range) */
    INKIpAddrList dest_ip_addr; /* destination ip address */
  } INKBypassEle;

/* cache.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    INKPdSsFormat cache_info;   /* general PdSs information */
    INKHmsTime time_period;     /* only valid if cache_act == INK_CACHE_PIN_IN_CACHE */
  } INKCacheEle;

/* congestion.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    INKPrimeDestT pd_type;
    char *pd_val;
    char *prefix;               /* optional */
    int port;                   /* optional */
    INKCongestionSchemeT scheme;        /* per_ip or per_host */
    int max_connection_failures;
    int fail_window;
    int proxy_retry_interval;
    int client_wait_interval;
    int wait_interval_alpha;
    int live_os_conn_timeout;
    int live_os_conn_retries;
    int dead_os_conn_timeout;
    int dead_os_conn_retries;
    int max_connection;
    char *error_page_uri;
  } INKCongestionEle;

/* hosting.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    INKPrimeDestT pd_type;
    char *pd_val;               /* domain or hostname  */
    INKIntList partitions;      /* must be a list of ints */
  } INKHostingEle;

/* icp.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    char *peer_hostname;        /* hostname of icp peer; ("localhost" name reserved for Traffic Server) */
    INKIpAddr peer_host_ip_addr;        /* ip address of icp peer (not required if peer_hostname) */
    INKIcpT peer_type;          /* 1: icp parent, 2: icp sibling */
    int peer_proxy_port;        /* port number of the TCP port used by the ICP peer for proxy communication */
    int peer_icp_port;          /* port number of the UDP port used by the ICP peer for ICP communication  */
    bool is_multicast;          /* false: multicast not enabled; true: multicast enabled */
    INKIpAddr mc_ip_addr;       /* multicast ip (can be 0 if is_multicast == false */
    INKMcTtlT mc_ttl;           /* multicast time to live; either IP multicast datagrams will not
                                   be forwarded beyond a single subnetwork, or allow delivery
                                   of IP multicast datagrams to more than one subnet
                                   (can be UNDEFINED if is_multicast == false */
  } INKIcpEle;

/* ip_allow.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    INKIpAddrEle *src_ip_addr;  /* source ip address (single or range) */
    INKIpAllowT action;
  } INKIpAllowEle;

/* ipnat.conf */
  typedef struct
  {
    INKCfgEle cfg_ele;
    char *intr;                 /* ethernet interface name that user traffic will enter through */
    INKIpAddr src_ip_addr;      /* the ip address user traffic is heading for */
    int src_cidr;               /* the cidr of the source IP (optional) */
    int src_port;               /* the port user traffic is heading to */
    INKIpAddr dest_ip_addr;     /* the ip address to redirect traffic to */
    int dest_port;              /* the port to redirect traffic to */
    INKConnectT type_con;       /* udp or tcp */
    INKProtocolT protocol;      /* (optional) user protocol, eg. dns */
  } INKIpFilterEle;

/* logs_xml.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    INKLogFilterActionT action; /* accept or reject records satisfying filter condition */
    char *filter_name;
    char *log_field;            /* possible choices listed on p.250 */
    INKLogConditionOpT compare_op;
    char *compare_str;          /* the comparison value can be any string or integer */
    int compare_int;            /* if int, then all the INKLogConditionOpT operations mean "equal" */
  } INKLogFilterEle;

  typedef struct
  {
    INKCfgEle cfg_ele;
    char *name;                 /* must be unique; can't be a pre-defined format */
    char *format;
    int aggregate_interval_secs;        /* (optional) use if format contains aggregate ops */
  } INKLogFormatEle;

  typedef struct
  {
    INKCfgEle cfg_ele;
    char *format_name;
    char *file_name;
    INKLogModeT log_mode;
    INKDomainList collation_hosts;      /* list of hosts (by name or IP addr) */
    INKStringList filters;      /* list of filter names that already exist */
    INKStringList protocols;    /* list of protocols, eg. http, nttp, icp */
    INKStringList server_hosts; /* list of host names */
  } INKLogObjectEle;

/* mgmt_allow.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    INKIpAddrEle *src_ip_addr;  /* source ip address (single or range) */
    INKMgmtAllowT action;
  } INKMgmtAllowEle;

/* parent.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    INKPdSsFormat parent_info;  /* general PdSs information */
    INKRrT rr;                  /*  possible values are INK_RRT_TRUE (go through proxy
                                   parent list in round robin),INK_RRT_STRICT (server
                                   requests striclty in turn), or INK_RRT_FALSE (no
                                   round robin selection) */
    INKDomainList proxy_list;   /* ordered list of parent proxies */
    bool direct;                /* indicate if go directly to origin server, default = false and does
                                   not bypass parent heirarchies */
  } INKParentProxyEle;          /* exactly one of rr or parent_proxy_act must be defined */

/* partition.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    int partition_num;          /* must be in range 1 - 255 */
    INKPartitionSchemeT scheme; /* http, mixt */
    int partition_size;         /* >= 128 MB, multiple of 128 */
    INKSizeFormatT size_format; /* percentage or absolute */
  } INKPartitionEle;

/* plugin.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    char *name;                 /* name of plugin */
    INKStringList args;         /* list of arguments */
  } INKPluginEle;

/* remap.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    bool map;                   /* if true: map, if false: remap */
    INKSchemeT from_scheme;     /* http, https, <scheme>://<host>:<port>/<path_prefix> */
    char *from_host;            /* from host */
    int from_port;              /* from port (can be 0) */
    char *from_path_prefix;     /* from path_prefix (can be NULL) */
    INKSchemeT to_scheme;
    char *to_host;              /* to host */
    int to_port;                /* to port (can be 0) */
    char *to_path_prefix;       /* to path_prefix (can be NULL) */
  } INKRemapEle;

/* socks.config */
/* INKqa10915: supports two rules types - the first rule type specifies the
   IP addresses of origin servers that TS should bypass SOCKS and access
   directly (this is when ip_addrs is used); the second rule
   type specifies which SOCKS servers to use for the addresses specified
   in dest_ip_addr; so this means that either ip_addrs is specified OR
   dest_ip_addr/socks_servers/rr are */
  typedef struct
  {
    INKCfgEle cfg_ele;
    INKIpAddrList ip_addrs;     /* list of ip addresses to bypass SOCKS server (INK_SOCKS_BYPASS) */
    INKIpAddrEle *dest_ip_addr; /* ip address(es) that will use the socks server
                                   specified in parent_list (INK_SOCKS_MULTIPLE rule) */
    INKDomainList socks_servers;        /* ordered list of SOCKS servers (INK_SOCKS_MULTIPLE rule) */
    INKRrT rr;                  /* possible values are INK_RRT_TRUE (go through proxy
                                   parent list in round robin),INK_RRT_STRICT (server
                                   requests striclty in turn), or INK_RRT_FALSE (no
                                   round robin selection) (INK_SOCKS_MULTIPLE rule) */
    char *username;             /* used for INK_SOCKS_AUTH rule */
    char *password;             /* used for INK_SOCKS_AUTH rule */
  } INKSocksEle;

/* splitdns.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    INKPrimeDestT pd_type;      /* INK_PD_DOMAIN, INK_PD_HOST, INK_PD_URL_REGEX only */
    char *pd_val;               /* primary destination value */
    INKDomainList dns_servers_addrs;    /* list of dns servers */
    char *def_domain;           /* (optional) default domain name (can be NULL) */
    INKDomainList search_list;  /* (optinal) domain search list (can be INVALID) */
  } INKSplitDnsEle;

/* storage.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    char *pathname;             /* the name of a partition, directory, or file */
    int size;                   /* size of the named pathname (in bytes); optional if raw partitions */
  } INKStorageEle;

/* update.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    char *url;                  /* url to update (HTTP based URLs) */
    INKStringList headers;      /* list of headers, separated by semicolons (can be NULL) */
    int offset_hour;            /* offset hour to start update; must be 00-23 hrs  */
    int interval;               /* in secs, frequency of updates starting at offset_hour */
    int recursion_depth;        /* starting at given URL, the depth to which referenced URLs are recursively updated */
  } INKUpdateEle;

/* vaddrs.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    INKIpAddr ip_addr;          /* virtual ip address */
    char *intr;                 /* network interface name (hme0) */
    int sub_intr;               /* the sub-interface number; must be between 1 and 255 */
  } INKVirtIpAddrEle;

/* rmserver.cfg */
  typedef struct
  {
    INKCfgEle cfg_ele;
    char *Vname;
    char *str_val;
    int int_val;
  } INKRmServerEle;

/* vscan.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    char *attr_name;            /* the attribute name */
    char *attr_val;             /* the attribute value */
  } INKVscanEle;

/* trust-host.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    char *hostname;             /* the trusted-host name */
  } INKVsTrustedHostEle;

/* extensions.config */
  typedef struct
  {
    INKCfgEle cfg_ele;
    char *file_ext;             /* the file extension */
  } INKVsExtensionEle;

/***************************************************************************
 * Function Types
 ***************************************************************************/

  typedef void (*INKEventSignalFunc) (char *name, char *msg, int pri, void *data);
  typedef void (*INKDisconnectFunc) (void *data);

/***************************************************************************
 * API Memory Management
 ***************************************************************************/
#define INKmalloc(s)      _INKmalloc ((s), INK_RES_MEM_PATH)
#define INKrealloc(p,s)   _INKrealloc ((p), (s), INK_RES_MEM_PATH)
#define INKstrdup(p)      _INKstrdup ((p), -1, INK_RES_MEM_PATH)
#define INKstrndup(p,n)   _INKstrdup ((p), (n), INK_RES_MEM_PATH)
#define INKfree(p)        _INKfree (p)

  inkapi void *_INKmalloc(unsigned int size, const char *path);
  inkapi void *_INKrealloc(void *ptr, unsigned int size, const char *path);
  inkapi char *_INKstrdup(const char *str, int length, const char *path);
  inkapi void _INKfree(void *ptr);

/***************************************************************************
 * API Helper Functions for Data Carrier Structures
 ***************************************************************************/

/*--- INKList operations --------------------------------------------------*/
  inkapi INKList INKListCreate();
  inkapi void INKListDestroy(INKList l);        /* list must be empty */
  inkapi INKError INKListEnqueue(INKList l, void *data);
  inkapi void *INKListDequeue(INKList l);
  inkapi bool INKListIsEmpty(INKList l);
  inkapi int INKListLen(INKList l);     /* returns -1 if list is invalid */
  inkapi bool INKListIsValid(INKList l);

/*--- INKIpAddrList operations --------------------------------------------*/
  inkapi INKIpAddrList INKIpAddrListCreate();
  inkapi void INKIpAddrListDestroy(INKIpAddrList ip_addrl);
  inkapi INKError INKIpAddrListEnqueue(INKIpAddrList ip_addrl, INKIpAddrEle * ip_addr);
  inkapi INKIpAddrEle *INKIpAddrListDequeue(INKIpAddrList ip_addrl);
  inkapi int INKIpAddrListLen(INKIpAddrList ip_addrl);
  inkapi bool INKIpAddrListIsEmpty(INKIpAddrList ip_addrl);
  inkapi int INKIpAddrListLen(INKIpAddrList ip_addrl);
  inkapi bool INKIpAddrListIsValid(INKIpAddrList ip_addrl);

/*--- INKPortList operations ----------------------------------------------*/
  inkapi INKPortList INKPortListCreate();
  inkapi void INKPortListDestroy(INKPortList portl);
  inkapi INKError INKPortListEnqueue(INKPortList portl, INKPortEle * port);
  inkapi INKPortEle *INKPortListDequeue(INKPortList portl);
  inkapi bool INKPortListIsEmpty(INKPortList portl);
  inkapi int INKPortListLen(INKPortList portl);
  inkapi bool INKPortListIsValid(INKPortList portl);

/*--- INKStringList operations --------------------------------------------*/
  inkapi INKStringList INKStringListCreate();
  inkapi void INKStringListDestroy(INKStringList strl);
  inkapi INKError INKStringListEnqueue(INKStringList strl, char *str);
  inkapi char *INKStringListDequeue(INKStringList strl);
  inkapi bool INKStringListIsEmpty(INKStringList strl);
  inkapi int INKStringListLen(INKStringList strl);
  inkapi bool INKStringListIsValid(INKStringList strl);

/*--- INKIntList operations --------------------------------------------*/
  inkapi INKIntList INKIntListCreate();
  inkapi void INKIntListDestroy(INKIntList intl);
  inkapi INKError INKIntListEnqueue(INKIntList intl, int *str);
  inkapi int *INKIntListDequeue(INKIntList intl);
  inkapi bool INKIntListIsEmpty(INKIntList intl);
  inkapi int INKIntListLen(INKIntList intl);
  inkapi bool INKIntListIsValid(INKIntList intl, int min, int max);

/*--- INKDomainList operations --------------------------------------------*/
  inkapi INKDomainList INKDomainListCreate();
  inkapi void INKDomainListDestroy(INKDomainList domainl);
  inkapi INKError INKDomainListEnqueue(INKDomainList domainl, INKDomain * domain);
  inkapi INKDomain *INKDomainListDequeue(INKDomainList domainl);
  inkapi bool INKDomainListIsEmpty(INKDomainList domainl);
  inkapi int INKDomainListLen(INKDomainList domainl);
  inkapi bool INKDomainListIsValid(INKDomainList domainl);

/*--- allocate/deallocate operations -------------------------------------*/
/* NOTE:
 * 1) Default values for INKxxEleCreate functions:
 *    - for all lists, default value is INK_INVALID_LIST. NO memory is
 *      allocated for an Ele's  list type member. The user must
 *      explicity call the INKxxListCreate() function to initialize it.
 *    - for char*'s and INKIpAddr the default is NULL (or INK_INVALID_IP_ADDR
 *      for INKIpAddr's); user must assign allocated memory to initialize any
 *      string or INKIpAddr members of an INKxxxEle
 *
 * 2) An Ele corresponds to a rule type in a file; this is why each Ele has an
 * INKRuleType to identify which type of rule it corresponds to.
 * For config files which only have one rule type, we can easily set the
 * rule type of the Ele in the EleCreate function since there's only one possible
 * option. However, note that for those config files with more than one rule
 * type, we cannot set the rule type in the EleCreate function since
 * we don't know which rule type the Ele corresponds to yet. Thus, the user must
 * specify the INKRuleTypeT when he/she creates the Ele.
 */

  inkapi INKEvent *INKEventCreate();
  inkapi void INKEventDestroy(INKEvent * event);
  inkapi INKRecordEle *INKRecordEleCreate();
  inkapi void INKRecordEleDestroy(INKRecordEle * ele);
  inkapi INKIpAddrEle *INKIpAddrEleCreate();
  inkapi void INKIpAddrEleDestroy(INKIpAddrEle * ele);
  inkapi INKPortEle *INKPortEleCreate();
  inkapi void INKPortEleDestroy(INKPortEle * ele);
  inkapi INKDomain *INKDomainCreate();
  inkapi void INKDomainDestroy(INKDomain * ele);
  inkapi INKSspec *INKSspecCreate();
  inkapi void INKSspecDestroy(INKSspec * ele);
  inkapi INKPdSsFormat *INKPdSsFormatCreate();
  inkapi void INKPdSsFormatDestroy(INKPdSsFormat * ele);

  inkapi INKAdminAccessEle *INKAdminAccessEleCreate();
  inkapi void INKAdminAccessEleDestroy(INKAdminAccessEle * ele);
  inkapi INKArmSecurityEle *INKArmSecurityEleCreate(INKRuleTypeT type);
  inkapi void INKArmSecurityEleDestroy(INKArmSecurityEle * ele);
  inkapi INKBypassEle *INKBypassEleCreate(INKRuleTypeT type);
  inkapi void INKBypassEleDestroy(INKBypassEle * ele);
  inkapi INKCacheEle *INKCacheEleCreate(INKRuleTypeT type);
  inkapi void INKCacheEleDestroy(INKCacheEle * ele);
  inkapi INKCongestionEle *INKCongestionEleCreate();
  inkapi void INKCongestionEleDestroy(INKCongestionEle * ele);
  inkapi INKHostingEle *INKHostingEleCreate();
  inkapi void INKHostingEleDestroy(INKHostingEle * ele);
  inkapi INKIcpEle *INKIcpEleCreate();
  inkapi void INKIcpEleDestroy(INKIcpEle * ele);
  inkapi INKIpFilterEle *INKIpFilterEleCreate();
  inkapi void INKIpFilterEleDestroy(INKIpFilterEle * ele);
  inkapi INKIpAllowEle *INKIpAllowEleCreate();
  inkapi void INKIpAllowEleDestroy(INKIpAllowEle * ele);
  inkapi INKLogFilterEle *INKLogFilterEleCreate();
  inkapi void INKLogFilterEleDestroy(INKLogFilterEle * ele);
  inkapi INKLogFormatEle *INKLogFormatEleCreate();
  inkapi void INKLogFormatEleDestroy(INKLogFormatEle * ele);
  inkapi INKLogObjectEle *INKLogObjectEleCreate();
  inkapi void INKLogObjectEleDestroy(INKLogObjectEle * ele);
  inkapi INKMgmtAllowEle *INKMgmtAllowEleCreate();
  inkapi void INKMgmtAllowEleDestroy(INKMgmtAllowEle * ele);
  inkapi INKParentProxyEle *INKParentProxyEleCreate(INKRuleTypeT type);
  inkapi void INKParentProxyEleDestroy(INKParentProxyEle * ele);
  inkapi INKPartitionEle *INKPartitionEleCreate();
  inkapi void INKPartitionEleDestroy(INKPartitionEle * ele);
  inkapi INKPluginEle *INKPluginEleCreate();
  inkapi void INKPluginEleDestroy(INKPluginEle * ele);
  inkapi INKRemapEle *INKRemapEleCreate(INKRuleTypeT type);
  inkapi void INKRemapEleDestroy(INKRemapEle * ele);
  inkapi INKSocksEle *INKSocksEleCreate(INKRuleTypeT type);
  inkapi void INKSocksEleDestroy(INKSocksEle * ele);
  inkapi INKSplitDnsEle *INKSplitDnsEleCreate();
  inkapi void INKSplitDnsEleDestroy(INKSplitDnsEle * ele);
  inkapi INKStorageEle *INKStorageEleCreate();
  inkapi void INKStorageEleDestroy(INKStorageEle * ele);
  inkapi INKUpdateEle *INKUpdateEleCreate();
  inkapi void INKUpdateEleDestroy(INKUpdateEle * ele);
  inkapi INKVirtIpAddrEle *INKVirtIpAddrEleCreate();
  inkapi void INKVirtIpAddrEleDestroy(INKVirtIpAddrEle * ele);
/*--- Ele helper operations -------------------------------------*/

/* INKIsValid: checks if the fields in the ele are all valid
 * Input:  ele - the ele to check (typecast any of the INKxxxEle's to an INKCfgEle)
 * Output: true if ele has valid fields for its rule type, false otherwise
 */
  bool INKIsValid(INKCfgEle * ele);

/***************************************************************************
 * API Core
 ***************************************************************************/

/*--- api initialization and shutdown -------------------------------------*/
/* INKInit: initializations required for API clients
 * Input: socket_path - not applicable for local clients
 *                      for remote users, the path to the config directory
 *         (eg. run from bin, socket_path = "../etc/trafficserver")
 * Output: INK_ERR_xx
 * Note: If remote client successfully connects, returns INK_ERR_OKAY; but
 *       even if not successful connection (eg. client program is started
 *       before TM) then can still make API calls and will try connecting then
 */
  inkapi INKError INKInit(const char *socket_path);

/* INKTerminate: does clean up for API clients
 * Input: <none>
 * Output: <none>
 */
  inkapi INKError INKTerminate();

/*--- plugin initialization -----------------------------------------------*/
/* INKPluginInit: called by traffic_manager to initialize the plugin
 * Input:  argc - argument count
 *         argv - argument array
 * Output: <none>
 * Note: To implement a program as a plugin, need to implement the INKPluginInit
 *       function and then add the plugin's name (eg. test-plugin.so) and argument
 *       list (if any) to the list in the plugin_mgmt.config file. The location of the
 *       mgmt plugins should be specified in the records.config variable
 *       "proxy.config.plugin.plugin_mgmt_dir" (if this directory is a relative
 *       pathname then, it is assumed that it is relative to the root directory
 *       defined in TS_ROOT). The default value is "etc/trafficserver/plugins_mgmt",
 *       which tells Traffic Manager to use the directory plugins_mgmt located in the
 *       same directory as records.config. You should place your shared library (*.so)
 *       into the directory you have specified.
 */
  inkexp extern void INKPluginInit(int argc, const char *argv[]);

/*--- network operations --------------------------------------------------*/
/* UNIMPLEMENTED: used for remote clients on a different machine */
  inkapi INKError INKConnect(INKIpAddr ip_addr, int port);
  inkapi INKError INKDisconnectCbRegister(INKDisconnectFunc * func, void *data);
  inkapi INKError INKDisconnectRetrySet(int retries, int retry_sleep_msec);
  inkapi INKError INKDisconnect();


/*--- control operations --------------------------------------------------*/
/* INKProxyStateGet: get the proxy state (on/off)
 * Input:  <none>
 * Output: proxy state (on/off)
 */
  inkapi INKProxyStateT INKProxyStateGet();

/* INKProxyStateSet: set the proxy state (on/off)
 * Input:  proxy_state - set to on/off
 *         clear - specifies if want to start TS with clear_cache or
 *                 clear_cache_hostdb option, or just run TS with no options;
 *                  only applies when turning proxy on
 * Output: INKError
 */
  inkapi INKError INKProxyStateSet(INKProxyStateT proxy_state, INKCacheClearT clear);

/* INKReconfigure: tell traffic_server to re-read its configuration files
 * Input:  <none>
 * Output: INKError
 */
  inkapi INKError INKReconfigure();

/* INKRestart: restarts Traffic Manager and Traffic Server
 * Input:  cluster - local or cluster-wide
 * Output: INKError
 */
  inkapi INKError INKRestart(bool cluster);

/* INKHardRestart: stops and then starts Traffic Server
 * Input:  <none>
 * Output: INKError
 * Note: only for remote API clients
 */
  inkapi INKError INKHardRestart();

/* INKActionDo: based on INKActionNeedT, will take appropriate action
 * Input: action - action that needs to be taken
 * Output: INKError
 */
  inkapi INKError INKActionDo(INKActionNeedT action);

/*--- diags output operations ---------------------------------------------*/
/* INKDiags: enables users to manipulate run-time diagnostics, and print
 *           user-formatted notices, warnings and errors
 * Input:  mode - diags mode
 *         fmt  - printf style format
 * Output: <none>
 */
  inkapi void INKDiags(INKDiagsT mode, const char *fmt, ...);

/* INKGetErrorMessage: convert error id to error message
 * Input:  error id (defined in INKError)
 * Output: corresponding error message (allocated memory)
 */
  char *INKGetErrorMessage(INKError error_id);

/*--- password operations -------------------------------------------------*/
/* INKEncryptPassword: encrypts a password
 * Input: passwd - a password string to encrypt (can be NULL)
 * Output: e_passwd - an encrypted passwd (xmalloc's memory)
 */
  inkapi INKError INKEncryptPassword(char *passwd, char **e_passwd);

/* INKEncryptToFile: Given the plain text password, this function will
 *                   encrypt the password and stores it to the specified file
 * Input: passwd - the plain text password
 *        filepath - the file location to store the encyrpted password
 * Output: INKError
 * Note: Uses certificate in ACL module for encryption.
 */
  inkapi INKError INKEncryptToFile(const char *passwd, const char *filepath);

/*--- direct file operations ----------------------------------------------*/
/* INKConfigFileRead: reads a config file into a buffer
 * Input:  file - the config file to read
 *         text - a buffer is allocated on the text char* pointer
 *         size - the size of the buffer is returned
 * Output: INKError
 */
  inkapi INKError INKConfigFileRead(INKFileNameT file, char **text, int *size, int *version);

/* INKConfigFileWrite: writes a config file into a buffer
 * Input:  file - the config file to write
 *         text - text buffer to write
 *         size - the size of the buffer to write
 *         version - the current version level; new file will have the
 *                  version number above this one  (if version < 0, then
 *                  just uses the next version number in the sequence)
 * Output: INKError
 */
  inkapi INKError INKConfigFileWrite(INKFileNameT file, char *text, int size, int version);

/* INKReadFromUrl: reads a remotely located config file into a buffer
 * Input:  url        - remote location of the file
 *         header     - a buffer is allocated on the header char* pointer
 *         headerSize - the size of the header buffer is returned
 *         body       - a buffer is allocated on the body char* pointer
 *         bodySize   - the size of the body buffer is returned
 * Output: INKError   - INK_ERR_OKAY if succeed, INK_ERR_FAIL otherwise
 * Obsolete:  inkapi INKError INKReadFromUrl (char *url, char **text, int *size);
 * NOTE: The URL can be expressed in the following forms:
 *       - http://www.inktomi.com:80/products/network/index.html
 *       - http://www.inktomi.com/products/network/index.html
 *       - http://www.inktomi.com/products/network/
 *       - http://www.inktomi.com/
 *       - http://www.inktomi.com
 *       - www.inktomi.com
 * NOTE: header and headerSize can be NULL
 */
  inkapi INKError INKReadFromUrl(char *url, char **header, int *headerSize, char **body, int *bodySize);

/* INKReadFromUrl: reads a remotely located config file into a buffer
 * Input:  url        - remote location of the file
 *         header     - a buffer is allocated on the header char* pointer
 *         headerSize - the size of the header buffer is returned
 *         body       - a buffer is allocated on the body char* pointer
 *         bodySize   - the size of the body buffer is returned
 *         timeout    - the max. connection timeout value before aborting.
 * Output: INKError   - INK_ERR_OKAY if succeed, INK_ERR_FAIL otherwise
 * NOTE: The URL can be expressed in the following forms:
 *       - http://www.inktomi.com:80/products/network/index.html
 *       - http://www.inktomi.com/products/network/index.html
 *       - http://www.inktomi.com/products/network/
 *       - http://www.inktomi.com/
 *       - http://www.inktomi.com
 *       - www.inktomi.com
 * NOTE: header and headerSize can be NULL
 */
  inkapi INKError INKReadFromUrlEx(const char *url, char **header, int *headerSize, char **body, int *bodySize, int timeout);

/*--- snapshot operations -------------------------------------------------*/
/* INKSnapshotTake: takes snapshot of configuration at that instant in time
 * Input:  snapshot_name - name to call new snapshot
 * Output: INKError
 */
  inkapi INKError INKSnapshotTake(char *snapshot_name);

/* INKSnapshotRestore: restores configuration to when the snapshot was taken
 * Input:  snapshot_name - name of snapshot to restore
 * Output: INKError
 */
  inkapi INKError INKSnapshotRestore(char *snapshot_name);

/* INKSnapshotRemove: removes the snapshot
 * Input:  snapshot_name - name of snapshot to remove
 * Output: INKError
 */
  inkapi INKError INKSnapshotRemove(char *snapshot_name);

/* INKSnapshotsGet: restores configuration to when the snapshot was taken
 * Input:  snapshots - the list which will store all snapshot names currently taken
 * Output: INKError
 */
  inkapi INKError INKSnapshotGetMlt(INKStringList snapshots);


/*--- ftp operations ------------------------------------------------------*/

/* INKMgmtFtpGet: retrieves a file from the specified ftp server
 * Input:
 * Output: INKError
 */
  inkapi INKError INKMgmtFtp(const char *ftpCmd, const char *ftp_server_name, const char *ftp_login, const char *ftp_password, const char *local,
                             const char *remote, char *output);
/*--- statistics operations -----------------------------------------------*/
/* INKStatsReset: sets all the statistics variables to their default values
 * Input: <none>
 * Outpue: INKErrr
 */
  inkapi INKError INKStatsReset();

/*--- variable operations -------------------------------------------------*/
/* INKRecordGet: gets a record
 * Input:  rec_name - the name of the record (proxy.config.record_name)
 *         rec_val  - allocated INKRecordEle structure, value stored inside
 * Output: INKError (if the rec_name does not exist, returns INK_ERR_FAIL)
 */
  inkapi INKError INKRecordGet(char *rec_name, INKRecordEle * rec_val);

/* INKRecordGet*: gets a record w/ a known type
 * Input:  rec_name - the name of the record (proxy.config.record_name)
 *         *_val    - allocated INKRecordEle structure, value stored inside
 * Output: INKError
 * Note: For INKRecordGetString, the function will allocate memory for the
 *       *string_val, so the caller must free (*string_val);
 */
  inkapi INKError INKRecordGetInt(const char *rec_name, INKInt * int_val);
  inkapi INKError INKRecordGetCounter(const char *rec_name, INKCounter * counter_val);
  inkapi INKError INKRecordGetFloat(const char *rec_name, INKFloat * float_val);
  inkapi INKError INKRecordGetString(const char *rec_name, INKString * string_val);

/* INKRecordGetMlt: gets a set of records
 * Input:  rec_list - list of record names the user wants to retrieve;
 *                    resulting gets will be stored in the same list;
 *                    if one get fails, transaction will be aborted
 * Output: INKError
 */
  inkapi INKError INKRecordGetMlt(INKStringList rec_names, INKList rec_vals);

/* INKRecordSet*: sets a record w/ a known type
 * Input:  rec_name     - the name of the record (proxy.config.record_name)
 *         *_val        - the value to set the record to
 *         *action_need - indicates which operation required by user for changes to take effect
 * Output: INKError
 */

  inkapi INKError INKRecordSet(const char *rec_name, const char *val, INKActionNeedT * action_need);
  inkapi INKError INKRecordSetInt(const char *rec_name, INKInt int_val, INKActionNeedT * action_need);
  inkapi INKError INKRecordSetCounter(const char *rec_name, INKCounter counter_val, INKActionNeedT * action_need);
  inkapi INKError INKRecordSetFloat(const char *rec_name, INKFloat float_val, INKActionNeedT * action_need);
  inkapi INKError INKRecordSetString(const char *rec_name, const char *string_val, INKActionNeedT * action_need);

/* INKRecordSetMlt: sets a set of records
 * Input:  rec_list     - list of record names the user wants to set;
 *                        if one set fails, transaction will be aborted
 *         *action_need - indicates which operation required by user for changes to take effect
 * Output: INKError
 */
  inkapi INKError INKRecordSetMlt(INKList rec_list, INKActionNeedT * action_need);

/*--- events --------------------------------------------------------------*/
/* Only a set of statically defined events exist. An event is either
 * active or inactive. An event is active when it is triggered, and
 * becomes inactive when resolved. Events are triggered and resolved
 * by specifying the event's name (which is predefined and immutable).
 */

/* UNIMPLEMENTED - wait for new alarm processor */
/* INKEventSignal: enables the user to trigger an event
 * Input:  event_name - "MGMT_ALARM_ADD_ALARM"
 *         ...        - variable argument list of parameters that go
 *                       go into event description when it is signalled
 * Output: INKError
 */
/*inkapi INKError               INKEventSignal (char *event_name, ...); */


/* INKEventResolve: enables the user to resolve an event
 * Input:  event_name - event to resolve
 * Output: INKError
 */
  inkapi INKError INKEventResolve(char *event_name);

/* INKActiveEventGetMlt: query for a list of all the currently active events
 * Input:  active_events - an empty INKList; if function call is successful,
 *                         active_events will contain names of the currently
 *                         active events
 * Output: INKError
 */
  inkapi INKError INKActiveEventGetMlt(INKList active_events);

/* INKEventIsActive: check if the specified event is active
 * Input:  event_name - name of event to check if active; must be one of
 *                      the predefined names
 *         is_current - when function completes, if true, then the event is
 *                      active
 * Output: INKError
 */
  inkapi INKError INKEventIsActive(char *event_name, bool * is_current);

/* INKEventSignalCbRegister: register a callback for a specific event or
 *                           for any event
 * Input:  event_name - the name of event to register callback for;
 *                      if NULL, the callback is registered for all events
 *         func       - callback function
 *         data       - data to pass to callback
 * Output: INKError
 */
  inkapi INKError INKEventSignalCbRegister(char *event_name, INKEventSignalFunc func, void *data);

/* INKEventSignalCbUnregister: unregister a callback for a specific event
 *                             or for any event
 * Input: event_name - the name of event to unregister callback for;
 *                     if NULL, the callback is unregistered for all events
 *         func       - callback function
 * Output: INKError
 */
  inkapi INKError INKEventSignalCbUnregister(char *event_name, INKEventSignalFunc func);


/*--- abstracted file operations ------------------------------------------*/
/* INKCfgContextCreate: allocates memory for an empty INKCfgContext for the specified file
 * Input:  file - the file
 * Output: INKCfgContext
 * Note: This function does not read the current rules in the file into
 * the INKCfgContext (must call INKCfgContextGet to do this). If you
 * do not call INKCfgContextGet before calling INKCfgContextCommit, then
 * you will overwite all the old rules in the config file!
 */
  inkapi INKCfgContext INKCfgContextCreate(INKFileNameT file);

/* INKCfgContextDestroy: deallocates all memory for the INKCfgContext
 * Input:  ctx - the INKCfgContext to destroy
 * Output: INKError
 */
  inkapi INKError INKCfgContextDestroy(INKCfgContext ctx);

/* INKCfgContextCommit: write new file copy based on ele's listed in ctx
 * Input:  ctx - where all the file's eles are stored
 *         *action_need - indicates which operation required by user for changes to take effect
 * Output: INKError
 * Note: If you do not call INKCfgContextGet before calling INKCfgContextCommit, then
 * you could possibly overwrite all the old rules in the config file!!
 */
  inkapi INKError INKCfgContextCommit(INKCfgContext ctx, INKActionNeedT * action_need, INKIntList errRules);


/* INKCfgContextGet: retrieves all the Ele's for the file specified in the ctx and
 *                puts them into ctx; note that the ele's in the INKCfgContext don't
 *                all have to be of the same ele type
 * Input: ctx - where all the most currfile's eles are stored
 * Output: INKError
 *
 */
  inkapi INKError INKCfgContextGet(INKCfgContext ctx);


/*--- INKCfgContext Operations --------------------------------------------*/
/*
 * These operations are used to manipulate the opaque INKCfgContext type,
 * eg. when want to modify a file
 */

/* INKCfgContextGetCount: returns number of Ele's in the INKCfgContext
 * Input:  ctx - the INKCfgContext to count the number of ele's in
 * Output: the number of Ele's
 */
  int INKCfgContextGetCount(INKCfgContext ctx);

/* INKCfgContextGetEleAt: retrieves the Ele at the specified index; user must
 *                        typecast the INKCfgEle to appropriate INKEle before using
 * Input:  ctx   - the INKCfgContext to retrieve the ele from
 *         index - the Ele position desired; first Ele located at index 0
 * Output: the Ele (typecasted as an INKCfgEle)
 */
  INKCfgEle *INKCfgContextGetEleAt(INKCfgContext ctx, int index);

/* INKCfgContextGetFirst: retrieves the first Ele in the INKCfgContext
 * Input:  ctx   - the INKCfgContext
 *         state - the current position in the Ele that the iterator is at
 * Output: returns first Ele in the ctx (typecasted as an INKCfgEle)
 */
  INKCfgEle *INKCfgContextGetFirst(INKCfgContext ctx, INKCfgIterState * state);

/* INKCfgContextGetNext: retrieves the next ele in the ctx that's located after
 *                       the one pointed to by the INKCfgIterState
 * Input:  ctx   - the INKCfgContext
 *         state - the current position in the Ele that the iterator is at
 * Output: returns the next Ele in the ctx (typecasted as an INKCfgEle)
 */
  INKCfgEle *INKCfgContextGetNext(INKCfgContext ctx, INKCfgIterState * state);

/* INKCfgContextMoveEleUp: shifts the Ele at the specified index one position up;
 *                         does nothing if Ele is at first position in the INKCfgContext
 * Input:  ctx   - the INKCfgContext
 *         index - the position of the Ele that needs to be shifted up
 * Output: INKError
 */
  INKError INKCfgContextMoveEleUp(INKCfgContext ctx, int index);

/* INKCfgContextMoveEleDown: shifts the Ele at the specified index one position down;
 *                           does nothing if Ele is last in the INKCfgContext
 * Input:  ctx   - the INKCfgContext
 *         index - the position of the Ele that needs to be shifted down
 * Output: INKError
 */
  INKError INKCfgContextMoveEleDown(INKCfgContext ctx, int index);

/* INKCfgContextAppendEle: apppends the ele to the end of the INKCfgContext
 * Input:  ctx   - the INKCfgContext
 *         ele - the Ele (typecasted as an INKCfgEle) to append to ctx
 * Output: INKError
 * Note: When appending the ele to the INKCfgContext, this function does NOT
 *       make a copy of the ele passed in; it uses the same memory! So you probably
 *       do not want to append the ele and then free the memory for the ele
 *       without first removing the ele from the INKCfgContext
 */
  INKError INKCfgContextAppendEle(INKCfgContext ctx, INKCfgEle * ele);

/* INKCfgContextInsertEleAt: inserts the ele at the specified index
 * Input:  ctx   - the INKCfgContext
 *         ele   - the Ele (typecasted as an INKCfgEle) to insert into ctx
 *         index - the position in ctx to insert the Ele
 * Output: INKError
 * Note: When inserting the ele into the INKCfgContext, this function does NOT
 *       make a copy of the ele passed in; it uses the same memory! So you probably
 *       do not want to insert the ele and then free the memory for the ele
 *       without first removing the ele from the INKCfgContext
 */
  INKError INKCfgContextInsertEleAt(INKCfgContext ctx, INKCfgEle * ele, int index);

/* INKCfgContextRemoveEleAt: removes the Ele at the specified index from the INKCfgContext
 * Input:  ctx   - the INKCfgContext
 *         index - the position of the Ele in the ctx to remove
 * Output: INKError
 */
  INKError INKCfgContextRemoveEleAt(INKCfgContext ctx, int index);

  /* INKCfgContextRemoveAll: removes all Eles from the INKCfgContext
   * Input:  ctx   - the INKCfgContext
   * Output: INKError
   */
  INKError INKCfgContextRemoveAll(INKCfgContext ctx);

/*--- INK Cache Inspector Operations --------------------------------------------*/

/* INKLookupFromCacheUrl
 *   Function takes an url and an 'info' buffer as input,
 *   lookups cache information of the url and saves the
 *   cache info to the info buffer
 */
  inkapi INKError INKLookupFromCacheUrl(INKString url, INKString * info);

/* INKLookupFromCacheUrlRegex
 *   Function takes a string in a regex form and returns
 *   a list of urls that match the regex
 ********************************************************/

  inkapi INKError INKLookupFromCacheUrlRegex(INKString url_regex, INKString * list);

/* INKDeleteFromCacheUrl
 *   Function takes an url and an 'info' buffer as input,
 *   deletes the url from cache if it's in the cache and
 *   returns the status of deletion
 ********************************************************/

  inkapi INKError INKDeleteFromCacheUrl(INKString url, INKString * info);

/* INKDeleteFromCacheUrlRegex
 *   Function takes a string in a regex form and returns
 *   a list of urls deleted from cache
 ********************************************************/

  inkapi INKError INKDeleteFromCacheUrlRegex(INKString url_regex, INKString * list);

/* INKInvalidateFromCacheUrlRegex
 *   Function takes a string in a regex form and returns
 *   a list of urls invalidated from cache
 ********************************************************/

  inkapi INKError INKInvalidateFromCacheUrlRegex(INKString url_regex, INKString * list);

/* These functions support the network configuration functionality
 * For each change of hostname, gateway, dns servers, and nick configurations
 * we should use these APIs to accomodate for it in TS, TM
 ******************************************************************/
  /* rmserver.cfg */

  inkapi INKError rm_change_ip(int, char **);

  inkapi INKError rm_change_hostname(char *);

  inkapi INKError rm_start_proxy();

  inkapi INKError rm_remove_ip(int, char **);


/* Net config functions */

  inkapi INKError INKSetHostname(INKString hostname);

  inkapi INKError INKSetGateway(INKString gateway_ip);

  inkapi INKError INKSetDNSServers(INKString dns_ips);

  inkapi INKError INKSetNICUp(INKString nic_name, bool static_ip, INKString ip, INKString old_ip, INKString netmask,
                              bool onboot, INKString gateway_ip);

  inkapi INKError INKSetProxyPort(INKString proxy_port);

  inkapi INKError INKSetNICDown(INKString nic_name, INKString ip_addrr);

  inkapi INKError INKSetSearchDomain(const char *search_name);

  inkapi INKError INKSetRmRealm(const char *hostname);

  inkapi INKError INKSetRmPNA_RDT_IP(const char *ip);

  inkapi INKError INKSetPNA_RDT_Port(const int port);

#ifdef __cplusplus
}
#endif                          /* __cplusplus */

#endif                          /* __INK_MGMT_API_H__ */
