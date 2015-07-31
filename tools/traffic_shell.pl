#!/usr/bin/perl
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
use warnings;
use strict;

use Apache::TS;
use Apache::TS::AdminClient;

# Global mgmt API connection...
my $CLI = Apache::TS::AdminClient->new() || die "Can't connect to the mgmt port";
my $ETC_PATH = Apache::TS::PREFIX . '/' . $CLI->get_config("proxy.config.config_dir");

# Helper functions around reading other configs
sub print_config {
  my $file = shift;

  open(FILE, "<${ETC_PATH}/$file") || die "Can't open $file";
  print <FILE>;
  close(FILE);
}
sub param_die {
  my ($param, $cmd) = @_;
  die "Unknown argument `${param}' to ${cmd}";
}


# Some helper functions around common metrics
sub get_on_off {
  my $stat = shift;
  return int($CLI->get_stat($stat)) > 0 ? "on" : "off";
}
sub get_string {
  my $stat = shift;
  return $CLI->get_stat($stat);
}
sub get_int {
  my $stat = shift;
  return int($CLI->get_stat($stat));
}
sub get_float {
  my $stat = shift;
  return sprintf("%.6f", $CLI->get_stat($stat));
}
sub get_pcnt {
  my $stat = shift;
  return sprintf("%.6f", $CLI->get_stat($stat) * 100);
}
sub get_with_si {
  my $stat = shift;
  my $si = shift;
  my $val = int($CLI->get_stat($stat));
  my $multi = 1;

  $multi = 1024*1024*1024 if $si eq "G";
  $multi = 1024*1024 if $si eq "M";
  $multi = 1024 if $si eq "K";

  return int($val / $multi);
}
sub get_switch {
  my $stat = shift;
  my $switch = shift;
  my $val = $CLI->get_stat($stat);

  return $switch->{$val} if exists($switch->{$val});
  return $switch->{"default"};
}


# Command: show:alarms
#
sub show_alarms {
    print "Not implemented, use 'traffic_line --alarms' instead\n";
}


# Command: show:cache
#
sub show_cache {
  my $param = shift || "";

  if ($param eq "") {
    my $http_cache = get_on_off("proxy.config.http.cache.http");
    my $max_obj = get_int("proxy.config.cache.max_doc_size");
    my $min_life = get_int("proxy.config.http.cache.heuristic_min_lifetime");
    my $max_life = get_int("proxy.config.http.cache.heuristic_max_lifetime");
    my $dynamic_urls = get_on_off("proxy.config.http.cache.cache_urls_that_look_dynamic");
    my $alternates = get_on_off("proxy.config.http.cache.enable_default_vary_headers");
    my $vary_def_text = get_string("proxy.config.http.cache.vary_default_text");
    my $vary_def_image = get_string("proxy.config.http.cache.vary_default_images");
    my $vary_def_other = get_string("proxy.config.http.cache.vary_default_other");

    my $when_reval = get_switch("proxy.config.http.cache.when_to_revalidate", {
      "0" => "When The Object Has Expired",
      "1" => "When The Object Has No Expiry Date",
      "2" => "Always",
      "3" => "Never",
      "default" => "unknown" });

    my $reqd_headers = get_switch("proxy.config.http.cache.required_headers", {
      "0" => "Nothing",
      "1" => "A Last Modified Time",
      "2" => "An Explicit Lifetime",
      "default" => "unknown" });

    my $cookies = get_switch("proxy.config.http.cache.cache_responses_to_cookies", {
      "0" => "No Content-types",
      "1" => "All Content-types",
      "2" => "Only Image-content Types",
      "3" => "Content Types which are not Text",
      "4" => "Content Types which are not Text with some exceptions\n",
      "default" => "" });

    print <<__EOL;
HTTP Caching --------------------------- $http_cache
Maximum HTTP Object Size ----------- NONE
Freshness
  Verify Freshness By Checking --------- $when_reval
  Minimum Information to be Cacheable -- $reqd_headers
  If Object has no Expiration Date:
    Leave it in Cache for at least ----- $min_life s
    but no more than ------------------- $max_life s
Variable Content
  Cache Responses to URLs that contain
    "?",";","cgi" or end in ".asp" ----- $dynamic_urls
  Alternates Enabled ------------------- $alternates
  Vary on HTTP Header Fields:
    Text ------------------------------- $vary_def_text
    Images ----------------------------- $vary_def_image
    Other ------------------------------ $vary_def_other
  Cache responses to requests containing cookies for:
    $cookies
__EOL
  } elsif ($param eq "rules") {
    print "cache.config rules\n";
    print "-------------------\n";
    print_config("cache.config");
  } elsif ($param eq "storage") {
    print "storage.config rules\n";
    print "--------------------\n";
    print_config("storage.config");
  } else {
    param_die($param, "show:cache");
  }
}


