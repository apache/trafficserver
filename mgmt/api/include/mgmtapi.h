/** @file

  Definitions for internal management API.

  Purpose: This file contains all API wrapper functions in one class. In
  order to eliminate the interdependencies of other library calls, new
  types and structs will be defined and used in the wrapper function calls.

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

#ifndef __TS_MGMT_API_H__
#define __TS_MGMT_API_H__

#include <stdbool.h>
#include <stdint.h>

/***************************************************************************
 * System Specific Items
 ***************************************************************************/

#define tsapi
#define inkexp
#define inkimp

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef TS_RES_MEM_PATH
#define __TS_RES_PATH(x) #x
#define _TS_RES_PATH(x) __TS_RES_PATH(x)
#define TS_RES_PATH(x) x __FILE__ ":" _TS_RES_PATH(__LINE__)
#define TS_RES_MEM_PATH TS_RES_PATH("memory/")
#endif

#define TM_OPT_BIND_STDOUT "bind_stdout"
#define TM_OPT_BIND_STDERR "bind_stderr"

/***************************************************************************
 * Error and Return Values
 ***************************************************************************/

typedef enum {
  TS_ERR_OKAY = 0,

  TS_ERR_READ_FILE,           /* Error occur in reading file */
  TS_ERR_WRITE_FILE,          /* Error occur in writing file */
  TS_ERR_PARSE_CONFIG_RULE,   /* Error in parsing configuration file */
  TS_ERR_INVALID_CONFIG_RULE, /* Invalid Configuration Rule */

  TS_ERR_NET_ESTABLISH, /* Problem in establishing a TCP socket */
  TS_ERR_NET_READ,      /* Problem reading from socket */
  TS_ERR_NET_WRITE,     /* Problem writing to socket */
  TS_ERR_NET_EOF,       /* Hit socket EOF */
  TS_ERR_NET_TIMEOUT,   /* Timed out while waiting for socket read */

  TS_ERR_SYS_CALL, /* Error in basic system call, eg. malloc */
  TS_ERR_PARAMS,   /* Invalid parameters for a fn */

  TS_ERR_NOT_SUPPORTED,     /* Operation not supported */
  TS_ERR_PERMISSION_DENIED, /* Operation not permitted */

  TS_ERR_FAIL
} TSMgmtError;

/***************************************************************************
 * Constants
 ***************************************************************************/

#define TS_INVALID_HANDLE NULL
#define TS_INVALID_LIST TS_INVALID_HANDLE
#define TS_INVALID_CFG_CONTEXT TS_INVALID_HANDLE
#define TS_INVALID_THREAD TS_INVALID_HANDLE
#define TS_INVALID_MUTEX TS_INVALID_HANDLE

#define TS_INVALID_IP_ADDR NULL
#define TS_INVALID_IP_CIDR -1
#define TS_INVALID_PORT 0

#define TS_SSPEC_TIME 0x1
#define TS_SSPEC_SRC_IP 0x2
#define TS_SSPEC_PREFIX 0x4
#define TS_SSPEC_SUFFIX 0x8
#define TS_SSPEC_PORT 0x10
#define TS_SSPEC_METHOD 0x20
#define TS_SSPEC_SCHEME 0x40

#define TS_ENCRYPT_PASSWD_LEN 23

/***************************************************************************
 * Types
 ***************************************************************************/

typedef int64_t TSInt;
typedef int64_t TSCounter;
typedef float TSFloat;
typedef bool TSBool;
typedef char *TSString;
typedef char *TSIpAddr;

typedef void *TSHandle;
typedef TSHandle TSList;
typedef TSHandle TSIpAddrList; /* contains TSIpAddrEle *'s */
typedef TSHandle TSPortList;   /* conatins TSPortEle *'s   */
typedef TSHandle TSDomainList; /* contains TSDomain *'s    */
typedef TSHandle TSStringList; /* contains char* 's         */
typedef TSHandle TSIntList;    /* contains int* 's          */

typedef TSHandle TSCfgContext;
typedef TSHandle TSCfgIterState;

/*--- basic control operations --------------------------------------------*/

typedef enum {
  TS_ACTION_SHUTDOWN,    /* change requires user to stop then start the Traffic Server and Manager (restart Traffic Cop) */
  TS_ACTION_RESTART,     /* change requires restart Traffic Server and Traffic Manager */
  TS_ACTION_DYNAMIC,     /* change is already made in function call */
  TS_ACTION_RECONFIGURE, /* change requires TS to reread configuration files */
  TS_ACTION_UNDEFINED
} TSActionNeedT;

typedef enum {
  TS_PROXY_ON,
  TS_PROXY_OFF,
  TS_PROXY_UNDEFINED,
} TSProxyStateT;

/* used when starting Traffic Server process */
typedef enum {
  TS_CACHE_CLEAR_ON,     /* run TS in  "clear entire cache" mode */
  TS_CACHE_CLEAR_HOSTDB, /* run TS in "only clear the host db cache" mode */
  TS_CACHE_CLEAR_OFF     /* starts TS in regualr mode w/o any options */
} TSCacheClearT;

/*--- diagnostic output operations ----------------------------------------*/

typedef enum {
  TS_DIAG_DIAG,
  TS_DIAG_DEBUG,
  TS_DIAG_STATUS,
  TS_DIAG_NOTE,
  TS_DIAG_WARNING,
  TS_DIAG_ERROR,
  TS_DIAG_FATAL, /* >= FATAL severity causes process termination */
  TS_DIAG_ALERT,
  TS_DIAG_EMERGENCY,
  TS_DIAG_UNDEFINED
} TSDiagsT;

/*--- event operations ----------------------------------------------------*/
typedef enum {
  TS_EVENT_PRIORITY_WARNING,
  TS_EVENT_PRIORITY_ERROR,
  TS_EVENT_PRIORITY_FATAL,
  TS_EVENT_PRIORITY_UNDEFINED
} TSEventPriorityT;

/*--- abstract file operations --------------------------------------------*/

typedef enum {
  TS_ACCESS_NONE,           /* no access */
  TS_ACCESS_MONITOR,        /* monitor only access */
  TS_ACCESS_MONITOR_VIEW,   /* monitor and view configuration access */
  TS_ACCESS_MONITOR_CHANGE, /* monitor and change configuration access */
  TS_ACCESS_UNDEFINED
} TSAccessT;

typedef enum {
  TS_REC_INT,
  TS_REC_COUNTER,
  TS_REC_FLOAT,
  TS_REC_STRING,
  TS_REC_UNDEFINED,
} TSRecordT;

typedef enum {
  TS_IP_SINGLE, /* single ip address */
  TS_IP_RANGE,  /* range ip address, eg. 1.1.1.1-2.2.2.2 */
  TS_IP_UNDEFINED
} TSIpAddrT;

typedef enum {
  TS_CON_TCP, /* TCP connection */
  TS_CON_UDP, /* UDP connection */
  TS_CON_UNDEFINED
} TSConnectT;

typedef enum       /* primary destination types */
{ TS_PD_DOMAIN,    /* domain name */
  TS_PD_HOST,      /* hostname */
  TS_PD_IP,        /* ip address */
  TS_PD_URL_REGEX, /* regular expression in url */
  TS_PD_URL,       /* regular expression in url */
  TS_PD_UNDEFINED } TSPrimeDestT;

