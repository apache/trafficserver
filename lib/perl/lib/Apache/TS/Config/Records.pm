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


############################################################################
# This is a simple module to let you read, modify and add to an Apache
# Traffic Server records.config file. The idea is that you would write a
# simple script (like example below) to update a "stock" records.config with
# the changes applicable to your application. This allows you to uprade to
# a newer default config file from a new release. See the embedded
# perldoc for more details.
############################################################################


package Apache::TS::Config::Records;

use Apache::TS::Config;

use warnings;
use strict;
require 5.006;

use Carp;

our $VERSION = "1.0";


#
# Constructor
#
sub new {
    my ($class, %args) = @_;
    my $self = {};
    my $fn = $args{file};

    $fn = $args{filename} unless defined($fn);
    $fn = "-" unless defined($fn);

    $self->{_filename} = $fn;  # Filename to open when loading and saving
    $self->{_configs} = [];            # Storage, and to to preserve order
    $self->{_lookup} = {};             # For faster lookup, indexes into the above
    $self->{_ix} = -1;                 # Empty
    bless $self, $class;

    $self->load() if $self->{_filename};

    return $self;
}


#
# Load a records.config file
#
sub load {
    my $self = shift;
    my %args = @_;
    my $fn = $args{file};

    $fn = $args{filename} unless defined($fn);
    $fn = $self->{_filename} unless defined($fn);

    open(FH, "<$fn") || die "Can't open file $fn for reading";
    while (<FH>) {
        chomp;
        my @p = split(/\s+/, $_, 4);

        push(@{$self->{_configs}}, [$_, \@p, TS_CONF_UNMODIFIED]);

        ++($self->{_ix});
        next unless ($#p == 3) && (($p[0] eq "LOCAL") || ($p[0] eq "CONFIG"));
        print "Warning! duplicate configuration $p[1]\n" if exists($self->{_lookup}->{$p[1]});

        $self->{_lookup}->{$p[1]} = $self->{_ix};
    }
}


#
# Get an existing configuration line, as an anon array.
#
sub get {
    my $self = shift;
    my %args = @_;
    my $c = $args{conf};

    $c = $args{config} unless defined($c);
    my $ix = $self->{_lookup}->{$c};

    return [] unless defined($ix);
    return $self->{_configs}->[$ix];
}


#
# Modify one configuration value
#
sub set {
    my $self = shift;
    my %args = @_;
    my $c = $args{conf};
    my $v = $args{val};

    $c = $args{config} unless defined($c);
    $v = $args{value} unless defined($v);

    my $ix = $self->{_lookup}->{$c};

    if (!defined($ix)) {
      my $type = $args{type};

      $type = "INT" unless defined($type);
      $self->append(line => "CONFIG $c $type $v");
    } else {
        my $val = $self->{_configs}->[$ix];

        @{$val->[1]}[3] = $v;
        $val->[2] = TS_CONF_MODIFIED;
    }
}


#
# Remove a configuration from the file.
#
sub remove {
    my $self = shift;
    my %args = @_;
    my $c = $args{conf};

    $c = $args{config} unless defined($c);

    my $ix = $self->{_lookup}->{$c};

    $self->{_configs}->[$ix]->[2] = TS_CONF_REMOVED if defined($ix);
}


#
# Append anything to the "end" of the configuration.
#
sub append {
    my $self = shift;
    my %args = @_;
    my $line = $args{line};

    my @p = split(/\s+/, $line, 4);

    # Don't appending duplicated configs
    if (($#p == 3) && exists($self->{_lookup}->{$p[1]})) {
        print "Warning: duplicate configuration $p[1]\n";
        return;
    }

    push(@{$self->{_configs}}, [$line, \@p, TS_CONF_UNMODIFIED]);
    ++($self->{_ix});
    $self->{_lookup}->{$p[1]} = $self->{_ix} if ($#p == 3) && (($p[0] eq "LOCAL") || ($p[0] eq "CONFIG"));
}


#
# Write the new configuration file to STDOUT, or provided
#
sub write {
    my $self = shift;
    my %args = @_;
    my $fn = $args{file};

    $fn = $args{filename} unless defined($fn);
    $fn = "-" unless defined($fn);

    if ($fn ne "-") {
        close(STDOUT);
        open(STDOUT, ">$fn") || die "Can't open $fn for writing";
    }

    foreach (@{$self->{_configs}}) {
        if ($_->[2] == TS_CONF_UNMODIFIED) {
            print $_->[0], "\n";
        } elsif ($_->[2] == TS_CONF_MODIFIED) {
            print join(" ", @{$_->[1]}), "\n";
        } else {
            # No-op if removed
        }
    }
}
1;

__END__

=head1 NAME

Apache::TS::Config::Records - Manage the Apache Traffic Server records.config file

=head1 SYNOPSIS

  #!/usr/bin/perl

  use Apache::TS::Config::Records;

  my $r = new Apache::TS::Config::Records(file => "/tmp/records.config");
  $r->set(conf => "proxy.config.log.extended_log_enabled",
          val => "123");
  $r->write(file => "/tmp/records.config.new");

=head1 DESCRIPTION

This module implements a convenient interface to read, modify and save
the records.config file as used by Apache Traffic Server.

Instantiating a new Config::Records class, with a file provided, will
automatically load that configuration. Don't call the load() method
explicitly in this case.

=head2 API Methods

The following are methods in the Records class.

=over 8

=item new

Instantiate a new object. The file name is optionally provided, and if
present that file is immediately loaded (see the load() method
below). Example:

  my $r = new Apache::TS::Config::Records(file => $fname);

=item load

Explicitly load a configuration file, merging the items with any
existing values. This is useful to for example merge multiple
configuration into one single structure

=item get

Get an existing configuration line. This is useful for
detecting that a config exists or not, for example. The
return value is an anonymous array like

  [<line string>, [value split into 4 fields, flag if changed]


You probably shouldn't modify this array.

=item set

Modify one configuration value, with the provided value. Both the conf
name and the value are required. Example:

  $r->set(conf => "proxy.config.exec_thread.autoconfig",
          val => "0");

conf is short for "config", val is short for "value", and all are
acceptable.

=item remove

Remove a specified configuration, the mandatory option is conf (or
"config"). Example:

  $r->remove(conf => "proxy.config.exec_thread.autoconfig");

=item append

Append a string to the "end" of the finished configuration file. We
will assure that no duplicated configurations are added. The input is a
single line, as per the normal records.config syntax. The purpose of
this is to add new sections to the configuration, with appropriate
comments etc. Example:

  $r->append(line => "");
  $r->append(line => "# My local stuff");
  $r->set(conf => "proxy.config.dns.dedicated_thread",
          val => "1");

=item write

Write the new configuration file to STDOUT, or a filename if
provided. Example:

  $r->write(file => "/etc/trafficserver/records.config");

=back

=head1 SEE ALSO

L<Apache::TS::Config>

=cut
