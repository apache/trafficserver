#!/usr/bin/perl
#
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
#
# Usage: ./encrypt_to_committer.pl < plaintext > ciphertext.asc

use strict;
use IPC::Open2;

my @keys;
my $data;
while(<>) {
  $data .= $_;
  /ID ([A-F0-9]+)/ && push @keys, $1;
}
unless(@keys) {
  while(<DATA>) {
    /ID ([A-F0-9]+)/ && push @keys, $1;
  }
}
my @args = ('-e', '-a');
foreach my $key (@keys) {
  push @args, '-r';
  push @args, $key;
}
push @args, @ARGV;
print STDERR "Encrypting to:\n\t".join("\n\t", @keys)."\n";
my $pid = open2(*Reader, *Writer, 'gpg', @args);
print Writer $data;
close(Writer);
print while(<Reader>);
close(Reader);
1;
__DATA__
gpg: encrypted with 4096-bit RSA key, ID E704BCA7, created 2013-03-07
      "Phil Sorber (ASF Key) <sorber@apache.org>"
gpg: encrypted with 2048-bit RSA key, ID 9295DFFF, created 2012-07-16
      "Brian Geffon <briang@apache.org>"
gpg: encrypted with 4096-bit RSA key, ID CB386B98, created 2012-06-26
      "Alan M. Carroll <amc@network-geographics.com>"
gpg: encrypted with 4096-bit RSA key, ID 20E73FD6, created 2011-11-09
      "Igor Galić <i.galic@brainsware.org>"
gpg: encrypted with 2048-bit RSA key, ID C431AECE, created 2012-04-10
      "Daniel Gruno <humbedooh@apache.org>"
gpg: encrypted with 4096-bit RSA key, ID 7911CE36, created 2009-10-30
      "Bryan W. Call <bcall@apache.org>"
gpg: encrypted with 4096-bit RSA key, ID A494B54F, created 2009-10-17
      "Leif Hedstrom (CODE SIGNING KEY) <zwoop@apache.org>"