typedef enum /* header information types */
{ TS_HDR_DATE,
  TS_HDR_HOST,
  TS_HDR_COOKIE,
  TS_HDR_CLIENT_IP,
  TS_HDR_UNDEFINED } TSHdrT;

typedef enum /* indicate if ICP parent cache or ICP sibling cache */
{ TS_ICP_PARENT,
  TS_ICP_SIBLING,
  TS_ICP_UNDEFINED } TSIcpT;

/* TODO: This should be removed */
typedef enum /* access privileges to news articles cached by Traffic Server  */
{ TS_IP_ALLOW_ALLOW,
  TS_IP_ALLOW_DENY,
  TS_IP_ALLOW_UNDEFINED } TSIpAllowT;

typedef enum               /* multicast time to live options */
{ TS_MC_TTL_SINGLE_SUBNET, /* forward multicast datagrams to single subnet */
  TS_MC_TTL_MULT_SUBNET,   /* deliver multicast to more than one subnet */
  TS_MC_TTL_UNDEFINED } TSMcTtlT;

typedef enum /* tells Traffic Server to accept or reject records satisfying filter condition */
{ TS_LOG_FILT_ACCEPT,
  TS_LOG_FILT_REJECT,
  TS_LOG_FILT_UNDEFINED } TSLogFilterActionT;

typedef enum         /* possible conditional operators used in filters */
{ TS_LOG_COND_MATCH, /* true if filter's field and value are idential; case-sensitive */
  TS_LOG_COND_CASE_INSENSITIVE_MATCH,
  TS_LOG_COND_CONTAIN, /* true if field contains the value; case-sensitive */
  TS_LOG_COND_CASE_INSENSITIVE_CONTAIN,
  TS_LOG_COND_UNDEFINED } TSLogConditionOpT;

typedef enum /* valid logging modes for LogObject's */
{ TS_LOG_MODE_ASCII,
  TS_LOG_MODE_BINARY,
  TS_LOG_ASCII_PIPE,
  TS_LOG_MODE_UNDEFINED } TSLogModeT;

typedef enum              /* methods of specifying groups of clients */
{ TS_CLIENT_GRP_IP,       /* ip range */
  TS_CLIENT_GRP_DOMAIN,   /* domain */
  TS_CLIENT_GRP_HOSTNAME, /* hostname */
  TS_CLIENT_GRP_UNDEFINED } TSClientGroupT;

typedef enum {
  TS_RR_TRUE,   /* go through parent cache list in round robin */
  TS_RR_STRICT, /* Traffic Server machines serve requests striclty in turn */
  TS_RR_FALSE,  /* no round robin selection */
  TS_RR_NONE,   /* no round-robin action tag specified */
  TS_RR_UNDEFINED
} TSRrT;

typedef enum /* a request URL method; used in Secondary Specifiers */
{ TS_METHOD_NONE,
  TS_METHOD_GET,
  TS_METHOD_POST,
  TS_METHOD_PUT,
  TS_METHOD_TRACE,
  TS_METHOD_PUSH,
  TS_METHOD_UNDEFINED } TSMethodT;

typedef enum /*  possible URL schemes */
{ TS_SCHEME_NONE,
  TS_SCHEME_HTTP,
  TS_SCHEME_HTTPS,
  TS_SCHEME_UNDEFINED } TSSchemeT;

typedef enum /* possible schemes to divide volume by */
{ TS_VOLUME_HTTP,
  TS_VOLUME_UNDEFINED } TSVolumeSchemeT;

/* specifies how size is specified */
typedef enum {
  TS_SIZE_FMT_PERCENT,  /* as a percentage */
  TS_SIZE_FMT_ABSOLUTE, /* as an absolute value */
  TS_SIZE_FMT_UNDEFINED,
} TSSizeFormatT;

typedef enum {
  TS_HTTP_CONGEST_PER_IP,
  TS_HTTP_CONGEST_PER_HOST,
  TS_HTTP_CONGEST_UNDEFINED,
} TSCongestionSchemeT;

typedef enum {
  TS_PROTOCOL_DNS,
  TS_PROTOCOL_UNDEFINED,
} TSProtocolT;

typedef enum {
  TS_FNAME_CACHE_OBJ,       /* cache.config */
  TS_FNAME_CONGESTION,      /* congestion.config */
  TS_FNAME_HOSTING,         /* hosting.config */
  TS_FNAME_ICP_PEER,        /* icp.config */
  TS_FNAME_IP_ALLOW,        /* ip_allow.config */
  TS_FNAME_LOGS_XML,        /* logs_xml.config */
  TS_FNAME_PARENT_PROXY,    /* parent.config */
  TS_FNAME_VOLUME,          /* volume.config */
  TS_FNAME_PLUGIN,          /* plugin.config */
  TS_FNAME_REMAP,           /* remap.config */
  TS_FNAME_SOCKS,           /* socks.config */
  TS_FNAME_SPLIT_DNS,       /* splitdns.config */
  TS_FNAME_STORAGE,         /* storage.config */
  TS_FNAME_VADDRS,          /* vaddrs.config */
  TS_FNAME_VSCAN,           /* vscan.config */
  TS_FNAME_VS_TRUSTED_HOST, /* trusted-host.config */
  TS_FNAME_VS_EXTENSION,    /* extensions.config */
  TS_FNAME_UNDEFINED
} TSFileNameT;

/* Each rule type within a file has its own enumeration.
 * Need this enumeration because it's possible there are different Ele's used
 * for rule types within the same file
 */
typedef enum {
  TS_CACHE_NEVER, /* cache.config */
  TS_CACHE_IGNORE_NO_CACHE,
  TS_CACHE_CLUSTER_CACHE_LOCAL,
  TS_CACHE_IGNORE_CLIENT_NO_CACHE,
  TS_CACHE_IGNORE_SERVER_NO_CACHE,
  TS_CACHE_PIN_IN_CACHE,
  TS_CACHE_REVALIDATE,
  TS_CACHE_TTL_IN_CACHE,
  TS_CACHE_AUTH_CONTENT,
  TS_CONGESTION, /* congestion.config */
  TS_HOSTING,    /* hosting.config */
  TS_ICP,        /* icp.config */
  TS_IP_ALLOW,   /* ip_allow.config */
  TS_LOG_FILTER, /* logs_xml.config */
  TS_LOG_OBJECT,
  TS_LOG_FORMAT,
  TS_PP_PARENT, /* parent.config */
  TS_PP_GO_DIRECT,
  TS_VOLUME,    /* volume.config */
  TS_PLUGIN,    /* plugin.config */
  TS_REMAP_MAP, /* remap.config */
  TS_REMAP_REVERSE_MAP,
  TS_REMAP_REDIRECT,
  TS_REMAP_REDIRECT_TEMP,
  TS_SOCKS_BYPASS, /* socks.config */
  TS_SOCKS_AUTH,
  TS_SOCKS_MULTIPLE,
  TS_SPLIT_DNS, /* splitdns.config */
  TS_STORAGE,   /* storage.config */
  TS_VADDRS,    /* vaddrs.config */
  TS_TYPE_UNDEFINED,
  TS_TYPE_COMMENT /* for internal use only */
} TSRuleTypeT;

