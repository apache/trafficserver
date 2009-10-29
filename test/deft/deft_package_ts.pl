#!/inktest/dist/bin/perl

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
#  deft_package_ts.pl
#
#
#  Description: builds a DEFT package from a Traffic Server build area
#
# TODO cleanup after CTRL-C
#
#  
#

#
# PACKAGE: read_manifiest
#
#   knows how read Traffic Server style manifests.
#   Originally this was a separate file but the issues with
#    perl module paths and Makefile VPATH are rather annoying
#
package read_manifest;

use File::Spec;
use strict vars;

our $cpp = "";

sub find_cpp {

    if (open (GCC, "gcc -v 2>&1 |")) {
	my $gcc = <GCC>;
	close(GCC);
	if ($gcc !~ /usage/) {
	    $cpp = "gcc -x c -E -P"
	}
	return $cpp;
    }

    if (open (GCC, "/usr/releng/bin/gcc -v 2>&1 |")) {
	my $gcc = <GCC>;
	close(GCC);
	if ($gcc !~ /usage/) {
	    $cpp = "/usr/releng/gcc -x c -E -P"
	}
	return $cpp;
    }

    if (open(CC, "cc -V 2>&1 |")) {
	my $cc = <CC>;
	close(CC);
	if ($cc !~ /usage/) {
	    $cpp = "cc -E -P";
	}
	return $cpp;
    }

    if (open(CC, "cc -c 2>&1 |")) {
	my $cc = <CC>;
	close(CC);
	if ($cc !~ /usage/) {
	    if ($cc =~ /gcc/) {
		$cpp = "cc -x c -E -P"
	    } else {
		$cpp = "cc -E -P";
	    }

	}
	return $cpp;
    }

    die "Could not find cpp\n";
}

sub build_path {
    my (@els) = @_;
    my $ret_value = "";

    my $tmp;
    while ($tmp = shift(@els)) {
	if ($ret_value && $ret_value !~ /\/$/) {
	    $ret_value = $ret_value . "/" . $tmp;
	} else {
	    $ret_value = $ret_value . $tmp;
	}
    }

    return $ret_value;
}

sub read_manifest_file {
    my ($mfile, $mdir, $hash_ref, $cpp_define) = @_;

    # Make sure we found access to cpp
    if ($cpp eq "") {
	find_cpp();
    }

    my $filename = $mdir . "/" . $mfile;

    if (!open(MANIFEST_FILE, "< $filename")) {
	warn "Could not open manifest file $filename : $!\n";
	return 1;
    }

    # fix unique if pid
    my $tmpfile = "/tmp/manifest_tmp." . "$>" .  "." . $$;
    if (!open(MANIFEST_OUT, "> $tmpfile")) {
	warn "Could not open manifest out $tmpfile : $!\n";
	return 1;
    }

    my $line;
    while ($line = <MANIFEST_FILE>) {
	if ($line =~ /^#define\s+OS_opt\s+(\S+)/) {
	    $line = "#define OS_opt BUILDTOP\n";
	}
	print MANIFEST_OUT $line;
    }

    close (MANIFEST_FILE);
    close (MANIFEST_OUT);

    if (!open(CPP_PIPE, "$cpp -D" . $cpp_define . " " . $tmpfile . " |")) {
	warn ("Could not open cpp pipe : $!\n");
	unlink($tmpfile);
	return 1;
    }

    my $from = "";
    my $to = "";
    my $file_block = 0;

    while ($line = <CPP_PIPE>) {

	# Ignore comments and blank lines
	if ($line =~ /^#/ || $line =~ /^\s*$/) {
	    $file_block = 0;
	    next;
	}

	if ($file_block == 1) {
	    if ($line =~ /^-/) {
		my @elements = split(/\s+/, $line);

		my $src = $elements[1];
		my $dest;
		if ($elements[2] eq "-") {
		    $dest = $elements[3];
		} else {
		    $dest = $src;
		}

		my $full_in = File::Spec->catfile($from, $src);
		my $full_out = File::Spec->catfile($to, $dest);

		# Because a one src file can be sent to multiple
		#  destinations, we use the target as the key
		#  to the hash
		$$hash_ref{$full_out} = $full_in;
	    } else {
		$file_block = 0;
	    }
	}

	if ($file_block == 0) {
	    $line =~ s/BUILDTOP\s+\//BUILDTOP\//;
	    $line =~ s/^\/\s*//;

	    ($from, $to) = split(/\s+/, $line, 2);

	    chomp($to);
	    $to =~ s/^\///;

	    $file_block = 1;
	}

#	print $line;
    }


    unlink($tmpfile);
    return 0;
}

