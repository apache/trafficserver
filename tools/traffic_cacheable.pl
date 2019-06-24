#!/usr/bin/perl

#   Licensed to the Apache Software Foundation (ASF) under one
#   or more contributor license agreements.  See the NOTICE file
#   distributed with this work for additional information
#   regarding copyright ownership.  The ASF licenses this file
#   to you under the Apache License, Version 2.0 (the
#   "License"); you may not use this file except in compliance
#   with the License.  You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.

use strict;
use warnings;
use LWP::UserAgent;
use IO::Socket::SSL;
use Data::Dumper;
use Getopt::Std;

my %histogram = (0 => 0,
                60 => 0,
                3600 => 0,
                86400 => 0,
                604800 => 0,
                2592000 => 0,
                30758400 => 0,
                'inf' => 0,
                'undef' => 0
);

my %histogram_with_heuristic = (0 => 0,
                60 => 0,
                3600 => 0,
                86400 => 0,
                604800 => 0,
                2592000 => 0,
                30758400 => 0,
                'inf' => 0,
                'undef' => 0
);

my %pretty_name = (0 => '0',
                   60 => '1 minute',
                   3600 => '1 hour',
                   86400 => '1 day',
                   604800 => '1 week',
                   2592000 => '1 month',
                   30758400 => '1 year',
                   'inf' => '> 1 year',
                   'undef' => 'undef'
);

my $debug = 0;

#-----------------------------------------------------------------------------
sub addToHistogram($$) {
  my($hist, $value) = @_;

  if (! defined $value) {
    $hist->{undef}++
  } elsif ($value <= 0) {
    $hist->{0}++;
  } elsif ($value <= 60) {
    $hist->{60}++;
  } elsif ($value <= 3600) {
    $hist->{3600}++;
  } elsif ($value <= 86400) {
    $hist->{86400}++;
  } elsif ($value <= 604800) {
    $hist->{604800}++;
  } elsif ($value <= 2592000) {
    $hist->{2592000}++;
  } elsif ($value <= 30758400) {
    $hist->{30758400}++;
  } else {
    $hist->{inf}++;
  }
}

#-----------------------------------------------------------------------------
sub usage() {
  print "USAGE: traffic_cacheable.pl [file]\n";
  print "\t-h\thelp";
  print "\n\ntraffic_cacheable.pl can read from a file or stdin\n";
  exit;
}