/* These are initialization options for the Init() function. */
typedef enum {
  TS_MGMT_OPT_DEFAULTS = 0,
  TS_MGMT_OPT_NO_EVENTS,    /* No event callbacks and threads */
  TS_MGMT_OPT_NO_SOCK_TESTS /* No socket test thread */
} TSInitOptionT;

typedef enum {
  TS_RESTART_OPT_NONE    = 0x0,
  TS_RESTART_OPT_CLUSTER = 0x01, /* Restart across the cluster */
  TS_RESTART_OPT_DRAIN   = 0x02, /* Wait for traffic to drain before restarting. */
} TSRestartOptionT;

/***************************************************************************
 * Structures
 ***************************************************************************/

/*--- general -------------------------------------------------------------*/

typedef struct {
  int d; /* days */
  int h; /* hours */
  int m; /* minutes */
  int s; /* seconds */
} TSHmsTime;

/*--- records -------------------------------------------------------------*/

typedef union { /* record value */
  TSInt int_val;
  TSCounter counter_val;
  TSFloat float_val;
  TSString string_val;
} TSRecordValueT;

typedef struct {
  char *rec_name;        /* record name */
  TSInt rec_class;       /* record class (RecT) */
  TSRecordT rec_type;    /* record type {TS_REC_INT...} */
  TSRecordValueT valueT; /* record value */
} TSRecordEle;

typedef struct {
  /* Common RecRecord fields ... */
  char *rec_name;
  TSRecordValueT rec_value;
  TSRecordValueT rec_default;
  TSRecordT rec_type; /* data type (RecDataT) */
  TSInt rec_class;    /* data class (RecT) */
  TSInt rec_version;
  TSInt rec_rsb; /* Raw Stat Block ID */
  TSInt rec_order;

  /* RecConfigMeta fields ... */
  TSInt rec_access;     /* access rights (RecAccessT) */
  TSInt rec_update;     /* update_required bitmask */
  TSInt rec_updatetype; /* update type (RecUpdateT) */
  TSInt rec_checktype;  /* syntax check type (RecCheckT) */
  TSInt rec_source;     /* source of data */
  char *rec_checkexpr;  /* syntax check expression */
} TSConfigRecordDescription;

/* Free (the contents of) a TSConfigRecordDescription */
tsapi void TSConfigRecordDescriptionFree(TSConfigRecordDescription *val);

/* Heap-allocate a TSConfigRecordDescription. */
tsapi TSConfigRecordDescription *TSConfigRecordDescriptionCreate(void);
/* Free and destroy a heap-allocated TSConfigRecordDescription. */
tsapi void TSConfigRecordDescriptionDestroy(TSConfigRecordDescription *);

/*--- events --------------------------------------------------------------*/

/* Note: Each event has a format String associated with it from which the
 *       description is constructed when an event is signalled. This format
 *       string though can be retrieved from the event-mapping table which
 *       is stored both locally and remotely.
 */

typedef struct {
  int id;
  char *name;                /* pre-set, immutable for PREDEFINED events */
  char *description;         /* predefined events have default */
  TSEventPriorityT priority; /* WARNING, ERROR, FATAL */
} TSMgmtEvent;

/* Will not be used until new Cougar Event Processor */
typedef struct {
  char *name;
  /*int signalCount; */         /* 0 is inactive, >= 1 is active event */
  /*unsigned long timestamp; */ /* only applies to active events */
} TSActiveEvent;

/*--- abstract file operations --------------------------------------------*/

typedef struct {
  TSIpAddrT type; /* single ip or an ip-range */
  TSIpAddr ip_a;  /* first ip */
  int cidr_a;     /* CIDR value, 0 if not defined */
  int port_a;     /* port, 0 if not defined */
  TSIpAddr ip_b;  /* second ip (if ip-range) */
  int cidr_b;     /* CIDR value, 0 if not defined */
  int port_b;     /* port, 0 if not defined */
} TSIpAddrEle;

typedef struct {
  int port_a; /* first port */
  int port_b; /* second port (0 if not a port range) */
} TSPortEle;

typedef struct {
  char *domain_val; /* a server name can be specified by name or IP address */
  /* used for www.host.com:8080 or 11.22.33.44:8000 */
  int port; /* (optional) */
} TSDomain;

/* there are a variety of secondary specifiers that can be used in a rule; more than
 * one secondary specifier can be used per rule, but a secondary specifier can only
 * be used once per rule (eg. time, src_ip, prefix, suffix, port, method, scheme)
 */
typedef struct {
  uint32_t active; /* valid field: TS_SSPEC_xxx */
  struct {         /* time range */
    int hour_a;
    int min_a;
    int hour_b;
    int min_b;
  } time;
  TSIpAddr src_ip;  /* client/source ip */
  char *prefix;     /* prefix in path part of URL */
  char *suffix;     /* suffix in the URL */
  TSPortEle *port;  /* requested URL port */
  TSMethodT method; /* get, post, put, trace */
  TSSchemeT scheme; /* HTTP */
} TSSspec;          /* Sspec = Secondary Specifier */

typedef struct {
  TSPrimeDestT pd_type; /* primary destination type: TS_PD_xxx */
  char *pd_val;         /* primary destination value; refers to the requested domain name,
                           host name, ip address, or regular expression to
                           be found in a URL  */
  TSSspec sec_spec;     /* secondary specifier */
} TSPdSsFormat;         /* PdSs = Primary Destination Secondary Specifier */

/* Generic Ele struct which is used as first member in all other Ele structs.
 * The TSCfgContext operations deal with TSCfgEle* type, so must typecast
 * all Ele's to an TSCfgEle*
 */
typedef struct {
  TSRuleTypeT type;
  TSMgmtError error;
} TSCfgEle;

/* cache.config */
typedef struct {
  TSCfgEle cfg_ele;
  TSPdSsFormat cache_info; /* general PdSs information */
  TSHmsTime time_period;   /* only valid if cache_act == TS_CACHE_PIN_IN_CACHE */
} TSCacheEle;

/* congestion.config */
typedef struct {
  TSCfgEle cfg_ele;
  TSPrimeDestT pd_type;
  char *pd_val;
  char *prefix;               /* optional */
  int port;                   /* optional */
  TSCongestionSchemeT scheme; /* per_ip or per_host */
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
} TSCongestionEle;

/* hosting.config */
typedef struct {
  TSCfgEle cfg_ele;
  TSPrimeDestT pd_type;
  char *pd_val;      /* domain or hostname  */
  TSIntList volumes; /* must be a list of ints */
} TSHostingEle;

/* icp.config */
typedef struct {
  TSCfgEle cfg_ele;
  char *peer_hostname;        /* hostname of icp peer; ("localhost" name reserved for Traffic Server) */
  TSIpAddr peer_host_ip_addr; /* ip address of icp peer (not required if peer_hostname) */
  TSIcpT peer_type;           /* 1: icp parent, 2: icp sibling */
  int peer_proxy_port;        /* port number of the TCP port used by the ICP peer for proxy communication */
  int peer_icp_port;          /* port number of the UDP port used by the ICP peer for ICP communication  */
  bool is_multicast;          /* false: multicast not enabled; true: multicast enabled */
  TSIpAddr mc_ip_addr;        /* multicast ip (can be 0 if is_multicast == false */
  TSMcTtlT mc_ttl;            /* multicast time to live; either IP multicast datagrams will not
                                  be forwarded beyond a single subnetwork, or allow delivery
                                  of IP multicast datagrams to more than one subnet
                                  (can be UNDEFINED if is_multicast == false */
} TSIcpEle;

