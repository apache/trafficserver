#!/usr/local/bin/perl 

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


#--------------------------------------------------------------------------
# ftp.pl
#
# This script downloads a file from remote ftp host.
#
 
#--------------------------------------------------------------------------

use Net::FTP;
use Getopt::Std;

$script_name = "ftp.pl";

sub usage {
    printf "\[Usage\] $script_name  \[-s ftp-host\] \[-d ftp-dir\] \[-n ftp-file\] \[-m ftp-mode\] \[-u ftp-user\] \[-p ftp-password\]\n";
    printf "                \[-h\]\n";
    printf "        -s        - ftp server\n";
    printf "        -d        - ftp directory\n";
    printf "        -n        - ftp file\n";
    printf "        -m        - ftp mode\n";
    printf "        -u        - ftp user id\n";
    printf "        -p        - ftp user password\n";
    printf "        -h        - help (this screen)\n";
}

sub ftp {

    my ($host, $dir, $name, $mode, $user, $password)=@_;

    my $ftp = Net::FTP->new($host);

    $ftp->login($user, $password);
    $ftp->cwd($dir);
    if( $mode eq "ascii" ) {
        $ftp->ascii();
    } else {
        $ftp->binary();
    }
    $ftp->get($name);
    $ftp->quit;
}

#--------------------------------------------------------------------------
# main() begins here
#--------------------------------------------------------------------------

# parse command args

&getopts("hs:d:n:m:u:p:");

if( defined $opt_h ) {
    usage;
    exit;
}

if( !defined $opt_s ) {
    usage;
    exit;
}

if( !defined $opt_d ) {
    usage;
    exit;
}

if( !defined $opt_n ) {
    usage;
    exit;
}

if( !defined $opt_m ) {
    usage;
    exit;
}

if( !defined $opt_u ) {
    usage;
    exit;
}

if( !defined $opt_p ) {
    usage;
    exit;
}

# ftp(ftp_host ftp_dir ftp_fn ftp_mode ftp_usr ftp_pw)
ftp($opt_s, $opt_d, $opt_n, $opt_m, $opt_u, $opt_p);

exit 1;
