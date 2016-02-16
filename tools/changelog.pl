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

my $fixversion = shift;
my $url = "https://issues.apache.org/jira";
my $jql = "project = TS AND status in (Resolved, Closed) AND fixVersion = $fixversion ORDER BY key ASC";

sub jira_search
{
  my $url = shift;
  my $jql = shift;
  my $index = shift;
  my $endpoint = "/rest/api/2/search";

  my $query = {
    jql => $jql,
    startAt => $index,
    fields => [
      "summary",
      "issuetype"
    ]
  };

  my $req_body = to_json($query);
  my $resp_body;
  my $curl = WWW::Curl::Easy->new;

  $curl->setopt(CURLOPT_POST, 1);
  $curl->setopt(CURLOPT_POSTFIELDS, $req_body);
  $curl->setopt(CURLOPT_HTTPHEADER, ['Content-Type: application/json']);
  open(my $fileb, ">", \$resp_body);
  $curl->setopt(CURLOPT_WRITEDATA, $fileb);
  $curl->setopt(CURLOPT_URL, $url . $endpoint);
  my $retcode = $curl->perform();
  if ($retcode == 0 && $curl->getinfo(CURLINFO_HTTP_CODE) == 200) {
    return from_json($resp_body);
  }

  undef;
}

my $count = 0;
my $changelog;
my $issues;

do
{
  $issues = jira_search($url, $jql, $count);

  if (!defined($issues))
  {
    exit 1;
  }

  foreach my $issue (@{ $issues->{issues} })
  {
    if (defined($issue))
    {
      push @{ $changelog->{$issue->{fields}{issuetype}{name}} }, {key => $issue->{key},  summary => $issue->{fields}{summary}};
      $count++;
    }
  }
}
while ($count < $issues->{total});

if (!defined($changelog))
{
  exit 1;
}

print "Changes with Apache Traffic Server $fixversion\n";

foreach my $key (sort keys %{ $changelog })
{
  print "\n$key:\n";
  foreach my $issue (@{ $changelog->{$key} })
  {
    chomp $issue->{summary};
    $issue->{summary} =~ s/\s+$//; # Trim trailing whitespace
    print "  *) [$issue->{key}] ";
    if (length($issue->{summary}) <= (131 - 15)) {
      print "$issue->{summary}\n";
    } else {
      print substr($issue->{summary}, 0, (131 - 18)), "...\n";
    }
  }
}
