#!/usr/bin/env perl

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

# Script to decode via header
#
# @File traffic_via.pl
# @Author Meera Mosale Nataraja
#
# Usage: Use it in 2 ways
# 1. Pass Via Header with -s option \n";
#    traffic_via [-s viaheader]";
#           or
# 2. Pipe curl output 
#    curl -v -H "X-Debug: Via" http://ats_server:port 2>&1| ./traffic_via.pl
# 

use strict;
use warnings;
use Data::Dumper qw(Dumper);
use Getopt::Long;

my $via_header;
my $help;

#Proxy request header flags and titles
my @proxy_header_array = (
    {
        "Request headers received from client:",
        { 
            'I' => "If Modified Since (IMS)",
            'C' => "cookie",
            'E' => "error in request",
            'S' => "simple request (not conditional)",
            'N' => "no-cache",
            ' ' => "unknown?",
        },
    },
   {
        "Result of Traffic Server cache lookup for URL:",
        {
                   'A' => "in cache, not acceptable (a cache \"MISS\")",
                   'H' => "in cache, fresh (a cache \"HIT\")",
                   'S' => "in cache, stale (a cache \"MISS\")",
                   'R' => "in cache, fresh Ram hit (a cache \"HIT\")",
                   'M' => "miss (a cache \"MISS\")",
                   ' ' => "unknown?",
        },
    },
    {
        "Response information received from origin server:",
        {
                    'E' => "error in response",
                    ' ' => "no server connection needed",
                    'S' => "served",
                    'N'=> "not-modified",
        }
    },
    {
        "Result of document write-to-cache:",
        {
                    'U' => "updated old cache copy",
                    'D' => "cached copy deleted",
                    'W' => "written into cache (new copy)",
                    ' ' => "no cache write performed",
        },
    },
    {
        "Proxy operation result:",
        { 
                    'R' => "origin server revalidated",
                    ' ' => "unknown?",
                    'S' => "served",
                    'N' => "not-modified",
        },
    },
    {
        "Error codes (if any):",
        {
                    'A' => "authorization failure",
                    'H' => "header syntax unacceptable",
                    'C' => "connection to server failed",
                    'T' => "connection timed out",
                    'S' => "server related error",
                    'D' => "dns failure",
                    'N' => "no error",
                    'F' => "request forbidden",
        },
    },
    {
        "Tunnel info:",
        {
                    ' ' => "no tunneling",
                    'U' => "tunneling because of url (url suggests dynamic content)",
                    'M' => "tunneling due to a method (e.g. CONNECT)",
                    'O' => "tunneling because cache is turned off",
                    'F' => "tunneling due to a header field (such as presence of If-Range header)",
        },
   },
   {
        "Cache type:",
        {
                    'I' => "icp",
                    ' ' => "cache miss or no cache lookup",
                    'C' => "cache",
        },
   },
   {
        "Cache lookup result:",
        {
                    ' ' => "no cache lookup",
                    'S' => "cache hit, but expired",
                    'U' => "cache hit, but client forces revalidate (e.g. Pragma: no-cache)",
                    'D' => "cache hit, but method forces revalidated (e.g. ftp, not anonymous)",
                    'I' => "conditional miss (client sent conditional, fresh in cache, returned 412)",
                    'H' => "cache hit",
                    'M' => "cache miss (url not in cache)",
                    'C' => "cache hit, but config forces revalidate",
                    'N' => "conditional hit (client sent conditional, doc fresh in cache, returned 304)",
        },
   },
   {
        "ICP status:",
        {
                    ' ' => "no icp",
                    'S' => "connection opened successfully",
                    'F' => "connection open failed",
        },
   },
   {
        "Parent proxy connection status:",
        {
                    ' ' => "no parent proxy",
                    'S' => "connection opened successfully",
                    'F' => "connection open failed",
        },
    
   },
   {
        "Origin server connection status:",
        {
                    ' ' => "no server connection",
                    'S' => "connection opened successfully",
                    'F' => "connection open failed",
        },
   },
);

##Print script usage
sub usage
{
    print "\nPass Via Header with -s option \n";
    print "Usage: traffic_via [-s viaheader]";
    print "\n        or          \n";
    print "\nPipe curl command output to this program";
    print "\nEg: curl xxxx | traffic_via\n";
    print "\n-h for help\n";
    exit;
}

if (@ARGV == 0) {
    #if passed through standard input
    my @userinput = <STDIN>;
    my $via_string;

    for my $element (@userinput) {
        #Pattern matching for Via
        if ($element =~ /Via:(.*)\[(.*)\]/) {
            #Search and grep via header
            $via_string = $2;    
            chomp($via_string);
            print "Via Header is [$via_string]";
            decode_via_header($via_string);
        }
    }
} else {
    usage() if (!GetOptions('s=s' => \$via_header,
                'help|?' => \$help) or
                defined $help);
    
    if (defined $via_header) {
        #if passed through commandline dashed argument
        print "Via Header is [$via_header]";
        decode_via_header($via_header);
    }

}

#Subroutine to decode via header
sub decode_via_header {
    my($header) = @_;
    my $hdrLength;
    my $newHeader;

    #Check via header syntax
    if ($header =~ /([a-zA-Z: ]+)/) {   
        #Get via header length
        $hdrLength = length($header);
        
        # Valid Via header length is 24 or 6.
        # When Via header length is 24, it will have both proxy request header result and operational results.
        if ($hdrLength == 24) {
            #Split via header: proxy result and operational result
            $newHeader = join('', split(':', $header));
        } elsif ($hdrLength == 6) {
            $newHeader = $header;
        } elsif ($hdrLength == 5) {
            # When Via header length is 5, it might be missing last field. Fill it and decode header.
            my $newHeader = "$header"." ";
        } else {
            # Invalid header size, come out.
            print "\nInvalid VIA header. VIA header length should be 6 or 24 characters\n";
            return;
        }
        convert_header_to_array($newHeader);
    }
    
    
}

sub convert_header_to_array {
    my ($viaHeader) = @_;
    my @ResultArray;
    #Convert string header into character array
    while ($viaHeader =~ /(.)/g) {
            #Only capital letters indicate flags
            if ($1 !~ m/[a-z]+/) {
                push(@ResultArray, $1); 
            }
    }
    print "\nVia Header details: \n";
    for (my $arrayIndex=0; $arrayIndex < scalar(@ResultArray); $arrayIndex++ ) {
        get_via_header_flags(\@proxy_header_array, $arrayIndex, $ResultArray[$arrayIndex]);
    }
}

#Get values from header arrays
sub get_via_header_flags {
    my ($arrayName, $inputIndex, $flag) = @_;

    my %flagValues;
    my @flagKeys;
    my %flags;
    my @keys;
    
    my @array = @$arrayName;
            
    %flagValues = %{$array[$inputIndex]};
    @flagKeys = keys (%flagValues);
    
    foreach my $keyEntry ( @flagKeys )  {
        printf ("%-55s", $keyEntry);
        %flags = %{$flagValues{$keyEntry}};
        @keys = keys (%flags);
        foreach my $key ( @keys )  {
            if ($key =~ /$flag/) { 
                 #print $flags{$key};
                 printf("%s",$flags{$key});
                 print "\n";
            }
        }
    }
}
