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


#

# This just runs one load regression session and writes everything to
# the screen.


# use /usr/ucb/rsh, not /usr/sbin/rsh...
$ENV{PATH} = "/usr/ucb:$ENV{PATH}";

require 'hostname.pl';
require 'ctime.pl';
########################################
# CONFIG
########################################
#$myserverportbase controls which range of ports are used on the ptest
# cluster.  It is a good idea to leave a gap of 20 ports between starting
# port #s.
$myserverportbase{'foo'} = 10140;
chomp($ARCH=`arch`);
$test_bindir = "/net/granite/qa/ts/share/bin";
#$test_bindir = "/net/proxydev/export/crawlspace/workareas/clarsen/q/quality/sqe/ts_test";
# $users controls the 'load'.
$users = 10;
$testlength=180;		# 3 minutes per test
$one_at_a_time = 1;
$version = "2.0";		# temp hack until multi-version support
########################################
# END of CONFIG
########################################
args:
    while ($x = $ARGV[0]) {
	if ($x eq "-h") {
	    shift(@ARGV);
	    $myhost = shift(@ARGV);
	} elsif ($x eq "-p") {
	    shift(@ARGV);
	    $myport = shift(@ARGV);
	} elsif ($x eq "-u") {
	    shift(@ARGV);
	    $users = shift(@ARGV);
	} elsif ($x eq "-version") {
	    shift(@ARGV);
	    $version = shift(@ARGV);
	} elsif ($x eq "-l") {
	    shift(@ARGV);
	    $testlength = shift(@ARGV);
	} elsif ($x eq "-v") {
	    $verbose = "-verbose";
	    shift(@ARGV);
	} elsif ($x eq "-all") {
	    $one_at_a_time = 0;
	    shift(@ARGV);
	} elsif ($x eq "-t") {
	    shift(@ARGV);
	    @tests = split(',',shift(@ARGV));
	} else {
	    last args;
	}
    }

$|=1;				# unbuffer I/O

if (!defined($myhost)) {
    $myhost  = &hostname;
}
open(ID,"id|");
while(<ID>) {
    chomp;
    if (/uid=\d+\(([^\)]*)/) {
	$mylogin = "$1";
    }
}
close(ID);
if (! defined($mylogin)) {
    die "Couldn't determine login from the id command.";
}

if (!defined($myport)) {
    if (! (-e "etc/trafficserver/records.config.shadow") && ! ( -e "etc/trafficserver/records.config")) {
	die "This must be run from traffic/proxy, and you must have etc/trafficserver/records.config.shadow set up.";
    }
    open(S,"<etc/trafficserver/records.config.shadow") || open(S,"<etc/trafficserver/records.config");
    while (<S>) {
	if (/proxy.config.http.server_port\s/) {
	    ($x,$y,$z,$myport) = split(/\s+/);
	}
    }
    close(S);
    if (!defined($myport)) {
	die "Couldn't determine your server port number";
    } else {
	print "Using proxy on $myhost:$myport\n";
    }
}

$baseport = $myserverportbase{$mylogin};
if (!defined($baseport)) {
    die "You must set \$myserverportbase{'$mylogin'}";
}

if ( ! (-d $test_bindir)) {
    die "$test_bindir doesn't exist?  Maybe /net/granite isn't available.";
}
$SIG{'TERM'} = sub {
    print "caught TERM\n";
    kill 'TERM', 0;
    exit;
};
$SIG{'INT'} = sub {
    print "caught INT\n";
    kill 'INT', 0;
    exit;
};
$SIG{'QUIT'} = sub {
    print "caught QUIT\n";
    kill 'QUIT', 0;
    exit;
};
sub test_maybe_exit {
    my ($rc) = @_;
    $rc = $rc & 0xffff;
    if ($rc > 0x80) {
	print "---- failed\n";
    }
    if ($rc != 0) {		# caught a signal
	print &ctime(time);
	die "Caught a signal";
    }
}
if ($one_at_a_time) {
    if (!defined(@tests)) {
	#@tests = ('bad-header', 'garbage-1' 'garbage-2', 'cache', 'inkbench', 'garbage-1' );
	@tests = ('jtest', 'cache', 'inkbench', 'bad-header', 'garbage-2', 'garbage-1' );
    }
    foreach $i (@tests) {
	print "---- Running '$i' test on $myhost:$myport for $testlength seconds with $users users load\n";
	print &ctime(time);
	&test_maybe_exit(system("$test_bindir/run_all.pl $myhost $myport -hostpool inktomi -version $version -users $users -baseport $baseport -testlength $testlength -only $i $verbose"));
	print &ctime(time);
	print "---- Done with $i test\n";
    }
} else {
    @except = ('http11');
    $excepts = join(',',@except);
    print "Running all tests (except $excepts) on $myhost:$myport for $testlength seconds with $users users total load\n";
    &test_maybe_exit(system("$test_bindir/run_all.pl $myhost $myport -hostpool inktomi -version $version -users $users -baseport $baseport -testlength $testlength -except $excepts $verbose"));
}
