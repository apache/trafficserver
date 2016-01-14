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

use Digest::SHA qw(hmac_sha1 hmac_sha1_hex);
use Digest::HMAC_MD5 qw(hmac_md5 hmac_md5_hex);
use Getopt::Long;
use strict;
use warnings;
my $key       = undef;
my $string    = undef;
my $useparts  = undef;
my $result    = undef;
my $duration  = undef;
my $keyindex  = undef;
my $verbose   = 0;
my $url       = undef;
my $client    = undef;
my $algorithm = 1;

$result = GetOptions(
	"url=s"       => \$url,
	"useparts=s"  => \$useparts,
	"duration=i"  => \$duration,
	"key=s"       => \$key,
	"client=s"    => \$client,
	"algorithm=i" => \$algorithm,
	"keyindex=i"  => \$keyindex,
	"verbose"     => \$verbose
);

if ( !defined($key) || !defined($url) || !defined($duration) || !defined($keyindex) ) {
	&help();
	exit(1);
}

$url =~ s/^http:\/\///;
my $i              = 0;
my $part_active    = 0;
my $j              = 0;
my @inactive_parts = ();
foreach my $part ( split( /\//, $url ) ) {
	if ( length($useparts) > $i ) {
		$part_active = substr( $useparts, $i++, 1 );
	}
	if ($part_active) {
		$string .= $part . "/";
	}
	else {
		$inactive_parts[$j] = $part;
	}
	$j++;
}
my $urlHasParams = index($string,"?");

chop($string);
if ( defined($client) ) {
  if ($urlHasParams > 0) {
	  $string .= "&C=" . $client . "&E=" . ( time() + $duration ) . "&A=" . $algorithm . "&K=" . $keyindex . "&P=" . $useparts . "&S=";
  }
  else {
	  $string .= "?C=" . $client . "&E=" . ( time() + $duration ) . "&A=" . $algorithm . "&K=" . $keyindex . "&P=" . $useparts . "&S=";
  }
}
else {
  if ($urlHasParams > 0) {
	  $string .= "&E=" . ( time() + $duration ) . "&A=" . $algorithm . "&K=" . $keyindex . "&P=" . $useparts . "&S=";
  }
  else {
	  $string .= "?E=" . ( time() + $duration ) . "&A=" . $algorithm . "&K=" . $keyindex . "&P=" . $useparts . "&S=";
  }
}

$verbose && print "signed string = " . $string . "\n";

my $digest;
if ( $algorithm == 1 ) {
	$digest = hmac_sha1_hex( $string, $key );
}
else {
	$digest = hmac_md5_hex( $string, $key );
}
if ($urlHasParams == -1) {
  my $qstring = ( split( /\?/, $string ) )[1];

  print "curl -s -o /dev/null -v --max-redirs 0 'http://" . $url . "?" . $qstring . $digest . "'\n";
}
else {
  my $url_noparams = ( split( /\?/, $url ) )[0];
  my $qstring = ( split( /\?/, $string ) )[1];

  print "curl -s -o /dev/null -v --max-redirs 0 'http://" . $url_noparams . "?" . $qstring . $digest . "'\n";
}

sub help {
	print "sign.pl - Example signing utility in perl for signed URLs\n";
	print "Usage: \n";
	print "  ./sign.pl  --url <value> \\ \n";
	print "             --useparts <value> \\ \n";
	print "             --algorithm <value> \\ \n";
	print "             --duration <value> \\ \n";
	print "             --keyindex <value> \\ \n";
	print "             [--client <value>] \\ \n";
	print "             --key <value>  \\ \n";
	print "             [--verbose] \n";
	print "\n";
}