/* ip_allow.config */
typedef struct {
  TSCfgEle cfg_ele;
  TSIpAddrEle *src_ip_addr; /* source ip address (single or range) */
  TSIpAllowT action;
} TSIpAllowEle;

/* logs_xml.config */
typedef struct {
  TSCfgEle cfg_ele;
  TSLogFilterActionT action; /* accept or reject records satisfying filter condition */
  char *filter_name;
  char *log_field; /* possible choices listed on p.250 */
  TSLogConditionOpT compare_op;
  char *compare_str; /* the comparison value can be any string or integer */
  int compare_int;   /* if int, then all the TSLogConditionOpT operations mean "equal" */
} TSLogFilterEle;

typedef struct {
  TSCfgEle cfg_ele;
  char *name; /* must be unique; can't be a pre-defined format */
  char *format;
  int aggregate_interval_secs; /* (optional) use if format contains aggregate ops */
} TSLogFormatEle;

typedef struct {
  TSCfgEle cfg_ele;
  char *format_name;
  char *file_name;
  TSLogModeT log_mode;
  TSDomainList collation_hosts; /* list of hosts (by name or IP addr) */
  TSStringList filters;         /* list of filter names that already exist */
  TSStringList protocols;       /* list of protocols, eg. http, nttp, icp */
  TSStringList server_hosts;    /* list of host names */
} TSLogObjectEle;

/* parent.config */
typedef struct {
  TSCfgEle cfg_ele;
  TSPdSsFormat parent_info; /* general PdSs information */
  TSRrT rr;                 /*  possible values are TS_RRT_TRUE (go through proxy
                                parent list in round robin),TS_RRT_STRICT (server
                                requests striclty in turn), or TS_RRT_FALSE (no
                                round robin selection) */
  TSDomainList proxy_list;  /* ordered list of parent proxies */
  bool direct;              /* indicate if go directly to origin server, default = false and does
                               not bypass parent heirarchies */
} TSParentProxyEle;         /* exactly one of rr or parent_proxy_act must be defined */

/* volume.config */
typedef struct {
  TSCfgEle cfg_ele;
  int volume_num;            /* must be in range 1 - 255 */
  TSVolumeSchemeT scheme;    /* http */
  int volume_size;           /* >= 128 MB, multiple of 128 */
  TSSizeFormatT size_format; /* percentage or absolute */
} TSVolumeEle;

/* plugin.config */
typedef struct {
  TSCfgEle cfg_ele;
  char *name;        /* name of plugin */
  TSStringList args; /* list of arguments */
} TSPluginEle;

/* remap.config */
typedef struct {
  TSCfgEle cfg_ele;
  bool map;               /* if true: map, if false: remap */
  TSSchemeT from_scheme;  /* http, https, <scheme>://<host>:<port>/<path_prefix> */
  char *from_host;        /* from host */
  int from_port;          /* from port (can be 0) */
  char *from_path_prefix; /* from path_prefix (can be NULL) */
  TSSchemeT to_scheme;
  char *to_host;        /* to host */
  int to_port;          /* to port (can be 0) */
  char *to_path_prefix; /* to path_prefix (can be NULL) */
} TSRemapEle;

/* socks.config */
/* TSqa10915: supports two rules types - the first rule type specifies the
   IP addresses of origin servers that TS should bypass SOCKS and access
   directly (this is when ip_addrs is used); the second rule
   type specifies which SOCKS servers to use for the addresses specified
   in dest_ip_addr; so this means that either ip_addrs is specified OR
   dest_ip_addr/socks_servers/rr are */
typedef struct {
  TSCfgEle cfg_ele;
  TSIpAddrList ip_addrs;      /* list of ip addresses to bypass SOCKS server (TS_SOCKS_BYPASS) */
  TSIpAddrEle *dest_ip_addr;  /* ip address(es) that will use the socks server
                                  specified in parent_list (TS_SOCKS_MULTIPLE rule) */
  TSDomainList socks_servers; /* ordered list of SOCKS servers (TS_SOCKS_MULTIPLE rule) */
  TSRrT rr;                   /* possible values are TS_RRT_TRUE (go through proxy
                                  parent list in round robin),TS_RRT_STRICT (server
                                  requests striclty in turn), or TS_RRT_FALSE (no
                                  round robin selection) (TS_SOCKS_MULTIPLE rule) */
  char *username;             /* used for TS_SOCKS_AUTH rule */
  char *password;             /* used for TS_SOCKS_AUTH rule */
} TSSocksEle;

/* splitdns.config */
typedef struct {
  TSCfgEle cfg_ele;
  TSPrimeDestT pd_type;           /* TS_PD_DOMAIN, TS_PD_HOST, TS_PD_URL_REGEX only */
  char *pd_val;                   /* primary destination value */
  TSDomainList dns_servers_addrs; /* list of dns servers */
  char *def_domain;               /* (optional) default domain name (can be NULL) */
  TSDomainList search_list;       /* (optinal) domain search list (can be INVALID) */
} TSSplitDnsEle;

/* storage.config */
typedef struct {
  TSCfgEle cfg_ele;
  char *pathname; /* the name of a disk partition, directory, or file */
  int size;       /* size of the named pathname (in bytes); optional if raw disk partitions */
} TSStorageEle;

/* vaddrs.config */
typedef struct {
  TSCfgEle cfg_ele;
  TSIpAddr ip_addr; /* virtual ip address */
  char *intr;       /* network interface name (hme0) */
  int sub_intr;     /* the sub-interface number; must be between 1 and 255 */
} TSVirtIpAddrEle;

/* rmserver.cfg */
typedef struct {
  TSCfgEle cfg_ele;
  char *Vname;
  char *str_val;
  int int_val;
} TSRmServerEle;

/***************************************************************************
 * Function Types
 ***************************************************************************/

typedef void (*TSEventSignalFunc)(char *name, char *msg, int pri, void *data);
typedef void (*TSDisconnectFunc)(void *data);

/***************************************************************************
 * API Memory Management
 ***************************************************************************/
#define TSmalloc(s) _TSmalloc((s), TS_RES_MEM_PATH)
#define TSrealloc(p, s) _TSrealloc((p), (s), TS_RES_MEM_PATH)
#define TSstrdup(p) _TSstrdup((p), -1, TS_RES_MEM_PATH)
#define TSstrndup(p, n) _TSstrdup((p), (n), TS_RES_MEM_PATH)
#define TSfree(p) _TSfree(p)

tsapi void *_TSmalloc(unsigned int size, const char *path);
tsapi void *_TSrealloc(void *ptr, unsigned int size, const char *path);
tsapi char *_TSstrdup(const char *str, int length, const char *path);
tsapi void _TSfree(void *ptr);

