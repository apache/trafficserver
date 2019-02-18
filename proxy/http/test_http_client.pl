#!/usr/bin/perl

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

require 5.001;
#use strict;
use sigtrap;
use Socket;

#forward declaration
sub process_input_http_requests_file($$$);
sub spawn_http_request($$$);
sub make_proxy_request($$$$$);
sub make_doc_filename($);
sub make_doc_http_filename($);

###########################################################
#
# global configuration parameters
#
###########################################################
my ($number_of_users) = 1;
my ($save_http_doc)   = 0;
###########################################################
#
# subroutine: process_input_http_requests_file filename
#
# Process input file. The file contains http requests.
###########################################################
sub process_input_http_requests_file($$$)
{
    my ($filename, $proxy_name, $proxy_port) = @_;
    my ($input, $host_name, $host_port, $request, $line);

    #open input file for read
    unless (open input, "<$filename") {
        print "cannot open $filename: $!\n";
        return;
    }

    while ($line = <input>) {
        $request .= $line;
        #replace \n with \r\n
        if (not $line =~ m/\r/) {
            $line =~ s/\n/\r\n/;
        }
        if ($line =~ m/host/i) {
            ($_, $host_name, $host_port) = split(/:/, $line, 3);
            if (not $host_port) {
                $host_port = 80;
            }
        } elsif (length($line) <= 2 && $line == "\n") {
            $request .= $line;
            if ($proxy_name and $proxy_port) {
                $request = make_proxy_request($request, $host_name, $host_port, $proxy_name, $proxy_port);
                spawn_http_request($proxy_name, $proxy_port, $request);
            } else {
                print $request;
                spawn_http_request($host_name, $host_port, $request);
            }
            $request = "";
        }
    }
    return;
}
###########################################################
#
# subroutine: spawn_http_request hostname hostport request
#
###########################################################
sub spawn_http_request($$$)
{
    my ($hostname, $hostport, $request) = @_;

    my ($pid);
    if (!defined($pid = fork)) {
        print "fork failed", "\n";
        exit;
    } elsif ($pid) {    # parent
        return;
    }
    # else, I am the child
    do_http_request($hostname, $hostport, $request);
    exit;
}
###########################################################
#
# subroutine: do_http_request hostname request
#
###########################################################
sub spawn_http_request($$$)
{
    my ($hostname, $hostport, $request) = @_;
    my ($line);
    my ($iaddr, $paddr, $proto);

    $hostname =~ s/\s//g;

    $iaddr = inet_aton($hostname) or die "no host: $hostname", "\n";
    $paddr = sockaddr_in($hostport, $iaddr);
    $proto = getprotobyname('tcp');

    unless (socket(Host, PF_INET, SOCK_STREAM, $proto)) {
        print "socket: $!", "\n";
        exit;
    }
    unless (connect(Host, $paddr)) {
        print "connect: $!", "\n";
        exit;
    }
    syswrite Host, $request, length($request);
    #process response
    process_http_response($request, $Host, 1, 1);
    print "request is done\n";
    close(Host);

    return;
}
###########################################################
#
# subroutine: process_http_response
#     request,
#     host_socket,
#     save_doc_flag,
#     save_http_flag
#
# options for save doc
# - save http response header in doc.http
# - save http doc in a unique file
###########################################################
sub process_http_response($$$$)
{
    my ($request, $Host, $save_doc_flag, $save_http_flag) = @_;
    my ($doc_filename, $http_filename);

    my ($doc_filename)      = make_doc_filename($request);
    my ($doc_http_filename) = make_doc_http_filename($request);

    print $doc_filename, ' ', $doc_http_filename, "\n";

    my ($doc_file, $doc_http_file);
    ########################
    # open files for write #
    ########################
    if ($save_doc_flag) {
        unless (open doc_file, ">$doc_filename") {
            print "cannot open $doc_filename for write", "\n";
            return;
        }
    }
    if ($save_http_flag) {
        unless (open doc_http_file, ">$doc_http_filename") {
            print "cannot open $doc_http_filename for write", "\n";
            return;
        }
    }
    ##############################
    # write http header and body #
    ##############################
    my ($http_header) = 1;
    my ($doc_body)    = 0;
    my ($line);

    while ($line = <Host>) {
        if ($http_header) {
            if ($save_http_flag) {
                print doc_http_file $line;
            }
            if (length($line) <= 2 && $line == "\n") {
                close doc_http_file;
                $http_header = 0;
                $doc_body    = 1;
            }
        } elsif ($save_doc_flag) {
            print doc_file $line;
        }
    }
    return;
}
###########################################################
#
# subroutine: make_proxy_request
#       request
#       host_name
#       host_port
#       proxy_name
#       proxy_port
#
###########################################################
sub make_proxy_request($$$$$)
{
    my ($request, $host_name, $host_port, $proxy_name, $proxy_port) = @_;
    my ($proxy_request) = $request;

    my ($url_prefix) = "http:\/\/$host_name\/";
    $url_prefix =~ s/\s//g;

    if ($host_port != 80) {
        $url_prefix .= ":$host_port\/";
    }
    $url_prefix =~ s/\s//g;

    $proxy_request =~ s/\//$url_prefix/;

    return ($proxy_request);
}
###########################################################
#
# subroutine: make_doc_filename request
#
# file name is: <host_name><doc_name>
###########################################################
sub make_doc_filename($)
{
    my ($request) = @_;
    my ($doc_filename);
    my ($host_name);

    ($_, $host_name) = split(/host:/i, $request, 2);
    ($host_name, $_) = split(/ /, $host_name);
    #replace every . with _
    $host_name =~ s/\./_/g;

    print $request,   "\n";
    print $host_name, "\n";

    ($_, $doc_filename) = split(/ /, $request, 2);
    #remove scheme://host_name if this is a proxy request
    #	if ($doc_filename =~ m/:\/\//)
    #	{
    #
    #	}
    #
    #	@@@@@@@@

    ($_, $doc_filename) = split(/\//, $doc_filename, 2);
    $doc_filename =~ s/\//_/g;
    #remove any white spaces
    $doc_filename =~ s/\s//g;

    print "doc name is: ", $doc_filename, "\n";

    if (length($doc_filename) <= 1) {
        $doc_filename = 'default.html';
    }

    $doc_filename = $host_name . '_' . $doc_filename;

    #remove any white spaces
    $doc_filename =~ s/\s//g;

    return ($doc_filename);
}
###########################################################
#
# subroutine: make_doc_filename request
#
###########################################################
sub make_doc_http_filename($)
{
    my ($request) = @_;
    my ($doc_http_filename);

    $doc_http_filename = make_doc_filename($request);
    $doc_http_filename .= '.http';

    return ($doc_http_filename);
}
###########################################################
#
# main entry point
#
###########################################################
if ($#ARGV != 1 and $#ARGV != 3) {
    print 'no proxy : test_http_client <input file> <number of users>', "\n";
    print 'use proxy: test_http_client <input file> <number of users> ';
    print '<proxy host> <proxy port>', "\n";
    exit;
}

if ($#ARGV == 1) {
    my ($infile, $nusers) = @ARGV;
    process_input_http_requests_file($infile, "", "");
} elsif ($#ARGV == 3) {
    my ($infile, $nusers, $proxy_name, $proxy_port) = @ARGV;
    process_input_http_requests_file($infile, $proxy_name, $proxy_port);
}

print "\n";
exit;

