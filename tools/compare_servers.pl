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

use strict;
use warnings;
use Getopt::Long;
use Data::Dumper;
use Net::hostent;
use Socket;
use LWP::UserAgent;
use Digest::SHA1;

my $verbose = 0;

#----------------------------------------------------------------------------
sub usage()
{
    print STDERR "USAGE: compare_hosts.pl --verbose level --host1 testing_host --host2 valid_host --file url_file\n\n";
    print STDERR "\t--host1         The host running the newest version\n";
    print STDERR "\t--host2         The host running the older version\n";
    print STDERR "\t--file          A file that contains a list of URLs\n";
    print STDERR "\t--verbose       verbose level 1-3, 1 is the least verbose\n\n";
    print STDERR "Example:\n";
    print STDERR "\tcompare_hosts.pl --host1 new_ats --host2 old_ats --file top_1000_urls\n";
    exit 1;
}

#----------------------------------------------------------------------------
sub compareHeaderNames($$)
{
    my ($response1, $response2) = @_;

    my @names1 = $response1->header_field_names;
    my @names2 = $response2->header_field_names;

    my %hash2;
    $hash2{$_} = 1 for (@names2);
    my %hash1;
    $hash1{$_} = 1 for (@names1);

    my $return_val = 0;    # header names match

    foreach my $name (@names1) {
        if (!defined $hash2{$name}) {
            print "\t\t- $name header not found on host2\n" if $verbose >= 2;
            $return_val = 1;
        }
    }

    foreach my $name (@names2) {
        if (!defined $hash1{$name}) {
            print "\t\t- $name header not found on host1\n" if $verbose >= 2;
            $return_val = 1;
        }
    }

    return $return_val;
}

#----------------------------------------------------------------------------
sub compareHeaderValues($$)
{
    my ($response1, $response2) = @_;

    my @test_headers =
      qw(ETag Cache-Control Connection Accept-Ranges Server Content-Type Access-Control-Allow-Methods Access-Control-Allow-Origin Strict-Transport-Security);
    my $return_val = 0;    # header valuse match

    if ($verbose >= 3) {
        foreach my $field ($response1->header_field_names) {
            print "\t\t\t~ " . $field . ": " . $response1->header($field) . "\n";
        }

        print "\t\tHost2: \n";

        foreach my $field ($response2->header_field_names) {
            print "\t\t\t~ " . $field . ": " . $response2->header($field) . "\n";
        }
    }

    # Test specific headers that are defined above
    foreach my $field (@test_headers) {
        my $value1 = $response1->header($field);
        my $value2 = $response2->header($field);

        if (defined $value1 && defined $value2) {
            if ($value1 ne $value2) {
                print "\t\t- $field: $value1 ne $value2\n" if $verbose;
                print "\t\t\t - Via host1: " . $response1->header('Via') . " host2: " . $response2->header('Via') . "\n"
                  if $verbose;
                print "\t\t\t - Last-Modified host1: "
                  . $response1->header('Last-Modified')
                  . " host2: "
                  . $response2->header('Last-Modified') . "\n"
                  if $verbose;
                if (defined $response2->header('Content-Encoding')) {
                    print "\t\t\t - Content-Encoding host1: "
                      . $response1->header('Content-Encoding')
                      . " host2: "
                      . $response2->header('Content-Encoding') . "\n";
                } else {
                    print "\t\t\t - Content-Encoding host1: " . $response1->header('Content-Encoding') . " host2: ''\n";
                }
                $return_val = 1;
            } else {
                print "\t\t- $field: $value1 eq $value2\n" if $verbose >= 2;
            }
        }
    }
    return $return_val;
}