/***************************************************************************
 * API Helper Functions for Data Carrier Structures
 ***************************************************************************/

/*--- TSList operations --------------------------------------------------*/
tsapi TSList TSListCreate();
tsapi void TSListDestroy(TSList l); /* list must be empty */
tsapi TSMgmtError TSListEnqueue(TSList l, void *data);
tsapi void *TSListDequeue(TSList l);
tsapi bool TSListIsEmpty(TSList l);
tsapi int TSListLen(TSList l); /* returns -1 if list is invalid */
tsapi bool TSListIsValid(TSList l);

/*--- TSIpAddrList operations --------------------------------------------*/
tsapi TSIpAddrList TSIpAddrListCreate();
tsapi void TSIpAddrListDestroy(TSIpAddrList ip_addrl);
tsapi TSMgmtError TSIpAddrListEnqueue(TSIpAddrList ip_addrl, TSIpAddrEle *ip_addr);
tsapi TSIpAddrEle *TSIpAddrListDequeue(TSIpAddrList ip_addrl);
tsapi int TSIpAddrListLen(TSIpAddrList ip_addrl);
tsapi bool TSIpAddrListIsEmpty(TSIpAddrList ip_addrl);
tsapi int TSIpAddrListLen(TSIpAddrList ip_addrl);
tsapi bool TSIpAddrListIsValid(TSIpAddrList ip_addrl);

/*--- TSPortList operations ----------------------------------------------*/
tsapi TSPortList TSPortListCreate();
tsapi void TSPortListDestroy(TSPortList portl);
tsapi TSMgmtError TSPortListEnqueue(TSPortList portl, TSPortEle *port);
tsapi TSPortEle *TSPortListDequeue(TSPortList portl);
tsapi bool TSPortListIsEmpty(TSPortList portl);
tsapi int TSPortListLen(TSPortList portl);
tsapi bool TSPortListIsValid(TSPortList portl);

/*--- TSStringList operations --------------------------------------------*/
tsapi TSStringList TSStringListCreate();
tsapi void TSStringListDestroy(TSStringList strl);
tsapi TSMgmtError TSStringListEnqueue(TSStringList strl, char *str);
tsapi char *TSStringListDequeue(TSStringList strl);
tsapi bool TSStringListIsEmpty(TSStringList strl);
tsapi int TSStringListLen(TSStringList strl);
tsapi bool TSStringListIsValid(TSStringList strl);

/*--- TSIntList operations --------------------------------------------*/
tsapi TSIntList TSIntListCreate();
tsapi void TSIntListDestroy(TSIntList intl);
tsapi TSMgmtError TSIntListEnqueue(TSIntList intl, int *str);
tsapi int *TSIntListDequeue(TSIntList intl);
tsapi bool TSIntListIsEmpty(TSIntList intl);
tsapi int TSIntListLen(TSIntList intl);
tsapi bool TSIntListIsValid(TSIntList intl, int min, int max);

/*--- TSDomainList operations --------------------------------------------*/
tsapi TSDomainList TSDomainListCreate();
tsapi void TSDomainListDestroy(TSDomainList domainl);
tsapi TSMgmtError TSDomainListEnqueue(TSDomainList domainl, TSDomain *domain);
tsapi TSDomain *TSDomainListDequeue(TSDomainList domainl);
tsapi bool TSDomainListIsEmpty(TSDomainList domainl);
tsapi int TSDomainListLen(TSDomainList domainl);
tsapi bool TSDomainListIsValid(TSDomainList domainl);

/*--- allocate/deallocate operations -------------------------------------*/
/* NOTE:
 * 1) Default values for TSxxEleCreate functions:
 *    - for all lists, default value is TS_INVALID_LIST. NO memory is
 *      allocated for an Ele's  list type member. The user must
 *      explicity call the TSxxListCreate() function to initialize it.
 *    - for char*'s and TSIpAddr the default is NULL (or TS_INVALID_IP_ADDR
 *      for TSIpAddr's); user must assign allocated memory to initialize any
 *      string or TSIpAddr members of an TSxxxEle
 *
 * 2) An Ele corresponds to a rule type in a file; this is why each Ele has an
 * TSRuleType to identify which type of rule it corresponds to.
 * For config files which only have one rule type, we can easily set the
 * rule type of the Ele in the EleCreate function since there's only one possible
 * option. However, note that for those config files with more than one rule
 * type, we cannot set the rule type in the EleCreate function since
 * we don't know which rule type the Ele corresponds to yet. Thus, the user must
 * specify the TSRuleTypeT when he/she creates the Ele.
 */

tsapi TSMgmtEvent *TSEventCreate();
tsapi void TSEventDestroy(TSMgmtEvent *event);
tsapi TSRecordEle *TSRecordEleCreate();
tsapi void TSRecordEleDestroy(TSRecordEle *ele);
tsapi TSIpAddrEle *TSIpAddrEleCreate();
tsapi void TSIpAddrEleDestroy(TSIpAddrEle *ele);
tsapi TSPortEle *TSPortEleCreate();
tsapi void TSPortEleDestroy(TSPortEle *ele);
tsapi TSDomain *TSDomainCreate();
tsapi void TSDomainDestroy(TSDomain *ele);
tsapi TSSspec *TSSspecCreate();
tsapi void TSSspecDestroy(TSSspec *ele);
tsapi TSPdSsFormat *TSPdSsFormatCreate();
tsapi void TSPdSsFormatDestroy(TSPdSsFormat &ele);
tsapi TSCacheEle *TSCacheEleCreate(TSRuleTypeT type);
tsapi void TSCacheEleDestroy(TSCacheEle *ele);
tsapi TSCongestionEle *TSCongestionEleCreate();
tsapi void TSCongestionEleDestroy(TSCongestionEle *ele);
tsapi TSHostingEle *TSHostingEleCreate();
tsapi void TSHostingEleDestroy(TSHostingEle *ele);
tsapi TSIcpEle *TSIcpEleCreate();
tsapi void TSIcpEleDestroy(TSIcpEle *ele);
tsapi TSIpAllowEle *TSIpAllowEleCreate();
tsapi void TSIpAllowEleDestroy(TSIpAllowEle *ele);
tsapi TSLogFilterEle *TSLogFilterEleCreate();
tsapi void TSLogFilterEleDestroy(TSLogFilterEle *ele);
tsapi TSLogFormatEle *TSLogFormatEleCreate();
tsapi void TSLogFormatEleDestroy(TSLogFormatEle *ele);
tsapi TSLogObjectEle *TSLogObjectEleCreate();
tsapi void TSLogObjectEleDestroy(TSLogObjectEle *ele);
tsapi TSParentProxyEle *TSParentProxyEleCreate(TSRuleTypeT type);
tsapi void TSParentProxyEleDestroy(TSParentProxyEle *ele);
tsapi TSVolumeEle *TSVolumeEleCreate();
tsapi void TSVolumeEleDestroy(TSVolumeEle *ele);
tsapi TSPluginEle *TSPluginEleCreate();
tsapi void TSPluginEleDestroy(TSPluginEle *ele);
tsapi TSRemapEle *TSRemapEleCreate(TSRuleTypeT type);
tsapi void TSRemapEleDestroy(TSRemapEle *ele);
tsapi TSSocksEle *TSSocksEleCreate(TSRuleTypeT type);
tsapi void TSSocksEleDestroy(TSSocksEle *ele);
tsapi TSSplitDnsEle *TSSplitDnsEleCreate();
tsapi void TSSplitDnsEleDestroy(TSSplitDnsEle *ele);
tsapi TSStorageEle *TSStorageEleCreate();
tsapi void TSStorageEleDestroy(TSStorageEle *ele);
tsapi TSVirtIpAddrEle *TSVirtIpAddrEleCreate();
tsapi void TSVirtIpAddrEleDestroy(TSVirtIpAddrEle *ele);
/*--- Ele helper operations -------------------------------------*/

