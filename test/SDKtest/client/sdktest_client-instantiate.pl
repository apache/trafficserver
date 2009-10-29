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
#  sdktest_client-instantiate.pl
#
#
#  Description:
#
#  DEFT framework instantiator for SDKtest client 
#
#


open(OUTPUT,">&=$ARGV[1]") || die "Couldn't open output: $!";

while ($tmp = <STDIN>) {
    print $tmp;
    if ($tmp =~ /^([^:]+):\s(.*)\n/) {
	$input_args{$1} = $2;
    }
}

$cmd_line = "";
$bin_dir = $input_args{"bin_dir"};
if ($bin_dir) {
    $cmd_line = "cmd_line: $bin_dir/SDKtest_client";
} else {
    warn("bin_dir not sent\n");
    exit(1);
}

# Figure out execution directory
$run_dir = $input_args{"run_dir"};
print OUTPUT $cmd_line . "\n";

#useful for debugging
#warn("cmdline: $cmd_line\n");


# Now get the config args passed to the DEFT create command
# Then generate a SDKtest_client.config


$config_blob = $input_args{"config_file"};
if ($config_blob) {
    open(CONFIG_IN, "< $config_blob") || die "Couldn't open config file: $!";
    while ($config = <CONFIG_IN>) {
	if ($config =~ /^([^:]+):\s(.*)\n/) {
	    $config_args{$1} = $2;
	}
    }
    close(CONFIG_IN);
} else {
    warn("config_file not sent\n");
    exit(1);
}


#
# Generate a SDKtest_client.config accrodingly to parameters
# passed to the instantiator.
# 
# Note: it would be better to have a more generic way of parsing
# parameters...
#

# We expect to have parameters:
#  proxy_host              mandatory
#  proxy_port              mandatory
#  server_port             mandatory
#  server_port             mandatory
#  users                   optional   default=100
#  hitrate                 optional   default=40
#  execution_interval      optional   default=60 (in seconds)
#  keepalive               optional   default=1  (number of txn on same connection. 1=NO keepalive)

# WATCHOUT ! "plugin" is not a parameter of SDKtest_client.config
# The right way for loading a plugin is to use the option "-p<plugin_name.so>"
# from the command line (i.e. from the test script).

# Mandatory parameters

if (!$config_args{"proxy_host"}   || !$config_args{"proxy_port"} ||
	!$config_args{"server_host"}  || !$config_args{"server_port"} ) {
    warn("failed to specify proxy_host, proxy_port, server_host or server_port in config\n");
    exit(1);
}


$client_config_file = $run_dir . "/SDKtest_client.config";
open (CLIENT_CFG, "> $client_config_file") || die "Can not write SDKtest_client.config: $!\n";

$proxy_host = $config_args{"proxy_host"};
print CLIENT_CFG "target_host = $proxy_host\n";
delete $config_args{"proxy_host"};

$proxy_port = $config_args{"proxy_port"};
print CLIENT_CFG "target_port = $proxy_port\n";
delete $config_args{"proxy_port"};

$server_host = $config_args{"server_host"};
$server_port = $config_args{"server_port"};
print CLIENT_CFG "origin_servers = $server_host:$server_port\n"; 
delete $config_args{"proxy_port"};

# Optional parameters

# "users" is an optional parameter. Default value 100
$users = $config_args{"users"};
if (!$users) {
	$users = 100;
}
print CLIENT_CFG "users = $users\n";
delete $config_args{"users"};

# "hitrate" is an optional parameter. Default value is 40
$hitrate = $config_args{"hitrate"};
if (!$hitrate) {
	$hitrate = 40;
}
print CLIENT_CFG "hitrate = $hitrate\n";
delete $config_args{"hitrate"};

# "keepalive" is an optional parameter. Default value is 1 
$keepalive = $config_args{"keepalive"};
if (!$keepalive) {
	$keepalive = 1;
}
print CLIENT_CFG "keepalive = $keepalive\n";
delete $config_args{"keepalive"};

# "execution_interval" is an optional parameter. Default value is 60
$execution_interval = $config_args{"execution_interval"};
if (!$execution_interval) {
	$execution_interval = 40;
}
print CLIENT_CFG "execution_interval = $execution_interval\n";
delete $config_args{"execution_interval"};

#
# Custom parameters: All remaining args are considered as custom parameters
# and are written as they are to the config file
#
foreach $arg_name (keys(%config_args)) {
	$arg_value = $config_args{$arg_name};
	print CLIENT_CFG "$arg_name = $arg_value\n";
}


print CLIENT_CFG << 'EOF';
clients = localhost 
client_program = SDKtest_client
reporting_interval = 1
hotset = 100
docset = 1000000000
warmup = 0
docsize_dist_file = docsize.real
byterate_dist_file = byterate.fast
thinktime_dist_file = thinktime.0
keepalive = 1
histogram_max = 30
histogram_resolution = 0.5
connect_cutoff    =  500
first_byte_cutoff = 1000
round_trip_cutoff = 2000
debug = 0 

EOF
close(CLIENT_CFG);    


# Create symlinks on config files necessary to execution of SDKtest_client 

%files_to_link =
(
  "byterate.3000" => "byterate.3000",
  "byterate.fast" => "byterate.fast",
  "byterate.modem" => "byterate.modem",
  "docsize.10000" => "docsize.10000",
  "docsize.isp" => "docsize.isp",
  "docsize.real" => "docsize.real",
  "docsize.specweb" => "docsize.specweb",
  "thinktime.0" => "thinktime.0",
  "thinktime.1sec" => "thinktime.1sec",
  "thinktime.slow" => "thinktime.slow",
);


foreach $f (keys(%files_to_link)) {
    $link_from =  $bin_dir . "/" . $f;
    $link_to =  $run_dir . "/" . $files_to_link{$f};
	symlink($link_from, $link_to);
}

exit(0);

