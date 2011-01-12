#!/inktest/dist/bin/perl

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

#
#  ts-instantiate.pl
#
#
#  Description:
#    script to manage traffic_server under the DEFT testing
#    framework
#
#  
#
#

use strict vars;
use English;
use File::Copy;

our $bin_dir;
our $run_dir;

our $base_port = 0;
our @additional_config_entries = ();
our %override;
our @remap_lines;
our @filter_lines;
our @ipnat_lines;

my @user_info = getpwuid $UID;
my $default_user = $user_info[0];

#####################################
#                                   #
# Finding the ethernet interface    #
#                                   #
#####################################

# Defaults in case we can't access ifconfig
our %default_interface = 
(
 "solaris" => "hme0",
 "dec_osf" => "tu0",
 "irix" => "ef0",
 "freebsd" => "de0",
 "linux" => "eth0",
 "hpux" => "lan0"
);


sub get_default_if {
    my $dif = $default_interface{$OSNAME};

    if (! $dif) {
	die "Unknown OS type, could determine default internface\n";
    }

    return $dif;
}

sub process_if_record {
    my ($if_name, $if_info) = @_;

    if ($if_name eq "") {
	return 0;
    }

    # Do not return loop back addrs
    if ($if_name =~ /^lo/) {
	return 0;
    }

    # Check to see if the address has an address
    if ($if_info =~ /(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})/) {
	# If it's not localhost, it's good enough
	if ($1 != 127 || $2 == 0 || $3 == 0 || $4 == 1) {
	    return 1;
	}
    }

    return 0;
}

sub find_ethernet_if {

    if (!open(IFCONFIG, "ifconfig -a |") && 
	!open(IFCONFIG, "/usr/sbin/ifconfig -a |")) {
	    warn "Warning: Could not open ifconfig : $! : guessing interface\n";
	    return get_default_if();
    }

    # Basic strategy is loop over the interfaces and use the first
    #  one that is up that is not localhost
    #
    # Between the various OS's there are several different formats
    #  for ifconfig output.  They have in common that interfaces
    #  start the line with "<if_name>:"   Some put new line between
    #  interfaces and others do not
    my $if_name = "";
    my $if_info = "";
    my $tmp;
    my $done;
    while ($tmp = <IFCONFIG>) {
	chomp($tmp);
	if ($tmp =~ /^(\w+):(.*)/) {

	    # We've got a new record, process old info
	    my $new_name = $1;
	    my $new_info = $2;
	    
	    if (process_if_record($if_name, $if_info)) {
		return $if_name;
	    }

	    $if_name = $new_name;
	    $if_info = $new_info;
	    
	} elsif ($tmp =~ /^\s*$/) {

	    # Blank lines tell us the old record is done 
	    if (process_if_record($if_name, $if_info)) {
		return $if_name;
	    }

	    $if_name = "";
	    $if_info = "";
		
	} else {
	    $if_info = $if_info . " " . $tmp;
	}
    }

    if (process_if_record($if_name, $if_info)) {
	return $if_name;
    }
    
    return get_default_if;
}

my $eth_if = find_ethernet_if();

###############*######################
#                                    #
# End: finding ethernet interface    #
#                                    #
######################################

our %defaults =
(
 "proxy.config.socks.socks_server_port" => 1080,
 "proxy.config.proxy_name" => "ink-proxy.example.com",
 "proxy.config.bin_path" => "bin",
 "proxy.config.alarm_email" => $default_user,
 "proxy.config.cluster.mc_group_addr" => "225.0.1.89",
 "proxy.config.admin.html_doc_root" => "ui",
 "proxy.config.admin.admin_user" => "admin",
 "proxy.config.admin.admin_password" => "21232F297A57A5A743894A0",
 "proxy.config.admin.user_id" => $default_user,
 "proxy.config.net.connections_throttle" => 8000,
 "proxy.config.cluster.ethernet_interface" => $eth_if,
 "proxy.config.icp.icp_interface" => $eth_if,
 "proxy.config.hostdb.size" => 200000,
 "proxy.config.reverse_proxy.enabled" => 0,
 "proxy.config.loadshedding.max_connections" => 0,
 );

