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
#  parse_dispatcher.pm
#
#
#   Description:
#
#   
#
#

package parse_dispatcher;
require "parse_generic.pm";

require Exporter;

use strict Vars;

our @ISA = qw(Exporter);
our @EXPORT = qw();
our @EXPORT_OK = qw();
our $VERSION = 1.00;

our %instance_hash = ();

our %predefined_mappings =
(
 "syntest" => "parse_syntest",
 "jtest" => "parse_jtest",
 "ts" => "parse_ts"
 );

sub load_parser_module {
    my ($parser_name) = @_;
    my $module_load_str = $parser_name . ".pm";

    eval "require \"$module_load_str\";";
    if ($@) {
	warn "Unable to load module $module_load_str : $@\n";
	return 1;
    } else {
	return 0;
    }
}

sub process_test_log_line {
    my ($line) = @_;
    my $r = "unknown";

    if ($line =~ /^\[\w{3} \w{3}\s{1,2}\d{1,2} \d{1,2}:\d{1,2}:\d{1,2}\.\d{1,3} ([^\]]+) ([^\]]+)\]\s+(.*)/) {

	my $instance = $1;
	my $level = $2;
	my $rest_of_line = $3;

	if ($instance eq "log_parse" && $level eq "directive") {
	    my ($directive, $d_instance, $d_parser) = split(/ /, $rest_of_line, 3);

	    if ($directive eq "log-parser-set") {
		my $load_result = load_parser_module($d_parser);

		if ($load_result == 0) {
		    $instance_hash{$d_instance} = $d_parser;
		    $r = "ok";
		} else {
		    $r = "error";
		}
	    } else {
		warn("bad directive sent to log_parse\n");
		$r = "warning";
	    }
	} else {

	    if (! $instance_hash{$instance}) {

		my $found_module = 0;
		if ($instance =~ /^(\D+)\d+$/) {
		    if ($predefined_mappings{$1}) {
			my $module_str = $predefined_mappings{$1};

			if ($module_str) {
			    my $load_result = load_parser_module($module_str);

			    if ($load_result == 0) {
				$instance_hash{$instance} = $module_str;
				$found_module = 1;
			    }
			}
		    }
		}

		if ($found_module == 0) {
		    $instance_hash{$instance} = "parse_generic";
		}
	    }

	    my $module_name = $instance_hash{$instance};
	    my $cmd = "\$r = " . $module_name . "::" . "process_test_log_line(\$instance, \$level, \\\$rest_of_line);";
	    eval $cmd;

	    if ($@) {
		warn "##### eval failed: $@\n";
	    }
	}
    }

    return $r;
}
