#!/usr/bin/perl

#
## Licensed to the Apache Software Foundation (ASF) under one
## or more contributor license agreements.  See the NOTICE file
## distributed with this work for additional information
## regarding copyright ownership.  The ASF licenses this file
## to you under the Apache License, Version 2.0 (the
## "License"); you may not use this file except in compliance
## with the License.  You may obtain a copy of the License at
##
##      http://www.apache.org/licenses/LICENSE-2.0
##
## Unless required by applicable law or agreed to in writing, software
## distributed under the License is distributed on an "AS IS" BASIS,
## WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
## See the License for the specific language governing permissions and
## limitations under the License.

# This script will compare two files containing record configs and metrics.
# It provides a list of common configs/metrics and their default value changes.
# It also lists configs/metrics difference between the files.

# By default, it compares only configs. To compare metrics, you can pass -m command
# line argument. The files should be generated with e.g.
#
#    $ traffic_ctl config match .
#
# You can compare configs between the files:
#     $ compare_records.pl -f <filename1> -f <filenam2>
#
# You can compare metrics between the files use -m command line argument:
#     $ compare_records.pl -m -f <filename1> -f <filenam2>
#
# @ File compare_records.pl
#
use strict;
use warnings;
use Getopt::Long;

my ($file1, $file2, $in_files, $help);
my %file1_settings;
my %file2_settings;
my $diff_metrics;

usage()
  if (
    @ARGV < 1
    or !GetOptions(
        'f=s@'   => \$in_files,
        'm'      => \$diff_metrics,
        'help|?' => \$help
    )
    or defined $help
  );

# Input file is mandatory
die "\nTwo input files must be specified to compare\n"
  unless defined $in_files;

# Print the usage
sub usage
{
    print "Unknown option: @_\n" if (@_);
    print "Provide 2 files to compare configs or metrics.\n";
    print "By default this tool will diff only configs,\n";
    print "to get diff of metrics pass -m flag\n\n";
    print "Usage: compare_records.pl -m -f <filename1> -f <filename2>\n";
    print "   -m to diff the metrics\n";
    print "   -h for help\n\n";
    print "where the files are generated with e.g.\n\n";
    print "    \$ traffic_ctl config match .\n";
    exit;
}

my @file_list = @$in_files;
die "\nProvide only two files to compare\n" if (scalar(@file_list) != 2);
my $in_file1 = $file_list[0];
my $in_file2 = $file_list[1];

# Open input files
if (defined $in_file1) {
    open $file1, $in_file1 or die "Could not open $in_file1: $!";
}
if (defined $in_file2) {
    open $file2, $in_file2 or die "Could not open $in_file2: $!";
}

# Read input files
while (my $setting = <$file1>) {
    chomp $setting;
    my ($record, $value) = split(/:/, $setting);
    if (defined $diff_metrics) {
        # Obtain only metrics, excluding configs
        if ($record !~ /proxy.config/) {
            $file1_settings{$record} = $value;
        }
    } else {
        # Obtain only configs
        if ($record =~ /proxy.config/) {
            $file1_settings{$record} = $value;
        }
    }
}
close $file1;

while (my $setting = <$file2>) {
    chomp $setting;
    my ($record, $value) = split(/:/, $setting);
    if (defined $diff_metrics) {
        # Obtain only metrics, excluding configs
        if ($record !~ /proxy.config/) {
            $file2_settings{$record} = $value;
        }
    } else {
        # Obtain only configs
        if ($record =~ /proxy.config/) {
            $file2_settings{$record} = $value;
        }
    }
}
close $file2;

# Subroutine to compare configs/metrics and obtain common and difference between them
sub compare_configs_or_metrics
{
    my ($records1, $records2, $file) = @_;
    my %common_settings;
    my %diff_settings;
    my %settings1 = %$records1;
    my %settings2 = %$records2;

    foreach my $record (sort keys %settings1) {
        if ($settings2{$record}) {
            $common_settings{$record} = $settings1{$record};
        } else {
            $diff_settings{$record} = $settings1{$record};
        }
    }

    print "####################################################################################\n";
    print "Configs/metrics found only in $file\n";
    print "####################################################################################\n";
    foreach my $key (sort keys %diff_settings) {
        print "$key\n";
    }
    return (\%common_settings);
}

# Subroutine to obtain changes in default values among common configs/metrics
sub compare_default_values
{
    my ($records1, $records2) = @_;
    my %settings1 = %$records1;
    my %settings2 = %$records2;

    foreach my $record (sort keys %settings1) {
        if (defined $settings1{$record} && $settings2{$record}) {
            if ($settings1{$record} ne $settings2{$record}) {
                # Values doesn't match
                print "$record default value changed from $settings1{$record} -> $settings2{$record}\n";
            }
        }
    }
}

# Obtain common configs/metrics between two files
my $common2 = compare_configs_or_metrics(\%file2_settings, \%file1_settings, $in_file2);
my %common2_settings = %$common2;

my $common1 = compare_configs_or_metrics(\%file1_settings, \%file2_settings, $in_file1);
my %common1_settings = %$common1;

print "####################################################################################\n";
print "Common configs/metrics between $in_file1 and $in_file2\n";
print "####################################################################################\n";
foreach my $key (sort keys %common2_settings) {
    print "$key\n";
}

# Compare common configs/metrics and obtain changes in default values
print "####################################################################################\n";
print "Default value changes in common configs/metrics between $in_file1 and $in_file2\n";
print "####################################################################################\n";
compare_default_values(\%common1_settings, \%common2_settings);
