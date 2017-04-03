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
