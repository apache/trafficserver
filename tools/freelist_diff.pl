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

sub usage {
    print "Usage: freelist_diff.pl dump1.txt dump2.txt\n";
}

sub int_meg {
    my $bytes = shift;
    return $bytes / (1024*1024);
}

sub load_file {
    my $file = shift;
    my %data;

    open(DATA, $file) || return undef;
    while(<DATA>) {
        my @items = split;
        chomp @items;

        if ($#items == 6) {
            $data{$items[6]} = [int_meg($items[0]), int_meg($items[2]), int_meg($items[4])];
        }
    }
    close(DATA);

    return \%data;
}

my $data1 = load_file($ARGV[0]) || die usage();
my $data2 = load_file($ARGV[1]) || die usage();
my %diff;

while (my ($key, $value) = each(%{$data1})) {
    # before alloc [0], after alloc [1], before in-use [2], after in-use [3]
    $diff{$key} = [ $value->[0], $data2->{$key}->[0], $value->[1], $data2->{$key}->[1],
                    # diff alloc [4], diff in-use [5]
                    $data2->{$key}->[0] - $value->[0], $data2->{$key}->[1] - $value->[1],
                    # type size [6]
                    $value->[2] ];
}

print "Sorted by in-use growth\n";
print "=======================\n";
foreach (sort {$diff{$b}->[5] <=> $diff{$a}->[5]} keys %diff) {
    printf("%s (%.3fM): %.1fM -> %.1fM == %.1fM\n", $_, $diff{$_}->[6], $diff{$_}->[2], $diff{$_}->[3], $diff{$_}->[5]);
}

print "\n\nSorted by allocated growth\n";
print "==========================\n";
foreach (sort {$diff{$b}->[4] <=> $diff{$a}->[4]} keys %diff) {
    printf("%s (%.3fM): %.1fM -> %.1fM == %.1fM\n", $_, $diff{$_}->[6], $diff{$_}->[0], $diff{$_}->[1], $diff{$_}->[4]);
}
