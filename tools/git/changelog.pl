#!/usr/bin/env perl
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

use WWW::Curl::Easy;
use JSON;
use Getopt::Std;

my $url       = "https://api.github.com";

sub usage() {
  print "USAGE: changelog.pl -o owner -r repo -m milestone [-a auth] [-v]\n";
  print "\t-v verbose\n\n";
  print "Example:\n";
  print "\tchangelog.pl -o apache -r trafficserver -m 8.0.6 -a auth_token\n";
  exit(1);
}

sub rate_fail
{
    print STDERR "You have exceeded your rate limit. Try using an auth token.\n";
    exit 2;
}

sub milestone_lookup($$$$$)
{
    my($curl, $url, $owner, $repo, $milestone_title) = @_;

    my $endpoint = "/repos/$owner/$repo/milestones";
    my $resp_body;

    $curl->setopt(CURLOPT_WRITEDATA, \$resp_body);
    $curl->setopt(CURLOPT_URL, $url . $endpoint);

    my $retcode = $curl->perform();
    if ($retcode == 0 && $curl->getinfo(CURLINFO_HTTP_CODE) == 200) {
        my $milestones = from_json($resp_body);
        foreach my $milestone (@{$milestones}) {
            if ($milestone->{title} eq $milestone_title) {
                return $milestone->{number};
            }
        }
    } elsif ($retcode == 0 && $curl->getinfo(CURLINFO_HTTP_CODE) == 403) {
        rate_fail();
    }

    undef;
}

sub is_merged($$$$$)
{
    my($curl, $url, $owner, $repo, $issue_id) = @_;

    my $endpoint = "/repos/$owner/$repo/pulls/$issue_id/merge";
    my $resp_body;

    $curl->setopt(CURLOPT_WRITEDATA, \$resp_body);
    $curl->setopt(CURLOPT_URL, $url . $endpoint);

    my $retcode = $curl->perform();
    if ($retcode == 0 && $curl->getinfo(CURLINFO_HTTP_CODE) == 204) {
        return 1;
    } elsif ($retcode == 0 && $curl->getinfo(CURLINFO_HTTP_CODE) == 403) {
        rate_fail();
    }

    undef;
}

sub issue_search($$$$$$)
{
    my($curl, $url, $owner, $repo, $milestone_id, $page) = @_;

    my $endpoint     = "/repos/$owner/$repo/issues";

    my $params = "milestone=$milestone_id&state=closed&page=$page";

    my $resp_body;

    $curl->setopt(CURLOPT_WRITEDATA, \$resp_body);
    $curl->setopt(CURLOPT_URL,       $url . $endpoint . '?' . $params);

    my $retcode = $curl->perform();
    if ($retcode == 0 && $curl->getinfo(CURLINFO_HTTP_CODE) == 200) {
        return from_json($resp_body);
    } elsif ($retcode == 0 && $curl->getinfo(CURLINFO_HTTP_CODE) == 403) {
        rate_fail();
    }

    undef;
}

{
    # get the command line arguments
    my %opts;
    getopts("o:r:m:a:v", \%opts);
    usage() if (! defined $opts{o} || ! defined $opts{r} || ! defined $opts{m});
    my $owner     = $opts{o};
    my $repo      = $opts{r};
    my $milestone = $opts{m};
    my $auth      = $opts{a};
    my $verbose   = 0;
    $verbose = 1 if (defined $opts{v});

    my $curl = WWW::Curl::Easy->new;
    $curl->setopt(CURLOPT_VERBOSE, 1) if $verbose;
    $curl->setopt(CURLOPT_HTTPHEADER, ['Accept: application/vnd.github.v3+json', 'User-Agent: Awesome-Octocat-App']);

    if (defined($auth)) {
        $curl->setopt(CURLOPT_USERPWD, $auth);
    }

    my $milestone_id = milestone_lookup($curl, $url, $owner, $repo, $milestone);

    if (!defined($milestone_id)) {
        print STDERR "Milestone not found: $milestone\n";
        exit 1;
    }

    my $issues;
    my $changelog;
    my $page = 1;

    print STDERR "Looking for issues from Milestone $milestone\n";

    do {
        print STDERR "Page $page\n";
        $issues = issue_search($curl, $url, $owner, $repo, $milestone_id, $page);
        foreach my $issue (@{$issues}) {
            if (defined($issue)) {
                print STDERR "Issue #" . $issue->{number} . " - " . $issue->{title} . " ";

                if (!exists($issue->{pull_request})) {
                    print STDERR "not a PR.\n";
                    next;
                }

                if (!is_merged($curl, $url, $owner, $repo, $issue->{number})) {
                    print STDERR "not merged.\n";
                    next;
                }

                print STDERR "added.\n";
                push @{$changelog}, {number => $issue->{number}, title => $issue->{title}};
            }
        }
        $page++;
    } while (scalar @{$issues});

    if (defined($changelog)) {
        print "Changes with Apache Traffic Server $milestone\n";

        foreach my $issue (sort {$a->{number} <=> $b->{number}} @{$changelog}) {
            print "  #$issue->{number} - $issue->{title}\n";
        }
    }
}