#
# PACKAGE: main
#
#
package main;

use strict vars;
use File::Spec;
use File::Path;
use File::Copy;
use File::Find;
use Cwd;

# Global config
our $build_dir;
our $output_dir;
our $quiet = 0;
our $verbose = 0;
our $remove_old = 0;

our $system = `uname -s`;
chomp($system);

sub find_src_dir {
    my ($bdir) = @_;

    my $sdir;
    my $makefile = File::Spec->catfile($bdir, "Makefile");

    if (open(MAKEFILE, "< $makefile")) {
	my $line;
	while ($line = <MAKEFILE>) {
	    if ($line =~ /^top_srcdir = (\S+)/) {
		$sdir = $1;
		last;
	    }
	}
	close(MAKEFILE);

	if (! $sdir) {
	    die "No top_srcdir var in $makefile\n";
	}

	return File::Spec->catdir($bdir, $sdir);
    } else {
	die "Could not open $makefile : $!\n";
    }
}

sub normalize_path {
    my ($input_path) = @_;

#    print "N: $input_path\n";

    my @path_el = split(/\/+/, $input_path);

    my $tmp;
    my $new_path = "";
    my $skips_needed = 0;

    while ($tmp = pop(@path_el)) {
#	print "Looking at $tmp\n";

	if ($tmp eq "..") {
	    $skips_needed++;
#	    print "Removing $junk\n";
	} elsif ($tmp ne "." && $tmp ne "") {
	    if ($skips_needed > 0) {
		$skips_needed--;
	    } else {
		if ($new_path) {
		    $new_path = $tmp . "/" . $new_path;
		} else {
		    $new_path = $tmp;
		}
	    }
	}
    }

    if ($input_path =~ /^\//) {
	$new_path = "/" . $new_path;
    }

    return $new_path;
}

sub build_pkg_file_name {
    my ($pkg_name, $id) = @_;

    my $username = scalar(getpwuid($>));

    my $platform;
    if ($system eq "SunOS" &&
	'uname -p' =~ /86$/) {
	$platform = "SunOSx86";
    } else {
	$platform = $system;
    }

    my @ltime = localtime();

    my $pkg_fname = sprintf("%s-%s-%s-%s.%.2d.%.2d.%.2d.%.2d",
			    $pkg_name, $platform,
			    $username, $id,
			    $ltime[4] + 1, $ltime[3],
			    $ltime[2], $ltime[1]);

    return $pkg_fname;
}


#
# Makes a copy to our tmp image of a single file
#
sub process_single_file {
    my ($part_from, $part_to, $build_dir, $src_dir, $tmp_dir) = @_;

     my ($crap, $out_dir, $out_file) = File::Spec->splitpath($part_to);

    $out_dir = File::Spec->catdir($tmp_dir, $out_dir);
    if (! -d $out_dir) {
	eval { mkpath($out_dir) };
	if ($@) {
	    print "Couldn't create $out_dir: $@\n";
	    next;
	}
    }

    my $copy_from = $part_from;
    if ($copy_from =~ s/^BUILDTOP\///) {
	$copy_from = File::Spec->catfile($build_dir, $copy_from);
    } else {
	$copy_from = File::Spec->catfile($src_dir, $copy_from);
    }

    my @stat_info = stat($copy_from);
    if (scalar(@stat_info) == 0) {
	print "--- Could not find $copy_from\n";
	next;
    }

    my $copy_to = File::Spec->catfile($tmp_dir, $part_to);

#    print "$copy_from : $copy_to\n";
    if (File::Copy::syscopy($copy_from, $copy_to) == 0) {
	warn("Failed to copy $copy_from to $copy_to : $!\n");
    }
    chmod(@stat_info[2] & 0755, $copy_to);
}    

#
# Makes the date file which is part of the package
#
sub output_date_file {
    my ($tmp_dir) = @_;

    my $date_str = scalar(localtime());
    my $date_path = File::Spec->catfile($tmp_dir, "DATE");
    
    if (!open (DATE_FILE, "> $date_path")) {
	warn ("Failed to create date file ($date_path) : $!\n");
	return;
    }

    print DATE_FILE "$date_str\n";
    close(DATE_FILE);
}

#
# Helpers to File::Find for building the package manifest
#
our @pkg_manifest_find_array = ();
sub pkg_manifest_find_helper() {
    if (-f $_) {
	push(@pkg_manifest_find_array, $File::Find::name);
    }
}

