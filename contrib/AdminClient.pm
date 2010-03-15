############################################################################
#
# Simple Apache Traffic Server client object, to communicate with the local manager.
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
# Example usage:
#    #!/usr/bin/perl
#
#    use Apache::TS::AdminClient;
#
#    my $cli = $cli = new Apache::TS::AdminClient;
#    my $val = $cli->get_stat("proxy.process.http.avg_transactions_per_client_connection:");
#
package Apache::TS::AdminClient;

use warnings;
use strict;

use IO::Socket::UNIX;
use IO::Select;


#
# Constructor
#
sub new {
  my $proto = shift;
  my $class = ref($proto) || $proto;
  my $self = {};
  my %args = @_;

  $self->{_socket_path} = $args{socket_path} || "/usr/local/var/trafficserver/cli"; # TODO: fix on install
  $self->{_socket} = undef;
  $self->{_select} = IO::Select->new();
  bless $self, $class;

  $self->open_socket();
  return $self;
}


#
# Destructor
#
sub DESTROY {
  my $self = shift;

  $self->close_socket();
}


#
# Open the socket (Unix domain)
#
sub open_socket {
  my $self = shift;
  my %args = @_;

  if (defined($self->{_socket})) {
    if ($args{force} || $args{reopen}) {
      $self->close_socket();
    } else {
      return 0 
    }
  }

  $self->{_socket} = IO::Socket::UNIX->new(Type => SOCK_STREAM,
                                          Peer => $self->{_socket_path});
  return 0 unless defined($self->{_socket});
  $self->{_select}->add($self->{_socket});

  return 1;
}

sub close_socket {
  my $self = shift;

  return 0 unless defined($self->{_socket});
  $self->{_select}->remove($self->{_socket});
  $self->{_socket}->close();
  $self->{_socket} = undef;
}


#
# Get (read) a stat out of the local manager. Note that the assumption is
# that you are calling this with an existing stats "name".
#
sub get_stat {
  my $self = shift;
  my $stat = shift;
  my $res = "";
  my $max_read_attempts = 25;

  return undef unless defined($self->{_socket});

  return undef unless $self->{_select}->can_write(10);
  $self->{_socket}->print("b get $stat\0");
  
  while ($res eq "") {
    return undef if ($max_read_attempts-- < 0);
    return undef unless $self->{_select}->can_read(10);

    my $status = $self->{_socket}->sysread($res, 1024);
    return undef unless defined($status) || ($status == 0);

    $res =~ s/\0+$//;
    $res =~ s/^\0+//;
  }

  my @parts = split(/;/, $res);

  return undef unless (scalar(@parts) == 3);
  return $parts[2] if ($parts[0] eq "1");
  return undef;
}

1;