/* TSIsValid: checks if the fields in the ele are all valid
 * Input:  ele - the ele to check (typecast any of the TSxxxEle's to an TSCfgEle)
 * Output: true if ele has valid fields for its rule type, false otherwise
 */
bool TSIsValid(TSCfgEle *ele);

/***************************************************************************
 * API Core
 ***************************************************************************/

/*--- api initialization and shutdown -------------------------------------*/
/* TSInit: initializations required for API clients
 * Input: socket_path - not applicable for local clients
 *                      for remote users, the path to the config directory.
 *                      If == NULL, we use the Layout engine by default.
 *        options - Control some features of the APIs
 * Output: TS_ERR_xx
 * Note: If remote client successfully connects, returns TS_ERR_OKAY; but
 *       even if not successful connection (eg. client program is started
 *       before TM) then can still make API calls and will try connecting then
 */
tsapi TSMgmtError TSInit(const char *socket_path, TSInitOptionT options);

/* TSTerminate: does clean up for API clients
 * Input: <none>
 * Output: <none>
 */
tsapi TSMgmtError TSTerminate();

/*--- plugin initialization -----------------------------------------------*/
/* TSPluginInit: called by traffic_manager to initialize the plugin
 * Input:  argc - argument count
 *         argv - argument array
 * Output: <none>
 */
inkexp extern void TSPluginInit(int argc, const char *argv[]);

/*--- network operations --------------------------------------------------*/
/* UNIMPLEMENTED: used for remote clients on a different machine */
tsapi TSMgmtError TSConnect(TSIpAddr ip_addr, int port);
tsapi TSMgmtError TSDisconnectCbRegister(TSDisconnectFunc *func, void *data);
tsapi TSMgmtError TSDisconnectRetrySet(int retries, int retry_sleep_msec);
tsapi TSMgmtError TSDisconnect();

/*--- control operations --------------------------------------------------*/
/* TSProxyStateGet: get the proxy state (on/off)
 * Input:  <none>
 * Output: proxy state (on/off)
 */
tsapi TSProxyStateT TSProxyStateGet();

/* TSProxyStateSet: set the proxy state (on/off)
 * Input:  proxy_state - set to on/off
 *         clear - specifies if want to start TS with clear_cache or
 *                 clear_cache_hostdb option, or just run TS with no options;
 *                  only applies when turning proxy on
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSProxyStateSet(TSProxyStateT proxy_state, TSCacheClearT clear);

/* TSProxyBacktraceGet: get a backtrace of the proxy
 * Input:  unsigned options - stack trace options
 * Output: formatted backtrace of the proxy
 * 	the caller must free this with TSfree
 */
tsapi TSMgmtError TSProxyBacktraceGet(unsigned, TSString *);

/* TSReconfigure: tell traffic_server to re-read its configuration files
 * Input:  <none>
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSReconfigure();

/* TSRestart: restarts Traffic Manager and Traffic Server
 * Input: options - bitmask of TSRestartOptionT
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSRestart(unsigned options);

/* TSActionDo: based on TSActionNeedT, will take appropriate action
 * Input: action - action that needs to be taken
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSActionDo(TSActionNeedT action);

/* TSBounce: restart the traffic_server process(es).
 * Input: options - bitmask of TSRestartOptionT
 * Output TSMgmtError
 */
tsapi TSMgmtError TSBounce(unsigned options);

/* TSStorageDeviceCmdOffline: Request to make a cache storage device offline.
 * @arg dev Target device, specified by path to device.
 * @return Success.
 */
tsapi TSMgmtError TSStorageDeviceCmdOffline(char const *dev);

/*--- diags output operations ---------------------------------------------*/
/* TSDiags: enables users to manipulate run-time diagnostics, and print
 *           user-formatted notices, warnings and errors
 * Input:  mode - diags mode
 *         fmt  - printf style format
 * Output: <none>
 */
tsapi void TSDiags(TSDiagsT mode, const char *fmt, ...);

/* TSGetErrorMessage: convert error id to error message
 * Input:  error id (defined in TSMgmtError)
 * Output: corresponding error message (allocated memory)
 */
char *TSGetErrorMessage(TSMgmtError error_id);

/*--- password operations -------------------------------------------------*/
/* TSEncryptPassword: encrypts a password
 * Input: passwd - a password string to encrypt (can be NULL)
 * Output: e_passwd - an encrypted passwd (ats_malloc's memory)
 */
tsapi TSMgmtError TSEncryptPassword(char *passwd, char **e_passwd);

/*--- direct file operations ----------------------------------------------*/
/* TSConfigFileRead: reads a config file into a buffer
 * Input:  file - the config file to read
 *         text - a buffer is allocated on the text char* pointer
 *         size - the size of the buffer is returned
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSConfigFileRead(TSFileNameT file, char **text, int *size, int *version);

/* TSConfigFileWrite: writes a config file into a buffer
 * Input:  file - the config file to write
 *         text - text buffer to write
 *         size - the size of the buffer to write
 *         version - the current version level; new file will have the
 *                  version number above this one  (if version < 0, then
 *                  just uses the next version number in the sequence)
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSConfigFileWrite(TSFileNameT file, char *text, int size, int version);

/* TSReadFromUrl: reads a remotely located config file into a buffer
 * Input:  url        - remote location of the file
 *         header     - a buffer is allocated on the header char* pointer
 *         headerSize - the size of the header buffer is returned
 *         body       - a buffer is allocated on the body char* pointer
 *         bodySize   - the size of the body buffer is returned
 * Output: TSMgmtError   - TS_ERR_OKAY if succeed, TS_ERR_FAIL otherwise
 * Obsolete:  tsapi TSMgmtError TSReadFromUrl (char *url, char **text, int *size);
 * NOTE: The URL can be expressed in the following forms:
 *       - http://www.example.com:80/products/network/index.html
 *       - http://www.example.com/products/network/index.html
 *       - http://www.example.com/products/network/
 *       - http://www.example.com/
 *       - http://www.example.com
 *       - www.example.com
 * NOTE: header and headerSize can be NULL
 */
tsapi TSMgmtError TSReadFromUrl(char *url, char **header, int *headerSize, char **body, int *bodySize);

