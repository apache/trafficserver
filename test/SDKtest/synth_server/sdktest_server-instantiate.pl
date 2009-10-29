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
#  sdktest_server-instantiate.pl
#
#
#  Description:
#
#  DEFT framework instantiator for sdktest_server
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
    $cmd_line = "cmd_line: $bin_dir/SDKtest_server";
} else {
    warn("bin_dir not sent\n");
    exit(1);
}

$run_dir = $input_args{"run_dir"};
if (!$run_dir) {
    warn("run_dir not sent\n");
}

# Figure out port and add it to command line
$ports = $input_args{"ports_avail"};

if ($ports =~ /^(\d+)-(\d+)/) {
    # range of port available = [$start_port - $end_port]
    $start_port = $1;
    $end_port = $2;

    print OUTPUT "ports_used: 1\n";
    $cmd_line = $cmd_line . " -p$start_port";
    print OUTPUT "port_binding: server $start_port\n";
} else {
    warn("ports_avail invalid\n");
}

$config_blob = $input_args{"config_file"};

if ($config_blob) {
    open(CONFIG_IN, "< $config_blob") || die "Couldn't open config file: $!";
    $config = <CONFIG_IN>;
    chomp($config);
    close(CONFIG_IN);

    $cmd_line = $cmd_line . " " . $config;
}

print OUTPUT $cmd_line . "\n";


# Create symlinks on config files necessary to execution of SDKtest_server 

%files_to_link =
(
  "SDKtest_server.config" => "SDKtest_server.config"
);


foreach $f (keys(%files_to_link)) {
    $link_from =  $bin_dir . "/" . $f;
    $link_to =  $run_dir . "/" . $files_to_link{$f};
    symlink($link_from, $link_to);
}

exit(0);

