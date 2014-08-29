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

package Apache::TS::AdminClient;

use warnings;
use strict;

require 5.006;

use Carp;
use IO::Socket::UNIX;
use IO::Select;

use Apache::TS;


# Mgmt API command constants, should track ts/mgmtapi.h
use constant {
    TS_FILE_READ            => 0,
    TS_FILE_WRITE           => 1,
    TS_RECORD_SET           => 2,
    TS_RECORD_GET           => 3,
    TS_PROXY_STATE_GET      => 4,
    TS_PROXY_STATE_SET      => 5,
    TS_RECONFIGURE          => 6,
    TS_RESTART              => 7,
    TS_BOUNCE               => 8,
    TS_EVENT_RESOLVE        => 9,
    TS_EVENT_GET_MLT        => 10,
    TS_EVENT_ACTIVE         => 11,
    TS_EVENT_REG_CALLBACK   => 12,
    TS_EVENT_UNREG_CALLBACK => 13,
    TS_EVENT_NOTIFY         => 14,
    TS_SNAPSHOT_TAKE        => 15,
    TS_SNAPSHOT_RESTORE     => 16,
    TS_SNAPSHOT_REMOVE      => 17,
    TS_SNAPSHOT_GET_MLT     => 18,
    TS_DIAGS                => 19,
    TS_STATS_RESET          => 20
};

# We treat both REC_INT and REC_COUNTER the same here
use constant {
    TS_REC_INT     => 0,
    TS_REC_COUNTER => 0,
    TS_REC_FLOAT   => 2,
    TS_REC_STRING  => 3
};

use constant {
    TS_ERR_OKAY                => 0,
    TS_ERR_READ_FILE           => 1,
    TS_ERR_WRITE_FILE          => 2,
    TS_ERR_PARSE_CONFIG_RULE   => 3,
    TS_ERR_INVALID_CONFIG_RULE => 4,
    TS_ERR_NET_ESTABLISH       => 5,
    TS_ERR_NET_READ            => 6,
    TS_ERR_NET_WRITE           => 7,
    TS_ERR_NET_EOF             => 8,
    TS_ERR_NET_TIMEOUT         => 9,
    TS_ERR_SYS_CALL            => 10,
    TS_ERR_PARAMS              => 11,
    TS_ERR_FAIL                => 12
};


# Semi-intelligent way of finding the mgmtapi socket.
sub _find_socket {
    my $path = shift || "";
    my $name = shift || "mgmtapisocket";
    my @sockets_def = (
        $path,
        Apache::TS::PREFIX . '/' . Apache::TS::REL_RUNTIMEDIR . '/' . 'mgmtapisocket',
        '/usr/local/var/trafficserver',
        '/usr/local/var/run/trafficserver',
        '/usr/local/var/run',
        '/var/trafficserver',
        '/var/run/trafficserver',
        '/var/run',
        '/opt/ats/var/trafficserver',
    );

    foreach my $socket (@sockets_def) {
        return $socket if (-S $socket);
        return "${socket}/${name}" if (-S "${socket}/${name}");

    }
    return undef;
}

#
# Constructor
#
sub new {
    my ($class, %args) = @_;
    my $self = {};

    $self->{_socket_path} = _find_socket($args{socket_path});
    $self->{_socket} = undef;
    croak
"Unable to locate socket, please pass socket_path with the management api socket location to Apache::TS::AdminClient"
      if (!$self->{_socket_path});
    if ((!-r $self->{_socket_path}) or (!-w $self->{_socket_path}) or (!-S $self->{_socket_path})) {
        croak "Unable to open $self->{_socket_path} for reads or writes";
    }

    $self->{_select} = IO::Select->new();
    bless $self, $class;

    $self->open_socket();

    return $self;
}

#
# Destructor
#
sub DESTROY {
    my $self = shift;
    return $self->close_socket();
}

#
# Open the socket (Unix domain)
#
sub open_socket {
    my $self = shift;
    my %args = @_;

    if (defined($self->{_socket})) {
        if ($args{force} || $args{reopen}) {
            $self->close_socket();
        }
        else {
            return undef;
        }
    }

    $self->{_socket} = IO::Socket::UNIX->new(
        Type => SOCK_STREAM,
        Peer => $self->{_socket_path}
   ) or croak("Error opening socket - $@");

    return undef unless defined($self->{_socket});
    $self->{_select}->add($self->{_socket});

    return $self;
}

