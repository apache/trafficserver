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
#  parse_fftools.pm
#
#   Description:
#     Generic parser for ffqtload, ffwmload2, ffrmload2, and the other ff
#     media load testers.
#
#   
#
#

package parse_fftools;
require Exporter;

use strict Vars;

our @ISA = qw(Exporter);
our @EXPORT = qw();
our @EXPORT_OK = qw();
our $VERSION = 1.00;

sub process_test_log_line {
    my ($instance_id, $level, $line) = @_;

    if ($$line =~ /DESCRIBE returned error 401/ ||
	$$line =~ /Exceeded max number of authentication attempts/ ||
	$$line =~ /80070005/) {
	# in some tests, we would like to see how MIXT logs failed auth 
	# attempts; therefore, we leave it up to the test to check for the
	# appropriate log entries
	return "ok";
    } elsif ($$line =~ /error/i ||
	     $$line =~ /Abort/i ||
	     $$line =~ /Fatal/i ||
	     $level =~ /error/i ) {
	return "error";
    } elsif ($$line =~ /always\]/) {
	return "ok";
    } elsif ($$line =~ /udp warning\] Allocated/) {
	return "ok";
    } elsif ($$line =~ /warning/i ||
	     $level =~ /warning/i) {
	return "warning";
    }  elsif ($level eq "stderr") {
	return "error";
    } else {
	return "ok";
    }
}
