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

# This script requires that you run futexes.stp system tap script first
# https://sourceware.org/systemtap/examples/#process/futexes.stp

use strict;
use warnings;
use Getopt::Std;

sub usage() {
  print "gdb_mutex_contention.pl -f futexes_output [-s symbols] [-m mutexes]\n";
  print "Example:\n";
  print "\tsudo timeout 30 ./futexes.stp > futexes.out\n";
  print "\tsudo ./gdb_mutex_contention.pl -f futexes.out\n";

  exit 1;
}

{
  my %opts;
  getopts('m:f:s:', \%opts);
  usage() if (! defined$opts{f});

  my $pid = 0;
  my $file = $opts{f};
  my $symbols = $opts{s} || 20;
  my $mutexes = $opts{m} || 10;
  my %locks;

  # Read output from futexes.stp
  open(INPUT, $file) || die $!;
  while(<INPUT>) {
    chomp;
    if (m/(ET_NET|traffic_server).+\[(\d+)\] lock (\S+) contended (\d+) times, (\d+) avg us/) {
      $pid = $2;
      my $lock = $3;
      my $frequency = $4;
      my $wait = $5;
      $locks{$lock}->{frequency} = $frequency;
      $locks{$lock}->{line} = $_;
      $locks{$lock}->{wait} = $wait * $frequency;
    }
  }

  # Grab the binary from the running pid
  my $binary = `ps --pid $pid -o command -h`;
  $binary = (split(' ', $binary))[0];
  chomp $binary;

  # Print out what we are going to run gdb on
  print "Running gdb over binary: $binary and pid: $pid\n";

  # Loop over the futexes that had the highest total wait time
  my $count = 0;
  foreach my $lock (sort { $locks{$b}->{wait} <=> $locks{$a}->{wait} } keys %locks) {
    print "lock: $locks{$lock}->{line} - total wait: $locks{$lock}->{wait}\n";

    open(MACRO, "> gdb.auto.mutex.macro") || die $!;
    print MACRO "handle SIGPIPE nostop\n";
    print MACRO "handle SIGPIPE noprint\n\n";
    print MACRO "rwatch *$lock\n\n";
    print MACRO "cont\n";
    print MACRO "bt 40\n";
    close(MACRO);

    system("gdb -x gdb.auto.mutex.macro --batch $binary $pid >& gdb.out");

    open(GDB_OUT, "gdb.out") || die $!;
    my $gdb_out_lines = 1;
    while (<GDB_OUT>) {
      chomp;
      if (m|\#\d+ (.+)|) {
        print "\t$_\n";
        last if ++$gdb_out_lines > $symbols;
      }

    }
    close(GDB_OUT);
    last if ++$count > $mutexes;
    print "\n";
  }

  unlink("gdb.auto.mutex.macro");
  unlink("gdb.out");
  close(INPUT);
}
