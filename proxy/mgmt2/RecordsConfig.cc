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


#include "ink_config.h"
#include "RecordsConfig.h"
#include "Main.h"

//-------------------------------------------------------------------------
// RecordsConfig
//-------------------------------------------------------------------------

RecordElement RecordsConfig[] = {

  //##############################################################################
  //#
  //# records.config items
  //#
  //##############################################################################

  //##############################################################################
  //#
  //# System Variables
  //#
  //##############################################################################
  {CONFIG, "proxy.config.product_company", "", INK_STRING, "Apache Software Foundation", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.product_vendor", "", INK_STRING, "Apache", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.product_name", "", INK_STRING, "Traffic Server", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cop_name", "", INK_STRING, "Traffic Cop", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.manager_name", "", INK_STRING, "Traffic Manager", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.server_name", "", INK_STRING, "Traffic Server", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  {CONFIG, "proxy.config.proxy_name", "", INK_STRING, "<proxy_name>", RU_REREAD, RR_REQUIRED, RC_STR, ".+", RA_NULL}
  ,
  {CONFIG, "proxy.config.bin_path", "", INK_STRING, "bin", RU_NULL, RR_REQUIRED, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.proxy_binary", "", INK_STRING, "traffic_server", RU_NULL, RR_REQUIRED, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.manager_binary", "", INK_STRING, "traffic_manager", RU_NULL, RR_REQUIRED, RC_NULL, NULL,
   RA_NULL}
  ,                             // required for traffic_cop
  {CONFIG, "proxy.config.cli_binary", "", INK_STRING, "traffic_line", RU_NULL, RR_REQUIRED, RC_NULL, NULL, RA_NULL}
  ,                             // required for mrtg
  {CONFIG, "proxy.config.watch_script", "", INK_STRING, "traffic_cop", RU_NULL, RR_REQUIRED, RC_NULL, NULL, RA_NULL}
  ,                             // required for mrtg
  {CONFIG, "proxy.config.proxy_binary_opts", "", INK_STRING, "-M", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.start_script", "", INK_STRING, "start", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.env_prep", "", INK_STRING, "example_prep.sh", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.config_dir", "", INK_STRING, "etc/trafficserver", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  // Jira TS-21
  {CONFIG, "proxy.config.local_state_dir", "", INK_STRING, "var/trafficserver", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.temp_dir", "", INK_STRING, "/tmp", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.alarm_email", "", INK_STRING, PKGSYSUSER, RU_REREAD, RR_NULL, RC_STR, ".*", RA_NULL}
  ,
  {CONFIG, "proxy.config.syslog_facility", "", INK_STRING, "LOG_DAEMON", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# Negative core limit means max out limit
  {CONFIG, "proxy.config.core_limit", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.stack_dump_enabled", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cop.core_signal", "", INK_INT, "0", RU_NULL, RR_REQUIRED, RC_NULL, NULL, RA_NULL}
  ,                             // needed by traffic_cop
  {CONFIG, "proxy.config.cop.linux_min_swapfree_kb", "", INK_INT, "10240", RU_NULL, RR_REQUIRED, RC_NULL, NULL, RA_NULL}
  ,                             // needed by traffic_cop
  {CONFIG, "proxy.config.cop.linux_min_memfree_kb", "", INK_INT, "10240", RU_NULL, RR_REQUIRED, RC_NULL, NULL, RA_NULL}
  ,                             // needed by traffic_cop
  //# 0 = disable (seconds)
  {CONFIG, "proxy.config.dump_mem_info_frequency", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# 0 = disable
  {CONFIG, "proxy.config.http_ui_enabled", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.max_disk_errors", "", INK_INT, "5", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# 0 = disable
  {CONFIG, "proxy.config.history_info_enabled", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# 0 = disable
  {CONFIG, "proxy.config.process_state_dump_mode", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.output.logfile", "", INK_STRING, "traffic.out", RU_RESTART_TC, RR_REQUIRED, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.snapshot_dir", "", INK_STRING, "snapshot", RU_NULL, RR_REQUIRED, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net_snapshot_filename", "", INK_STRING, "net.config.xml", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //# 0 = disable
  {CONFIG, "proxy.config.res_track_memory", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //# Traffic Server system settings
  //##############################################################################
  // The maximum number of chunks to allocate with mmap. Setting this to zero disables all use of mmap. (Unix only)
  {CONFIG, "proxy.config.system.mmap_max", "", INK_INT, "1073741823", RU_RESTART_TS, RR_NULL, RC_INT, NULL, RA_READ_ONLY}
  ,
  // Enable/disable memalign preallocation memory
  {CONFIG, "proxy.config.system.memalign_heap", "", INK_INT, "0", RU_NULL, RR_NULL, RC_INT, NULL, RA_READ_ONLY}
  ,
  // Traffic Server Execution threads configuration
  // By default Traffic Server set number of execution threads equal to total CPUs
  {CONFIG, "proxy.config.exec_thread.autoconfig", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_INT, "[0-65535]",
   RA_READ_ONLY}
  ,
  {CONFIG, "proxy.config.exec_thread.autoconfig.scale", "", INK_FLOAT, "2.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_READ_ONLY}
  ,
  {CONFIG, "proxy.config.exec_thread.limit", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_INT, "[1-1024]", RA_READ_ONLY}
  ,
  {CONFIG, "proxy.config.accept_threads", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_READ_ONLY}
  ,
  {CONFIG, "proxy.config.thread.default.stacksize", "", INK_INT, "1048576", RU_RESTART_TS, RR_NULL, RC_INT,
   "[131072-104857600]", RA_READ_ONLY}
  ,
  {CONFIG, "proxy.config.user_name", NULL, INK_STRING, "", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# Version Info
  //#
  //##############################################################################
  {PROCESS, "proxy.process.version.server.short", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.version.server.long", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.version.server.build_number", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.version.server.build_time", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.version.server.build_date", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.version.server.build_machine", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.version.server.build_person", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //##############################################################################
  //#
  //# Connection Collapsing
  //#
  //# 1. hashtable_enabled: if set to 1, requests will first search the
  //#    hashtable to see if another similar request is already being served.
  //# 2. rww_wait_time: read-while-write wait time: While read while write is
  //#    enabled, the secondary clients will wait this amount of time after which
  //#    cache lookup is retried.
  //# 3. revaildate_window_period: while revaidation of a cached object is being
  //#    done, the secondary clients for the same url will serve the stale object for
  //#    this amount of time, after the revalidation had started.
  //#
  //##############################################################################

  {CONFIG, "proxy.config.connection_collapsing.hashtable_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT,
   "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.connection_collapsing.rww_wait_time", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.connection_collapsing.revalidate_window_period", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,

  //##############################################################################
  //#
  //# Support for SRV records
  //#
  //##############################################################################
  {CONFIG, "proxy.config.srv_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,

  //##############################################################################
  //#
  //# Support for disabling check for Accept-* / Content-* header mismatch
  //#
  //##############################################################################
  {CONFIG, "proxy.config.http.cache.ignore_accept_mismatch", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.ignore_accept_language_mismatch", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT,
   "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.ignore_accept_encoding_mismatch", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT,
   "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.ignore_accept_charset_mismatch", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT,
   "[0-1]", RA_NULL}
  ,

  //##############################################################################
  //#
  //# Redirection
  //#
  //# 1. redirection_enabled: if set to 1, redirection is enabled.
  //# 2. number_of_redirectionse: The maximum number of redirections TS permits
  //# 3. post_copy_size: The maximum POST data size TS permits to copy
  //#
  //##############################################################################

  {CONFIG, "proxy.config.http.redirection_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.number_of_redirections", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.post_copy_size", "", INK_INT, "2048", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

//##############################################################################
  //#
  //# Diagnostics
  //#
  //# Enable by setting proxy.config.diags.debug.enabled to 1
  //# Route each type of diagnostic with a string, each character representing:
  //#    O  stdout
  //#    E  stderr
  //#    S  syslog
  //#    L  diags.log
  //#
  //##############################################################################
  {CONFIG, "proxy.config.diags.debug.enabled", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.diags.debug.tags", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.diags.action.enabled", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.diags.action.tags", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.diags.show_location", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.diags.output.diag", "", INK_STRING, "E", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.diags.output.debug", "", INK_STRING, "E", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.diags.output.status", "", INK_STRING, "S", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.diags.output.note", "", INK_STRING, "S", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.diags.output.warning", "", INK_STRING, "S", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.diags.output.error", "", INK_STRING, "SE", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.diags.output.fatal", "", INK_STRING, "SE", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.diags.output.alert", "", INK_STRING, "SE", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.diags.output.emergency", "", INK_STRING, "SE", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# Local Manager
  //#
  //##############################################################################
  {CONFIG, "proxy.config.lm.pserver_timeout_secs", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.lm.pserver_timeout_msecs", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.lm.sem_id", "", INK_INT, "11452", RU_NULL, RR_REQUIRED, RC_NULL, NULL, RA_NULL}
  ,                             // needed by cop
  {CONFIG, "proxy.config.cluster.delta_thresh", "", INK_INT, "30", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.peer_timeout", "", INK_INT, "30", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.startup_timeout", "", INK_INT, "10", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# cluster type requires restart to change
  //# 1 is full clustering, 2 is mgmt only, 3 is no clustering
  {LOCAL, "proxy.local.cluster.type", "", INK_INT, "3", RU_RESTART_TM, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.rsport", "", INK_INT, "8088", RU_NULL, RR_REQUIRED, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.mcport", "", INK_INT, "8089", RU_REREAD, RR_REQUIRED, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.mc_group_addr", "", INK_STRING, "224.0.1.37", RU_REREAD, RR_REQUIRED,
   RC_IP, "[0-255]\\.[0-255]\\.[0-255]\\.[0-255]", RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.mc_ttl", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.log_bogus_mc_msgs", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.html_doc_root", "", INK_STRING, "<html_doc_root>", RU_NULL, RR_REQUIRED, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.web_interface_port", "", INK_INT, "8081", RU_RESTART_TM, RR_REQUIRED,
   RC_INT, "[0-65535]", RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.autoconf_port", "", INK_INT, "8083", RU_RESTART_TM, RR_REQUIRED, RC_INT,
   "[0-65535]", RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.autoconf.localhost_only", "", INK_INT, "0", RU_RESTART_TM, RR_NULL, RC_INT, "[0-1]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.autoconf.pac_filename", "", INK_STRING, "proxy.pac", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.autoconf.wpad_filename", "", INK_STRING, "wpad.dat", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  // overseer_mode: 0 disabled, 1 monitor only (get), 2 full acces (get, set, reread, bounce, restart)
  {CONFIG, "proxy.config.admin.overseer_mode", "", INK_INT, "0", RU_RESTART_TM, RR_NULL, RC_INT, "[0-2]", RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.overseer_port", "", INK_INT, "9898", RU_NULL, RR_REQUIRED, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.admin_user", "", INK_STRING, "<admin_user_name>", RU_REREAD, RR_REQUIRED, RC_STR, ".+",
   RA_NO_ACCESS}
  ,
  {CONFIG, "proxy.config.admin.admin_password", "", INK_STRING, "<admin_user_password>", RU_REREAD, RR_REQUIRED, RC_STR,
   ".*", RA_NO_ACCESS}
  ,
  {CONFIG, "proxy.config.admin.access_control_file", "", INK_STRING, "admin_access.config", RU_RESTART_TM, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.feature_set", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.basic_auth", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.use_ssl", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.ssl_cert_file", "", INK_STRING, "private_key.pem", RU_RESTART_TM, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.number_config_bak", "", INK_INT, "3", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.user_id", "", INK_STRING, PKGSYSUSER, RU_NULL, RR_REQUIRED, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.ui_refresh_rate", "", INK_INT, "30", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.load_factor", "", INK_FLOAT, "250.00", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.log_mgmt_access", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.log_resolve_hostname", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.ip_allow.filename", "", INK_STRING, "mgmt_allow.config", RU_REREAD, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.advanced_ui", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.cli_enabled", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.cli_path", "", INK_STRING, "cli", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.cli_port", "", INK_INT, "9000", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.lang_dict", "", INK_STRING, "english.dict", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.session", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.admin.session.timeout", "", INK_INT, "600", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //##############################################################################
  //#
  //# UDP configuration stuff: hidden variables
  //#
  //##############################################################################

  {CONFIG, "proxy.config.udp.free_cancelled_pkts_sec", "", INK_INT, "10", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.udp.periodic_cleanup", "", INK_INT, "10", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.udp.send_retries", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //##############################################################################
  //#
  //# Bandwith Management file
  //#
  //##############################################################################
  {CONFIG, "proxy.config.bandwidth_mgmt.filename", "", INK_STRING, "bandwidth_mgmt_xml.config", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# Process Manager
  //#
  //##############################################################################
  {CONFIG, "proxy.config.process_manager.timeout", "", INK_INT, "5", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.process_manager.enable_mgmt_port", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.process_manager.mgmt_port", "", INK_INT, "8084", RU_NULL, RR_REQUIRED, RC_NULL,
   NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# Virtual IP Manager
  //#
  //##############################################################################
  {CONFIG, "proxy.config.vmap.enabled", "", INK_INT, "0", RU_NULL, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.vmap.addr_file", "", INK_STRING, "vaddrs.config", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.vmap.down_up_timeout", "", INK_INT, "10", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ping.npacks_to_trans", "", INK_INT, "5", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ping.timeout_sec", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# Alarm Configuration
  //#
  //##############################################################################
  //        #################################################################
  //        # execute alarm as "<abs_path>/<bin> "<MSG_STRING_FROM_PROXY>"" #
  //        #################################################################
  {CONFIG, "proxy.config.alarm.bin", "", INK_STRING, "example_alarm_bin.sh", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.alarm.abs_path", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.alarm.script_runtime", "", INK_INT, "5", RU_REREAD, RR_NULL, RC_INT, "[0-300]", RA_NULL}
  ,
  //##############################################################################
  //#
  //# Traffic Net
  //#
  //##############################################################################
  {CONFIG, "proxy.config.traffic_net.traffic_net_mode", "", INK_INT, "3", RU_REREAD, RR_NULL, RC_INT, "[0-3]", RA_NULL}
  ,
  {CONFIG, "proxy.config.traffic_net.traffic_net_frequency", "", INK_INT, "86400", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.traffic_net.traffic_net_uid", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.traffic_net.traffic_net_lid", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.traffic_net.traffic_net_server", "", INK_STRING, "sm-linux-1.example.com", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.traffic_net.traffic_net_port", "", INK_INT, "80", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.traffic_net.traffic_net_path", "", INK_STRING, "/traffic-net", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.traffic_net.traffic_net_encryption", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.traffic_net.traffic_net_attempts", "", INK_COUNTER, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.traffic_net.traffic_net_successes", "", INK_COUNTER, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.traffic_net.last_traffic_net_attempt_time", "", INK_STRING, "Never", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.traffic_net.last_traffic_net_success_time", "", INK_STRING, "Never", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.traffic_net.total_traffic_net_bytes_sent", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //##############################################################################
  //#
  //# Inktoswitch Configuration(NOT USED)
  //#
  //##############################################################################
  {CONFIG, "proxy.config.http.inktoswitch_enabled", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.router_ip", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.router_port", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //####################################################################
  //# congestion control
  //####################################################################
  {CONFIG, "proxy.config.http.congestion_control.enabled", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.localtime", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.filename", "", INK_STRING, "congestion.config", RU_REREAD, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.default.max_connection_failures", "", INK_INT, "5", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.default.fail_window", "", INK_INT, "120", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.default.proxy_retry_interval", "", INK_INT, "10", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.default.client_wait_interval", "", INK_INT, "300", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.default.wait_interval_alpha", "", INK_INT, "30", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.default.live_os_conn_timeout", "", INK_INT, "60", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.default.live_os_conn_retries", "", INK_INT, "2", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.default.dead_os_conn_timeout", "", INK_INT, "15", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.default.dead_os_conn_retries", "", INK_INT, "1", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.default.max_connection", "", INK_INT, "-1", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.default.error_page", "", INK_STRING, "congestion#retryAfter", RU_NULL,
   RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.default.congestion_scheme", "", INK_STRING, "per_ip", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.congestion_control.default.snmp", "", INK_STRING, "on", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,

  //        ###########
  //        # Parsing #
  //        ###########
  {CONFIG, "proxy.config.header.parse.no_host_url_redirect", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_STR, ".*",
   RA_NULL}
  ,
  //##############################################################################
  //#
  //# Authentication Basic Realm
  //##############################################################################
  {CONFIG, "proxy.config.auth.enabled", "", INK_INT, "0",
   RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.auth.cache.filename", "", INK_STRING,
   "authcache.db", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.auth.cache.path", "", INK_STRING,
   "/var/trafficserver", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.auth.cache.size", "", INK_INT, "5000",
   RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.auth.cache.storage_size", "", INK_INT, "15728640",
   RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.auth.flags", "", INK_INT, "0",
   RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.auth.scope", "", INK_STRING,
   "TE", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.auth.authenticate_session", "", INK_INT,
   "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.proxy.authenticate.basic.realm", "",
   INK_STRING, NULL, RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.auth.convert_filter_to_policy", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]",
   RA_NULL}
  ,
  // assumes executable is stored in bin_path directory
  {CONFIG, "proxy.config.auth.convert_bin", "", INK_STRING, "filter_to_policy", RU_REREAD, RR_NULL, RC_STR, ".*",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.auth.password_file_path", "", INK_STRING, "var/trafficserver", RU_REREAD, RR_NULL, RC_NULL,
   ".*", RA_NULL}
  ,
  //##############################################################################x
  //#
  //# LDAP
  //#
  //##############################################################################
  {CONFIG, "proxy.config.ldap.auth.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.cache.filename", "", INK_STRING, "ldap.db", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.cache.size", "", INK_INT, "5000", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.cache.storage_path", "", INK_STRING, "var/trafficserver", RU_RESTART_TS, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.cache.storage_size", "", INK_INT, "15728640", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.auth.ttl_value", "", INK_INT, "3000", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.auth.purge_cache_on_auth_fail", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.auth.query.timeout", "", INK_INT, "10", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.auth.periodic.timeout.interval", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
//  {CONFIG, "proxy.config.ldap.auth.multiple.ldap_servers.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_NULL},
//  {CONFIG, "proxy.config.ldap.auth.bypass.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_NULL},
//  {CONFIG, "proxy.config.ldap.auth.multiple.ldap_servers.config.file", "", INK_STRING, "ldapsrvr.config", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL},
  {CONFIG, "proxy.config.ldap.proc.ldap.server.name", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_STR, ".*",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.proc.ldap.server.port", "", INK_INT, "389", RU_RESTART_TS, RR_NULL, RC_INT, "[0-65535]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.proc.ldap.base.dn", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_STR, ".*", RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.proc.ldap.uid_filter", "", INK_STRING, "uid", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,

  {CONFIG, "proxy.config.ldap.proc.ldap.attribute.name", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.proc.ldap.attribute.value", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.secure.cert.db.path", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.secure.bind.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.auth.redirect_url", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.auth.threads", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  {CONFIG, "proxy.config.ldap.auth.bound_attr_search", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  {PROCESS, "proxy.process.ldap.cache.hits", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.ldap.cache.misses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.ldap.server.errors", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.ldap.denied.authorizations", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.ldap.auth.timed_out", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.ldap.cancelled.authentications", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.ldap.server.connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

//
// congestion control
//
  {PROCESS, "proxy.process.congestion.congested_on_conn_failures", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.congestion.congested_on_max_connection", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,

  {CONFIG, "proxy.config.ldap.proc.ldap.server.bind_dn", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ldap.proc.ldap.server.bind_pwd", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //##############################################################################
  //#
  //# NTLM
  //#
  //##############################################################################
  {CONFIG, "proxy.config.ntlm.auth.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.dc.list", "", INK_STRING, "", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.dc.load_balance", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.dc.max_connections", "", INK_INT, "3", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.dc.max_conn_time", "", INK_INT, "1800", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.nt_domain", "", INK_STRING, "", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.nt_host", "", INK_STRING, "traffic_server", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.queue_len", "", INK_INT, "10", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.req_timeout", "", INK_INT, "20", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.dc.retry_time", "", INK_INT, "300", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# Ntlm domain controller threshold is the number of request that must fail within
  //#  the retry window for the parent to be marked down
  {CONFIG, "proxy.config.ntlm.dc.fail_threshold", "", INK_INT, "5", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.fail_open", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.allow_guest_login", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  {CONFIG, "proxy.config.ntlm.cache.enabled", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.cache.filename", "", INK_STRING, "authcache.db", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.cache.size", "", INK_INT, "5000", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.cache.storage_path", "", INK_STRING, "var/trafficserver", RU_RESTART_TS, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.cache.storage_size", "", INK_INT, "15728640", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ntlm.cache.ttl_value", "", INK_INT, "3600", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  {PROCESS, "proxy.process.ntlm.cache.hits", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.ntlm.cache.misses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.ntlm.server.errors", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.ntlm.denied.authorizations", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.ntlm.cancelled.authentications", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.ntlm.server.connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

//##############################################################################
  //#
  //# RADIUS Authentication
  //#
  //##############################################################################
  {CONFIG, "proxy.config.radius.auth.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.proc.radius.primary_server.name", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_STR,
   "^[^[:space:]]*$", RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.proc.radius.primary_server.auth_port", "", INK_INT, "1812", RU_RESTART_TS, RR_NULL,
   RC_INT, "[0-65535]", RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.proc.radius.primary_server.acct_port", "", INK_INT, "1813", RU_RESTART_TS, RR_NULL,
   RC_INT, "[0-65535]", RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.proc.radius.primary_server.shared_key", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL,
   RC_STR, ".*", RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.proc.radius.primary_server.shared_key_file", "", INK_STRING, NULL, RU_RESTART_TS,
   RR_NULL, RC_STR, ".*", RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.proc.radius.secondary_server.name", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL,
   RC_STR, "^[^[:space:]]*$", RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.proc.radius.secondary_server.auth_port", "", INK_INT, "1812", RU_RESTART_TS, RR_NULL,
   RC_INT, "[0-65535]", RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.proc.radius.secondary_server.acct_port", "", INK_INT, "1813", RU_RESTART_TS, RR_NULL,
   RC_INT, "[0-65535]", RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.proc.radius.secondary_server.shared_key", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL,
   RC_STR, ".*", RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.proc.radius.secondary_server.shared_key_file", "", INK_STRING, NULL, RU_RESTART_TS,
   RR_NULL, RC_STR, ".*", RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.auth.min_timeout", "", INK_INT, "10", RU_RESTART_TS, RR_NULL, RC_INT, "[0-65535]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.auth.max_retries", "", INK_INT, "10", RU_RESTART_TS, RR_NULL, RC_INT, "[0-65535]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.cache.size", "", INK_INT, "1000", RU_RESTART_TS, RR_NULL, RC_INT, "[256-65535]", RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.cache.storage_size", "", INK_INT, "15728640", RU_RESTART_TS, RR_NULL, RC_INT, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.auth.ttl_value", "", INK_INT, "60", RU_RESTART_TS, RR_NULL, RC_INT, "[0-65535]", RA_NULL}
  ,
  {CONFIG, "proxy.config.radius.auth.purge_cache_on_auth_fail", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT,
   "[0-1]", RA_NULL}
  ,
  //##############################################################################
  //#
  //# userName Cache
  //#
  //##############################################################################
  {CONFIG, "proxy.config.username.cache.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.username.cache.filename", "", INK_STRING, "username.db", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.username.cache.size", "", INK_INT, "5000", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.username.cache.storage_path", "", INK_STRING, "var/trafficserver", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.username.cache.storage_size", "", INK_INT, "15728640", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# HTTP Engine
  //#
  //##############################################################################
  //        ##########
  //        # basics #
  //        ##########
  //       #
  //       # The main server_port is listed here, other server ports is a
  //       # string of ports, separated by whitespace.
  //       #
  //       # The port attributes should be set to X(default behavior). For
  //       # example ...server_other_ports STRING 1234:X 12345:X
  //       #
  {CONFIG, "proxy.config.http.enabled", "", INK_INT, "1", RU_RESTART_TM, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.server_port", "", INK_INT, "8080", RU_RESTART_TM, RR_REQUIRED, RC_INT,
   "[0-65535]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.server_port_attr", "", INK_STRING, "X", RU_RESTART_TM, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.server_other_ports", "", INK_STRING, NULL, RU_RESTART_TM, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.insert_request_via_str", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.insert_response_via_str", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //        # verbose via string
  //        #
  //        # 0 - no extra info added to string
  //        # 1 - all extra information added
  //        # 2 - some extra info added
  {CONFIG, "proxy.config.http.verbose_via_str", "", INK_INT, "2", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.request_via_str", "", INK_STRING, "ApacheTrafficServer/" PACKAGE_VERSION, RU_REREAD,
   RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.response_via_str", "", INK_STRING, "ApacheTrafficServer/" PACKAGE_VERSION, RU_REREAD,
   RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.response_server_enabled", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, "[0-3]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.response_server_str", "", INK_STRING, "ATS/" PACKAGE_VERSION, RU_REREAD, RR_NULL, RC_NULL,
   ".*", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.enable_url_expandomatic", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.no_dns_just_forward_to_parent", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.uncacheable_requests_bypass_parent", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT,
   "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.no_origin_server_dns", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.keep_alive_enabled", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.keep_alive_post_out", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.chunking_enabled", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.session_auth_cache_keep_alive_enabled", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  //       # Send http11 requests
  //       #
  //       #   0 - Never
  //       #   1 - Always
  //       #   2 - if the server has returned http1.1 before
  //       #   3 - if the client request is 1.1 & the server
  //       #         has returned 1.1 before
  //       #
  {CONFIG, "proxy.config.http.send_http11_requests", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //       #  origin_server_pipeline and user_agent_pipeline
  //       #
  //       #  0      - no keepalive
  //       #  n >= 1 - max pipeline window
  //       #           (1 is the same HTTP/1.0 keepalive)
  //       #
  {CONFIG, "proxy.config.http.origin_server_pipeline", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.user_agent_pipeline", "", INK_INT, "4", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.share_server_sessions", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.wuts_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.log_spider_codes", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.record_heartbeat", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.record_tcp_mem_hit", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.default_buffer_size", "", INK_INT, "8", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.default_buffer_water_mark", "", INK_INT, "32768", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.enable_http_info", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.server_max_connections", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.origin_max_connections", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.origin_min_keep_alive_connections", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "^[0-9]+$", RA_NULL}
  ,

  //       ##########################
  //       # HTTP referer filtering #
  //       ##########################
  {CONFIG, "proxy.config.http.referer_filter", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.referer_format_redirect", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.referer_default_redirect", "", INK_STRING, "http://www.apache.org", RU_REREAD, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,

  //       ##########################################################
  //       # HTTP Accept-Encoding filtering (depends on User-Agent) #
  //       ##########################################################
  {CONFIG, "proxy.config.http.accept_encoding_filter_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.accept_encoding_filter.filename", "", INK_STRING, "ae_ua.config", RU_REREAD, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,

  //       ########################
  //       # HTTP Quick filtering #
  //       ########################
  //       This is dedicated and very specific 'HTTP method' filter.
  //       Note: If method does not match, filtering will be skipped.
  //       bits 15-0 - HTTP method mask
  //            0x0000 - Any possible HTTP method (or you can use 0xFFFF)
  //            0x0001 - CONNECT
  //            0x0002 - DELETE
  //            0x0004 - GET
  //            0x0008 - HEAD
  //            0x0010 - ICP_QUERY
  //            0x0020 - OPTIONS
  //            0x0040 - POST
  //            0x0080 - PURGE
  //            0x0100 - PUT
  //            0x0200 - TRACE
  //            0x0400 - PUSH
  //       bits 18-16 - reserved
  //       bits 30-17 - reserved
  //       bit 31 - Action (allow=1, deny=0)
  //       Note: if 'proxy.config.http.quick_filter.mask' is equal 0, there is no 'quick http filtering' at all
  {CONFIG, "proxy.config.http.quick_filter.mask", "", INK_INT, "0x80020082", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //        ##############################
  //        # parent proxy configuration #
  //        ##############################
  {CONFIG, "proxy.config.http.parent_proxy_routing_enable", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.parent_proxies", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_STR, ".*", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.parent_proxy.file", "", INK_STRING, "parent.config", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.parent_proxy.retry_time", "", INK_INT, "300", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# Parent fail threshold is the number of request that must fail within
  //#  the retry window for the parent to be marked down
  {CONFIG, "proxy.config.http.parent_proxy.fail_threshold", "", INK_INT, "10", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.parent_proxy.total_connect_attempts", "", INK_INT, "4", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.parent_proxy.per_parent_connect_attempts", "", INK_INT, "2", RU_REREAD, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.parent_proxy.connect_attempts_timeout", "", INK_INT, "30", RU_REREAD, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.forward.proxy_auth_to_parent", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //        ###################################
  //        # NO DNS DOC IN CACHE             #
  //        ###################################
  {CONFIG, "proxy.config.http.doc_in_cache_skip_dns", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,

  //        ###################################
  //        # HTTP connection timeouts (secs) #
  //        ###################################
  //       #
  //       # out: proxy -> os connection
  //       # in : ua -> proxy connection
  //       #
  {CONFIG, "proxy.config.http.keep_alive_no_activity_timeout_in", "", INK_INT, "10", RU_REREAD, RR_NULL, RC_STR,
   "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.keep_alive_no_activity_timeout_out", "", INK_INT, "10", RU_REREAD, RR_NULL, RC_STR,
   "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.transaction_no_activity_timeout_in", "", INK_INT, "120", RU_REREAD, RR_NULL, RC_STR,
   "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.transaction_no_activity_timeout_out", "", INK_INT, "120", RU_REREAD, RR_NULL, RC_STR,
   "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.transaction_active_timeout_in", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.transaction_active_timeout_out", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.accept_no_activity_timeout", "", INK_INT, "120", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.background_fill_active_timeout", "", INK_INT, "60", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.background_fill_completed_threshold", "", INK_FLOAT, "0.5", RU_REREAD, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  //        ##################################
  //        # origin server connect attempts #
  //        ##################################
  {CONFIG, "proxy.config.http.connect_attempts_max_retries", "", INK_INT, "6", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.connect_attempts_max_retries_dead_server", "", INK_INT, "2", RU_REREAD, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.connect_attempts_rr_retries", "", INK_INT, "2", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.connect_attempts_timeout", "", INK_INT, "30", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.streaming_connect_attempts_timeout", "", INK_INT, "1800", RU_REREAD, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.post_connect_attempts_timeout", "", INK_INT, "1800", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.down_server.cache_time", "", INK_INT, "900", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.down_server.abort_threshold", "", INK_INT, "10", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.negative_revalidating_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.negative_revalidating_lifetime", "", INK_INT, "1800", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.negative_caching_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.negative_caching_lifetime", "", INK_INT, "1800", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //        #########################
  //        # proxy users variables #
  //        #########################
  {CONFIG, "proxy.config.http.anonymize_remove_from", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.anonymize_remove_referer", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.anonymize_remove_user_agent", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.anonymize_remove_cookie", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.anonymize_remove_client_ip", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.anonymize_insert_client_ip", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.append_xforwards_header", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.anonymize_other_header_list", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_STR, ".*",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.snarf_username_from_authorization", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.insert_squid_x_forwarded_for", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.insert_age_in_response", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.avoid_content_spoofing", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.enable_http_stats", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.normalize_ae_gzip", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,

  //        ####################################################
  //        # Global User-Agent header                         #
  //        ####################################################
  {CONFIG, "proxy.config.http.global_user_agent_header", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_STR, ".*", RA_NULL}
  ,

  //        ############
  //        # security #
  //        ############
  {CONFIG, "proxy.config.http.request_header_max_size", "", INK_INT, "131072", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.response_header_max_size", "", INK_INT, "131072", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.push_method_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  //        #################
  //        # cache control #
  //        #################
  {CONFIG, "proxy.config.http.cache.http", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.ignore_client_no_cache", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.ignore_client_cc_max_age", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.ims_on_client_no_cache", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.ignore_server_no_cache", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //       # cache responses to cookies has 4 options
  //       #
  //       #  0 - do not cache any responses to cookies
  //       #  1 - cache for any content-type (ignore cookies)
  //       #  2 - cache only for image types
  //       #  3 - cache for all but text content-types
  //       #  4 - cache for all but text content-types except OS response without "Set-Cookie" or with "Cache-Control: public"
  {CONFIG, "proxy.config.http.cache.cache_responses_to_cookies", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_INT, "[0-4]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.ignore_authentication", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.cache_urls_that_look_dynamic", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_INT,
   "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.enable_default_vary_headers", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.max_open_read_retries", "", INK_INT, "2", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.open_read_retry_time", "", INK_INT, "100", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.max_open_write_retries", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.open_write_retry_time", "", INK_INT, "500", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //       #  when_to_revalidate has 4 options:
  //       #
  //       #  0 - default. use use cache directives or heuristic
  //       #  1 - stale if heuristic
  //       #  2 - always stale (always revalidate)
  //       #  3 - never stale
  //       #
  {CONFIG, "proxy.config.http.cache.when_to_revalidate", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //       # MSIE browsers currently don't send no-cache headers to
  //       # reverse proxies or transparent caches, this variable controls
  //       # when to add no-cache headers to MSIE requests:
  //       #
  //       #  0 - default; no-cache not added to MSIE requests
  //       #  1 - no-cache added to IMS MSIE requests
  //       #  2 - no-cache added to all MSIE requests
  {CONFIG, "proxy.config.http.cache.when_to_add_no_cache_to_msie_requests", "", INK_INT, "0", RU_REREAD, RR_NULL,
   RC_INT, "[0-2]", RA_NULL}
  ,
  //
  //       #  required headers: three options
  //       #
  //       #  0 - No required headers to make document cachable
  //       #  1 - at least, "Last-Modified:" header required
  //       #  2 - explicit lifetime required, "Expires:" or "Cache-Control:"
  //       #
  {CONFIG, "proxy.config.http.cache.required_headers", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.max_stale_age", "", INK_INT, "604800", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.range.lookup", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //        ########################
  //        # heuristic expiration #
  //        ########################
  {CONFIG, "proxy.config.http.cache.heuristic_min_lifetime", "", INK_INT, "3600", RU_REREAD, RR_NULL, RC_STR,
   "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.heuristic_max_lifetime", "", INK_INT, "86400", RU_REREAD, RR_NULL, RC_STR,
   "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.heuristic_lm_factor", "", INK_FLOAT, "0.10", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.guaranteed_min_lifetime", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.guaranteed_max_lifetime", "", INK_INT, "31536000", RU_REREAD, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.fuzz.time", "", INK_INT, "240", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.fuzz.min_time", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.fuzz.probability", "", INK_FLOAT, "0.005", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //        #########################################
  //        # dynamic content & content negotiation #
  //        #########################################
  {CONFIG, "proxy.config.http.cache.vary_default_text", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_STR, ".*", RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.vary_default_images", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_STR, ".*",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.http.cache.vary_default_other", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_STR, ".*", RA_NULL}
  ,
  //        ###################
  //        # Error Reporting #
  //        ###################
  {CONFIG, "proxy.config.http.errors.log_error_pages", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.http.slow.log.threshold", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "^[0-9]+$", RA_NULL}
  ,

  //##############################################################################
  //#
  //# Customizable User Response Pages
  //#
  //##############################################################################
  //# 0 - turn off customizable user response pages
  //# 1 - enable customizable user response pages in only the "default" directory
  //# 2 - enable language-targeted user response pages
  {CONFIG, "proxy.config.body_factory.enable_customizations", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-2]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.body_factory.enable_logging", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.body_factory.template_sets_dir", "", INK_STRING, "etc/trafficserver/body_factory", RU_RESTART_TS,
   RR_NULL, RC_STR, "^[^[:space:]]+$", RA_NULL}
  ,
  //# 0 - never suppress generated responses
  //# 1 - always suppress generated responses
  //# 2 - suppress responses for intercepted traffic
  {CONFIG, "proxy.config.body_factory.response_suppression_mode", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT,
   "[0-2]", RA_NULL}
  ,
  //##############################################################################
  //#
  //# Http Statistics
  //#
  //##############################################################################
  //# Standardized Statistics - transaction stats
  {PROCESS, "proxy.process.http.incoming_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.outgoing_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_hit_fresh", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_hit_revalidated", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_hit_ims", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_hit_stale_served", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_miss_cold", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_miss_changed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_miss_not_cacheable", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_miss_client_no_cache", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_miss_ims", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_read_error", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# Standardized Statistics - dynamic stats
  {PROCESS, "proxy.process.http.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.current_parent_proxy_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //#
  //# The transaction stats
  //#
  {PROCESS, "proxy.process.http.incoming_responses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.invalid_client_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.missing_host_hdr", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.get_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.head_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.trace_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.options_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.post_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.put_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.push_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.delete_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.purge_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.connect_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.extension_method_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.client_no_cache_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.broken_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_lookups", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_writes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_updates", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_deletes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_write_errors", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_read_errors", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tunnels", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.icp_suggested_lookups", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_taxonomy.i0_n0_m0", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_taxonomy.i0_n0_m1", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_taxonomy.i0_n1_m0", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_taxonomy.i0_n1_m1", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_taxonomy.i1_n0_m0", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_taxonomy.i1_n0_m1", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_taxonomy.i1_n1_m0", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_taxonomy.i1_n1_m1", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.client_transaction_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.client_write_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.server_read_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.icp_transaction_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.icp_raw_transaction_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.parent_proxy_transaction_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.parent_proxy_raw_transaction_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.server_transaction_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.server_raw_transaction_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.user_agent_request_header_total_size", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.user_agent_response_header_total_size", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.user_agent_request_document_total_size", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.user_agent_response_document_total_size", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.origin_server_request_header_total_size", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.origin_server_response_header_total_size", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.origin_server_request_document_total_size", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.origin_server_response_document_total_size", "", INK_INT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.parent_proxy_request_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.parent_proxy_response_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.pushed_response_header_total_size", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.pushed_document_total_size", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.response_document_size_100", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.response_document_size_1K", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.response_document_size_3K", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.response_document_size_5K", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.response_document_size_10K", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.response_document_size_1M", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.response_document_size_inf", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_document_size_100", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_document_size_1K", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_document_size_3K", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_document_size_5K", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_document_size_10K", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_document_size_1M", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.request_document_size_inf", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.user_agent_speed_bytes_per_sec_100", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.user_agent_speed_bytes_per_sec_1K", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.user_agent_speed_bytes_per_sec_10K", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.user_agent_speed_bytes_per_sec_100K", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.user_agent_speed_bytes_per_sec_1M", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.user_agent_speed_bytes_per_sec_10M", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.user_agent_speed_bytes_per_sec_100M", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.origin_server_speed_bytes_per_sec_100", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.origin_server_speed_bytes_per_sec_1K", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.origin_server_speed_bytes_per_sec_10K", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.origin_server_speed_bytes_per_sec_100K", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.origin_server_speed_bytes_per_sec_1M", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.origin_server_speed_bytes_per_sec_10M", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.origin_server_speed_bytes_per_sec_100M", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.total_transactions_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.total_transactions_think_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //# Client's perspective stats - counts
  {PROCESS, "proxy.process.http.transaction_counts.hit_fresh", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_counts.hit_revalidated", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_counts.miss_cold", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_counts.miss_changed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_counts.miss_client_no_cache", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_counts.miss_not_cacheable", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_counts.errors.aborts", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_counts.errors.possible_aborts", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_counts.errors.connect_failed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_counts.errors.pre_accept_hangups", "", INK_INT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_counts.errors.empty_hangups", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_counts.errors.early_hangups", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_counts.errors.other", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //# Client's perspective stats - times
  {PROCESS, "proxy.process.http.transaction_totaltime.hit_fresh", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_totaltime.hit_revalidated", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_totaltime.miss_cold", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_totaltime.miss_changed", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_totaltime.miss_client_no_cache", "", INK_FLOAT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_totaltime.miss_not_cacheable", "", INK_FLOAT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_totaltime.errors.aborts", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_totaltime.errors.possible_aborts", "", INK_FLOAT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_totaltime.errors.connect_failed", "", INK_FLOAT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_totaltime.errors.pre_accept_hangups", "", INK_FLOAT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_totaltime.errors.empty_hangups", "", INK_FLOAT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_totaltime.errors.early_hangups", "", INK_FLOAT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.transaction_totaltime.errors.other", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  //# Bandwidth Savings Transaction Stats
  {PROCESS, "proxy.process.http.tcp_hit_count_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_hit_user_agent_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_hit_origin_server_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_miss_count_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_miss_user_agent_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_miss_origin_server_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_expired_miss_count_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_expired_miss_user_agent_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_expired_miss_origin_server_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_refresh_hit_count_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_refresh_hit_user_agent_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_refresh_hit_origin_server_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_refresh_miss_count_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_refresh_miss_user_agent_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_refresh_miss_origin_server_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_client_refresh_count_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_client_refresh_user_agent_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_client_refresh_origin_server_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_ims_hit_count_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_ims_hit_user_agent_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_ims_hit_origin_server_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_ims_miss_count_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_ims_miss_user_agent_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.tcp_ims_miss_origin_server_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.err_client_abort_count_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.err_client_abort_user_agent_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.err_client_abort_origin_server_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.err_connect_fail_count_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.err_connect_fail_user_agent_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.err_connect_fail_origin_server_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.misc_count_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.misc_user_agent_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.misc_origin_server_bytes_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.background_fill_bytes_aborted_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.background_fill_bytes_completed_stat", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  //#
  //# The dynamic stats
  //#
  {PROCESS, "proxy.process.http.background_fill_current_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.current_client_transactions", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.current_parent_proxy_transactions", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.current_icp_transactions", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.current_server_transactions", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.current_parent_proxy_raw_transactions", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.current_icp_raw_transactions", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.current_server_raw_transactions", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.total_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.total_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.total_parent_proxy_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.client_connection_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.parent_proxy_connection_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.http.server_connection_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.cache_connection_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.avg_transactions_per_client_connection", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.avg_transactions_per_server_connection", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.http.avg_transactions_per_parent_connection", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# SOCKS Processor
  //#
  //##############################################################################
  {CONFIG, "proxy.config.socks.socks_needed", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.socks.socks_version", "", INK_INT, "4", RU_RESTART_TS, RR_NULL, RC_INT, "[4-5]", RA_NULL}
  ,
  {CONFIG, "proxy.config.socks.socks_config_file", "", INK_STRING, "socks.config", RU_RESTART_TS, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.socks.socks_timeout", "", INK_INT, "100", RU_RESTART_TS, RR_NULL, RC_STR, "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.socks.server_connect_timeout", "", INK_INT, "10", RU_RESTART_TS, RR_NULL, RC_STR, "^[0-9]+$",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.socks.per_server_connection_attempts", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.socks.connection_attempts", "", INK_INT, "4", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.socks.server_retry_timeout", "", INK_INT, "300", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.socks.default_servers", "", INK_STRING, "", RU_RESTART_TS, RR_NULL, RC_STR,
   "^([^[:space:]]+:[0-9]+;?)*$", RA_NULL}
  ,
  {CONFIG, "proxy.config.socks.server_retry_time", "", INK_INT, "300", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.socks.server_fail_threshold", "", INK_INT, "2", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.socks.accept_enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.socks.accept_port", "", INK_INT, "1080", RU_RESTART_TS, RR_NULL, RC_INT, "[0-65535]", RA_NULL}
  ,
  {CONFIG, "proxy.config.socks.http_port", "", INK_INT, "80", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  {PROCESS, "proxy.process.socks.connections_unsuccessful", "", INK_COUNTER, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.socks.connections_successful", "", INK_COUNTER, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.socks.connections_currently_open", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.socks.proxy.tunneled_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.socks.proxy.http_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# I/O Subsystem
  //#
  //##############################################################################
  {CONFIG, "proxy.config.io.max_buffer_size", "", INK_INT, "32768", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# Net Subsystem
  //#
  //##############################################################################
  {CONFIG, "proxy.config.net.connections_throttle", "", INK_INT, "<connections_throttle>", RU_RESTART_TS, RR_REQUIRED,
   RC_STR, "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.net.throttle_enabled", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.net.max_poll_delay", "", INK_INT, "128", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.listen_backlog", "", INK_INT, "1024", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.accept_throttle", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.tcp_accept_defer_timeout", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "^[0-9]+$",
   RA_NULL}
  ,

  // deprecated configuration options - bcall 4/25/07
  // these should be removed in the future
  {CONFIG, "proxy.config.net.sock_recv_buffer_size", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.sock_send_buffer_size", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.sock_option_flag", "", INK_INT, "0x0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.os_sock_recv_buffer_size", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.os_sock_send_buffer_size", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.os_sock_option_flag", "", INK_INT, "0x0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  // end of deprecated config options

  {CONFIG, "proxy.config.net.sock_recv_buffer_size_in", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.sock_send_buffer_size_in", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.sock_option_flag_in", "", INK_INT, "0x0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.sock_recv_buffer_size_out", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.sock_send_buffer_size_out", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.sock_option_flag_out", "", INK_INT, "0x0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.sock_mss_in", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.nt.main_accept_pool_size", "", INK_INT, "500", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.net.read_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.net.write_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.net.connections_currently_open", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.net.accepts_currently_open", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.net.calls_to_readfromnet", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.net.calls_to_readfromnet_afterpoll", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.net.calls_to_read", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.net.calls_to_read_nodata", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.net.calls_to_writetonet", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.net.calls_to_writetonet_afterpoll", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.net.calls_to_write", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.net.calls_to_write_nodata", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# Cluster Subsystem
  //#
  //##############################################################################
  {CONFIG, "proxy.config.cluster.cluster_port", "", INK_INT, "8086", RU_RESTART_TS, RR_REQUIRED, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.cluster_configuration", "", INK_STRING, "cluster.config", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.ethernet_interface", "", INK_STRING, NULL, RU_RESTART_TS, RR_REQUIRED, RC_STR,
   "^[^[:space:]]*$", RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.enable_monitor", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.monitor_interval_secs", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.send_buffer_size", "", INK_INT, "10485760", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.receive_buffer_size", "", INK_INT, "10485760", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.sock_option_flag", "", INK_INT, "0x0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.rpc_cache_cluster", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##################################################################
  //# Cluster interconnect load monitoring configuration options.
  //# Internal use only
  //##################################################################
  //# load monitor_enabled: -1 = compute only, 0 = disable, 1 = compute and act
  {CONFIG, "proxy.config.cluster.load_monitor_enabled", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.ping_send_interval_msecs", "", INK_INT, "100", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.ping_response_buckets", "", INK_INT, "100", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.msecs_per_ping_response_bucket", "", INK_INT, "50", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.ping_latency_threshold_msecs", "", INK_INT, "500", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.load_compute_interval_msecs", "", INK_INT, "5000", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.periodic_timer_interval_msecs", "", INK_INT, "100", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.ping_history_buf_length", "", INK_INT, "120", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.cluster_load_clear_duration", "", INK_INT, "24", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.cluster.cluster_load_exceed_duration", "", INK_INT, "4", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //##################################################################
  {PROCESS, "proxy.process.cluster.connections_open", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.connections_opened", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.connections_closed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.connections_avg_time", "", INK_FLOAT, "0.00", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.slow_ctrl_msgs_sent", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.connections_locked", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.op_delayed_for_lock", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.reads", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.read_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.writes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.write_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.control_messages_sent", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.control_messages_received", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.control_messages_avg_send_time", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.control_messages_avg_receive_time", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.nodes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.machines_allocated", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.machines_freed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.configuration_changes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.net_backup", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.connections_bumped", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.delayed_reads", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.byte_bank_used", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.alloc_data_news", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.write_bb_mallocs", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.partial_reads", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.partial_writes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.cache_outstanding", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.remote_op_timeouts", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.remote_op_reply_timeouts", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.chan_inuse", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.open_delays", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.open_delay_time", "", INK_FLOAT, "0.00", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.cache_callbacks", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.cache_callback_time", "", INK_FLOAT, "0.00", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.rmt_cache_callbacks", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.rmt_cache_callback_time", "", INK_FLOAT, "0.00", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.lkrmt_cache_callbacks", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.lkrmt_cache_callback_time", "", INK_FLOAT, "0.00", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.thread_steal_expires", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.local_connections_closed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.local_connection_time", "", INK_FLOAT, "0.00", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.remote_connections_closed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.remote_connection_time", "", INK_FLOAT, "0.00", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.rdmsg_assemble_time", "", INK_FLOAT, "0.00", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.cluster_ping_time", "", INK_FLOAT, "0.00", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.setdata_no_clustervc", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.setdata_no_tunnel", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.setdata_no_cachevc", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.setdata_no_cluster", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.vc_write_stall", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.no_remote_space", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.level1_bank", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.multilevel_bank", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.vc_cache_insert_lock_misses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.vc_cache_inserts", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.vc_cache_lookup_lock_misses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.vc_cache_lookup_hits", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.vc_cache_lookup_misses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.vc_cache_scans", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.vc_cache_scan_lock_misses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.vc_cache_purges", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cluster.write_lock_misses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# Nca Subsystem
  //#
  //##############################################################################
#ifdef USE_NCA
  {PROCESS, "proxy.process.nca.upcalls", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.nca.downcalls", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.nca.defered_downcalls", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
#endif
  //##############################################################################
  //#
  //# Cache
  //#
  //##############################################################################
  {CONFIG, "proxy.config.cache.storage_filename", "", INK_STRING, "storage.config", RU_RESTART_TS, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.control.filename", "", INK_STRING, "cache.config", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.ip_allow.filename", "", INK_STRING, "ip_allow.config", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.hosting_filename", "", INK_STRING, "hosting.config", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.partition_filename", "", INK_STRING, "partition.config", RU_RESTART_TS, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.permit.pinning", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.vary_on_user_agent", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //  # 0 - MD5 hash
  //  # 1 - MMH hash
  {CONFIG, "proxy.config.cache.url_hash_method", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //  # default the ram cache size to AUTO_SIZE (-1)
  //  # alternatively: 20971520 (20MB)
  {CONFIG, "proxy.config.cache.ram_cache.size", "", INK_LLONG, "-1", RU_RESTART_TS, RR_NULL, RC_STR, "^-?[0-9]+$",
   RA_NULL}
  ,
  //  # how often should the directory be synced (seconds)
  {CONFIG, "proxy.config.cache.dir.sync_frequency", "", INK_INT, "60", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.hostdb.disable_reverse_lookup", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.select_alternate", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.ram_cache_cutoff", "", INK_LLONG, "1048576", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.ram_cache_mixt_cutoff", "", INK_INT, "1048576", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //  # The maximum number of alternates that are allowed for any given URL.
  //  # It is not possible to strictly enforce this if the variable
  //  #   'proxy.config.cache.vary_on_user_agent' is set to 1.
  //  # (0 disables the maximum number of alts check)
  {CONFIG, "proxy.config.cache.limits.http.max_alts", "", INK_INT, "5", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$", RA_NULL}
  ,
  //  # The maximum size of a document that will be stored in the cache.
  //  # (0 disables the maximum document size check)
  {CONFIG, "proxy.config.net.enable_ink_disk_io", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.ink_disk_io_watermark", "", INK_INT, "400", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.ink_aio_write_threads", "", INK_INT, "10", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.net.max_kqueue_len", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.max_doc_size", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.min_average_object_size", "", INK_INT, "8000", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.max_agg_delay", "", INK_INT, "1000", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.threads_per_disk", "", INK_INT, "4", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.aio_sleep_time", "", INK_INT, "100", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.check_disk_idle", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.agg_write_backlog", "", INK_INT, "5242880", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.enable_checksum", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.alt_rewrite_max_size", "", INK_INT, "4096", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.cache.enable_read_while_writer", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //
  //  #
  //  # UNIMPLEMENTED cache config
  //  #
  //
  //# 0 - MD5 fingerprinting
  //# 1 - MMH fingerprinting
  //# 2 - No fingerprinting
  //# UNIMPLEMENTED CONFIG proxy.config.cache.fingerprint_method INT 2
  //# UNIMPLEMENTED: CONFIG proxy.config.cache.limits.http.quota FLOAT 1.0
  //# UNIMPLEMENTED: CONFIG proxy.config.cache.limits.rtsp.quota FLOAT 1.0
  //
  //  #
  //  # cache stats
  //  #
  //
  {PROCESS, "proxy.process.cache.bytes_used", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.bytes_total", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.ram_cache.bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.ram_cache.hits", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.ram_cache.misses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.lookup.active", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.lookup.success", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.lookup.failure", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.read.active", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.read.success", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.read.failure", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.write.active", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.write.success", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.write.failure", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.write.backlog.failure", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.update.active", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.update.success", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.update.failure", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.remove.active", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.remove.success", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.remove.failure", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.direntries.total", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.direntries.used", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.directory_collision", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.frags_per_doc.1", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.frags_per_doc.2", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.frags_per_doc.3+", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.read_busy.success", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.read_busy.failure", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.vector_marshals", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.hdr_marshals", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.hdr_marshal_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  // Disk Stats
  {PROCESS, "proxy.process.cache.read_per_sec", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.KB_read_per_sec", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.write_per_sec", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.cache.KB_write_per_sec", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# DNS
  //#
  //##############################################################################
  {CONFIG, "proxy.config.dns.lookup_timeout", "", INK_INT, "20", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.dns.retries", "", INK_INT, "5", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.dns.search_default_domains", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.dns.failover_number", "", INK_INT, "5", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.dns.failover_period", "", INK_INT, "60", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.dns.max_dns_in_flight", "", INK_INT, "60", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.dns.splitDNS.enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.dns.splitdns.filename", "", INK_STRING, "splitdns.config", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.dns.splitdns.def_domain", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_STR, "^[^[:space:]]*$",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.dns.url_expansions", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.dns.nameservers", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.dns.round_robin_nameservers", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.dns.total_dns_lookups", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.dns.lookup_avg_time", "", INK_FLOAT, "0.00", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.dns.fail_avg_time", "", INK_FLOAT, "0.00", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.dns.success_avg_time", "", INK_FLOAT, "0.00", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.dns.lookup_successes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.dns.lookup_failures", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.dns.retries", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.dns.max_retries_exceeded", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.dns.in_flight", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# HostDB
  //#
  //##############################################################################
  {CONFIG, "proxy.config.hostdb", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //       # up to 511 characters, may not be changed while running
  {CONFIG, "proxy.config.hostdb.filename", "", INK_STRING, "host.db", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //       # in entries, may not be changed while running
  {CONFIG, "proxy.config.hostdb.size", "", INK_INT, "200000", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.hostdb.storage_path", "", INK_STRING, "etc/trafficserver/internal", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.hostdb.storage_size", "", INK_INT, "33554432", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //       # in minutes (all three)
  //       #  0 = obey, 1 = ignore, 2 = min(X,ttl), 3 = max(X,ttl)
  {CONFIG, "proxy.config.hostdb.ttl_mode", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.hostdb.lookup_timeout", "", INK_INT, "120", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.hostdb.timeout", "", INK_INT, "1440", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.hostdb.verify_after", "", INK_INT, "720", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.hostdb.fail.timeout", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.hostdb.re_dns_on_reload", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.hostdb.serve_stale_for", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //       # move entries to the owner on a lookup?
  {CONFIG, "proxy.config.hostdb.migrate_on_demand", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //       # find DNS results on another node in the cluster?
  {CONFIG, "proxy.config.hostdb.cluster", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //       # find DNS results for round-robin hosts on another node in the cluster?
  {CONFIG, "proxy.config.hostdb.cluster.round_robin", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //       # round-robin addresses for single clients
  //       # (can cause authentication problems)
  {CONFIG, "proxy.config.hostdb.strict_round_robin", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //       # how often should the hostdb be synced (seconds)
  {CONFIG, "proxy.config.cache.hostdb.sync_frequency", "", INK_INT, "60", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.hostdb.total_entries", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.hostdb.total_lookups", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.hostdb.total_hits", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.hostdb.ttl", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.hostdb.ttl_expires", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.hostdb.re_dns_on_reload", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.hostdb.bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##########################################################################
  //#
  //# HTTP
  //#
  //##########################################################################
  //        #######
  //        # SSL #
  //        #######
  {CONFIG, "proxy.config.http.ssl_ports", "", INK_STRING, "443 563", RU_REREAD, RR_NULL, RC_STR,
   "^[[:digit:][:space:]]+$", RA_NULL}
  ,
  //##########################################################################
  //        ###########
  //        # CONNECT #
  //        ###########
  {CONFIG, "proxy.config.http.connect_ports", "", INK_STRING, "443 563", RU_REREAD, RR_NULL, RC_STR,
   "^[[:digit:][:space:]]+$", RA_NULL}
  ,
  //        #########
  //        # Stats #
  //        #########
  //# frequency is in seconds
  {CONFIG, "proxy.config.stats.snap_frequency", "", INK_INT, "60", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.stats.config_file", "", INK_STRING, "stats.config", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  // Jira TS-21
  {CONFIG, "proxy.config.stats.snap_file", "", INK_STRING, "stats.snap", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //        ###########
  //        # Parsing #
  //        ###########
  //##############################################################################
  //#
  //# New Logging Config
  //#
  //##############################################################################
  //# possible values for logging_enabled
  //# 0: no logging at all
  //# 1: log errors only
  //# 2: full logging
  {CONFIG, "proxy.config.log2.logging_enabled", "", INK_INT, "2", RU_REREAD, RR_NULL, RC_INT, "[0-4]", RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.log_buffer_size", "", INK_INT, "9216", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.max_entries_per_buffer", "", INK_INT, "100", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.max_secs_per_buffer", "", INK_INT, "5", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.max_space_mb_for_logs", "", INK_INT, "2000", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.max_space_mb_for_orphan_logs", "", INK_INT, "25", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.max_space_mb_headroom", "", INK_INT, "10", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.hostname", "", INK_STRING, "localhost", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.logfile_dir", "", INK_STRING, "var/log/trafficserver", RU_REREAD, RR_NULL, RC_STR, "^[^[:space:]]+$",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.logfile_perm", "", INK_STRING, "rw-r--r--", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.custom_logs_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.xml_logs_config", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.config_file", "", INK_STRING, "logs.config", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.xml_config_file", "", INK_STRING, "logs_xml.config", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.hosts_config_file", "", INK_STRING, "log_hosts.config", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.squid_log_enabled", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.squid_log_is_ascii", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.squid_log_name", "", INK_STRING, "squid", RU_REREAD, RR_NULL, RC_STR, "^[^[:space:]]*$",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.squid_log_header", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.common_log_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.common_log_is_ascii", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.common_log_name", "", INK_STRING, "common", RU_REREAD, RR_NULL, RC_STR, "^[^[:space:]]*$",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.common_log_header", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.extended_log_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.extended_log_is_ascii", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.extended_log_name", "", INK_STRING, "extended", RU_REREAD, RR_NULL, RC_STR,
   "^[^[:space:]]*$", RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.extended_log_header", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.extended2_log_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.extended2_log_is_ascii", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.extended2_log_name", "", INK_STRING, "extended2", RU_REREAD, RR_NULL, RC_STR,
   "^[^[:space:]]*$", RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.extended2_log_header", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.separate_icp_logs", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.separate_mixt_logs", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.separate_host_logs", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.collation_host", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_STR, "^[^[:space:]]*$",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.collation_port", "", INK_INT, "8085", RU_REREAD, RR_REQUIRED, RC_INT,
   "[0-65535]", RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.collation_secret", "", INK_STRING, "foobar", RU_REREAD, RR_NULL, RC_STR, ".*", RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.collation_host_tagged", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.collation_retry_sec", "", INK_INT, "5", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.collation_max_send_buffers", "", INK_INT, "16", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.rolling_enabled", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_INT, "[0-4]", RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.rolling_interval_sec", "", INK_INT, "86400", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.rolling_offset_hr", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.rolling_size_mb", "", INK_INT, "10", RU_REREAD, RR_NULL, RC_STR, "^0*[1-9][0-9]*$",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.auto_delete_rolled_files", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.sampling_frequency", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.space_used_frequency", "", INK_INT, "2", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.file_stat_frequency", "", INK_INT, "32", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.ascii_buffer_size", "", INK_INT, "36864", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.max_line_size", "", INK_INT, "9216", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.overspill_report_count", "", INK_INT, "500", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  /*                                                                             */
  /* Begin  HCL Modifications.                                                   */
  /*                                                                             */
  {CONFIG, "proxy.config.log2.search_rolling_interval_sec", "", INK_INT, "86400", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.search_log_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.search_server_ip_addr", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.search_server_port", "", INK_INT, "8080", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.search_top_sites", "", INK_INT, "100", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.search_url_filter", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.log2.search_log_filters", "", INK_STRING, NULL, RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  /*                                                                             */
  /* End    HCL Modifications.                                                   */
  /*                                                                             */
  //##############################################################################
  //#
  //# New Logging Stats
  //#
  //##############################################################################
  //# bytes moved
  {PROCESS, "proxy.process.log2.bytes_buffered", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.log2.bytes_written_to_disk", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.log2.bytes_sent_to_network", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.log2.bytes_received_from_network", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //# I/O
  {PROCESS, "proxy.process.log2.log_files_open", "", INK_COUNTER, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.log2.log_files_space_used", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# events, should be COUNTERs
  {PROCESS, "proxy.process.log2.event_log_error", "", INK_COUNTER, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.log2.event_log_access", "", INK_COUNTER, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.log2.event_log_access_fail", "", INK_COUNTER, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.log2.event_log_access_skip", "", INK_COUNTER, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //#
  //# Some resource settings, currently only obeyed by MIXT
  //#
  //##############################################################################
  {CONFIG, "proxy.config.resource.target_maxmem_mb", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-65536]",
   RA_NULL}
  ,

  //##############################################################################
  //#
  //# MIXT WMT Config
  //#
  //##############################################################################
  {CONFIG, "proxy.config.wmt.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.port", "", INK_INT, "1755", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.proxyonly", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.loadhost", "", INK_STRING, "no-load-host-set.no-domain", RU_RESTART_TS, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.loadpath", "", INK_STRING, "no-file", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.chunksize_sec", "", INK_INT, "10", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.asx_rewrite.enabled", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.asx_cache.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.post_wait_time", "", INK_INT, "15", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.redirect.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.tcp_max_backlog_sec", "", INK_INT, "10", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.tcp_backlog_behavior", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.log_per_client", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.debug_level", "", INK_STRING, "info", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.ntlm.domain", "", INK_STRING, "", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.ntlm.host", "", INK_STRING, "", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.max_rexmit_memory", "", INK_INT, "20971520", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.media_bridge.version", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.media_bridge.livehosts", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.media_bridge.name", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.media_bridge.port", "", INK_INT, "10022", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.media_bridge.mount_point", "", INK_STRING, "ffnet", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.media_bridge.monitor.version", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.media_bridge.monitor.livehosts", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.media_bridge.monitor.name", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.media_bridge.monitor.port", "", INK_INT, "10088", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.inactivity_timeout", "", INK_INT, "21600", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.http.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.http.proxyonly", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.log_http_intercept", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.old_splitter_logging", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.loopback.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.file_attribute_mask", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.admin_only_mcast_start", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  // OLD WMT VARIABLES -- to be removed
  {CONFIG, "proxy.config.wmt.prebuffering_ms", "", INK_INT, "5000", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.prebuffering_ms_tcp", "", INK_INT, "200", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.reverse_proxy.oldasxbehavior", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.debug_tags.enabled", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.mem_startdrop_mb", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-65536]", RA_NULL}
  ,
  {CONFIG, "proxy.config.wmt.debug.maxgap", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //##############################################################################
  //#
  //# QuickTime Config
  //#
  //##############################################################################
  {CONFIG, "proxy.config.qt.enabled", "", INK_INT, "0", RU_RESTART_TM, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.mixt.rtsp_proxy_port", "", INK_INT, "554", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.qt.tunnel_rni_req", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.qt.media_bridge.name", "", INK_STRING, NULL, RU_RESTART_TC, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.qt.media_bridge.port", "", INK_INT, "10036", RU_RESTART_TC, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.qt.media_bridge.mount_point", "", INK_STRING, "ffnet", RU_RESTART_TC, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.qt.media_bridge.monitor.name", "", INK_STRING, NULL, RU_RESTART_TC, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.qt.media_bridge.monitor.port", "", INK_INT, "10088", RU_RESTART_TC, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.qt.live_splitter.enabled", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.qt.digest_masquerade.enabled", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# RNI Config
  //#
  //##############################################################################
  {CONFIG, "proxy.config.rni.enabled", "", INK_INT, "0", RU_RESTART_TM, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.rni.verbosity", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.rni.cache_port", "", INK_INT, "7802", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.rni.control_port", "", INK_INT, "7803", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.rni.auth_port", "", INK_INT, "7808", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.rni.upstream_cache_port", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.rni.proxy_cache_dir", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //# Following used with Traffic COP
  {CONFIG, "proxy.config.rni.watcher_enabled", "", INK_INT, "0", RU_RESTART_TC, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.rni.proxy_port", "", INK_INT, "9231", RU_RESTART_TC, RR_NULL, RC_INT, "[0-65535]", RA_NULL}
  ,
  {CONFIG, "proxy.config.rni.proxy_pid_path", "", INK_STRING, NULL, RU_RESTART_TC, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.rni.proxy_restart_cmd", "", INK_STRING, NULL, RU_RESTART_TC, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.rni.proxy_restart_interval", "", INK_INT, "20", RU_RESTART_TC, RR_NULL, RC_INT, "[10-3600]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.rni.proxy_service_name", "", INK_STRING, "RMProxy", RU_RESTART_TC, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.rni.rpass_watcher_enabled", "", INK_INT, "0", RU_RESTART_TC, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.rni.rpass_restart_cmd", "", INK_STRING, NULL, RU_RESTART_TC, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //##############################################################################
  //#
  //# RNI Stats
  //#
  //##############################################################################
  //# Basic stats
  {PROCESS, "proxy.process.rni.object_count", "", INK_COUNTER, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.block_hit_count", "", INK_COUNTER, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.block_miss_count", "", INK_COUNTER, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.byte_hit_sum", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.byte_miss_sum", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.time_hit_sum", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.time_miss_sum", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# Cross-protocol stats for the dashboard/node pages
  //#
  //# Downstream = to/from user agent
  //# Upstream = to/from origin server
  {PROCESS, "proxy.process.rni.downstream_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.downstream.request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.downstream.response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.upstream_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.upstream.request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.upstream.response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.errors.aborts", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.server.errors.connect_failed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.rni.server.errors.other", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# Derivable RNI stats
  //#
  //# total_blocks_served = block_hit_count + block_miss_count
  //# total_bytes_served = byte_hit_sum + byte_miss_sum
  //# total_time_spent = time_hit_sum + time_miss_sum
  //# ave_blocks_per_object = total_blocks_served / object_count
  //# ave_hit_time = time_hit_sum / block_hit_count
  //# ave_miss_time = time_miss_sum / block_miss_count
  //# bandwidth_savings = time_hit_sum / total_time_spent
  //# ...
  //# WMT
  {PROCESS, "proxy.process.wmt.downstream.request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.downstream.response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.upstream.request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.upstream.response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.unique_live_streams", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.current_unique_live_streams", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.downstream_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.upstream_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.current_unique_live_streams", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.object_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.block_hit_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.block_miss_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.byte_hit_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.byte_miss_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.server.errors.connect_failed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.server.errors.aborts", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# WMT over HTTP
  {PROCESS, "proxy.process.wmt.http.downstream.request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.http.downstream.response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.http.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.multicast.current_streams", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.multicast.downstream.bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.seeks", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.fast_forward", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.rewind", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.push.files_pushed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.push.bytes_pushed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.wmt.push.errors", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //# QT
  {PROCESS, "proxy.process.qt.downstream.request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.downstream.response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.upstream.request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.upstream.response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.unique_live_streams", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.current_unique_live_streams", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.client.server_bytes_read", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.client.cache_bytes_read", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.downstream_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.upstream_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.object_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.block_hit_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.block_miss_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.byte_hit_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.byte_miss_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.server.errors.connect_failed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.qt.server.errors.aborts", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //# MPEG4
  {PROCESS, "proxy.process.mpeg4.downstream.request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.downstream.response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.upstream.request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.upstream.response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.unique_live_streams", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.current_unique_live_streams", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.client.server_bytes_read", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.client.cache_bytes_read", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.downstream_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.upstream_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.object_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.block_hit_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.block_miss_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.byte_hit_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.byte_miss_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.mpeg4.server.errors.connect_failed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,

//##############################################################################
  //#
  //# MIXT MP3 Config
  //#
  //##############################################################################
  {CONFIG, "proxy.config.mixt.mp3.enabled", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.mixt.mp3.port", "", INK_INT, "8000", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //##############################################################################
  //#
  //# MIXT Push Config
  //#
  //##############################################################################
  {CONFIG, "proxy.config.mixt.push.enabled", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.mixt.push.port", "", INK_INT, "1900", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.mixt.push.verbosity", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.mixt.push.password", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# MIXT WMTMCast Config
  //#
  //##############################################################################
  {CONFIG, "proxy.config.mixt.wmtmcast.enabled", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# AAA Config
  //#
  //##############################################################################
  {CONFIG, "proxy.config.aaa.billing.reporting_interval", "", INK_INT, "300", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.aaa.billing.machine_name", "", INK_STRING, "wire-dev02.example.com", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.aaa.billing.install_directory", "", INK_STRING, "/opt/portal/6.0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.aaa.billing.event_file_location", "", INK_STRING, "/export/crawlspace/workareas/pin/test",
   RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# AAA Hash table
  //#
  //##############################################################################
  {CONFIG, "proxy.config.aaa.hashtable.size", "", INK_INT, "127", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# AAA Radius Plugin Configurations
  //#
  //##############################################################################
  {CONFIG, "proxy.config.aaa.radius.auth_port", "", INK_INT, "1812", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.aaa.radius.acct_port", "", INK_INT, "1813", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.aaa.radius.is_proxy", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.aaa.radius.radius_server_ip", "", INK_STRING, "cachedev.example.com", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.aaa.radius.radius_server_auth_port", "", INK_INT, "1812", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.aaa.radius.radius_server_acct_port", "", INK_INT, "1813", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.aaa.radius.radius_server_key", "", INK_STRING, "npdev-cachedev", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.aaa.radius.max_retries", "", INK_INT, "3", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.aaa.radius.min_timeout", "", INK_INT, "10", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.aaa.radius.database_path", "", INK_STRING, "etc/trafficserver/plugins/aaa/raddb", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.aaa.radius.log_path", "", INK_STRING, "var/log/trafficserver", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //##############################################################################
  //#
  //# Raft Config
  //#
  //##############################################################################
  {CONFIG, "proxy.config.raft.enabled", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.raft.accept_port", "", INK_INT, "3025", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.raft.server_port", "", INK_INT, "3025", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.raft.proxy_version_min", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.raft.proxy_version_max", "", INK_INT, "268435456", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# Content Filtering
  //#
  //##############################################################################
  {CONFIG, "proxy.config.content_filter.filename", "", INK_STRING, "filter.config", RU_RESTART_TS, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  //##############################################################################
  //#
  //# Reverse Proxy
  //#
  //##############################################################################
  {CONFIG, "proxy.config.reverse_proxy.enabled", "", INK_INT, "<accel_enable>", RU_REREAD, RR_REQUIRED, RC_INT, "[0-1]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.url_remap.default_to_server_pac", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.url_remap.default_to_server_pac_port", "", INK_INT, "-1", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.url_remap.filename", "", INK_STRING, "remap.config", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.url_remap.remap_required", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.url_remap.pristine_host_hdr", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  // url remap mode
  // # 0 - same as URL_REMAP_ALL (instead of disabling all remapping)
  // # 1 - URL_REMAP_ALL remap url's of all requests
  // # 2 - URL_REMAP_FOR_OS remap url's for requests to OS's only
  {CONFIG, "proxy.config.url_remap.url_remap_mode", "", INK_INT, "1", RU_RESTART_TS, RR_NULL, RC_INT, "[0-2]", RA_NULL}
  ,
  //##############################################################################
  //#
  //# SSL Termination
  //#
  //##############################################################################
  {CONFIG, "proxy.config.ssl.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.accelerator_required", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.accelerator.type", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.number.threads", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.atalla.lib.path", "", INK_STRING, "/opt/atalla/lib", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.ncipher.lib.path", "", INK_STRING, "/opt/nfast/toolkits/hwcrhk", RU_RESTART_TS, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.cswift.lib.path", "", INK_STRING, "/usr/lib", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.broadcom.lib.path", "", INK_STRING, "/usr/lib", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.server_port", "", INK_INT, "4443", RU_RESTART_TS, RR_NULL, RC_INT, "[0-65535]", RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.client.certification_level", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-2]",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.server.cert.filename", "", INK_STRING, "server.pem", RU_RESTART_TS, RR_NULL, RC_STR,
   "^[^[:space:]]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.server.cert.path", "", INK_STRING, "etc/trafficserver", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.server.cert_chain.filename", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_STR, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.server.multicert.filename", "", INK_STRING, "ssl_multicert.config", RU_RESTART_TS, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.server.private_key.filename", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_STR,
   "^[^[:space:]]*$", RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.server.private_key.path", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.CA.cert.filename", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_STR, "^[^[:space:]]*$",
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.CA.cert.path", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.client.verify.server", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.client.cert.filename", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_STR,
   "^[^[:space:]]*$", RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.client.cert.path", "", INK_STRING, "etc/trafficserver", RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.client.private_key.filename", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_STR,
   "^[^[:space:]]*$", RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.client.private_key.path", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.client.CA.cert.filename", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_STR,
   "^[^[:space:]]*$", RA_NULL}
  ,
  {CONFIG, "proxy.config.ssl.client.CA.cert.path", "", INK_STRING, NULL, RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //# ICP Configuration
  //##############################################################################
  //#       enabled=0 ICP disabled
  //#       enabled=1 Allow receive of ICP queries
  //#       enabled=2 Allow send/receive of ICP queries
  //##############################################################################
  {CONFIG, "proxy.config.icp.enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-2]", RA_NULL}
  ,
  {CONFIG, "proxy.config.icp.stale_icp_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.icp.icp_interface", "", INK_STRING, NULL, RU_RESTART_TS, RR_REQUIRED, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.icp.icp_port", "", INK_INT, "3130", RU_REREAD, RR_NULL, RC_INT, "[0-65535]", RA_NULL}
  ,
  {CONFIG, "proxy.config.icp.multicast_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.icp.query_timeout", "", INK_INT, "2", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.icp.icp_configuration", "", INK_STRING, "icp.config", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.icp.lookup_local", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.icp.reply_to_unknown_peer", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.icp.default_reply_port", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.config_mgmt_callouts", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.reconfig_polls", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.reconfig_events", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.invalid_poll_data", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.icp_incoming_nolock", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.no_data_read", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.short_read", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.invalid_sender", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.read_not_v2_icp", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.icp_remote_query_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.icp_remote_responses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.cache_lookup_success", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.cache_lookup_fail", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.query_response_write", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.query_response_partial_write", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.no_icp_request_for_response", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.icp_response_request_nolock", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.icp_response_not_active_nolock", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.icp_start_icpoff", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.icp_start_nolock", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.send_query_partial_write", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.icp_queries_no_expected_replies", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.icp_not_active_nolock", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.icp_query_hits", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.icp_query_misses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.invalid_icp_query_response", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.icp_query_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.total_icp_response_time", "", INK_FLOAT, "0.00", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.total_udp_send_queries", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.icp.total_icp_request_time", "", INK_FLOAT, "0.00", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //# Scheduled Update Configuration
  //##############################################################################
  {CONFIG, "proxy.config.update.enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.update.update_configuration", "", INK_STRING, "update.config", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.update.force", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.update.retry_count", "", INK_INT, "10", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.update.retry_interval", "", INK_INT, "2", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.update.concurrent_updates", "", INK_INT, "100", RU_REREAD, RR_NULL, RC_STR, "^[0-9]+$", RA_NULL}
  ,
  {CONFIG, "proxy.config.update.max_update_state_machines", "", INK_INT, "500", RU_REREAD, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.update.memory_use_mb", "", INK_INT, "50", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.update.successes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.update.no_actions", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.update.fails", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.update.unknown_status", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {PROCESS, "proxy.process.update.state_machines", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //##############################################################################
  //# SNMP Configuration
  //##############################################################################
  //# Start MIB-II Host MIB (TCP/UDP stats, etc.) and traffic server MIB
  {CONFIG, "proxy.config.snmp.master_agent_enabled", "", INK_INT, "1", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.snmp.snmp_encap_enabled", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.snmp.trap_message", "", INK_STRING, "Traffic server trap", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,

  //##############################################################################
  //# Plug-in Configuration
  //##############################################################################
  //# Directory in which to find plugins
  {CONFIG, "proxy.config.plugin.plugin_dir", "", INK_STRING, "libexec/trafficserver", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.plugin.plugin_mgmt_dir", "", INK_STRING, "etc/trafficserver/plugins_mgmt", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.plugin.extensions_dir", "", INK_STRING,
   "var/trafficserver", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //##############################################################################
  //#
  //# Remote Access Framework (RAF) config - (for integration into SF test harness)
  //#
  //# ** TM raf is *NOW* disabled by default with port 20098 (see am-1/tcl/common/defaults.tcl)
  //##############################################################################
  {CONFIG, "proxy.config.raf.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.raf.port", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.raf.manager.enabled", "", INK_INT, "0", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.raf.manager.port", "", INK_INT, "20098", RU_RESTART_TS, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //##############################################################################
  //#
  //# lm.config items
  //#
  //##############################################################################

  //##############################################################################
  //#
  //# Local Manager Specific Records File
  //#
  //# <RECORD-TYPE> <NAME> <TYPE> <VALUE (till end of line)>
  //#
  //# Add NODE       Records Here
  //##############################################################################
  {NODE, "proxy.node.num_processes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.hostname_FQ", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.hostname", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.xact_scale", "", INK_INT, "200", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //#
  //# Restart Stats
  //#
  {NODE, "proxy.node.restarts.manager.start_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.restarts.proxy.start_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.restarts.proxy.stop_time", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.restarts.proxy.restart_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //#
  //# Manager Version Info
  //#
  {NODE, "proxy.node.version.manager.short", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.version.manager.long", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.version.manager.build_number", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.version.manager.build_time", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.version.manager.build_date", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.version.manager.build_machine", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.version.manager.build_person", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //#
  //# SSL parent proxying info
  //#
  //# Set the value of this variable to 1 if this node
  //#  is also the default parent for all ssl requests
  //#  in a cluster. Setting the value to 1 will prevent
  //#  SSL requests from this proxy to a parent from
  //#  self-looping.
  //#
  {LOCAL, "proxy.local.http.parent_proxy.disable_connect_tunneling", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //#
  //# Dash Board Stats
  //#
  //# Valid Per Node - Can Not Sum For Cluster Stats
  //#
  {NODE, "proxy.node.http.cache_hit_ratio", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.cache_hit_ratio_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.bandwidth_hit_ratio", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.bandwidth_hit_ratio_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.bandwidth_hit_ratio", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.bandwidth_hit_ratio_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.hostdb.hit_ratio", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.hostdb.hit_ratio_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.proxy_running", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.cache.percent_free", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.cache_hit_ratio", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.cache_hit_ratio_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.cache_hit_ratio_avg_10s_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.bandwidth_hit_ratio_avg_10s_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //#
  //# Node page 5 minute average stats
  //# Valid Per Node - Can Not Sum For Cluster Stats
  //#
  {NODE, "proxy.node.bandwidth_hit_ratio_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.cache_hit_fresh_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.cache_hit_revalidated_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.cache_hit_ims_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.cache_hit_stale_served_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.rni.block_hit_count_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.block_hit_count_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.qt.block_hit_count_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.mpeg4.block_hit_count_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.cache_miss_cold_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.cache_miss_changed_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.cache_miss_not_cacheable_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.cache_miss_client_no_cache_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.cache_miss_ims_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.cache_read_error_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.rni.block_miss_count_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.block_miss_count_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.qt.block_miss_count_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.mpeg4.block_miss_count_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.cache_total_hits_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.cache_total_misses_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.cache_hit_ratio_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.hostdb.total_lookups_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.hostdb.total_hits_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.hostdb.hit_ratio_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# Protocol Stats
  //#
  //# Valid Per Node - Can Not Sum For Cluster Stats
  //#
  {NODE, "proxy.node.http.transaction_counts_avg_10s.hit_fresh", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_counts_avg_10s.hit_revalidated", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_counts_avg_10s.miss_cold", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_counts_avg_10s.miss_not_cacheable", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_counts_avg_10s.miss_changed", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_counts_avg_10s.miss_client_no_cache", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_counts_avg_10s.errors.connect_failed", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_counts_avg_10s.errors.aborts", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_counts_avg_10s.errors.possible_aborts", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_counts_avg_10s.errors.pre_accept_hangups", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_counts_avg_10s.errors.early_hangups", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_counts_avg_10s.errors.empty_hangups", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_counts_avg_10s.errors.other", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_counts_avg_10s.other.unclassified", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.hit_fresh", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.hit_revalidated", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.miss_cold", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.miss_not_cacheable", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.miss_changed", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.miss_client_no_cache", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.errors.connect_failed", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.errors.aborts", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.errors.possible_aborts", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.errors.pre_accept_hangups", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.errors.early_hangups", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.errors.empty_hangups", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.errors.other", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.other.unclassified", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.hit_fresh_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.hit_revalidated_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.miss_cold_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.miss_not_cacheable_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.miss_changed_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.miss_client_no_cache_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.errors.connect_failed_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.errors.aborts_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.errors.possible_aborts_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.errors.pre_accept_hangups_int_pct", "", INK_INT, "0", RU_NULL,
   RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.errors.early_hangups_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.errors.empty_hangups_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.errors.other_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_frac_avg_10s.other.unclassified_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_msec_avg_10s.hit_fresh", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_msec_avg_10s.hit_revalidated", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_msec_avg_10s.miss_cold", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_msec_avg_10s.miss_not_cacheable", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_msec_avg_10s.miss_changed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_msec_avg_10s.miss_client_no_cache", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_msec_avg_10s.errors.connect_failed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_msec_avg_10s.errors.aborts", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_msec_avg_10s.errors.possible_aborts", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_msec_avg_10s.errors.pre_accept_hangups", "", INK_INT, "0", RU_NULL, RR_NULL,
   RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_msec_avg_10s.errors.early_hangups", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_msec_avg_10s.errors.empty_hangups", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_msec_avg_10s.errors.other", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.transaction_msec_avg_10s.other.unclassified", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  //#
  //# Can Sum For Cluster Stats
  //#
  //
  //# HTTP
  {NODE, "proxy.node.http.throughput", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.user_agent_xacts_per_second", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.user_agent_current_connections_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.user_agent_total_request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.user_agent_total_response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.user_agents_total_transactions_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.user_agents_total_documents_served", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.origin_server_current_connections_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.origin_server_total_request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.origin_server_total_response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.parent_proxy_total_request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.parent_proxy_total_response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.origin_server_total_transactions_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.http.cache_current_connections_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.current_parent_proxy_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.cache_total_hits", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.http.cache_total_misses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //#RNI
  {NODE, "proxy.node.rni.user_agents_total_documents_served", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.rni.user_agent_xacts_per_second", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.rni.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.rni.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.rni.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.rni.downstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.rni.upstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //#WMT
  {NODE, "proxy.node.wmt.user_agents_total_documents_served", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.user_agent_xacts_per_second", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.downstream_request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.downstream_response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.upstream_request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.upstream_response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.downstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.upstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.total_current_unique_streams_ondemand", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.wmt.total_unique_streams_ondemand", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.downstream.total_request_bytes_ondemand", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.wmt.downstream.total_response_bytes_ondemand", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.wmt.total_current_unique_streams_live", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.total_unique_streams_live", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.downstream.total_request_bytes_live", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.wmt.downstream.total_response_bytes_live", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.wmt.current_server_connections_mmst", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.server_connections_mmst", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.upstream.request_bytes_mmst", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.upstream.response_bytes_mmst", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.ondemand.byte_hit_ratio", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.total.byte_hit_ratio", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.wmt.ondemand.byte_hit_ratio_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.wmt.total.byte_hit_ratio_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //#QT
  {NODE, "proxy.node.qt.user_agents_total_documents_served", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.qt.user_agent_xacts_per_second", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.qt.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.qt.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.qt.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.qt.downstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.qt.upstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //#MPEG4
  {NODE, "proxy.node.mpeg4.user_agents_total_documents_served", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {NODE, "proxy.node.mpeg4.user_agent_xacts_per_second", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.mpeg4.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.mpeg4.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.mpeg4.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.mpeg4.downstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.mpeg4.upstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# Cache
  {NODE, "proxy.node.cache.contents.num_docs", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.cache.bytes_total", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.cache.bytes_total_mb", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.cache.bytes_free", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.cache.bytes_free_mb", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.cache.percent_free_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.cache_total_hits", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.cache_total_misses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# DNS
  {NODE, "proxy.node.dns.lookups_per_second", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.dns.lookup_avg_time_ms", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.dns.total_dns_lookups", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# HostDB
  {NODE, "proxy.node.hostdb.total_lookups", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.hostdb.total_hits", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# Cluster
  {NODE, "proxy.node.cluster.nodes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# Overall
  {NODE, "proxy.node.client_throughput_out", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.client_throughput_out_kbit", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.user_agent_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.origin_server_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.user_agent_total_bytes_avg_10s", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.origin_server_total_bytes_avg_10s", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.user_agent_xacts_per_second", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.node.user_agents_total_documents_served", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  // # Remap WMT rewrite value to old WMT value
  {NODE, "proxy.process.wmt.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.process.wmt.downstream_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.process.wmt.downstream.request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.process.wmt.downstream.response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.process.wmt.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.process.wmt.upstream_requests", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.process.wmt.upstream.request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {NODE, "proxy.process.wmt.upstream.response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //#
  //# Add CLUSTER    Records Here
  //#
  //# For Node page
  {CLUSTER, "proxy.cluster.user_agent_total_bytes_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.origin_server_total_bytes_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.bandwidth_hit_ratio", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.bandwidth_hit_ratio_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.bandwidth_hit_ratio_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# HTTP
  {CLUSTER, "proxy.cluster.http.throughput", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.cache_hit_ratio", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.cache_hit_ratio_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.cache_total_hits", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.cache_total_misses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.bandwidth_hit_ratio", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.bandwidth_hit_ratio_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.user_agent_xacts_per_second", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.user_agent_current_connections_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.user_agent_total_request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.user_agent_total_response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.user_agents_total_transactions_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.user_agents_total_documents_served", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.origin_server_current_connections_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.origin_server_total_request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.origin_server_total_response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.origin_server_total_transactions_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.cache_current_connections_count", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.current_parent_proxy_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.parent_proxy_total_request_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.http.parent_proxy_total_response_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //#RNI
  {CLUSTER, "proxy.cluster.rni.user_agent_xacts_per_second", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.rni.user_agents_total_documents_served", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.rni.upstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.rni.downstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.rni.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.rni.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.rni.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //#WMT
  {CLUSTER, "proxy.cluster.wmt.user_agent_xacts_per_second", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.user_agents_total_documents_served", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.upstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.downstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.total_current_unique_streams_ondemand", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.total_unique_streams_ondemand", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.downstream.total_request_bytes_ondemand", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.downstream.total_response_bytes_ondemand", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL,
   NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.total_current_unique_streams_live", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.total_unique_streams_live", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.downstream.total_request_bytes_live", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.downstream.total_response_bytes_live", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.current_server_connections_mmst", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.server_connections_mmst", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.upstream.request_bytes_mmst", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.upstream.response_bytes_mmst", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.ondemand.byte_hit_ratio", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.total.byte_hit_ratio", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.ondemand.byte_hit_ratio_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.wmt.total.byte_hit_ratio_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  //#QT
  {CLUSTER, "proxy.cluster.qt.user_agent_xacts_per_second", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.qt.user_agents_total_documents_served", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.qt.upstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.qt.downstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.qt.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.qt.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.qt.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //#MPEG4
  {CLUSTER, "proxy.cluster.mpeg4.user_agent_xacts_per_second", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.mpeg4.user_agents_total_documents_served", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.mpeg4.upstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.mpeg4.downstream_total_bytes", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.mpeg4.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.mpeg4.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.mpeg4.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# Cache
  {CLUSTER, "proxy.cluster.cache.contents.num_docs", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.cache.bytes_free", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.cache.bytes_free_mb", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.cache.percent_free", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.cache.percent_free_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.cache.bytes_free_mb", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.cache_hit_ratio", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.cache_hit_ratio_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.cache_total_hits", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.cache_total_misses", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.current_cache_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.cache_total_hits_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.cache_total_misses_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.cache_hit_ratio_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# DNS
  {CLUSTER, "proxy.cluster.dns.lookups_per_second", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.dns.total_dns_lookups", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# HostDB
  {CLUSTER, "proxy.cluster.hostdb.hit_ratio", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.hostdb.hit_ratio_int_pct", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.hostdb.total_lookups_avg_10s", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.hostdb.total_hits_avg_10s", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.hostdb.hit_ratio_avg_10s", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# Overall
  {CLUSTER, "proxy.cluster.user_agent_xacts_per_second", "", INK_FLOAT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.client_throughput_out", "", INK_FLOAT, "0.0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.client_throughput_out_kbit", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.current_client_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CLUSTER, "proxy.cluster.current_server_connections", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  //# Add LOCAL Records Here
  {LOCAL, "proxy.local.incoming_ip_to_bind", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {LOCAL, "proxy.local.outgoing_ip_to_bind", "", INK_STRING, NULL, RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {LOCAL, "proxy.local.log2.collation_mode", "", INK_INT, "0", RU_REREAD, RR_NULL, RC_INT, "[0-4]", RA_NULL}
  ,

  //#Prefetch configuration
  {CONFIG, "proxy.config.prefetch.prefetch_enabled", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.prefetch.child_port", "", INK_INT, "39679", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.prefetch.config_file", "", INK_STRING, "prefetch.config", RU_NULL, RR_NULL, RC_NULL, NULL,
   RA_NULL}
  ,
  {CONFIG, "proxy.config.prefetch.url_buffer_size", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.prefetch.url_buffer_timeout", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.prefetch.keepalive_timeout", "", INK_INT, "900", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.prefetch.push_cached_objects", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.prefetch.default_url_proto", "", INK_STRING, "tcp", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.prefetch.default_data_proto", "", INK_STRING, "tcp", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.prefetch.max_object_size", "", INK_INT, "1000000000", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.prefetch.max_recursion", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.prefetch.redirection", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  // StatSystemV2 config
  {CONFIG, "proxy.config.stat_collector.interval", "", INK_INT, "600", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL},
  {CONFIG, "proxy.config.stat_collector.port", "", INK_INT, "8091", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL},
  {CONFIG, "proxy.config.stat_systemV2.max_stats_allowed", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL},
  {CONFIG, "proxy.config.stat_systemV2.num_stats_estimate", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL},

  //############
  //#
  //# Eric's super cool remap processor
  //#
  //############
  {CONFIG, "proxy.config.remap.use_remap_processor", "", INK_INT, "0", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,
  {CONFIG, "proxy.config.remap.num_remap_threads", "", INK_INT, "1", RU_NULL, RR_NULL, RC_NULL, NULL, RA_NULL}
  ,

  //#DNS_cache configuration
  {CONFIG, "proxy.config.dns.proxy.enabled", "", INK_INT, "0", RU_RESTART_TM, RR_NULL, RC_INT, "[0-1]", RA_NULL}
  ,
  {CONFIG, "proxy.config.dns.proxy_port", "", INK_INT, "53", RU_RESTART_TM, RR_NULL, RC_INT, "[0-65535]", RA_NULL}
  ,
  //##############################################################################
  //#
  //# The End
  //#
  //##############################################################################

  {0, 0, 0, INVALID, 0, RU_NULL, RR_NULL}
};

//-------------------------------------------------------------------------
// RecordsConfigIndex
//-------------------------------------------------------------------------
// remove this when librecords is done.

MgmtHashTable *RecordsConfigIndex = NULL;

//-------------------------------------------------------------------------
// RecordsConfigInit
//-------------------------------------------------------------------------
// remove this when librecords is done.
void
RecordsConfigInit()
{
  int r;
  RecordsConfigIndex = NEW(new MgmtHashTable("records_config_index", false, InkHashTableKeyType_String));
  for (r = 0; RecordsConfig[r].value_type != INVALID; r++) {
    RecordsConfigIndex->mgmt_hash_table_insert(RecordsConfig[r].name, (void *)(intptr_t)r);
  }
}

#ifndef FOR_INSTALL

//-------------------------------------------------------------------------
// LibRecordsConfigInit
//-------------------------------------------------------------------------
void
LibRecordsConfigInit()
{

  // IGNORE: RecordsConfig[r].required
  // IGNORE: RecordsConfig[r].description

  int r = 0;
  RecInt tempInt = 0;
  int64 tempLLong = 0;
  RecFloat tempFloat = 0.0;
  RecCounter tempCounter = 0;

  enum RecT type = RECT_NULL;
  enum RecUpdateT update = RECU_NULL;
  enum RecCheckT check = RECC_NULL;
  enum RecAccessT access = RECA_NULL;

  for (r = 0; RecordsConfig[r].value_type != INVALID; r++) {

    if (RecordsConfig[r].type != CONFIG) {
      // flag this error when librecords migration is
      // completed
      /*
         mgmt_log(stderr, "[rec] %s is not of CONFIG type.\n",
         RecordsConfig[r].name);
       */
      //      continue;
    }

    switch (RecordsConfig[r].type) {
    case CONFIG:
      type = RECT_CONFIG;
      break;
    case PROCESS:
      type = RECT_PROCESS;
      break;
    case NODE:
      type = RECT_NODE;
      break;
    case CLUSTER:
      type = RECT_CLUSTER;
      break;
    case LOCAL:
      type = RECT_LOCAL;
      break;
    default:
      ink_debug_assert(true);
    }

    // RU_XXX -> RECU_XXX
    switch (RecordsConfig[r].update) {
    case RU_NULL:
      update = RECU_NULL;
      break;
    case RU_REREAD:
      update = RECU_DYNAMIC;
      break;
    case RU_RESTART_TS:
      update = RECU_RESTART_TS;
      break;
    case RU_RESTART_TM:
      update = RECU_RESTART_TM;
      break;
    case RU_RESTART_TC:
      update = RECU_RESTART_TC;
      break;
    default:
      ink_debug_assert(true);
      break;
    }

    // RC_XXX -> RECC_XXX
    switch (RecordsConfig[r].check) {
    case RC_NULL:
      check = RECC_NULL;
      break;
    case RC_STR:
      check = RECC_STR;
      break;
    case RC_INT:
      check = RECC_INT;
      break;
    case RC_IP:
      check = RECC_IP;
      break;
    default:
      ink_debug_assert(true);
      break;
    }

    // RA_XXX -> RECA_XXX
    switch (RecordsConfig[r].access) {
    case RA_NULL:
      access = RECA_NULL;
      break;
    case RA_NO_ACCESS:
      access = RECA_NO_ACCESS;
      break;
    case RA_READ_ONLY:
      access = RECA_READ_ONLY;
      break;
    default:
      ink_debug_assert(true);
      break;
    }

    if (type == RECT_CONFIG || type == RECT_LOCAL) {
      switch (RecordsConfig[r].value_type) {

      case INK_INT:
        tempInt = (RecInt) ink_atoi64(RecordsConfig[r].value);
        RecRegisterConfigInt(type, RecordsConfig[r].name, tempInt, update, check, RecordsConfig[r].regex, access);
        break;

      case INK_LLONG:
        tempLLong = (RecInt) ink_atoi64(RecordsConfig[r].value);
        RecRegisterConfigLLong(type, RecordsConfig[r].name, tempLLong, update, check, RecordsConfig[r].regex, access);
        break;

      case INK_FLOAT:
        tempFloat = (RecFloat) atof(RecordsConfig[r].value);
        RecRegisterConfigFloat(type, RecordsConfig[r].name, tempFloat, update, check, RecordsConfig[r].regex, access);
        break;

      case INK_STRING:
        RecRegisterConfigString(type,
                                RecordsConfig[r].name,
                                RecordsConfig[r].value, update, check, RecordsConfig[r].regex, access);
        break;

      case INK_COUNTER:
        tempCounter = (RecCounter) ink_atoi64(RecordsConfig[r].value);
        RecRegisterConfigCounter(type,
                                 RecordsConfig[r].name, tempCounter, update, check, RecordsConfig[r].regex, access);
        break;

      default:
        ink_debug_assert(true);
        break;

      }                         // switch
    } else if (RecordsConfig[r].type != PROCESS) {
      switch (RecordsConfig[r].value_type) {

      case INK_INT:
        tempInt = (RecInt) ink_atoi64(RecordsConfig[r].value);
        RecRegisterStatInt(type, RecordsConfig[r].name, tempInt, RECP_NON_PERSISTENT);
        break;

      case INK_LLONG:
        tempLLong = (RecLLong) ink_atoi64(RecordsConfig[r].value);
        RecRegisterStatLLong(type, RecordsConfig[r].name, tempLLong, RECP_NON_PERSISTENT);
        break;

      case INK_FLOAT:
        tempFloat = (RecFloat) atof(RecordsConfig[r].value);
        RecRegisterStatFloat(type, RecordsConfig[r].name, tempFloat, RECP_NON_PERSISTENT);
        break;

      case INK_STRING:
        RecRegisterStatString(type, RecordsConfig[r].name, (RecString)RecordsConfig[r].value, RECP_NON_PERSISTENT);
        break;

      case INK_COUNTER:
        tempCounter = (RecCounter) ink_atoi64(RecordsConfig[r].value);
        RecRegisterStatCounter(type, RecordsConfig[r].name, tempCounter, RECP_NON_PERSISTENT);
        break;

      default:
        ink_debug_assert(true);
        break;

      }                         // switch
    }


  }

  // test_librecords();

}

void
test_librecords()
{

  RecRegisterStatInt(RECT_PROCESS, "proxy.process.librecords.testing.int", (RecInt) 100, RECP_NON_PERSISTENT);

  RecRegisterStatFloat(RECT_NODE, "proxy.node.librecords.testing.float", (RecFloat) 100.1, RECP_NON_PERSISTENT);

  RecRegisterStatString(RECT_CLUSTER,
                        "proxy.cluster.librecords.testing.string", (RecString) "Hello World\n", RECP_NON_PERSISTENT);

  RecRegisterStatCounter(RECT_LOCAL, "proxy.local.librecords.testing.counter", (RecCounter) 99, RECP_NON_PERSISTENT);
}

#endif