our %export_ports =
(
 "proxy.config.http.server_port" => "tsHttpPort",
);

our %base_port_hash  =
(
 "proxy.config.http.server_port" => 0,
 "proxy.config.admin.web_interface_port" => 2,
 "proxy.config.admin.overseer_port" => 3,
 "proxy.config.admin.autoconf_port" => 4,
 "proxy.config.process_manager.mgmt_port" => 5,
 "proxy.config.log.collation_port" => 7,
 "proxy.config.cluster.cluster_port" => 8,
 "proxy.config.cluster.rsport" => 10,
 "proxy.config.cluster.mcport" => 11,
 );

our %config_meta = ();

sub process_records_blob {
    my $line;
    while ($line = <CONFIG_BLOB>) {
	if ($line =~ /^\[([^\]]*)\]\s*$/) {
	    return $1;
	}

	my ($cmd, $arg1, $arg2, $arg3, $arg4) = split(/\s+/, $line, 5);

	if ($cmd eq "base_port") {
	    $base_port = $arg1;
	} elsif ($cmd eq "add") {
	    my $config_entry = $arg1 . " " . $arg2 . " " . $arg3 . " " . $arg4;
	    push @additional_config_entries, $config_entry;
	} else {
	    $override{$cmd} = $arg1;
	}
    }

    return "";
}

sub process_remap_config {
    my $line;
    while ($line = <CONFIG_BLOB>) {
	if ($line =~ /^\[([^\]]*)\]\s*$/) {
	    return $1;
	} else {
	    push(@remap_lines, $line);
	}
    }
    return "";
}

sub process_filter_config {
    my $line;
    while ($line = <CONFIG_BLOB>) {
	if ($line =~ /^\[([^\]]*)\]\s*$/) {
	    return $1;
	} else {
	    push(@filter_lines, $line);
	}
    }
    return "";
}

sub process_ipnat_conf {
    my $line;
    while ($line = <CONFIG_BLOB>) {
	if ($line =~ /^\[([^\]]*)\]\s*$/) {
	    return $1;
	} else {
	    push(@ipnat_lines, $line);
	}
    }
    return "";
}

sub process_meta_config {
    my $line;
    while ($line = <CONFIG_BLOB>) {
	if ($line =~ /^\[([^\]]*)\]\s*$/) {
	    return $1;
	} else {
	    if ($line =~ /(\S+):\s+(\S+)/) {
		print "meta config '$1' = '$2'\n";
		$config_meta{$1} = $2;
	    } elsif ($line !~ /^\s*$/) {
		warn("Malformed meta config: $line\n");
	    }
	}
    }
    return "";
}

sub process_other_file_blob {
    my ($file_name) = @_;
    my $file_path = $run_dir . "/etc/trafficserver/" . $file_name;
    
    open(OTHER_FILE, "> $file_path") || die "Failed to write file $file_name: $!\n";

    my $line;
    while ($line = <CONFIG_BLOB>) {
	if ($line =~ /^\[([^\]]*)\]\s*$/) {
	    close(OTHER_FILE);
	    return $1;
	}
	print OTHER_FILE $line;
    }

    close (OTHER_FILE);
    return "";
};

sub read_test_config_blob {
    my ($test_config_blob) = @_;

    open (CONFIG_BLOB, "< $test_config_blob") || die "Failed to open config_blob: $!\n";

    my $line;
    my $file_name = "";
    while ($line = <CONFIG_BLOB>) {

	if ($line =~ /^\[([^\]]*)\]\s*$/) {
	     $file_name= $1;
	     last;
	 }
    }

    while ($file_name ne "") {
	if ($file_name eq "records.config") {
	    $file_name = process_records_blob();
	} elsif ($file_name eq "remap.config") {
	    $file_name = process_remap_config();
	} elsif ($file_name eq "filter.config") {
	    $file_name = process_filter_config();
	} elsif ($file_name eq "ipnat.conf") {
	    $file_name = process_ipnat_conf();
	} elsif ($file_name eq "meta") {
	    $file_name = process_meta_config();
	} else {
	    # any other file is writting to config 
	    $file_name = process_other_file_blob($file_name);
	}
    }

    close (CONFIG_BLOB);
}

