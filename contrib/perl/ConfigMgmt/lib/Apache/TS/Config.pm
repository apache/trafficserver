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
package Apache::TS::Config;

use warnings;
use strict;

require 5.006;

use Carp;
require Exporter;

our @ISA    = qw( Exporter );
our @EXPORT = qw(TS_CONF_UNMODIFIED TS_CONF_MODIFIED TS_CONF_REMOVED);

our $VERSION = "1.0";

# Constants
use constant {
    TS_CONF_UNMODIFIED     => 0,
    TS_CONF_MODIFIED       => 1,
    TS_CONF_REMOVED        => 2
};