# Command: show:cache-stats
#
sub show_cache_stats {
  my $bytes_used = get_with_si("proxy.process.cache.bytes_used", "G");
  my $bytes_total = get_with_si("proxy.process.cache.bytes_total", "G");

  my $ram_cache_total_bytes = get_int("proxy.process.cache.ram_cache.total_bytes");
  my $ram_cache_bytes_used = get_int("proxy.process.cache.ram_cache.bytes_used");
  my $ram_cache_hits = get_int("proxy.process.cache.ram_cache.hits");
  my $ram_cache_misses = get_int("proxy.process.cache.ram_cache.misses");
  my $lookup_active = get_int("proxy.process.cache.lookup.active");
  my $lookup_success = get_int("proxy.process.cache.lookup.success");
  my $lookup_failure = get_int("proxy.process.cache.lookup.failure");
  my $read_active = get_int("proxy.process.cache.read.active");
  my $read_success = get_int("proxy.process.cache.read.success");
  my $read_failure = get_int("proxy.process.cache.read.failure");
  my $write_active = get_int("proxy.process.cache.write.active");
  my $write_success = get_int("proxy.process.cache.write.success");
  my $write_failure = get_int("proxy.process.cache.write.failure");
  my $update_active = get_int("proxy.process.cache.update.active");
  my $update_success = get_int("proxy.process.cache.update.success");
  my $update_failure = get_int("proxy.process.cache.update.failure");
  my $remove_active = get_int("proxy.process.cache.remove.active");
  my $remove_success = get_int("proxy.process.cache.remove.success");
  my $remove_failure = get_int("proxy.process.cache.remove.failure");

  print <<__EOL
Bytes Used --- $bytes_used GB
Cache Size --- $bytes_used GB
--RAM Cache--
Total Bytes -- $ram_cache_total_bytes
Bytes Used --- $ram_cache_bytes_used
Hits --------- $ram_cache_hits
Misses ------- $ram_cache_misses
--Lookups--
In Progress -- $lookup_active
Hits --------- $lookup_success
Misses ------- $lookup_failure
--Reads--
In Progress -- $read_active
Hits --------- $read_success
Misses ------- $read_failure
--Writes--
In Progress -- $write_active
Hits --------- $write_success
Misses ------- $write_failure
--Updates--
In Progress -- $update_active
Hits --------- $update_success
Misses ------- $update_failure
--Removes--
In Progress -- $remove_active
Hits --------- $remove_success
Misses ------- $remove_failure
__EOL
}


# Command: show:cluster
#
sub show_cluster {
  my $cluster = get_int("proxy.config.cluster.cluster_port");
  my $cluster_rs = get_int("proxy.config.cluster.rsport");
  my $cluster_mc = get_int("proxy.config.cluster.mcport");

  print <<__EOF
Cluster Port ----------- $cluster
Cluster RS Port -------- $cluster_rs
Cluster MC Port -------- $cluster_mc
__EOF
}


# Command: show:dns-resolver
#
sub show_dns_resolver {
  my $dns_search_default_domains = get_on_off("proxy.config.dns.search_default_domains");
  my $http_enable_url_expandomatic = get_on_off("proxy.config.http.enable_url_expandomatic");
  print <<__EOF
Local Domain Expansion -- $dns_search_default_domains
.com Domain Expansion --- $http_enable_url_expandomatic
__EOF
}


