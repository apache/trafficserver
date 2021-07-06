#!/usr/bin/env perl

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

# This tool is a "replacement" for make, which wraps the make command around
# a filter that tries to normalize the output such that the filenames are relative
# to the build directory. This passes all arguments passed along to make itself,
# and you can override the "make" command using the MAKE environment variable.

use strict;
use Term::ANSIColor;

my %COLORS = (
    "_header"  => "italic green",
    "_line"    => "cyan",
    "_message" => "italic magenta",
    "error"    => "red",
    "warning"  => "yellow",
    "note"     => "blue"
);
my $CMD = $ENV{"MAKE"} || "make" . " 2>&1 " . join(" ", @ARGV);
my @DIRS = ();

print colored("Running: $CMD\n", $COLORS{"_header"});

open my $cmd, '-|', $CMD;
while (my $line = <$cmd>) {
    if ($line =~ /(error|warning|note):/) {
        my $msg   = $1;
        my @parts = split(/:/, $line);
        my $file  = $parts[0];

        if (substr($file, 0, 1) ne "/") {
            $file =~ s/^[\.\/]+//;    # Strip leading ./, ../, ../../, etc.

            # Lazy eval on this, assuming that we will not find errors typically...
            if (!@DIRS) {
                @DIRS = split(/\n/, `find . -type d | fgrep -v -e .deps -e .libs -e .git -e .vscode`);
            }

            foreach (@DIRS) {
                if (-f "$_/$file") {
                    $file = "$_/$file";
                    last;
                }
            }
        }
        print colored("$file:$parts[1]:$parts[2]:", $COLORS{"_line"});
        print colored("$msg",                       $COLORS{"$msg"});
        print colored("$parts[4]\n",                $COLORS{"_message"});
    } else {
        print $line;
    }
}