/* TSReadFromUrl: reads a remotely located config file into a buffer
 * Input:  url        - remote location of the file
 *         header     - a buffer is allocated on the header char* pointer
 *         headerSize - the size of the header buffer is returned
 *         body       - a buffer is allocated on the body char* pointer
 *         bodySize   - the size of the body buffer is returned
 *         timeout    - the max. connection timeout value before aborting.
 * Output: TSMgmtError   - TS_ERR_OKAY if succeed, TS_ERR_FAIL otherwise
 * NOTE: The URL can be expressed in the following forms:
 *       - http://www.example.com:80/products/network/index.html
 *       - http://www.example.com/products/network/index.html
 *       - http://www.example.com/products/network/
 *       - http://www.example.com/
 *       - http://www.example.com
 *       - www.example.com
 * NOTE: header and headerSize can be NULL
 */
tsapi TSMgmtError TSReadFromUrlEx(const char *url, char **header, int *headerSize, char **body, int *bodySize, int timeout);

/*--- snapshot operations -------------------------------------------------*/
/* TSSnapshotTake: takes snapshot of configuration at that instant in time
 * Input:  snapshot_name - name to call new snapshot
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSSnapshotTake(char *snapshot_name);

/* TSSnapshotRestore: restores configuration to when the snapshot was taken
 * Input:  snapshot_name - name of snapshot to restore
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSSnapshotRestore(char *snapshot_name);

/* TSSnapshotRemove: removes the snapshot
 * Input:  snapshot_name - name of snapshot to remove
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSSnapshotRemove(char *snapshot_name);

/* TSSnapshotsGet: restores configuration to when the snapshot was taken
 * Input:  snapshots - the list which will store all snapshot names currently taken
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSSnapshotGetMlt(TSStringList snapshots);

/*--- statistics operations -----------------------------------------------*/
/* TSStatsReset: sets all the statistics variables to their default values
 * Input: cluster - Reset the stats clusterwide or not
 * Outpue: TSErrr
 */
tsapi TSMgmtError TSStatsReset(bool cluster, const char *name);

/*--- variable operations -------------------------------------------------*/
/* TSRecordGet: gets a record
 * Input:  rec_name - the name of the record (proxy.config.record_name)
 *         rec_val  - allocated TSRecordEle structure, value stored inside
 * Output: TSMgmtError (if the rec_name does not exist, returns TS_ERR_FAIL)
 */
tsapi TSMgmtError TSRecordGet(const char *rec_name, TSRecordEle *rec_val);

/* TSRecordGet*: gets a record w/ a known type
 * Input:  rec_name - the name of the record (proxy.config.record_name)
 *         *_val    - allocated TSRecordEle structure, value stored inside
 * Output: TSMgmtError
 * Note: For TSRecordGetString, the function will allocate memory for the
 *       *string_val, so the caller must free (*string_val);
 */
tsapi TSMgmtError TSRecordGetInt(const char *rec_name, TSInt *int_val);
tsapi TSMgmtError TSRecordGetCounter(const char *rec_name, TSCounter *counter_val);
tsapi TSMgmtError TSRecordGetFloat(const char *rec_name, TSFloat *float_val);
tsapi TSMgmtError TSRecordGetString(const char *rec_name, TSString *string_val);

/* TSRecordGetMlt: gets a set of records
 * Input:  rec_list - list of record names the user wants to retrieve;
 *                    resulting gets will be stored in the same list;
 *                    if one get fails, transaction will be aborted
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSRecordGetMlt(TSStringList rec_names, TSList rec_vals);

/* TSRecordGetMatchMlt: gets a set of records
 * Input:  rec_regex - regular expression to match against record names
 * Output: TSMgmtError, TSList of TSRecordEle
 */
tsapi TSMgmtError TSRecordGetMatchMlt(const char *rec_regex, TSList list);

/* TSRecordSet*: sets a record w/ a known type
 * Input:  rec_name     - the name of the record (proxy.config.record_name)
 *         *_val        - the value to set the record to
 *         *action_need - indicates which operation required by user for changes to take effect
 * Output: TSMgmtError
 */

tsapi TSMgmtError TSRecordSet(const char *rec_name, const char *val, TSActionNeedT *action_need);
tsapi TSMgmtError TSRecordSetInt(const char *rec_name, TSInt int_val, TSActionNeedT *action_need);
tsapi TSMgmtError TSRecordSetCounter(const char *rec_name, TSCounter counter_val, TSActionNeedT *action_need);
tsapi TSMgmtError TSRecordSetFloat(const char *rec_name, TSFloat float_val, TSActionNeedT *action_need);
tsapi TSMgmtError TSRecordSetString(const char *rec_name, const char *string_val, TSActionNeedT *action_need);

/* TSConfigRecordDescribe: fetch a full description of a configuration record
 * Input: rec_name  - name of the record
 *        flags     - (unused) fetch flags bitmask
 *        val       - output value;
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSConfigRecordDescribe(const char *rec_name, unsigned flags, TSConfigRecordDescription *val);
tsapi TSMgmtError TSConfigRecordDescribeMatchMlt(const char *rec_regex, unsigned flags, TSList list);

/* TSRecordSetMlt: sets a set of records
 * Input:  rec_list     - list of record names the user wants to set;
 *                        if one set fails, transaction will be aborted
 *         *action_need - indicates which operation required by user for changes to take effect
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSRecordSetMlt(TSList rec_list, TSActionNeedT *action_need);

/*--- events --------------------------------------------------------------*/
/* Only a set of statically defined events exist. An event is either
 * active or inactive. An event is active when it is triggered, and
 * becomes inactive when resolved. Events are triggered and resolved
 * by specifying the event's name (which is predefined and immutable).
 */

/* UNIMPLEMENTED - wait for new alarm processor */
/* TSEventSignal: enables the user to trigger an event
 * Input:  event_name - "MGMT_ALARM_ADD_ALARM"
 *         ...        - variable argument list of parameters that go
 *                       go into event description when it is signalled
 * Output: TSMgmtError
 */
/*tsapi TSMgmtError               TSEventSignal (char *event_name, ...); */

/* TSEventResolve: enables the user to resolve an event
 * Input:  event_name - event to resolve
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSEventResolve(const char *event_name);

/* TSActiveEventGetMlt: query for a list of all the currently active events
 * Input:  active_events - an empty TSList; if function call is successful,
 *                         active_events will contain names of the currently
 *                         active events
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSActiveEventGetMlt(TSList active_events);

/* TSEventIsActive: check if the specified event is active
 * Input:  event_name - name of event to check if active; must be one of
 *                      the predefined names
 *         is_current - when function completes, if true, then the event is
 *                      active
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSEventIsActive(char *event_name, bool *is_current);

/* TSEventSignalCbRegister: register a callback for a specific event or
 *                           for any event
 * Input:  event_name - the name of event to register callback for;
 *                      if NULL, the callback is registered for all events
 *         func       - callback function
 *         data       - data to pass to callback
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSEventSignalCbRegister(char *event_name, TSEventSignalFunc func, void *data);

/* TSEventSignalCbUnregister: unregister a callback for a specific event
 *                             or for any event
 * Input: event_name - the name of event to unregister callback for;
 *                     if NULL, the callback is unregistered for all events
 *         func       - callback function
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSEventSignalCbUnregister(char *event_name, TSEventSignalFunc func);

/*--- abstracted file operations ------------------------------------------*/
/* TSCfgContextCreate: allocates memory for an empty TSCfgContext for the specified file
 * Input:  file - the file
 * Output: TSCfgContext
 * Note: This function does not read the current rules in the file into
 * the TSCfgContext (must call TSCfgContextGet to do this). If you
 * do not call TSCfgContextGet before calling TSCfgContextCommit, then
 * you will overwite all the old rules in the config file!
 */
