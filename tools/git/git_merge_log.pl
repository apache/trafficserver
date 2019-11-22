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

use Getopt::Std;

sub usage() {
  print "USAGE: git_merge_log.pl -b local_branch -r remote_branch -m remote_master_branch\n";
  print "\tgit_merge_log.pl -b 8.0.x -r apache/8.0.x -m apache/master\n";
  exit(1);
}

sub git_status($$) {
  my($remote_branch, $local_branch) = @_;
  my %status;
  my @commits;

  my @lines = `git log --no-merges --cherry-mark --right-only $remote_branch...$local_branch --pretty=format:'%m~%an~%cn~%aD~%h~%s'`;
  foreach my $line (@lines) {
    #print $line;
    chomp $line;
    my @array = split('~', $line);
    $status{$array[4]} = \@array;
    push(@commits, $array[4]);
  }

  return \%status, \@commits;
}

{
  my %opts;
  getopts("b:r:m:", \%opts);
  usage() if (! defined $opts{b} || ! defined $opts{r} || ! defined $opts{m});

  my $remote_branch = $opts{r};
  my $local_branch = $opts{b};
  my $remote_master = $opts{m};

  my($status_branch, $commits) = git_status($remote_branch, $local_branch);
  my($status_master) = git_status($remote_master, $local_branch);

  my $total = scalar(keys %$status_branch);
  my $count = 0;
  foreach my $hash (keys %$status_branch) {
    ++$count;
    print STDERR "\r[$count / $total] hash: $hash";

    # different status and if they are found
    my $cherry_mark_master = 0;
    my $cherry_mark_branch = 0;
    my $cherry_pick_hash_master = 0;
    my $cherry_pick_hash_branch = 0;

    # set the cherry mark status
    $cherry_mark_master = 1 if ($status_master->{$hash}[0] eq '=');
    $cherry_mark_branch = 1 if ($status_branch->{$hash}[0] eq '=');

    # valdiate to see if the status is = or >
    die if ($status_master->{$hash}[0] ne '=' && $status_master->{$hash}[0] ne '>');
    die if ($status_branch->{$hash}[0] ne '=' && $status_branch->{$hash}[0] ne '>');

    # remove the cherry mark status
    shift(@{$status_branch->{$hash}});

    # find the cherry pick hash value
    my @show = `git show $hash`;
    my $hash_in_comment = 0;
    my $cherrypick_hash;
    foreach my $show_line (@show) {
      if ($show_line =~ m|cherry picked from commit (\w+)|) {
        $hash_in_comment++;
        $cherrypick_hash = $1;

        my @out = `git log $remote_master | grep "$cherrypick_hash"`;
        $cherry_pick_hash_master = 1 if $? == 0;

        @out = `git log $remote_branch | grep "$cherrypick_hash"`;
        $cherry_pick_hash_branch = 1 if $? == 0;
      }
    }

    # add cherry pick hash to the commit status
    if ($hash_in_comment == 0) {
      unshift(@{$status_branch->{$hash}}, 'no hash');
    } elsif ($hash_in_comment == 1) {
      unshift(@{$status_branch->{$hash}}, $cherrypick_hash);
    } elsif ($hash_in_comment > 1) {
      unshift(@{$status_branch->{$hash}}, 'multiple hashes');
    } else {
      die;
    }

    # put the status of the cherry pick hash on the commit status
    if ($cherry_pick_hash_branch == 1) {
      unshift(@{$status_branch->{$hash}}, 'found');
    } else {
      unshift(@{$status_branch->{$hash}}, 'not found');
    }

    if ($cherry_pick_hash_master == 1) {
      unshift(@{$status_branch->{$hash}}, 'found');
    } else {
      unshift(@{$status_branch->{$hash}}, 'not found');
    }

    # put the status of the cherry mark on the commit status
    if ($cherry_mark_branch == 1) {
      unshift(@{$status_branch->{$hash}}, 'found');
    } else {
      unshift(@{$status_branch->{$hash}}, 'not found');
    }

    if ($cherry_mark_master == 1) {
      unshift(@{$status_branch->{$hash}}, 'found');
    } else {
      unshift(@{$status_branch->{$hash}}, 'not found');
    }
  }

  print "\n";
  print join("\t", ('Cherry Mark Master', 'Cherry Mark Branch', 'Cherry Pick Master', 'Cherry Pick Branch',
             'Cherry Pick Hash', 'Author', 'Committer', 'Date', 'Local Hash', 'Commit Comment')), "\n";

  foreach my $hash (@$commits) {
    print join("\t", @{$status_branch->{$hash}}), "\n";
  }
}
