#!/usr/bin/perl

#
## Licensed to the Apache Software Foundation (ASF) under one
## or more contributor license agreements.  See the NOTICE file
## distributed with this work for additional information
## regarding copyright ownership.  The ASF licenses this file
## to you under the Apache License, Version 2.0 (the
## "License"); you may not use this file except in compliance
## with the License.  You may obtain a copy of the License at
##
##      http://www.apache.org/licenses/LICENSE-2.0
##
## Unless required by applicable law or agreed to in writing, software
## distributed under the License is distributed on an "AS IS" BASIS,
## WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
## See the License for the specific language governing permissions and
## limitations under the License.

use strict;
use warnings;

{
  print "sni:\n";
  while (<>) {
    chomp;
    #print "y $_\n";
    if (m|fqdn\s+=\s+'(\S+)',|) {
      my $fqdn = $1;
      if ($fqdn =~ m|\*|) {
        $fqdn = "'" . $fqdn . "'";
      }
      print "- fqdn: $fqdn\n";
      while (<>) {
        chomp;
        last if (m|},|);
        #print "x $_\n";
        if (m|(\w+)\s+=\s+'(\S+)',?|) {
          my $key = $1;
          my $value = $2;
          if ($key eq 'verify_server_policy') {
            $value = 'PERMISSIVE' if ($value eq 'moderate');
            $value = 'DISABLED' if ($value eq 'disabled');
          }
          print "  $key: $value\n"
        }
      }
    }
  }
}
