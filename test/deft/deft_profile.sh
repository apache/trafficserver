#!/bin/bash

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

# SOURCE THIS FILE INTO YOUR ENVIRONMENT

USER=`basename $HOME`
SRC_WORKAREA=${SRC_WORKAREA:-/export/workareas/$USER/traffic}
DEFT_PORTS="${DEFT_PORTS:--p 12000}"

# export DEFT_PM_DEBUG='.*'

DEFT_WORKAREA=/inktest/$USER-0
DEFT_INSTALL=$SRC_WORKAREA/deft-install
export PERL5LIB=$DEFT_INSTALL:$DEFT_INSTALL/scripts/perl_lib/

DEFT_HELP="

   DEFT Mode help:

   To run a program from the deft-install directory type the
   following:  

        ./run_test.sh <BUILD_DIR> [-m] [-v] -p <PORT> -s <SCRIPT> [-k val]

   The 'k' flag is used to lengthen the 'kill timeout' when running 
   instrumented binaries.  The 'v' flag starts up the event viewer,
   which is almost a must-have.
   
   To start the process manager separately (-m) do the 
   following steps:  First chdir to /inktest/<your directory>.  
   Then, run the process manager:  

        ./proc_manager* -p <PORT> -d . -T.\*

   If you do not want to use the GUI viewer [-v] then keep tail on
   the test log, it gets truncated with each new test; so you don't 
   need to re-tail the file.

        tail -f $DEFT_WORKAREA/log/test.log

   Examples:

   ./run_test.sh BUILD $DEFT_PORTS -s plugins/thread-1/thread_functional.pl -v
   ./run_test.sh BUILD $DEFT_PORTS -g SDK_full
   ./run_test.sh COVERAGE $DEFT_PORTS -v my_test.pl -k 300

   See scripts/acc/http/ldap/ldap-1.pl for a example syntest driven script.

   DEFT helper commands:

    cd_deft() changes to the deft install directory
    deft_check <script.pl>  will do a perl syntax check of a perl script.

      Example: cd_deft; cd scripts/plugins/null-transform; deft_check null_functional.pl

   debugging:
    deft_start_proc() starts the process manager with debugging tags turned on.
    deft_start_test() runs a test in debug mode against the manual process mgr.
"

deft_start_proc() {
  local here=`pwd`
  cd $DEFT_WORKAREA
  ./proc_manager* $DEFT_PORTS -d . -T.\*;
  cd $here
}

deft_start_test() {
   local build=$1;
   local deft_script=$2;
   local here=`pwd`
   cd $DEFT_INSTALL
   ./run_test.sh $build -m $DEFT_PORTS -s $deft_script -T.\*
   cd $here
}

cd_deft() {
   cd $DEFT_INSTALL
}

deft_check () {
	mkdir -p $DEFT_WORKAREA/tmp/
	local NF=$DEFT_WORKAREA/tmp/$1
	echo 'sub TestExec::boot_TestExec() {}' > $NF
    cat $1 >> $NF
	perl -cw $NF
}

echo "$DEFT_HELP";