#-----------------------------------------------------------------------------
{
  my %opts;
  getopts("h", \%opts);
  usage() if (defined $opts{h});

  my $ua = LWP::UserAgent->new(
    ssl_opts => {
        verify_hostname => 0,
        SSL_verify_mode => IO::Socket::SSL::SSL_VERIFY_NONE,
    },
  );

  my %response_codes;
  my %cache_control_exists;
  my %expires_exists;
  my %last_modified_exists;
  my %pragma_exists;
  my $cc_and_expires_noexist = 0;
  my $cache_headers_noexist = 0;
  my $ats_cache_hit = 0;

  my $count = 0;
  while (my $url = <>) {
    $count++;
    chomp $url;
    my $req = HTTP::Request->new(GET => $url);

    print "Testing url: $url\n" if $debug;
    print STDERR "\rTesting url: $count" if ! $debug;
    my $res = $ua->request($req);
    my $freshness = $res->freshness_lifetime(heuristic_expiry => 0);
    my $freshness_with_heuristic = $res->freshness_lifetime();

    if ($res->is_success) {
      my $cache_control = $res->header('Cache-Control');
      my $expires = $res->header('Expires');
      my $last_modified = $res->header('Last-Modified');
      my $pragma = $res->header('Pragma');
      my $via = $res->header('Via');

      print "\tcode: ", $res->code, "\n" if $debug;
      print "\tCache-Control: ", $cache_control, "\n" if $debug;
      print "\tExpires: ", $expires, "\n" if $debug;
      print "\tcalculated age: ", $res->current_age, "\n" if $debug;
      print "\tfreshness lifetime: ", $freshness, "\n" if $debug;

      $response_codes{$res->code}++;
      if ($res->code == 200) {
        addToHistogram(\%histogram, $freshness);
        addToHistogram(\%histogram_with_heuristic, $freshness_with_heuristic);

        # check cache-control
        if (defined $cache_control) {
          $cache_control_exists{private}++ if $cache_control =~ m|private|i;
          $cache_control_exists{'no-cache'}++ if $cache_control =~ m|no-cache|i;
          $cache_control_exists{'no-store'}++ if $cache_control =~ m|no-store|i;
          $cache_control_exists{'max-age'}++ if $cache_control =~ m|max-age|i;
          $cache_control_exists{'maxage'}++ if $cache_control =~ m|maxage|i;
        } else {
          $cache_control_exists{undef}++;
        }

        # check expires
        if (defined $expires) {
          $expires_exists{defined}++;
        } else {
          $expires_exists{undef}++;
        }

        # check last-modified
        if (defined $last_modified) {
          $last_modified_exists{defined}++;
        } else {
          $last_modified_exists{undef}++;
        }

        # check pragma
        if (defined $pragma) {
          $pragma_exists{defined}++;
        } else {
          $pragma_exists{undef}++;
        }

        # check via
        if (defined $via) {
          my @items = split(/,/, $via);
          my $last = pop(@items);
          #print "$last\n";
          if (my($codes) = $last =~ m|\[([\w\s]+)\]|) {
            #print "$codes\n";
            $ats_cache_hit++ if ($codes =~ m|^c[HR]|);
          }
        }

        $cc_and_expires_noexist++ if (! defined $cache_control && ! defined $expires);
        $cache_headers_noexist++ if (! defined $cache_control && ! defined $expires && ! defined $last_modified);
      }
    }
    #last if $count == 5;
  }
  print "\n" if ! $debug;
  print "\n";

  print "Total Tested: $count\n";
  print "Response Codes:\n";
  foreach my $key (sort keys %response_codes) {
    printf("\t%s: %d (%.2f%%)\n", $key, $response_codes{$key}, $response_codes{$key} / $count * 100);
  }

  print "Freshness:\n";
  foreach my $key (0, 60, 3600, 86400, 604800, 2592000, 30758400, 'inf', 'undef') {
    printf("\t%s: %d (%.2f%%)\n", $pretty_name{$key}, $histogram{$key}, $histogram{$key} / $count * 100);
  }

  print "Freshness with Heuristic using Last-Modified (RFC 7234 4.2.2):\n";
  foreach my $key (0, 60, 3600, 86400, 604800, 2592000, 30758400, 'inf', 'undef') {
    printf("\t%s: %d (%.2f%%)\n", $pretty_name{$key}, $histogram_with_heuristic{$key}, $histogram_with_heuristic{$key} / $count * 100);
  }

  print "Headers:\n";
  print "\tCache-Control:\n";
  foreach my $key (sort keys %cache_control_exists) {
    printf("\t\t%s: %d (%.2f%%)\n", $key, $cache_control_exists{$key}, $cache_control_exists{$key} / $count * 100);
  }

  print "\tExpires:\n";
  foreach my $key (sort keys %expires_exists) {
    printf("\t\t%s: %d (%.2f%%)\n", $key, $expires_exists{$key}, $expires_exists{$key} / $count * 100);
  }

  print "\tLast-Modified:\n";
  foreach my $key (sort keys %last_modified_exists) {
    printf("\t\t%s: %d (%.2f%%)\n", $key, $last_modified_exists{$key}, $last_modified_exists{$key} / $count * 100);
  }

  print "\tPragma:\n";
  foreach my $key (sort keys %pragma_exists) {
    printf("\t\t%s: %d (%.2f%%)\n", $key, $pragma_exists{$key}, $pragma_exists{$key} / $count * 100);
  }

  print "Extra:\n";
  printf("\t%s: %d (%.2f%%)\n", 'Cache-Control / Expires not set', $cc_and_expires_noexist, $cc_and_expires_noexist / $count * 100);
  printf("\t%s: %d (%.2f%%)\n", 'Cache-Control / Expires / Last-Modified not set', $cache_headers_noexist, $cache_headers_noexist / $count * 100);
  printf("\t%s: %d (%.2f%%)\n", 'ATS Cache Hit (via header)', $ats_cache_hit, $ats_cache_hit / $count * 100);
}