sub close_socket {
    my $self = shift;

    # if socket doesn't exist, return as there's nothing to do.
    return unless defined($self->{_socket});

    # gracefully close socket.
    $self->{_select}->remove($self->{_socket});
    $self->{_socket}->close();
    $self->{_socket} = undef;

    return $self;
}

#
# Do reads()'s on our Unix domain socket, takes an optional timeout, in ms's.
#
sub _do_read {
    my $self = shift;
    my $timeout = shift || 1/1000.0; # 1ms by default
    my $res = "";

    while ($self->{_select}->can_read($timeout)) {
        my $rc = $self->{_socket}->sysread($res, 1024, length($res));
    }

    return $res || undef;
}


#
# Get (read) a stat out of the local manager. Note that the assumption is
# that you are calling this with an existing stats "name".
#
sub get_stat {
    my ($self, $stat) = @_;
    my $res               = "";
    my $max_read_attempts = 25;

    return undef unless defined($self->{_socket});
    return undef unless $self->{_select}->can_write(10);

# This is a total hack for now, we need to wrap this into the proper mgmt API library.
    $self->{_socket}->print(pack("sla*", TS_RECORD_GET, length($stat)), $stat);
    $res = $self->_do_read();

    my @resp = unpack("slls", $res);
    return undef unless (scalar(@resp) == 4);

    if ($resp[0] == TS_ERR_OKAY) {
        if ($resp[3] < TS_REC_FLOAT) {
            @resp = unpack("sllsq", $res);
            return undef unless (scalar(@resp) == 5);
            return int($resp[4]);
        }
        elsif ($resp[3] == TS_REC_FLOAT) {
            @resp = unpack("sllsf", $res);
            return undef unless (scalar(@resp) == 5);
            return $resp[4];
        }
        elsif ($resp[3] == TS_REC_STRING) {
            @resp = unpack("sllsa*", $res);
            return undef unless (scalar(@resp) == 5);
	    my @result = split($stat, $resp[4]);
            return $result[0];
        }
    }

    return undef;
}
*get_config = \&get_stat;

1;

__END__

#-=-=-=-=-=-=-=-= Give us some POD please =-=-=-=-=-=-=-=- 

=head1 NAME:

Apache::TS::AdminClient - a perl interface to the statistics and configuration settings stored within Apache Traffic Server.

=head1 SYNOPSIS

  #!/usr/bin/perl
  use Apache::TS::AdminClient;

  my $cli = Apache::TS::AdminClient->new(%input);
  my $string = $cli->get_stat("proxy.config.product_company");
  print "$string\n";


=head1 DESCRIPTION:

AdminClient opens a TCP connection to a unix domain socket on local disk.  When the connection is established, 
AdminClient will write requests to the socket and wait for Apache Traffic Server to return a response.  Valid 
request strings can be found in RecordsConfig.cc which is included with Apache Traffic Server source.  
A list of valid request strings are included with this documentation, but this included list may not be complete
as future releases of Apache Traffic Server may include new request strings or remove existing ones.  

=head1 CONSTRUCTOR

When the object is created for this module, it assumes the 'Unix Domain Socket' is at the default location from
the Apache Traffic Server installation. This can be changed when creating the object by setting B<'socket_path'>.
For example: 

=over 4

=item my $cli = AdminClient->new(socket_path=> "/var/trafficserver");


This would make the module look for the 'Unix Domain Socket' in the directory '/var/trafficserver'. The path
can optionally include the name of the Socket file, without it the constructor defaults to 'mgmtapisocket'.

=back

=head1 PUBLIC METHODS

To read a single metric (or configuration), two APIs are available:

=over 4

=item $cli->get_stat($stats_name);

=item $cli->get_config($config_name);

This will return a (scalar) value for this metric or configuration.

=back

=head1 traffic_line

There is a command line tool included with Apache Traffic Server called traffic_line which overlaps with this module.  traffic_line 
can be used to read and write statistics or config settings that this module can.  Hence if you don't want to write a perl one-liner to 
get to this information, traffic_line is your tool.