# Command: show:dns-stats
#
sub show_dns_stats {
  my $lookups_per_second = get_float("proxy.node.dns.lookups_per_second");

  print <<__EOF
DNS Lookups Per Second -- $lookups_per_second
__EOF
}


# Command: show:hostdb
#
sub show_hostdb {
  my $lookup_timeout = get_int("proxy.config.hostdb.lookup_timeout");
  my $timeout = get_int("proxy.config.hostdb.timeout");
  my $verify_after = get_int("proxy.config.hostdb.verify_after");
  my $fail_timeout = get_int("proxy.config.hostdb.fail.timeout");
  my $re_dns_on_reload = get_on_off("proxy.config.hostdb.re_dns_on_reload");
  my $dns_lookup_timeout = get_int("proxy.config.dns.lookup_timeout");
  my $dns_retries = get_int("proxy.config.dns.retries");
  print <<__EOF
Lookup Timeout ----------- $lookup_timeout s
Foreground Timeout ------- $timeout s
Background Timeout ------- $verify_after s
Invalid Host Timeout ----- $fail_timeout s
Re-DNS on Reload --------- $re_dns_on_reload
Resolve Attempt Timeout -- $dns_lookup_timeout s
Number of retries -------- $dns_retries
__EOF
}


# Command: show:hostdb-stats
#
sub show_hostdb_stats {
  my $hit_ratio = get_float("proxy.node.hostdb.hit_ratio");
  my $lookups_per_second = get_float("proxy.node.dns.lookups_per_second");

  print <<__EOF
Host Database hit Rate -- $hit_ratio % *
DNS Lookups Per Second -- $lookups_per_second

* Value reprensents 10 second average.
__EOF
}


# Command: show:http
#
sub show_http {
  my $http_enabled = get_on_off("proxy.config.http.cache.http");
  my $http_server = get_string("proxy.config.http.server_ports");
  my $keepalive_timeout_in = get_int("proxy.config.http.keep_alive_no_activity_timeout_in");
  my $keepalive_timeout_out = get_int("proxy.config.http.keep_alive_no_activity_timeout_out");
  my $inactivity_timeout_in = get_int("proxy.config.http.transaction_no_activity_timeout_in");
  my $inactivity_timeout_out = get_int("proxy.config.http.transaction_no_activity_timeout_out");
  my $activity_timeout_in = get_int("proxy.config.http.transaction_active_timeout_in");
  my $activity_timeout_out = get_int("proxy.config.http.transaction_active_timeout_out");
  my $max_alts = get_int("proxy.config.cache.limits.http.max_alts");
  my $remove_from = get_int("proxy.config.http.anonymize_remove_from");
  my $remove_referer = get_int("proxy.config.http.anonymize_remove_referer");
  my $remove_user_agent = get_int("proxy.config.http.anonymize_remove_user_agent");
  my $remove_cookie = get_int("proxy.config.http.anonymize_remove_cookie");
  my $other_header_list = get_string("proxy.config.http.anonymize_other_header_list");
  my $insert_client_ip = get_int("proxy.config.http.anonymize_insert_client_ip");
  my $remove_client_ip = get_int("proxy.config.http.anonymize_remove_client_ip");
  my $global_user_agent = get_string("proxy.config.http.global_user_agent_header");

  # A bunch of strings here are optional...
  my $optional = "";

  if ($remove_from || $remove_referer || $remove_user_agent || $remove_cookie) {
    $optional = "Remove the following common headers -- \n";
    $optional .= "From\n" if $remove_from;
    $optional .= "Referer\n" if $remove_referer;
    $optional .= "User-Agent\n" if $remove_user_agent;
    $optional .= "Cookie\n" if $remove_cookie;
  }

  if ($other_header_list ne "NULL") {
    $optional .= "Remove additional headers ----- $other_header_list\n";
  }

  if ($insert_client_ip) {
    $optional .= "Insert Client IP Address into Header\n";
  }

  if ($remove_client_ip) {
    $optional .= "Remove Client IP Address from Header\n";
  }

  if ($global_user_agent ne "NULL") {
    $optional .= "Set User-Agent header to $global_user_agent\n";
  }


  print <<__EOF
HTTP Caching ------------------ $http_enabled
HTTP Server Port(s) ----------- $http_server
Keep-Alive Timeout Inbound ---- $keepalive_timeout_in s
Keep-Alive Timeout Outbound --- $keepalive_timeout_out s
Inactivity Timeout Inbound ---- $inactivity_timeout_in s
Inactivity Timeout Outbound --- $inactivity_timeout_out s
Activity Timeout Inbound ------ $activity_timeout_in s
Activity Timeout Outbound ----- $activity_timeout_out s
Maximum Number of Alternates -- $max_alts
${optional}
__EOF
}


