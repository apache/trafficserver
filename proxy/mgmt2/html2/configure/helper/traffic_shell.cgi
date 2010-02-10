#!/usr/bin/perl

#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
$ENV {'REQUEST_METHOD'} =~ tr/a-z/A-Z/;
if ($ENV{'REQUEST_METHOD'} eq "POST") {
    read(STDIN, $buffer, $ENV{'CONTENT_LENGTH'});
} else {
    $buffer = $ENV{'QUERY_STRING'};
}
@pairs = split(/&/, $buffer);
foreach $pair (@pairs) {
    ($name, $value) = split(/=/, $pair);
    $value =~ tr/+/ /;
    $value =~ s/%(..)/pack("C", hex($1))/eg;
    $ENV{$name} = $value;
}

# handle post data here!
#foreach $key (keys(%ENV)) {
#    print "$key = $ENV{$key}<br>";
#}

# find out current InktomiHome
my $path = $ENV{ROOT} || $ENV{INST_ROOT};
if (!$path) {
  if (open(fh, "/etc/traffic_server")) {
    while (<fh>) {
      chomp;
      $InktomiHome = $_;
      last;
    }
  } else {
    $InktomiHome = "/home/trafficserver";
  }
} else {
  $InktomiHome = $path;
}

exec("${InktomiHome}/bin/start_traffic_shell -f ${InktomiHome}/share/yts_ui/configure/helper/otwu.tcl");
exit 0;