#----------------------------------------------------------------------------
{
    my %stats;

    $ENV{PERL_LWP_SSL_VERIFY_HOSTNAME} = '0';
    my ($host1, $host2, $file);
    GetOptions(
        "host1=s"   => \$host1,
        "host2=s"   => \$host2,
        "file=s"    => \$file,
        "verbose=f" => \$verbose
    ) || die $!;

    usage() if (!defined $host1 || !defined $host2 || !defined $file);

    my $count                  = 0;
    my $status_error           = 0;
    my $sha_error              = 0;
    my $header_names_mismatch  = 0;
    my $header_values_mismatch = 0;

    my $host1_addr = inet_ntoa(inet_aton($host1));
    my $host2_addr = inet_ntoa(inet_aton($host2));

    print "Testing with host1: $host1 ($host1_addr) - host2: $host2 ($host2_addr)\n";
    print '-' x 78, "\n";

    open(FILE, $file) || die $!;

    # Create a user agent object
    my $ua1 = LWP::UserAgent->new(keep_alive => 100);
    $ua1->agent("MyApp/0.1 ");

    # Create a user agent object
    my $ua2 = LWP::UserAgent->new(keep_alive => 100);
    $ua2->agent("MyApp/0.1 ");

    while (my $url = <FILE>) {
        next if ($url =~ m|hc.l.yimg.com|);
        chomp $url;
        my $exit = 0;

        if ($url =~ m|(https?)://([^/]+)(.+)|) {

            my $scheme = $1;
            my $host   = $2;
            my $path   = $3;

            $count++;
            print "Test $count - URL: $url\n";

            my $port = 80;
            $port = 443 if $scheme eq 'https';

            my $request1 = HTTP::Request->new(GET => "${scheme}://${host1_addr}${path}");
            $request1->header('Host' => $host);
            my $response1 = $ua1->request($request1);

            my $request2 = HTTP::Request->new(GET => "${scheme}://${host2_addr}${path}");
            $request2->header('Host'            => $host);
            $request2->header('Accept-Encoding' => 'deflate');
            my $response2 = $ua2->request($request2);

            print "\tStatus code for host1: " . $response1->code . " - host2: " . $response2->code . "\n" if $verbose;

            my $sha1 = Digest::SHA1->new;
            $sha1->add($response1->content);
            my $digest1 = $sha1->hexdigest;
            open(FILE1, "> /tmp/tmp1");
            open(FILE2, "> /tmp/tmp2");
            print FILE1 $response1->content;
            print FILE2 $response2->content;
            close FILE1;
            close FILE2;
            #print $response1->content, "\n"; # for internal debugging
            #print $response2->content, "\n"; # for internal debugging

            my $sha2 = Digest::SHA1->new;
            $sha2->add($response2->content);
            my $digest2 = $sha2->hexdigest;

            print "\tSHA hash for host1: $digest1 - host2: $digest2\n" if $verbose;

            # Build up stats
            if ($response1->status_line eq $response2->status_line) {

                # Do the hashes
                if ($digest1 eq $digest2) {
                    $stats{stat_line_match}->{$response1->code}->{sha_match}++;
                    print "\tResponse code: " . $response1->code . " - Status lines and SHA1 of response bodies match\n";
                } else {
                    $stats{stat_line_match}->{$response1->code}->{sha_nomatch}++;
                    print "\tResponse code: " . $response1->code . " - Status lines match SHA1 doesn't match\n";
                    $sha_error++;
                    #$exit = 1 if $response1->code == 200; # for internal debugging
                }

                # Compare the header field names
                if (compareHeaderNames($response1, $response2) == 0) {
                    $stats{stat_line_match}->{$response1->code}->{field_names_match}++;
                } else {
                    $stats{stat_line_match}->{$response1->code}->{field_names_nomatch}++;
                    $header_names_mismatch++;
                }

                # Compare the values of the header fields
                if (compareHeaderValues($response1, $response2) == 0) {
                    $stats{stat_line_match}->{$response1->code}->{field_values_match}++;
                } else {
                    $stats{stat_line_match}->{$response1->code}->{field_values_nomatch}++;
                    $header_values_mismatch++;
                }
            } else {
                $status_error++;
                $stats{stat_line_nomatch}++;
                print "\tERROR: status lines don't match\n";
            }

            last if $exit;
        }
    }

    print '-' x 78, "\n";
    print "SUMMARY:\n";
    print "URLs tested: $count\n";
    print "Status line mismatches: $status_error\n";
    print "SHA1 mismatches: $sha_error\n";
    print "Responses with header names mismatches: $header_names_mismatch\n";
    print "Responses with header values mismatches: $header_values_mismatch\n";
    print Dumper \%stats if $verbose;
}