tsapi TSCfgContext TSCfgContextCreate(TSFileNameT file);

/* TSCfgContextDestroy: deallocates all memory for the TSCfgContext
 * Input:  ctx - the TSCfgContext to destroy
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSCfgContextDestroy(TSCfgContext ctx);

/* TSCfgContextCommit: write new file copy based on ele's listed in ctx
 * Input:  ctx - where all the file's eles are stored
 *         *action_need - indicates which operation required by user for changes to take effect
 * Output: TSMgmtError
 * Note: If you do not call TSCfgContextGet before calling TSCfgContextCommit, then
 * you could possibly overwrite all the old rules in the config file!!
 */
tsapi TSMgmtError TSCfgContextCommit(TSCfgContext ctx, TSActionNeedT *action_need, TSIntList errRules);

/* TSCfgContextGet: retrieves all the Ele's for the file specified in the ctx and
 *                puts them into ctx; note that the ele's in the TSCfgContext don't
 *                all have to be of the same ele type
 * Input: ctx - where all the most currfile's eles are stored
 * Output: TSMgmtError
 *
 */
tsapi TSMgmtError TSCfgContextGet(TSCfgContext ctx);

/*--- TSCfgContext Operations --------------------------------------------*/
/*
 * These operations are used to manipulate the opaque TSCfgContext type,
 * eg. when want to modify a file
 */

/* TSCfgContextGetCount: returns number of Ele's in the TSCfgContext
 * Input:  ctx - the TSCfgContext to count the number of ele's in
 * Output: the number of Ele's
 */
int TSCfgContextGetCount(TSCfgContext ctx);

/* TSCfgContextGetEleAt: retrieves the Ele at the specified index; user must
 *                        typecast the TSCfgEle to appropriate TSEle before using
 * Input:  ctx   - the TSCfgContext to retrieve the ele from
 *         index - the Ele position desired; first Ele located at index 0
 * Output: the Ele (typecasted as an TSCfgEle)
 */
TSCfgEle *TSCfgContextGetEleAt(TSCfgContext ctx, int index);

/* TSCfgContextGetFirst: retrieves the first Ele in the TSCfgContext
 * Input:  ctx   - the TSCfgContext
 *         state - the current position in the Ele that the iterator is at
 * Output: returns first Ele in the ctx (typecasted as an TSCfgEle)
 */
TSCfgEle *TSCfgContextGetFirst(TSCfgContext ctx, TSCfgIterState *state);

/* TSCfgContextGetNext: retrieves the next ele in the ctx that's located after
 *                       the one pointed to by the TSCfgIterState
 * Input:  ctx   - the TSCfgContext
 *         state - the current position in the Ele that the iterator is at
 * Output: returns the next Ele in the ctx (typecasted as an TSCfgEle)
 */
TSCfgEle *TSCfgContextGetNext(TSCfgContext ctx, TSCfgIterState *state);

/* TSCfgContextMoveEleUp: shifts the Ele at the specified index one position up;
 *                         does nothing if Ele is at first position in the TSCfgContext
 * Input:  ctx   - the TSCfgContext
 *         index - the position of the Ele that needs to be shifted up
 * Output: TSMgmtError
 */
TSMgmtError TSCfgContextMoveEleUp(TSCfgContext ctx, int index);

/* TSCfgContextMoveEleDown: shifts the Ele at the specified index one position down;
 *                           does nothing if Ele is last in the TSCfgContext
 * Input:  ctx   - the TSCfgContext
 *         index - the position of the Ele that needs to be shifted down
 * Output: TSMgmtError
 */
TSMgmtError TSCfgContextMoveEleDown(TSCfgContext ctx, int index);

/* TSCfgContextAppendEle: appends the ele to the end of the TSCfgContext
 * Input:  ctx   - the TSCfgContext
 *         ele - the Ele (typecasted as an TSCfgEle) to append to ctx
 * Output: TSMgmtError
 * Note: When appending the ele to the TSCfgContext, this function does NOT
 *       make a copy of the ele passed in; it uses the same memory! So you probably
 *       do not want to append the ele and then free the memory for the ele
 *       without first removing the ele from the TSCfgContext
 */
TSMgmtError TSCfgContextAppendEle(TSCfgContext ctx, TSCfgEle *ele);

/* TSCfgContextInsertEleAt: inserts the ele at the specified index
 * Input:  ctx   - the TSCfgContext
 *         ele   - the Ele (typecasted as an TSCfgEle) to insert into ctx
 *         index - the position in ctx to insert the Ele
 * Output: TSMgmtError
 * Note: When inserting the ele into the TSCfgContext, this function does NOT
 *       make a copy of the ele passed in; it uses the same memory! So you probably
 *       do not want to insert the ele and then free the memory for the ele
 *       without first removing the ele from the TSCfgContext
 */
TSMgmtError TSCfgContextInsertEleAt(TSCfgContext ctx, TSCfgEle *ele, int index);

/* TSCfgContextRemoveEleAt: removes the Ele at the specified index from the TSCfgContext
 * Input:  ctx   - the TSCfgContext
 *         index - the position of the Ele in the ctx to remove
 * Output: TSMgmtError
 */
TSMgmtError TSCfgContextRemoveEleAt(TSCfgContext ctx, int index);

/* TSCfgContextRemoveAll: removes all Eles from the TSCfgContext
 * Input:  ctx   - the TSCfgContext
 * Output: TSMgmtError
 */
TSMgmtError TSCfgContextRemoveAll(TSCfgContext ctx);

/*--- TS Cache Inspector Operations --------------------------------------------*/

/* TSLookupFromCacheUrl
 *   Function takes an url and an 'info' buffer as input,
 *   lookups cache information of the url and saves the
 *   cache info to the info buffer
 */
tsapi TSMgmtError TSLookupFromCacheUrl(TSString url, TSString *info);

/* TSLookupFromCacheUrlRegex
 *   Function takes a string in a regex form and returns
 *   a list of urls that match the regex
 ********************************************************/

tsapi TSMgmtError TSLookupFromCacheUrlRegex(TSString url_regex, TSString *list);

/* TSDeleteFromCacheUrl
 *   Function takes an url and an 'info' buffer as input,
 *   deletes the url from cache if it's in the cache and
 *   returns the status of deletion
 ********************************************************/

tsapi TSMgmtError TSDeleteFromCacheUrl(TSString url, TSString *info);

/* TSDeleteFromCacheUrlRegex
 *   Function takes a string in a regex form and returns
 *   a list of urls deleted from cache
 ********************************************************/

tsapi TSMgmtError TSDeleteFromCacheUrlRegex(TSString url_regex, TSString *list);

/* TSInvalidateFromCacheUrlRegex
 *   Function takes a string in a regex form and returns
 *   a list of urls invalidated from cache
 ********************************************************/

tsapi TSMgmtError TSInvalidateFromCacheUrlRegex(TSString url_regex, TSString *list);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __TS_MGMT_API_H__ */
