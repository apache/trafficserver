#!/usr/bin/perl

# A brief file description
#
# @section license License
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

my $runs = 0;
my $fetches = 0;
my $conns = 0;
my $parallel = 0;
my $bytes = 0;
my $seconds = 0;
my $mean_bytes = 0;
my $fetches_sec = 0.0;
my $bytes_sec = 0.0;
my %msecs_connect = ( "mean" => 0.0,
                      "max" => 0.0,
                      "min" => 0.0 );
my %msecs_response = ( "mean" => 0.0,
                       "max" => 0.0,
                       "min" => 0.0 );


while (<>) {
  my @c = split();
  if (/fetches on/) {
    $fetches += $c[0];
    $conns += $c[3];
    $parallel += $c[5];
    $bytes += $c[8];
    $seconds += $c[11];
    $runs++;
  } elsif (/mean bytes/) {
    $mean_bytes += $c[0];
  } elsif (/fetches\/sec/) {
    $fetches_sec += $c[0];
    $bytes_sec += $c[2];
  } elsif (/msecs\/connect/) {
    $msecs_connect{"mean"} += $c[1];
    $msecs_connect{"max"} += $c[3];
    $msecs_connect{"min"} += $c[5];
  } elsif (/msecs\/first/) {
    $msecs_response{"mean"} += $c[1];
    $msecs_response{"max"} += $c[3];
    $msecs_response{"min"} += $c[5];
  }
}

print "Total runs: ", $runs, "\n";
printf "%d fetches on %d conns, %d max parallell, %.5e bytes in %d seconds\n",
  $fetches, $conns, $parallel, $bytes, $seconds / $runs;
print $mean_bytes/$runs, " mean bytes/fetch\n";
printf "%.2f fetches/sec, %.5e bytes/sec\n",  $fetches_sec, $bytes_sec;
print "msecs/connect: ", $msecs_connect{"mean"}/$runs, " mean, ",
  $msecs_connect{"max"}/$runs, " max, ", $msecs_connect{"min"}/$runs, " min\n";
print "msecs/first-response: ", $msecs_response{"mean"}/$runs, " mean, ",
  $msecs_response{"max"}/$runs, " max, ", $msecs_response{"min"}/$runs, " min\n";