sub build_pkg_manifest {
    my ($tmp_dir) = @_;

    @pkg_manifest_find_array = ();
    find(\&pkg_manifest_find_helper, $tmp_dir);

    my $manifest_file = File::Spec->catfile($tmp_dir, "MANIFEST");
    if (!open(PKG_MANIFEST, "> $manifest_file")) {
	warn("Failed to created pkg manifest : $!\n");
    }
    my $i;
    foreach $i (@pkg_manifest_find_array) {
	my $rindex = ($i, $tmp_dir);

	if ($rindex >= 0) {
	    my $tocut = $rindex + length($tmp_dir);

	    # We want to eliminate trailing slash as well
	    $tocut++;

	    my $tmp = substr($i, $tocut);

	    print PKG_MANIFEST "$tmp\n";
	} else {
	    warn("Internal error in creating manifest\n");
	}
    }

    close(PKG_MANIFEST);
}

sub read_ts_manifests {
    my ($build_dir, $src_dir) = @_;

    my %file_hash = ();
    my $manifest_dir = File::Spec->catfile($src_dir, "proxy/manifest");

    if (! -d $manifest_dir) {
	die "No manifest dir $manifest_dir\n";
    }

    # Read the platform dependent manifest file.
    my %system_to_define = ("SunOS" => "MKENVsolaris",
			    "Linux" => "MKENVlx86");
    my $sys_define = $system_to_define{$system};

    if ($sys_define ) {
      read_manifest::read_manifest_file("traffic.db", $manifest_dir,
					\%file_hash, $sys_define);
    } else {
	warn "Error No define set for $system\n";
	return 1;
    }
 
    # Read the adm manifest (UI files)
    read_manifest::read_manifest_file("traffic_adm.db", $manifest_dir,
				  \%file_hash, "MKENVtraffic_adm");

    my %filtered_hash = ();

    # Filter down just to the files we care about
    my $i;
    foreach $i (sort(keys(%file_hash))) {
	if ($i eq "bin/traffic_server" ||
	    $i eq "bin/traffic_manager" ||
	    $i eq "bin/traffic_cop" ||
	    $i eq "bin/traffic_line" ||
	    $i =~ /^bin\/libldap/ ||
	    $i =~ /^config\// ||
	    $i =~ /^ui\//) {
	    $filtered_hash{$i} = $file_hash{$i};
	}
    }

    return \%filtered_hash;
}


# 
#  Helper stuff for use File::Find to do expanisions
#     on '*' in file names
#
our @glob_find_array = ();
sub glob_find_helper() {
    my $file = $File::Find::name;
    if ($file !~ /\/CVS\// &&
	-f $file) {
	push(@glob_find_array, $file);
    }
}


sub copy_files_to_image {
    my ($build_dir, $src_dir, $tmp_dir, $hash_ref) = @_;
    
    # Loop over the filtered file list and make a copy to the tmp dir
    my $i;
    foreach $i (sort(keys(%$hash_ref))) {

	# First cleck to see if there is any globbing going ong
	my $glob_check = $$hash_ref{$i};
	if ($glob_check =~ s/\*$//) {

	    # Unfortunately, this isn't real globbing.  It's a recursive copy
	    # First we need to find all the files
	    @glob_find_array = ();
	    find(\&glob_find_helper, File::Spec->catfile($src_dir,$glob_check));

	    # Now that we got all the files, we need to process the names one
	    #  by one
	    my $j;
	    while ($j = shift(@glob_find_array)) {
		# We need strip off the $src_dir component
		my $rindex = index($j, $src_dir);

		if ($rindex == 0) {
		    # Strip off the src_path from the find result
		    $j = substr($j, length($src_dir)+1);
		    my ($crap, $xpath, $xfile) = File::Spec->splitpath($j);

		    # Remove the globbing target and construct the to path
		    my $to_dir_glob_strip = $i;
		    $to_dir_glob_strip =~ s/\*$//;
		    my $to_file = File::Spec->catfile($to_dir_glob_strip, $xfile);

		    process_single_file($j, $to_file, $build_dir, $src_dir, $tmp_dir);
		} else {
		    die "Internal error on $$hash_ref{$i}\n";
		}
		
	    }
	} else {
	    process_single_file($$hash_ref{$i}, $i, $build_dir, $src_dir, $tmp_dir);
	}
    }
}