# Command: show:http-stats
#
sub show_http_stats {
  my $user_agent_response_document_total_size = get_with_si("proxy.process.http.user_agent_response_document_total_size", "M");
  my $user_agent_response_header_total_size = get_with_si("proxy.process.http.user_agent_response_header_total_size", "M");
  my $current_client_connections = get_int("proxy.process.http.current_client_connections");
  my $current_client_transactions = get_int("proxy.process.http.current_client_transactions");
  my $origin_server_response_document_total_size = get_with_si("proxy.process.http.origin_server_response_document_total_size", "M");
  my $origin_server_response_header_total_size = get_with_si("proxy.process.http.origin_server_response_header_total_size", "M");
  my $current_server_connections = get_int("proxy.process.http.current_server_connections");
  my $current_server_transactions = get_int("proxy.process.http.current_server_transactions");

  print <<__EOF
Total Document Bytes ----- $user_agent_response_document_total_size MB
Total Header Bytes ------- $user_agent_response_header_total_size MB
Total Connections -------- $current_client_connections
Transactins In Progress -- $current_client_transactions
--Server--
Total Document Bytes ----- $origin_server_response_document_total_size MB
Total Header Bytes ------- $origin_server_response_header_total_size MB
Total Connections -------- $current_server_connections
Transactins In Progress -- $current_server_transactions
__EOF
}


