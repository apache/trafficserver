#!/bin/sh

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

#
# File: INKMgmtAPICheckTcl.sh
#
# Purpose: This script determines whether a tcl interpreter is available and
#           functional on the current platform with the current environment.
#            
# Use: It is expected that this script will be called from Traffic_Manager
#       before attempting to use any tcl helper scripts and the results of
#       this call will be used in Traffic_Manager to determine whether to
#       proceed.
#
# Note: It is possible that tcl may be a wrapper which, when executed, comes
#        comes back with a message stating that the user should use tcl.foo or
#        tcl.bar.
#
# Note: This script was designed to produce nothing redirected from stdout.
#       As used in conjunction with the FTP Snapshot mechansism (for which it
#        was originally designed), the caller will pass in a buffer, and if
#        this script exits with 'zero', will re-used that buffer for output
#        from the actaul FTP.tcl script.  This is the reason why it is
#        imparative that stdout remain clean on success.
#       Also, the stdout message upon failure needs to be 'ERROR: {whatever}'
#        in order to be complient with the current design of the FTP SnapShot
#        mechanism...this is what triggers error output to be displayed correctly.
#
#####

    # Simple 'which' testing...
which_out=`which tclsh`
if test "x${which_out}" = "x"; then
    which_out='error for next test: tcl not found.'
fi

    # Number of words will always be '1' if it were found correctly.
    #  (and never '1' if not...hopefully :)
num_words=`echo ${which_out} | wc -w | awk '{print $1}'`


    # And if it were found, we still want to make sure that we can
    #  use it via a minimal test, at least.
if test "x${num_words}" = "x1"; then
    test_puts=`echo "puts test_string" | ${which_out}`
    if test "x${test_puts}" != "xtest_string"; then
       echo "ERROR: INKMgmtAPICheckTcl.sh failed to _use_ tclsh"
       exit 1
    fi
else
    echo "ERROR: INKMgmtAPICheckTcl.sh failed to _find_ tclsh"
    exit 1
fi

exit 0


    # Alternate test which doesn't control stdout very well.

#test_program='tcl'
#
#test_puts=`echo "puts test_string" | ${test_program}`
#if test "x${test_puts}" != "xtest_string"; then
#    which_out='ERROR: tcl not found by INKMgmtAPICheckTcl.sh.'
#    exit 1
#fi
#
#exit 0