sub remove_old_pkgs {
    my ($pkg_name) = @_;

    my $prefix;
    if ($pkg_name =~ /^([^-]+)-([^-]+)/) {
	$prefix = $1 . "." . $2;
    } else {
	die "Internal error - malformed pkg name: $pkg_name\n";
    }

    if (!opendir(ODIR, $output_dir)) {
	print "-- remove old failed; could not read output dir : $!\n";
	return;
    }

    my @files = readdir(ODIR);

    my $tmp;
    while ($tmp = shift(@files)) {
	if ($tmp =~ /^$prefix.+\.tgz$/) {
	    $tmp = File::Spec->catfile($output_dir, $tmp);
	    unlink($tmp);
	}
    }

    closedir(ODIR);
}

sub make_the_pkg {
    my ($tmp_dir, $pkg_fname) = @_;

    my $output_pkg = File::Spec->catfile($output_dir, $pkg_fname . ".tgz");
    $output_pkg = File::Spec->rel2abs($output_pkg);

    my ($crap, $xdir, $xfile) = File::Spec->splitpath($tmp_dir);

    my $orig_dir = getcwd();
    chdir($xdir);

    my $cmd = "tar -cf - " . $xfile;
    $cmd = $cmd . " | gzip - > " . $output_pkg;

    if ($verbose) {
	print "Running: $cmd\n";
    }
    my @result = system($cmd);

    chdir($orig_dir);

    return $output_pkg;
}
    
sub do_the_work {
    my ($build_dir) = @_;

    my $src_dir = find_src_dir($build_dir);
    $src_dir = normalize_path($src_dir);

    if ($quiet == 0) {
	print "Processing TS manifest files....\n";
    }
    my $file_hash = read_ts_manifests($build_dir, $src_dir);

    # We need the deft instantiator as well
    $$file_hash{"bin/ts-instantiate.pl"} = "proxy/ts-instantiate.pl";

    my $pkg_fname = build_pkg_file_name (ts, "tsunami");
    my $tmp_top_dir = "deft_pkg_tmp." . $$;
    my $tmp_dir = File::Spec->catfile ($tmp_top_dir, $pkg_fname);
    eval { mkpath($tmp_dir) };
    if ($@) {
	die "Couldn't create $tmp_dir: $@\n";
	
    }

    if ($quiet == 0) {
	print "Creating install image....\n";
    }
    copy_files_to_image($build_dir, $src_dir, $tmp_dir, $file_hash);

    output_date_file($tmp_dir);
    build_pkg_manifest($tmp_dir);

    if ($remove_old > 0) {
	if ($quiet == 0) {
	    print "Removing old packages....\n";
	    remove_old_pkgs($pkg_fname);
	}
    }

    if ($quiet == 0) {    
	print "Building DEFT pkg....\n";
    }
    my $pkg_loc = make_the_pkg($tmp_dir, $pkg_fname);

    if ($quiet == 0) {    
	print "Cleaning up....\n";
    }
    rmtree($tmp_top_dir);

    if ($quiet < 2) {
	print "Result: $pkg_loc\n";
    }
}
    

sub usage {
    print STDERR "Usage: deft_package_ts.pl -b <build_dir> [-o <output_dir>]\n";
    print STDERR "   -b <build_dir>  : Specifies location of TS build dir\n";
    print STDERR "   -o <output_dir> : Specifies dir where package should be put\n";
    print STDERR "   -R              : Remove old packages from output_dir\n";
    print STDERR "   -q              : Quiet.  -q -q is very quiet\n";
    print STDERR "   -v              : Verbose\n";
    print STDERR "   -h              : Usage message\n";
    exit(1);
}

our $arg;

while ($arg = shift(@ARGV)) {
    if ($arg eq "-b") {
	$build_dir = shift(@ARGV);
    } elsif ($arg eq "-o") {
	$output_dir = shift(@ARGV);

	if (! -d $output_dir) {
	    die "Ouput dir $output_dir doesn't exist\n";
	}
    } elsif ($arg eq "-q") {
	$quiet++;
	$verbose = 0;
    } elsif ($arg eq "-q") {
	$verbose = 1;
	$quiet = 0;
    } elsif ($arg eq "-R") {
	$remove_old = 1;
    } elsif ($arg eq "-h" || $arg eq "-H" ||
	     $arg eq "-help" || arg eq "--help") {
	usage();
    } else {
	usage();
    }

    if (! $build_dir) {
	usage();
    }

    if (! $output_dir) {
	$output_dir = getcwd();
    }
}

if ($build_dir !~ /^\//) {
    $build_dir = getcwd() . "/" . $build_dir;
}

do_the_work($build_dir);