# Command: show:http-trans-stats
#
sub show_http_trans_stats {
  my $frac_avg_10s_hit_fresh = get_pcnt("proxy.node.http.transaction_frac_avg_10s.hit_fresh");
  my $frac_avg_10s_hit_revalidated = get_pcnt("proxy.node.http.transaction_frac_avg_10s.hit_revalidated");
  my $frac_avg_10s_miss_cold = get_pcnt("proxy.node.http.transaction_frac_avg_10s.miss_cold");
  my $frac_avg_10s_miss_not_cachable = get_pcnt("proxy.node.http.transaction_frac_avg_10s.miss_not_cacheable");
  my $frac_avg_10s_miss_changed = get_pcnt("proxy.node.http.transaction_frac_avg_10s.miss_changed");
  my $frac_avg_10s_miss_client_no_cache = get_pcnt("proxy.node.http.transaction_frac_avg_10s.miss_client_no_cache");
  my $frac_avg_10s_errors_connect_failed = get_pcnt("proxy.node.http.transaction_frac_avg_10s.errors.connect_failed");
  my $frac_avg_10s_errors_other = get_pcnt("proxy.node.http.transaction_frac_avg_10s.errors.other");
  my $frac_avg_10s_errors_aborts = get_pcnt("proxy.node.http.transaction_frac_avg_10s.errors.aborts");
  my $frac_avg_10s_errors_possible_aborts = get_pcnt("proxy.node.http.transaction_frac_avg_10s.errors.possible_aborts");
  my $frac_avg_10s_errors_early_hangups = get_pcnt("proxy.node.http.transaction_frac_avg_10s.errors.early_hangups");
  my $frac_avg_10s_errors_empty_hangups = get_pcnt("proxy.node.http.transaction_frac_avg_10s.errors.empty_hangups");
  my $frac_avg_10s_errors_pre_accept_hangups = get_pcnt("proxy.node.http.transaction_frac_avg_10s.errors.pre_accept_hangups");
  my $frac_avg_10s_other_unclassified = get_pcnt("proxy.node.http.transaction_frac_avg_10s.other.unclassified");

  my $msec_avg_10s_hit_fresh = get_int("proxy.node.http.transaction_msec_avg_10s.hit_fresh");
  my $msec_avg_10s_hit_revalidated = get_int("proxy.node.http.transaction_msec_avg_10s.hit_revalidated");
  my $msec_avg_10s_miss_cold = get_int("proxy.node.http.transaction_msec_avg_10s.miss_cold");
  my $msec_avg_10s_miss_not_cachable = get_int("proxy.node.http.transaction_msec_avg_10s.miss_not_cacheable");
  my $msec_avg_10s_miss_changed = get_int("proxy.node.http.transaction_msec_avg_10s.miss_changed");
  my $msec_avg_10s_miss_client_no_cache = get_int("proxy.node.http.transaction_msec_avg_10s.miss_client_no_cache");
  my $msec_avg_10s_errors_connect_failed = get_int("proxy.node.http.transaction_msec_avg_10s.errors.connect_failed");
  my $msec_avg_10s_errors_other = get_int("proxy.node.http.transaction_msec_avg_10s.errors.other");
  my $msec_avg_10s_errors_aborts = get_int("proxy.node.http.transaction_msec_avg_10s.errors.aborts");
  my $msec_avg_10s_errors_possible_aborts = get_int("proxy.node.http.transaction_msec_avg_10s.errors.possible_aborts");
  my $msec_avg_10s_errors_early_hangups = get_int("proxy.node.http.transaction_msec_avg_10s.errors.early_hangups");
  my $msec_avg_10s_errors_empty_hangups = get_int("proxy.node.http.transaction_msec_avg_10s.errors.empty_hangups");
  my $msec_avg_10s_errors_pre_accept_hangups = get_int("proxy.node.http.transaction_msec_avg_10s.errors.pre_accept_hangups");
  my $msec_avg_10s_other_unclassified = get_int("proxy.node.http.transaction_msec_avg_10s.other.unclassified");

  print <<__EOF
HTTP Transaction Frequency and Speeds
Transaction Type              Frequency   Speed(ms)
--Hits--
Fresh ----------------------- $frac_avg_10s_hit_fresh %  $msec_avg_10s_hit_fresh
Stale Revalidated ----------- $frac_avg_10s_hit_revalidated %  $msec_avg_10s_hit_revalidated
--Misses--
Now Cached ------------------ $frac_avg_10s_miss_cold %  $msec_avg_10s_miss_cold
Server No Cache ------------- $frac_avg_10s_miss_not_cachable %  $msec_avg_10s_miss_not_cachable
Stale Reloaded -------------- $frac_avg_10s_miss_changed %  $msec_avg_10s_miss_changed
Client No Cache ------------- $frac_avg_10s_miss_client_no_cache %  $msec_avg_10s_miss_client_no_cache
--Errors--
Connection Failures --------- $frac_avg_10s_errors_connect_failed %  $msec_avg_10s_errors_connect_failed
Other Errors ---------------- $frac_avg_10s_errors_other %  $msec_avg_10s_errors_other
--Aborted Transactions--
Client Aborts --------------- $frac_avg_10s_errors_aborts %  $msec_avg_10s_errors_aborts
Questionable Client Aborts -- $frac_avg_10s_errors_possible_aborts %  $msec_avg_10s_errors_possible_aborts
Partial Request Hangups ----- $frac_avg_10s_errors_early_hangups %  $msec_avg_10s_errors_early_hangups
Pre-Request Hangups --------- $frac_avg_10s_errors_empty_hangups %  $msec_avg_10s_errors_empty_hangups
Pre-Connect Hangups --------- $frac_avg_10s_errors_pre_accept_hangups %  $msec_avg_10s_errors_pre_accept_hangups
--Other Transactions--
Unclassified ---------------- $frac_avg_10s_other_unclassified %  $msec_avg_10s_other_unclassified
__EOF
}


