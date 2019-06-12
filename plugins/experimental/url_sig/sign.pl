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
use MIME::Base64::URLSafe ();
use strict;
use warnings;
my $key        = undef;
my $string     = undef;
my $useparts   = undef;
my $result     = undef;
my $duration   = undef;
my $keyindex   = undef;
my $verbose    = 0;
my $url        = undef;
my $client     = undef;
my $algorithm  = 1;
my $pathparams = 0;
my $sig_anchor = undef;
my $proxy      = undef;
my $scheme     = "http://";

$result = GetOptions(
    "url=s"       => \$url,
    "useparts=s"  => \$useparts,
    "duration=i"  => \$duration,
    "key=s"       => \$key,
    "client=s"    => \$client,
    "algorithm=i" => \$algorithm,
    "keyindex=i"  => \$keyindex,
    "verbose"     => \$verbose,
    "pathparams"  => \$pathparams,
    "proxy=s"     => \$proxy,
    "siganchor=s" => \$sig_anchor
);

if (!defined($key) || !defined($url) || !defined($duration) || !defined($keyindex)) {
    &help();
    exit(1);
}
if (defined($proxy)) {
    if ($proxy !~ /http\:\/\/.*\:\d\d/) {
        &help();
    }
}

if ($url =~ m/^https/) {
    $url =~ s/^https:\/\///;
    $scheme = "https://";
} else {
    $url =~ s/^http:\/\///;
}

my $url_prefix = $url;
$url_prefix =~ s/^([^:]*:\/\/).*$/$1/;
$url =~ s/^[^:]+:\/\///;
my $i              = 0;
my $part_active    = 0;
my $j              = 0;
my @inactive_parts = ();

my $query_params = undef;
my $urlHasParams = index($url, "?");
my $file         = undef;

