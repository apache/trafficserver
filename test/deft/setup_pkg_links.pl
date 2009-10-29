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
# setup_pkg_links.pl
#
#
#
#   Description:
#
#   
#
#

use strict vars;

if (scalar(@ARGV) != 2) {
    print("Usage: setup_pkg_links.pl <real_pkg_dir> <link_link_dir>\n");
}

my $real_pkg_dir = $ARGV[0];
my $link_pkg_dir = $ARGV[1];
my %already_have = ();

opendir(LINK_DIR, $link_pkg_dir) || die "Could not read dir $link_pkg_dir : $!\n";

my $tmp;
while ($tmp = readdir(LINK_DIR)) {
    if ($tmp =~ /(^[^-]+)-([^-]+)-([^-]+)-([^-]+)\.\d\d\.\d\d\.\d\d\.\d\d.tgz$/) {
	# Looks like a pkg file
	my $have_pkg = $1 . "-" . $2;
	print "Adding have pkg $have_pkg\n";

	$already_have{$have_pkg} = $tmp;
    }
}

close (LINK_DIR);


opendir(SRC_DIR, $real_pkg_dir) || die "Could not read dir $real_pkg_dir : $!\n";

while ($tmp = readdir(SRC_DIR)) {
    if ($tmp =~ /(^[^-]+)-([^-]+)-([^-]+)-([^-]+)\.\d\d\.\d\d\.\d\d\.\d\d.tgz$/) {
	# Looks like a pkg file
	my $src_pkg = $1 . "-" . $2;

	if ($already_have{$src_pkg} ne $tmp) {
            if (defined $already_have{$src_pkg}) {
                my $old_fake = $link_pkg_dir . '/' . $already_have{$src_pkg};
                unlink($old_fake) || warn "Unlink of $old_fake failed : $!";
                print "Unlinked $old_fake\n";
            }
	    my $real = $real_pkg_dir . "/" . $tmp;
	    my $fake = $link_pkg_dir . "/" . $tmp;
	    symlink($real, $fake) || warn "Symlink of $tmp failed : $!";
	    print "Symlinked $tmp\n";
	} else {
	    print "Skipping $tmp since we already have it\n";
	}
    }
}

close (SRC_DIR);

exit(0);