=head1 List of configurations

The Apache Traffic Server Administration Manual will explain what these strings represent.  (http://trafficserver.apache.org/docs/)

 proxy.config.accept_threads
 proxy.config.task_threads
 proxy.config.admin.admin_user
 proxy.config.admin.autoconf.localhost_only
 proxy.config.admin.autoconf.pac_filename
 proxy.config.admin.autoconf_port
 proxy.config.admin.autoconf.doc_root
 proxy.config.admin.cli_path
 proxy.config.admin.number_config_bak
 proxy.config.admin.user_id
 proxy.config.alarm.abs_path
 proxy.config.alarm.bin
 proxy.config.alarm_email
 proxy.config.alarm.script_runtime
 proxy.config.bandwidth_mgmt.filename
 proxy.config.bin_path
 proxy.config.body_factory.enable_customizations
 proxy.config.body_factory.enable_logging
 proxy.config.body_factory.response_suppression_mode
 proxy.config.body_factory.template_sets_dir
 proxy.config.cache.agg_write_backlog
 proxy.config.cache.alt_rewrite_max_size
 proxy.config.cache.control.filename
 proxy.config.cache.dir.sync_frequency
 proxy.config.cache.enable_checksum
 proxy.config.cache.enable_read_while_writer
 proxy.config.cache.hostdb.disable_reverse_lookup
 proxy.config.cache.hostdb.sync_frequency
 proxy.config.cache.hosting_filename
 proxy.config.cache.ip_allow.filename
 proxy.config.cache.limits.http.max_alts
 proxy.config.cache.max_disk_errors
 proxy.config.cache.max_doc_size
 proxy.config.cache.min_average_object_size
 proxy.config.cache.volume_filename
 proxy.config.cache.permit.pinning
 proxy.config.cache.ram_cache_cutoff
 proxy.config.cache.ram_cache.size
 proxy.config.cache.select_alternate
 proxy.config.cache.storage_filename
 proxy.config.cache.threads_per_disk
 proxy.config.cache.url_hash_method
 proxy.config.cache.vary_on_user_agent
 proxy.config.cache.mutex_retry_delay
 proxy.config.cluster.cluster_configuration
 proxy.config.cluster.cluster_load_clear_duration
 proxy.config.cluster.cluster_load_exceed_duration
 proxy.config.cluster.cluster_port
 proxy.config.cluster.delta_thresh
 proxy.config.cluster.enable_monitor
 proxy.config.cluster.ethernet_interface
 proxy.config.cluster.load_compute_interval_msecs
 proxy.config.cluster.load_monitor_enabled
 proxy.config.cluster.log_bogus_mc_msgs
 proxy.config.cluster.mc_group_addr
 proxy.config.cluster.mcport
 proxy.config.cluster.mc_ttl
 proxy.config.cluster.monitor_interval_secs
 proxy.config.cluster.msecs_per_ping_response_bucket
 proxy.config.cluster.peer_timeout
 proxy.config.cluster.periodic_timer_interval_msecs
 proxy.config.cluster.ping_history_buf_length
 proxy.config.cluster.ping_latency_threshold_msecs
 proxy.config.cluster.ping_response_buckets
 proxy.config.cluster.ping_send_interval_msecs
 proxy.config.cluster.receive_buffer_size
 proxy.config.cluster.rpc_cache_cluster
 proxy.config.cluster.rsport
 proxy.config.cluster.send_buffer_size
 proxy.config.cluster.sock_option_flag
 proxy.config.cluster.startup_timeout
 proxy.config.cluster.threads
 proxy.config.config_dir
 proxy.config.cop.core_signal
 proxy.config.cop.linux_min_memfree_kb
 proxy.config.cop.linux_min_swapfree_kb
 proxy.config.core_limit
 proxy.config.diags.action.enabled
 proxy.config.diags.action.tags
 proxy.config.diags.debug.enabled
 proxy.config.diags.debug.tags
 proxy.config.diags.output.alert
 proxy.config.diags.output.debug
 proxy.config.diags.output.diag
 proxy.config.diags.output.emergency
 proxy.config.diags.output.error
 proxy.config.diags.output.fatal
 proxy.config.diags.output.note
 proxy.config.diags.output.status
 proxy.config.diags.output.warning
 proxy.config.diags.show_location
 proxy.config.dns.failover_number
 proxy.config.dns.failover_period
 proxy.config.dns.lookup_timeout
 proxy.config.dns.max_dns_in_flight
 proxy.config.dns.nameservers
 proxy.config.dns.resolv_conf
 proxy.config.dns.retries
 proxy.config.dns.round_robin_nameservers
 proxy.config.dns.search_default_domains
 proxy.config.dns.splitDNS.enabled
 proxy.config.dns.splitdns.filename
 proxy.config.dns.url_expansions
 proxy.config.dump_mem_info_frequency
 proxy.config.env_prep
 proxy.config.exec_thread.autoconfig
 proxy.config.exec_thread.autoconfig.scale
 proxy.config.exec_thread.limit
 proxy.config.header.parse.no_host_url_redirect
 proxy.config.hostdb
 proxy.config.hostdb.cluster
 proxy.config.hostdb.cluster.round_robin
 proxy.config.hostdb.fail.timeout
 proxy.config.hostdb.filename
 proxy.config.hostdb.lookup_timeout
 proxy.config.hostdb.migrate_on_demand
 proxy.config.hostdb.re_dns_on_reload
 proxy.config.hostdb.serve_stale_for
 proxy.config.hostdb.size
 proxy.config.hostdb.storage_path
 proxy.config.hostdb.storage_size
 proxy.config.hostdb.strict_round_robin
 proxy.config.hostdb.timeout
 proxy.config.hostdb.ttl_mode
 proxy.config.hostdb.verify_after
 proxy.config.http.accept_encoding_filter.filename
 proxy.config.http.accept_no_activity_timeout
 proxy.config.http.anonymize_insert_client_ip
 proxy.config.http.anonymize_other_header_list
 proxy.config.http.anonymize_remove_client_ip
 proxy.config.http.anonymize_remove_cookie
 proxy.config.http.anonymize_remove_from
 proxy.config.http.anonymize_remove_referer
 proxy.config.http.anonymize_remove_user_agent
 proxy.config.http.background_fill_active_timeout
 proxy.config.http.background_fill_completed_threshold
 proxy.config.http.cache.cache_responses_to_cookies
 proxy.config.http.cache.cache_urls_that_look_dynamic
 proxy.config.http.cache.enable_default_vary_headers
 proxy.config.http.cache.fuzz.min_time
 proxy.config.http.cache.fuzz.probability
 proxy.config.http.cache.fuzz.time
 proxy.config.http.cache.guaranteed_max_lifetime
 proxy.config.http.cache.guaranteed_min_lifetime
 proxy.config.http.cache.heuristic_lm_factor
 proxy.config.http.cache.heuristic_max_lifetime
 proxy.config.http.cache.heuristic_min_lifetime
 proxy.config.http.cache.http
 proxy.config.http.cache.ignore_accept_charset_mismatch
 proxy.config.http.cache.ignore_accept_encoding_mismatch
 proxy.config.http.cache.ignore_accept_language_mismatch
 proxy.config.http.cache.ignore_accept_mismatch
 proxy.config.http.cache.ignore_authentication
 proxy.config.http.cache.ignore_client_cc_max_age
 proxy.config.http.cache.cluster_cache_local
 proxy.config.http.cache.ignore_client_no_cache
 proxy.config.http.cache.ignore_server_no_cache
 proxy.config.http.cache.ims_on_client_no_cache
 proxy.config.http.cache.max_open_read_retries
 proxy.config.http.cache.max_open_write_retries
 proxy.config.http.cache.max_stale_age
 proxy.config.http.cache.open_read_retry_time
 proxy.config.http.cache.range.lookup
 proxy.config.http.cache.range.write
 proxy.config.http.cache.required_headers
 proxy.config.http.cache.vary_default_images
 proxy.config.http.cache.vary_default_other
 proxy.config.http.cache.vary_default_text
 proxy.config.http.cache.when_to_revalidate
 proxy.config.http.chunking_enabled
 proxy.config.http.congestion_control.default.client_wait_interval
 proxy.config.http.congestion_control.default.congestion_scheme
 proxy.config.http.congestion_control.default.dead_os_conn_retries
 proxy.config.http.congestion_control.default.dead_os_conn_timeout
 proxy.config.http.congestion_control.default.error_page
 proxy.config.http.congestion_control.default.fail_window
 proxy.config.http.congestion_control.default.live_os_conn_retries
 proxy.config.http.congestion_control.default.live_os_conn_timeout
 proxy.config.http.congestion_control.default.max_connection
 proxy.config.http.congestion_control.default.max_connection_failures
 proxy.config.http.congestion_control.default.proxy_retry_interval
 proxy.config.http.congestion_control.default.wait_interval_alpha
 proxy.config.http.congestion_control.enabled
 proxy.config.http.congestion_control.filename
 proxy.config.http.congestion_control.localtime
 proxy.config.http.connect_attempts_max_retries
 proxy.config.http.connect_attempts_max_retries_dead_server
 proxy.config.http.connect_attempts_rr_retries
 proxy.config.http.connect_attempts_timeout
 proxy.config.http.connect_ports
 proxy.config.http.default_buffer_size
 proxy.config.http.default_buffer_water_mark
 proxy.config.http.doc_in_cache_skip_dns
 proxy.config.http.down_server.abort_threshold
 proxy.config.http.down_server.cache_time
 proxy.config.http.enabled
 proxy.config.http.enable_http_info
 proxy.config.http.enable_http_stats
 proxy.config.http.enable_url_expandomatic
 proxy.config.http.errors.log_error_pages
 proxy.config.http.forward.proxy_auth_to_parent
 proxy.config.http.global_user_agent_header
 proxy.config.http.insert_age_in_response
 proxy.config.http.insert_request_via_str
 proxy.config.http.insert_response_via_str
 proxy.config.http.insert_squid_x_forwarded_for
 proxy.config.http.keep_alive_enabled_in
 proxy.config.http.keep_alive_enabled_out
 proxy.config.http.keep_alive_no_activity_timeout_in
 proxy.config.http.keep_alive_no_activity_timeout_out
 proxy.config.http.keep_alive_post_out
 proxy.config.http.negative_caching_enabled
 proxy.config.http.negative_caching_lifetime
 proxy.config.http.negative_revalidating_enabled
 proxy.config.http.negative_revalidating_lifetime
 proxy.config.http.no_dns_just_forward_to_parent
 proxy.config.http.no_origin_server_dns
 proxy.config.http.normalize_ae_gzip
 proxy.config.http.number_of_redirections
 proxy.config.http.origin_max_connections
 proxy.config.http.origin_min_keep_alive_connections
 proxy.config.http.parent_proxies
 proxy.config.http.parent_proxy.connect_attempts_timeout
 proxy.config.http.parent_proxy.fail_threshold
 proxy.config.http.parent_proxy.file
 proxy.config.http.parent_proxy.per_parent_connect_attempts
 proxy.config.http.parent_proxy.retry_time
 proxy.config.http.parent_proxy_routing_enable
 proxy.config.http.parent_proxy.total_connect_attempts
 proxy.config.http.post_connect_attempts_timeout
 proxy.config.http.post_copy_size
 proxy.config.http.push_method_enabled
 proxy.config.http.quick_filter.mask
 proxy.config.http.record_heartbeat
 proxy.config.http.redirection_enabled
 proxy.config.http.referer_default_redirect
 proxy.config.http.referer_filter
 proxy.config.http.referer_format_redirect
 proxy.config.http.request_header_max_size
 proxy.config.http.request_via_str
 proxy.config.http.response_header_max_size
 proxy.config.http.response_server_enabled
 proxy.config.http.response_server_str
 proxy.config.http.response_via_str
 proxy.config.http.send_http11_requests
 proxy.config.http.server_max_connections
 proxy.config.http.server_port
 proxy.config.http.server_port_attr
 proxy.config.http.share_server_sessions
 proxy.config.http.slow.log.threshold
 proxy.config.http.connect_ports
 proxy.config.http.transaction_active_timeout_in
 proxy.config.http.transaction_active_timeout_out
 proxy.config.http.transaction_no_activity_timeout_in
 proxy.config.http.transaction_no_activity_timeout_out
 proxy.config.http_ui_enabled
 proxy.config.http.uncacheable_requests_bypass_parent
 proxy.config.icp.default_reply_port
 proxy.config.icp.enabled
 proxy.config.icp.icp_configuration
 proxy.config.icp.icp_interface
 proxy.config.icp.icp_port
 proxy.config.icp.lookup_local
 proxy.config.icp.multicast_enabled
 proxy.config.icp.query_timeout
 proxy.config.icp.reply_to_unknown_peer
 proxy.config.icp.stale_icp_enabled
 proxy.config.io.max_buffer_size
 proxy.config.lm.pserver_timeout_msecs
 proxy.config.lm.pserver_timeout_secs
 proxy.config.lm.sem_id
 proxy.config.local_state_dir
 proxy.config.log.ascii_buffer_size
 proxy.config.log.auto_delete_rolled_files
 proxy.config.log.collation_host
 proxy.config.log.collation_host_tagged
 proxy.config.log.collation_max_send_buffers
 proxy.config.log.collation_port
 proxy.config.log.collation_retry_sec
 proxy.config.log.collation_secret
 proxy.config.log.common_log_enabled
 proxy.config.log.common_log_header
 proxy.config.log.common_log_is_ascii
 proxy.config.log.common_log_name
 proxy.config.log.custom_logs_enabled
 proxy.config.log.extended2_log_enabled
 proxy.config.log.extended2_log_header
 proxy.config.log.extended2_log_is_ascii
 proxy.config.log.extended2_log_name
 proxy.config.log.extended_log_enabled
 proxy.config.log.extended_log_header
 proxy.config.log.extended_log_is_ascii
 proxy.config.log.extended_log_name
 proxy.config.log.file_stat_frequency
 proxy.config.log.hostname
 proxy.config.log.hosts_config_file
 proxy.config.log.log_buffer_size
 proxy.config.log.logfile_dir
 proxy.config.log.logfile_perm
 proxy.config.log.logging_enabled
 proxy.config.log.max_line_size
 proxy.config.log.max_secs_per_buffer
 proxy.config.log.max_space_mb_for_logs
 proxy.config.log.max_space_mb_for_orphan_logs
 proxy.config.log.max_space_mb_headroom
 proxy.config.log.overspill_report_count
 proxy.config.log.rolling_enabled
 proxy.config.log.rolling_interval_sec
 proxy.config.log.rolling_offset_hr
 proxy.config.log.rolling_size_mb
 proxy.config.log.sampling_frequency
 proxy.config.log.search_log_enabled
 proxy.config.log.search_log_filters
 proxy.config.log.search_rolling_interval_sec
 proxy.config.log.search_server_ip_addr
 proxy.config.log.search_server_port
 proxy.config.log.search_top_sites
 proxy.config.log.search_url_filter
 proxy.config.log.separate_host_logs
 proxy.config.log.separate_icp_logs
 proxy.config.log.space_used_frequency
 proxy.config.log.squid_log_enabled
 proxy.config.log.squid_log_header
 proxy.config.log.squid_log_is_ascii
 proxy.config.log.squid_log_name
 proxy.config.log.xml_config_file
 proxy.config.manager_binary
 proxy.config.net.connections_throttle
 proxy.config.net.listen_backlog
 proxy.config.net_snapshot_filename
 proxy.config.net.sock_mss_in
 proxy.config.net.sock_option_flag_in
 proxy.config.net.sock_option_flag_out
 proxy.config.net.sock_recv_buffer_size_in
 proxy.config.net.sock_recv_buffer_size_out
 proxy.config.net.sock_send_buffer_size_in
 proxy.config.net.sock_send_buffer_size_out
 proxy.config.net.defer_accept
 proxy.config.output.logfile
 proxy.config.ping.npacks_to_trans
 proxy.config.ping.timeout_sec
 proxy.config.plugin.plugin_dir
 proxy.config.plugin.plugin_mgmt_dir
 proxy.config.prefetch.child_port
 proxy.config.prefetch.config_file
 proxy.config.prefetch.default_data_proto
 proxy.config.prefetch.default_url_proto
 proxy.config.prefetch.keepalive_timeout
 proxy.config.prefetch.max_object_size
 proxy.config.prefetch.max_recursion
 proxy.config.prefetch.prefetch_enabled
 proxy.config.prefetch.push_cached_objects
 proxy.config.prefetch.redirection
 proxy.config.prefetch.url_buffer_size
 proxy.config.prefetch.url_buffer_timeout
 proxy.config.process_manager.enable_mgmt_port
 proxy.config.process_manager.mgmt_port
 proxy.config.process_manager.timeout
 proxy.config.product_company
 proxy.config.product_name
 proxy.config.product_vendor
 proxy.config.proxy.authenticate.basic.realm
 proxy.config.proxy_binary
 proxy.config.proxy_binary_opts
 proxy.config.proxy_name
 proxy.config.remap.num_remap_threads
 proxy.config.res_track_memory
 proxy.config.reverse_proxy.enabled
 proxy.config.reverse_proxy.oldasxbehavior
 proxy.config.snapshot_dir
 proxy.config.socks.accept_enabled
 proxy.config.socks.accept_port
 proxy.config.socks.connection_attempts
 proxy.config.socks.default_servers
 proxy.config.socks.http_port
 proxy.config.socks.per_server_connection_attempts
 proxy.config.socks.server_connect_timeout
 proxy.config.socks.server_fail_threshold
 proxy.config.socks.server_retry_time
 proxy.config.socks.server_retry_timeout
 proxy.config.socks.socks_config_file
 proxy.config.socks.socks_needed
 proxy.config.socks.socks_timeout
 proxy.config.socks.socks_version
 proxy.config.srv_enabled
 proxy.config.ssl.CA.cert.filename
 proxy.config.ssl.CA.cert.path
 proxy.config.ssl.client.CA.cert.filename
 proxy.config.ssl.client.CA.cert.path
 proxy.config.ssl.client.cert.filename
 proxy.config.ssl.client.certification_level
 proxy.config.ssl.client.cert.path
 proxy.config.ssl.client.private_key.filename
 proxy.config.ssl.client.private_key.path
 proxy.config.ssl.client.verify.server
 proxy.config.ssl.enabled
 proxy.config.ssl.number.threads
 proxy.config.ssl.server.cert_chain.filename
 proxy.config.ssl.server.cert.path
 proxy.config.ssl.server.cipher_suite
 proxy.config.ssl.server.honor_cipher_order
 proxy.config.ssl.SSLv2
 proxy.config.ssl.SSLv3
 proxy.config.ssl.TLSv1
 proxy.config.ssl.compression
 proxy.config.ssl.server.multicert.filename
 proxy.config.ssl.server_port
 proxy.config.ssl.server.private_key.path
 proxy.config.stack_dump_enabled
 proxy.config.stat_collector.interval
 proxy.config.stat_collector.port
 proxy.config.stats.config_file
 proxy.config.stats.snap_file
 proxy.config.stats.snap_frequency
 proxy.config.syslog_facility
 proxy.config.system.mmap_max
 proxy.config.system.file_max_pct
 proxy.config.thread.default.stacksize
 proxy.config.udp.free_cancelled_pkts_sec
 proxy.config.udp.periodic_cleanup
 proxy.config.udp.send_retries
 proxy.config.update.concurrent_updates
 proxy.config.update.enabled
 proxy.config.update.force
 proxy.config.update.max_update_state_machines
 proxy.config.update.memory_use_mb
 proxy.config.update.retry_count
 proxy.config.update.retry_interval
 proxy.config.update.update_configuration
 proxy.config.url_remap.default_to_server_pac
 proxy.config.url_remap.default_to_server_pac_port
 proxy.config.url_remap.filename
 proxy.config.url_remap.pristine_host_hdr
 proxy.config.url_remap.remap_required
 proxy.config.user_name
 proxy.config.vmap.addr_file
 proxy.config.vmap.down_up_timeout
 proxy.config.vmap.enabled

=head1 LICENSE

 Simple Apache Traffic Server client object, to communicate with the local manager.

 Licensed to the Apache Software Foundation (ASF) under one or more
 contributor license agreements.  See the NOTICE file distributed with
 this work for additional information regarding copyright ownership.
 The ASF licenses this file to You under the Apache License, Version 2.0
 (the "License"); you may not use this file except in compliance with
 the License.  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.

=cut

#-=-=-=-=-=-=-=-= No more POD for you =-=-=-=-=-=-=-=- 