my %records_seen = {};

sub output_records_config {
    my ($records_config_in, $records_config_out, $port_start) = @_;

    open (RECORDS_IN, "< $records_config_in") ||
	die "Couldn't open $records_config_in: $!\n";

    open (RECORDS_OUT, "> $records_config_out") ||
	die "Couldn't open $records_config_out: $!\n";

    my $rec_line;
    my $num_ports_alloc = 0;

    while($rec_line = <RECORDS_IN>) {
	my $new_line;
	if ($rec_line =~ /^\s*#/) {
	    $new_line = $rec_line;
	} else {
	    my $changed = 0;
	    my $tmp;
	    my ($r_class, $r_name, $r_type, $r_val) = split (/\s+/, $rec_line, 4);

	    # Look at defaults
	    if (defined($tmp = $defaults{$r_name})) {
		$r_val = $tmp;
		$changed = 1;
	    }
	    
	    # Look at the port stuff
	    if (defined($tmp = $base_port_hash{$r_name})) {
		$r_val = $port_start + $num_ports_alloc;
		$changed = 1;
		$num_ports_alloc++;
	    }

	    # Look at rc overrides
	    if (defined($tmp = $override{$r_name})) {
		$r_val = $tmp;
		$changed = 1;
	    }

	    $records_seen{$r_name} = $r_val;

	    if ($changed) {
		$new_line = $r_class . " " . $r_name . " " . "$r_type" . " " . "$r_val" . "\n";
	    } else {
		$new_line = $rec_line;
	    }
	}

	print RECORDS_OUT $new_line;
    }

    close(RECORDS_IN);

    my $config_entry;
    foreach $config_entry (@additional_config_entries) {
	my ($r_class, $r_name, $r_type, $r_val) = split (/\s+/, $config_entry, 4);

	if ($records_seen{$r_name}) {
	    # We already has an entry for this record so ignore the addition
	    next;
	}

	if ($r_val eq "<alloc_port>") {
	    my $tmp;
	    if (defined($tmp = $base_port_hash{$r_name})) {
		$r_val = $num_ports_alloc + $port_start;
		$config_entry = $r_class . " " . $r_name . " " . "$r_type" . " " . "$r_val" . "\n";
		$num_ports_alloc++;
	    }
	}

	$records_seen{$r_name} = $r_val;	
	print RECORDS_OUT $config_entry . "\n";
    }

    close(RECORDS_OUT);

    return $num_ports_alloc;
}

sub output_remap_config {
    my $remap_file = $run_dir . "/etc/trafficserver/remap.config";
    
    open(REMAP_FILE, "> $remap_file") || die "Failed to write file $remap_file: $!\n";

    my $line;
    while ($line = shift(@remap_lines)) {
	chomp($line);
	my $output_line = "";
	while ($line) {
	    if ($line =~ /([^\(]*)&&\(([^\)]+)\)(.*)/) {
		$output_line = $output_line . $1;
		my $rec_var = $2;

		if ($records_seen{$rec_var}) {
		    $output_line = $output_line . $records_seen{$rec_var};
		} else {
		    $output_line = $output_line . "&&(" . $2 . ")";
		}
		$line = $3;
	    } else {
		$output_line = $output_line . $line;
		$line = "";
	    }
	}

	print REMAP_FILE "$output_line\n";
    }

    close(REMAP_FILE);
}