my @parts = (split(/\//, $url));
my $parts_size = scalar(@parts);

if ($pathparams) {
    if (scalar(@parts) > 1) {
        $file = pop @parts;
    } else {
        print STDERR "\nERROR: No file segment in the path when using --pathparams.\n\n";
        &help();
        exit 1;
    }
    if ($urlHasParams) {
        $file = (split(/\?/, $file))[0];
    }
    $parts_size = scalar(@parts);
}
if ($urlHasParams > 0) {
    if (!$pathparams) {
        ($parts[$parts_size - 1], $query_params) = (split(/\?/, $parts[$parts_size - 1]));
    } else {
        $query_params = (split(/\?/, $url))[1];
    }
}

foreach my $part (@parts) {
    if (length($useparts) > $i) {
        $part_active = substr($useparts, $i++, 1);
    }
    if ($part_active) {
        $string .= $part . "/";
    } else {
        $inactive_parts[$j] = $part;
    }
    $j++;
}

my $signing_signature = undef;

chop($string);
if ($pathparams) {
    if (defined($client)) {
        $signing_signature =
          ";C=" . $client . ";E=" . (time() + $duration) . ";A=" . $algorithm . ";K=" . $keyindex . ";P=" . $useparts . ";S=";
        $string .= $signing_signature;
    } else {
        $signing_signature = ";E=" . (time() + $duration) . ";A=" . $algorithm . ";K=" . $keyindex . ";P=" . $useparts . ";S=";
        $string .= $signing_signature;
    }
} else {
    if (defined($client)) {
        if ($urlHasParams > 0) {
            $signing_signature =
                "?$query_params" . "&C="
              . $client . "&E="
              . (time() + $duration) . "&A="
              . $algorithm . "&K="
              . $keyindex . "&P="
              . $useparts . "&S=";
            $string .= $signing_signature;
        } else {
            $signing_signature =
              "?C=" . $client . "&E=" . (time() + $duration) . "&A=" . $algorithm . "&K=" . $keyindex . "&P=" . $useparts . "&S=";
            $string .= $signing_signature;
        }
    } else {
        if ($urlHasParams > 0) {
            $signing_signature =
              "?$query_params" . "&E=" . (time() + $duration) . "&A=" . $algorithm . "&K=" . $keyindex . "&P=" . $useparts . "&S=";
            $string .= $signing_signature;
        } else {
            $signing_signature = "?E=" . (time() + $duration) . "&A=" . $algorithm . "&K=" . $keyindex . "&P=" . $useparts . "&S=";
            $string .= $signing_signature;
        }
    }
}

my $digest;
if ($algorithm == 1) {
    $digest = hmac_sha1_hex($string, $key);
} else {
    $digest = hmac_md5_hex($string, $key);
}

$verbose && print "\nSigned String: $string\n\n";
$verbose && print "\nUrl: $url\n";
$verbose && print "\nsigning_signature: $signing_signature\n";
$verbose && print "\ndigest: $digest\n";

if ($urlHasParams == -1) {    # no application query parameters.
    if (!defined($proxy)) {
        if (!$pathparams) {
            print "curl -s -o /dev/null -v --max-redirs 0 '$scheme" . $url . $signing_signature . $digest . "'\n\n";
        } else {
            my $index = rindex($url, '/');
            $url = substr($url, 0, $index);
            my $encoded = MIME::Base64::URLSafe::encode($signing_signature . $digest);
            if (defined($sig_anchor)) {
                print "curl -s -o /dev/null -v --max-redirs 0 '$scheme" . $url . ";${sig_anchor}=" . $encoded . "/$file" . "'\n\n";
            } else {
                print "curl -s -o /dev/null -v --max-redirs 0 '$scheme" . $url . "/" . $encoded . "/$file" . "'\n\n";
            }
        }
    } else {
        if (!$pathparams) {
            print "curl -s -o /dev/null -v --max-redirs 0 --proxy $proxy '$scheme" . $url . $signing_signature . $digest . "'\n\n";
        } else {
            my $index = rindex($url, '/');
            $url = substr($url, 0, $index);
            my $encoded = MIME::Base64::URLSafe::encode($signing_signature . $digest);
            if (defined($sig_anchor)) {
                print "curl -s -o /dev/null -v --max-redirs 0 --proxy $proxy '$scheme"
                  . $url
                  . ";${sig_anchor}="
                  . $encoded
                  . "/$file" . "'\n\n";
            } else {
                print "curl -s -o /dev/null -v --max-redirs 0 --proxy $proxy '$scheme" . $url . "/" . $encoded . "/$file" . "'\n\n";
            }
        }
    }
} else {    # has application parameters.
    $url = (split(/\?/, $url))[0];
    if (!defined($proxy)) {
        if (!$pathparams) {
            print "curl -s -o /dev/null -v --max-redirs 0 '$scheme" . $url . $signing_signature . $digest . "'\n\n";
        } else {
            my $index = rindex($url, '/');
            $url = substr($url, 0, $index);
            my $encoded = MIME::Base64::URLSafe::encode($signing_signature . $digest);
            if (defined($sig_anchor)) {
                print "curl -s -o /dev/null -v --max-redirs 0 '$scheme"
                  . $url
                  . ";${sig_anchor}="
                  . $encoded . "/"
                  . $file
                  . "?$query_params" . "'\n\n";
            } else {
                print "curl -s -o /dev/null -v --max-redirs 0 '$scheme" . $url . "/" . $encoded . "/" . $file . "?$query_params"
                  . "'\n\n";
            }
        }
    } else {
        if (!$pathparams) {
            print "curl -s -o /dev/null -v --max-redirs 0 --proxy $proxy '$scheme" . $url . $signing_signature . $digest . "'\n\n";
        } else {
            my $index = rindex($url, '/');
            $url = substr($url, 0, $index);
            my $encoded = MIME::Base64::URLSafe::encode($signing_signature . $digest);
            if (defined($sig_anchor)) {
                print "curl -s -o /dev/null -v --max-redirs 0 --proxy $proxy '$scheme"
                  . $url
                  . ";${sig_anchor}="
                  . $encoded . "/"
                  . $file
                  . "?$query_params" . "'\n\n";
            } else {
                print "curl -s -o /dev/null -v --max-redirs 0 --proxy $proxy '$scheme"
                  . $url . "/"
                  . $encoded
                  . "/$file?$query_params" . "'\n\n";
            }
        }
    }
}

sub help
{
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
    print "             [--pathparams] \n";
    print "             [--proxy <url:port value>] ex value: http://myproxy:80\n";
    print "\n";
}