# Command: show:icp
#
sub show_icp {
  my $param = shift || "";

  if ($param eq "") {
    my $icp_enabled = get_on_off("proxy.config.icp.enabled");
    my $icp_port = get_int("proxy.config.icp.icp_port");
    my $multicast_enabled = get_on_off("proxy.config.icp.multicast_enabled");
    my $query_timeout = get_int("proxy.config.icp.query_timeout");
    print <<__EOF
ICP Mode Enabled ------- $icp_enabled
ICP Port --------------- $icp_port
ICP Multicast Enabled -- $multicast_enabled
ICP Query Timeout ------ $query_timeout s
__EOF
  } elsif ($param eq "peers") {
    print "icp.config Rules\n";
    print "----------------\n";
    print_config("icp.config");
  } else {
    param_die($param, "show:icp");
  }
}


# Command: show:icp-stats
#
sub show_icp_stats {
  my $icp_query_requests = get_int("proxy.process.icp.icp_query_requests");
  my $total_udp_send_queries = get_int("proxy.process.icp.total_udp_send_queries");
  my $icp_query_hits = get_int("proxy.process.icp.icp_query_hits");
  my $icp_query_misses = get_int("proxy.process.icp.icp_query_misses");
  my $icp_remote_responses = get_int("proxy.process.icp.icp_remote_responses");
  my $total_icp_response_time = get_float("proxy.process.icp.total_icp_response_time");
  my $total_icp_request_time = get_float("proxy.process.icp.total_icp_request_time");
  my $icp_remote_query_requests = get_int("proxy.process.icp.icp_remote_query_requests");
  my $cache_lookup_success = get_int("proxy.process.icp.cache_lookup_success");
  my $cache_lookup_fail = get_int("proxy.process.icp.cache_lookup_fail");
  my $query_response_write = get_int("proxy.process.icp.query_response_write");
  
  print <<__EOF
--Queries Originating From This Node--
Query Requests ----------------------------- $icp_query_requests
Query Messages Sent ------------------------ $total_udp_send_queries
Peer Hit Messages Received ----------------- $icp_query_hits
Peer Miss Messages Received ---------------- $icp_query_misses
Total Responses Received ------------------- $icp_remote_responses
Average ICP Message Response Time ---------- $total_icp_response_time ms
Average ICP Request Time ------------------- $total_icp_request_time ms

--Queries Originating from ICP Peers--
Query Messages Received -------------------- $icp_remote_query_requests
Remote Query Hits -------------------------- $cache_lookup_success
Remote Query Misses ------------------------ $cache_lookup_fail
Successful Response Message Sent to Peers -- $query_response_write
__EOF
}


# Command: show:logging
#
sub show_logging {
  my $logging_enabled = get_switch("proxy.config.log.logging_enabled", {
    "0" => "no logging",
    "1" => "errors only",
    "2" => "transactions only",
    "3" => "errors and transactions",
    "default" => "invalid mode"});

  my $log_space = get_int("proxy.config.log.max_space_mb_for_logs");
  my $headroom_space = get_int("proxy.config.log.max_space_mb_headroom");

  my $collation_mode = get_on_off("proxy.local.log.collation_mode");
  my $collation_host = get_string("proxy.config.log.collation_host");
  my $collation_port = get_int("proxy.config.log.collation_port");
  my $collation_secret = get_string("proxy.config.log.collation_secret");
  my $host_tag = get_on_off("proxy.config.log.collation_host_tagged");
  my $preproc_threads = get_on_off("proxy.config.log.collation_preproc_threads");
  my $orphan_space = get_int("proxy.config.log.max_space_mb_for_orphan_logs");

  my $custom_log = get_on_off("proxy.config.log.custom_logs_enabled");

  my $rolling = get_on_off("proxy.config.log.rolling_enabled");
  my $roll_offset_hr = get_int("proxy.config.log.rolling_offset_hr");
  my $roll_interval = get_int("proxy.config.log.rolling_interval_sec");
  my $auto_delete = get_on_off("proxy.config.log.auto_delete_rolled_files");
  
  print <<__EOF
Logging Mode ----------------------------- $logging_enabled

Management
  Log Space Limit ------------------------ $log_space MB
  Log Space Headroom --------------------- $headroom_space MB

Log Collation ---------------------------- $collation_mode
  Host ----------------------------------- $collation_host
  Port ----------------------------------- $collation_port
  Secret --------------------------------- $collation_secret
  Host Tagged ---------------------------- $host_tag
  Preproc Threads ------------------------ $preproc_threads
  Space Limit for Orphan Files ----------- $orphan_space MB

Custom Logs ------------------------------ $custom_log

Rolling ---------------------------------- $rolling
  Roll Offset Hour ----------------------- $roll_offset_hr
  Roll Interval -------------------------- $roll_interval s
  Auto-delete rolled files (low space) --- $auto_delete
__EOF
}


