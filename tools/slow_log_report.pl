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
#use Data::Dumper;

sub addStat($$$) {
  my($stats, $key, $value) = @_;
  #print "$key $value\n";
  $stats->{$key}->{total} = 0 if (! defined $stats->{$key}->{total});
  $stats->{$key}->{count} = 0 if (! defined $stats->{$key}->{count});
  return if (! ($value =~ m|^-?\d+\.?\d*$|));
  #print "$key\n";
  $stats->{$key}->{total} += $value if $value >= 0;
  $stats->{$key}->{count}++ if $value >= 0;
  push(@{$stats->{$key}->{values}}, $value)  if $value >= 0;
}

sub displayStat($) {
  my($stats) = @_;

  printf("%25s %10s %10s %10s %10s %10s %10s %10s %10s\n", 'key', 'total', 'count', 'mean', 'median', '95th', '99th', 'min', 'max');
  foreach my $key ('ua_begin', 'ua_first_read', 'ua_read_header_done', 'cache_open_read_begin', 'cache_open_read_end', 'dns_lookup_begin', 'dns_lookup_end', 'server_connect', 'server_connect_end', 'server_first_read', 'server_read_header_done', 'server_close', 'ua_close', 'sm_finish') {

    my $count = $stats->{$key}->{count};
    my $total = $stats->{$key}->{total};
    if (!defined $stats->{$key}->{values}) {
      next;
      #print "$key\n";
      #die $key;
    }
    my @sorted = sort {$a <=> $b} @{$stats->{$key}->{values}};
    my $median = $sorted[int($count/2)];
    my $p95th = $sorted[int($count * .95)];
    my $p99th = $sorted[int($count * .99)];
    my $min = $sorted[0];
    my $max = $sorted[$count - 1];
    my $mean = 0;
    $mean = $total / $count if $count > 0;

    printf("%25s %10.2f %10.2f %10.2f %10.2f %10.2f %10.2f %10.2f %10.2f\n", $key, $total, $count, $mean, $median, $p95th, $p99th, $min, $max);
  }
  print "NOTE: Times are in seconds\n";
}

{
  my %stats;

  while (<>) {
    chomp;
    s/unique id/unique_id/;
    s/server state/server_state/;
    s/client state/client_state/;
    if (m|Slow Request: .+ (ua_begin: .+)|) {
      my %data = split(/: | /, $1);
      foreach my $key (keys %data) {
	next if (!defined $data{$key});
	#print "$key $data{$key}\n";
        addStat(\%stats, $key, $data{$key});
      }
    }
  }

  displayStat(\%stats);
}
