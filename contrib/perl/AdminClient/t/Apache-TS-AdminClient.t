# Before `make install' is performed this script should be runnable with
# `make test'. After `make install' it should work as `perl Apache-TS-AdminClient.t'

#########################

# change 'tests => 1' to 'tests => last_test_to_print';


use Test::More tests => 2;
BEGIN { use_ok('Apache::TS::AdminClient') };

#########################

# Insert your test code below, the Test::More module is use()ed here so read
# its man page ( perldoc Test::More ) for help writing this test script.

#----- is this right or do we need to use Test::MockObject as well? 
our @methods = qw(new DESTROY open_socket close_socket get_stat);
can_ok('Apache::TS::AdminClient', @methods);