# Command: show:logging-stats
#
sub show_logging_stats {
  my $log_file_open = get_int("proxy.process.log.log_files_open");
  my $log_files_space_used = get_int("proxy.process.log.log_files_space_used");
  my $event_log_access = get_int("proxy.process.log.event_log_access");
  my $event_log_access_skip = get_int("proxy.process.log.event_log_access_skip");
  my $event_log_error = get_int("proxy.process.log.event_log_error");
  
  print <<__EOF
Current Open Log Files ----------- $log_file_open
Space Used For Log Files --------- $log_files_space_used
Number of Access Events Logged --- $event_log_access
Number of Access Events Skipped -- $event_log_access_skip
Number of Error Events Logged ---- $event_log_error
__EOF
}


# Command: show:parent
#
sub show_parent {
  my $param = shift || "";

  if ($param eq "") {
    my $parent_enabled = get_on_off("proxy.config.http.parent_proxy_routing_enable");
    my $parent_cache = get_string("proxy.config.http.parent_proxies");

    print <<__EOF
Parent Caching -- $parent_enabled
Parent Cache ---- $parent_cache
__EOF
  } elsif ($param eq "rules") {
    print "parent.config rules\n";
    print "-------------------\n";
    print_config("parent.config");
  } else {
    param_die($param, "show:parent");
  }
}


# Command: show:proxy
#
sub show_proxy {
  my $name = get_string("proxy.config.proxy_name");
  print "Name -- ", $name, "\n";
}


# Command: show:proxy-stats
#
sub show_proxy_stats {
  my $cache_hit_ratio = get_pcnt("proxy.node.cache_hit_ratio");
  my $cache_hit_mem_ratio = get_pcnt("proxy.node.cache_hit_mem_ratio");
  my $bandwidth_hit_ratio = get_pcnt("proxy.node.bandwidth_hit_ratio");
  my $percent_free = get_pcnt("proxy.node.cache.percent_free");

  my $current_server_connection = get_int("proxy.node.current_server_connections");
  my $current_client_connection = get_int("proxy.node.current_client_connections");
  my $current_cache_connection = get_int("proxy.node.current_cache_connections");

  my $client_throughput_out = get_float("proxy.node.client_throughput_out");
  my $xacts_per_second = get_float("proxy.node.user_agent_xacts_per_second");
  
  print <<__EOF
Document Hit Rate -------- $cache_hit_ratio % *
Ram cache Hit Rate ------- $cache_hit_mem_ratio % *
Bandwidth Saving --------- $bandwidth_hit_ratio % *
Cache Percent Free ------- $percent_free %
Open Server Connections -- $current_server_connection
Open Client Connections -- $current_client_connection
Open Cache Connections --- $current_cache_connection
Client Throughput -------- $client_throughput_out MBit/Sec
Transaction Per Second --- $xacts_per_second

* Value represents 10 second average.
__EOF
}


# Command: show:remap
#
sub show_remap {
  print "remap.config rules\n";
  print "-------------------\n";
  print_config("remap.config");
}


# Command: show:security
#
sub show_security {
  print "Traffic Server Access\n";
  print "-------------------\n";
  print_config("ip_allow.config");
}