sub output_ipnat_conf {
    my $ipnat_file = $run_dir . "/etc/trafficserver/ipnat.conf";
    
    open(IPNAT_FILE, "> $ipnat_file") || die "Failed to write file $ipnat_file: $!\n";

    my $line;
    while ($line = shift(@ipnat_lines)) {
	chomp($line);
	my $output_line = "";
	while ($line) {
	    if ($line =~ /([^\(]*)&&\(([^\)]+)\)(.*)/) {
		$output_line = $output_line . $1;
		my $rec_var = $2;

		if ($records_seen{$rec_var}) {
		    $output_line = $output_line . $records_seen{$rec_var};
		} else {
		    $output_line = $output_line . "&&(" . $2 . ")";
		}
		$line = $3;
	    } else {
		$output_line = $output_line . $line;
		$line = "";
	    }
	}

	print IPNAT_FILE "$output_line\n";
    }

    close(IPNAT_FILE);
}


sub output_filter_config {
    my $filter_file = $run_dir . "/etc/trafficserver/filter.config";
    
    open(FILTER_FILE, "> $filter_file") || die "Failed to write file $filter_file: $!\n";

    my $line;
    while ($line = shift(@filter_lines)) {
	chomp($line);
	my $output_line = "";
	while ($line) {
	    if ($line =~ /([^\(]*)&&\(eval(.)(.*)\2\)(.*)/) {
		$output_line = $output_line . $1;
		# print "eval|" . $3 . "|\n";
		my $tmp = eval $3;
		$output_line = $output_line . $tmp;
		$line = $4;
	    } elsif ($line =~ /([^\(]*)&&\(([^\)]+)\)(.*)/) {
		$output_line = $output_line . $1;
		my $rec_var = $2;

		if ($records_seen{$rec_var}) {
		    $output_line = $output_line . $records_seen{$rec_var};
		} else {
		    $output_line = $output_line . "&&(" . $2 . ")";
		}
		$line = $3;
	    } else {
		$output_line = $output_line . $line;
		$line = "";
	    }
	}

	print FILTER_FILE "$output_line\n";
    }

    close(FILTER_FILE);
}

sub build_port_bindings {
    my ($records_config) = @_;
    my $output_line = "port_binding:";

    open (RECORDS, "< $records_config") ||
	die "Couldn't open $records_config: $!\n";

    my $rec_line;

    while($rec_line = <RECORDS>) {
	if ($rec_line !~ /^\s*#/) {
	    my ($r_class, $r_name, $r_type, $r_val) = split (/\s+/, $rec_line);

	    if ($export_ports{$r_name}) {
		$output_line = $output_line . " " . $export_ports{$r_name} . " " . $r_val;
	    }
	}
    }

    return $output_line;
}

our @populate_dirs = ( "bin", "etc/trafficserver", "etc/trafficserver/internal", "logs");

our %populate_symlinks =
( "bin/traffic_server" => "bin/traffic_server",
  "bin/traffic_manager" => "bin/traffic_manager",
  "bin/traffic_cop" => "bin/traffic_cop",
  "bin/traffic_line" => "bin/traffic_line",
  "bin/start_traffic_server" => "bin/start_traffic_server",
  "bin/stop_traffic_server" => "bin/stop_traffic_server",
  "bin/filter_to_policy" => "bin/filter_to_policy",
  "etc/trafficserver/body_factory" => "etc/trafficserver/body_factory",
  "etc/trafficserver/plugins" => "etc/trafficserver/plugins",
  "lib" => "lib",
  "ui" => "ui"
);

our %populate_exclude_config =
(
 "records.config" => 1,
 "records.config.shadow" => 1,
 "storage.config.shadow" => 1
);

sub populate_run_dir() {
    my ($dir, $from_file_key);

    foreach $dir (@populate_dirs) {
	my $rdir = $run_dir . "/" . $dir;
	if (! -d $rdir) {
	    mkdir($rdir);
	}
    }

    foreach $from_file_key (keys (%populate_symlinks)) {
	my $from_file = $bin_dir . "/" . $from_file_key;
	my $to_file = $run_dir . "/" . $populate_symlinks{$from_file_key};

	if (! -e $to_file && ! -l $to_file) {
	    symlink($from_file, $to_file);
	}
    }

    my $config_dir_source = $bin_dir . "/etc/trafficserver";
    my $config_dir_target = $run_dir . "/etc/trafficserver";

    opendir(CONFIG_SOURCE_DIR, $config_dir_source) ||
	die "Couldn't open config src dir: $!\n";

    my $dir_ent;
    while ($dir_ent = readdir(CONFIG_SOURCE_DIR)) {
	if (! $populate_exclude_config{$dir_ent}) {
	    my $source_file = $config_dir_source . "/" . $dir_ent;
	    if (-f $source_file) {
		my $target_file = $config_dir_target . "/" . $dir_ent;
		copy($source_file, $target_file) || die "Copy of $dir_ent failed: $!\n";
	    }
	}
    }
}

open(OUTPUT,">&=$ARGV[1]") || die "Couldn't open output: $!";

my $tmp;
my %input_args;

while ($tmp = <STDIN>) {
    print $tmp;
    if ($tmp =~ /^([^:]+):\s(.*)\n/) {
	$input_args{$1} = $2;
    }
}

my $records_config_in = "";
$bin_dir = $input_args{"bin_dir"};

if ($bin_dir) {
    $records_config_in = $bin_dir . "/etc/trafficserver/records.config";
} else {
    warn("bin_dir not sent\n");
    exit(1);
}

$run_dir = $input_args{"run_dir"};
my $no_run_dir = $input_args{"no_run_dir"};

my $records_config_out;
if ($no_run_dir) {
    $records_config_out = $bin_dir . "/etc/trafficserver/records.config.shadow";
    $run_dir = $bin_dir;
} else {
    $records_config_out = $run_dir . "/etc/trafficserver/records.config";
}

if (!$run_dir) {
    warn("run_dir not sent\n");
    exit(1);
}

if (! $no_run_dir) {
    populate_run_dir();
}

my $ports = $input_args{"ports_avail"};

my $config_blob = $input_args{"config_file"};
if (!$config_blob) {
    die("config_blob not sent\n");
}

read_test_config_blob($config_blob);

my ($start_port, $end_port);
if ($ports =~ /^(\d+)-(\d+)/) {
    $start_port = $1;
     $end_port = $2;
} else {
    warn("ports_avail invalid\n");
}

my $use_start_port = $base_port;
if ($use_start_port == 0) {
    $use_start_port = $start_port;
}

my $ports_used;

$ports_used = output_records_config($records_config_in,
				    $records_config_out, $use_start_port);

if (scalar(@remap_lines) > 0) {
    output_remap_config();
}

if (scalar(@filter_lines) > 0) {
    output_filter_config();
}

if (scalar(@ipnat_lines) > 0) {
    output_ipnat_conf();
}

my $cmd_line = "cmd_line: $bin_dir/bin/";
if ($config_meta{"run_manager"}) {
   $cmd_line = $cmd_line . "traffic_manager";
} else {
   $cmd_line = $cmd_line . "traffic_server";
}

if ($use_start_port == $start_port) {
    print OUTPUT "ports_used: $ports_used\n";
}

my $bindings = build_port_bindings($records_config_out);
print OUTPUT $bindings . "\n";

print OUTPUT $cmd_line . "\n";

#
my $ld_lib_path = $ENV{"LD_LIBRARY_PATH"};

if ($ld_lib_path eq "") {
    $ld_lib_path = $run_dir . "/bin";
    $ld_lib_path .= $run_dir . "/lib";
} else {
    $ld_lib_path  = $ld_lib_path . ":" . $run_dir . "/bin";
    $ld_lib_path .= $ld_lib_path . ":" . $run_dir . "/lib";
}

print OUTPUT "env_vars: LD_LIBRARY_PATH=" . $ld_lib_path . "\n";

exit(0);
