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

my $owner = shift;
my $repo = shift;
my $milestone = shift;
my $url = "https://api.github.com";

sub milestone_lookup
{
  my $url = shift;
  my $owner = shift;
  my $repo = shift;
  my $milestone_title = shift;
  my $endpoint = "/repos/$owner/$repo/milestones";

  my $params = "state=all";

  my $resp_body;
  my $curl = WWW::Curl::Easy->new;

  #$curl->setopt(CURLOPT_VERBOSE, 1);
  $curl->setopt(CURLOPT_HTTPHEADER, ['Accept: application/vnd.github.v3+json', 'User-Agent: Awesome-Octocat-App']);
  $curl->setopt(CURLOPT_WRITEDATA, \$resp_body);
  $curl->setopt(CURLOPT_URL, $url . $endpoint . '?' . $params);

  my $retcode = $curl->perform();
  if ($retcode == 0 && $curl->getinfo(CURLINFO_HTTP_CODE) == 200)
  {
    my $milestones = from_json($resp_body);
    foreach my $milestone (@{ $milestones })
    {
      if ($milestone->{title} eq $milestone_title)
      {
        return $milestone->{number};
      }
    }
  }

  return undef;
}

sub issue_search
{
  my $url = shift;
  my $owner = shift;
  my $repo = shift;
  my $milestone_id = shift;
  my $page = shift;
  my $endpoint = "/repos/$owner/$repo/issues";

  my $params = "milestone=$milestone_id&state=closed&page=$page";

  my $resp_body;
  my $curl = WWW::Curl::Easy->new;

  #$curl->setopt(CURLOPT_VERBOSE, 1);
  $curl->setopt(CURLOPT_HTTPHEADER, ['Accept: application/vnd.github.v3+json', 'User-Agent: Awesome-Octocat-App']);
  $curl->setopt(CURLOPT_WRITEDATA, \$resp_body);
  $curl->setopt(CURLOPT_URL, $url . $endpoint . '?' . $params);

  my $retcode = $curl->perform();
  if ($retcode == 0 && $curl->getinfo(CURLINFO_HTTP_CODE) == 200) {
    return from_json($resp_body);
  }

  undef;
}

my $milestone_id = milestone_lookup($url, $owner, $repo, $milestone);

if (!defined($milestone_id))
{
  exit 1;
}

my $issues;
my $changelog;
my $page = 1;

do {
  $issues = issue_search($url, $owner, $repo, $milestone_id, $page);
  foreach my $issue (@{ $issues })
  {
    if (defined($issue))
    {
      push @{ $changelog }, {number => $issue->{number},  title => $issue->{title}};
    }
  }
  $page++;
} while (scalar @{ $issues });

if (defined($changelog))
{
  print "Changes with Apache Traffic Server $milestone\n";

  foreach my $issue (sort {$a->{number} <=> $b->{number}} @{ $changelog })
  {
    print "  #$issue->{number} - $issue->{title}\n";
  }
}