# Command: show:socks
#
sub show_socks {
  my $param = shift || "";

  if ($param eq "") {
    my $socks_enabled = get_on_off("proxy.config.socks.socks_needed");
    my $version = get_int("proxy.config.socks.socks_version");
    my $default_servers = get_string("proxy.config.socks.default_servers");
    my $accept_enabled = get_on_off("proxy.config.socks.accept_enabled");
    my $accept_port = get_int("proxy.config.socks.accept_port");
    
    print <<__EOF
SOCKS -------------------- $socks_enabled
SOCKS Version ------------ $version
SOCKS Default Servers ---- $default_servers
SOCKS Accept Enabled ----- $accept_enabled
SOCKS Accept Port -------- $accept_port
__EOF
  } elsif ($param eq "rules") {
    print "socks.config rules\n";
    print "------------------\n";
    print_config("socks.config");
  } else {
    param_die($param, "show:socks");
  }
}


# Command: show:ssl
#
sub show_ssl {
  my $connect_ports = get_string("proxy.config.http.connect_ports");
  print "Restrict CONNECT connections to Ports -- ", $connect_ports, "\n";
}


# Command: show:status
#
sub show_status {
    print "Not implemented, use 'traffic_line --status' instead\n";
}


# Command: show:version
#
sub show_version {
  my $ts_version = get_string("proxy.process.version.server.short");
  my $tm_version = get_string("proxy.node.version.manager.short");

  print <<__EOF
traffic_server version --- $ts_version
traffic_manager version -- $tm_version
__EOF
}


# Command: show:virtual-ip
#
sub show_virtual_ip {
  print <<__EOF
Not supported
__EOF
}

# Basic help function
sub help {
  print <<__EOF
Usage: traffic_shell <command> [argument]

   show:cache [rules | storage]
   show:cache-stats
   show:cluster
   show:dns-resolver
   show:dns-stats
   show:hostdb
   show:hostdb-stats
   show:http
   show:http-stats
   show:http-trans-stats
   show:icp [peers]
   show:icp-stats
   show:logging
   show:logging-stats
   show:parent [rules]
   show:proxy
   show:proxy-stats
   show:remap
   show:scheduled-update [rules]
   show:security
   show:socks [rules]
   show:ssl
   show:status
   show:version
   show:virtual-ip
   help
__EOF
}


#
# Dispatcher  / command line
#
my %COMMANDS = ( "show:alarms", \&show_alarms,
                 "show:cache", \&show_cache,
                "show:cache-stats", \&show_cache_stats,
                "show:cluster",  \&show_cluster,
                "show:dns-resolver", \&show_dns_resolver,
                "show:dns-stats", \&show_dns_stats,
                "show:hostdb", \&show_hostdb,
                "show:hostdb-stats", \&show_hostdb_stats,
                "show:http", \&show_http,
                "show:http-stats", \&show_http_stats,
                "show:http-trans-stats", \&show_http_trans_stats,
                "show:icp", \&show_icp,
                "show:icp-stats", \&show_icp_stats,
                "show:logging", \&show_logging,
                "show:logging-stats", \&show_logging_stats,
                "show:parent", \&show_parent,
                "show:proxy", \&show_proxy,
                "show:proxy-stats", \&show_proxy_stats,
                "show:remap", \&show_remap,
                "show:scheduled-update", \&show_scheduled_update,
                "show:security", \&show_security,
                "show:socks", \&show_socks,
                "show:ssl", \&show_ssl,
                "show:status", \&show_status,
                "show:version", \&show_version,
                "show:virtual-ip", \&show_virtual_ip,
                "help", \&help,
                "show", \&help);

if ($#ARGV >= 0) {
  my $cmd = shift;

  die "Not valid command: $cmd" unless exists($COMMANDS{$cmd});
  my $func = $COMMANDS{$cmd};

  $func->(@ARGV);
} else {
  use Term::ReadLine;
  my $term = Term::ReadLine->new('Apache Traffic Server');

  my $prompt = "trafficserver> ";
  my $OUT = $term->OUT || \*STDOUT;

  while (defined ($_ = $term->readline($prompt))) {
    chomp;
    my ($cmd, @args) = split;

    if (exists($COMMANDS{$cmd})) {
      my $func = $COMMANDS{$cmd};
      $func->(@args);
      print "\n";
    } else {
      print "invalid command name \"$cmd\"\n";
    }
    $term->addhistory($_) if /\S/;
  }
}
