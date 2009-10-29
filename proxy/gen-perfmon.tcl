#!/usr/releng/bin/tclsh

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
#
# generate perfmon counter definitions (.INI) and header files.

# Note of convention:
#   ${counterbase}OBJ will be the offset of the documentation
#   string for the DLL.

set targetdir "perfdll"
set srcreldir "../${targetdir}"
set counterbase TrafficServer
set counterOffsetsInc ${counterbase}-off.h
set counterDatastrInc ${counterbase}-str.h
set counterDatastrInit ${counterbase}-str-def.c
# global variable in DLL that contains counter documentation for perfmon
set counterDataDefinition "${counterbase}DataDefinition"
set counterCounterBindings "${counterbase}CounterBindings"
#set counterDatastrLibDef lib${counterbase}.def

# data accessors -- since you can't just touch DLL data without
# properly initializing DLL, may as well use a procedure to make these
# changes.  Can also enforce bounds checking this way. 
set apptargetdir "mgrperfmon"
set counterDatastrAccessInc ${counterbase}-str-access.h
set counterDatastrAccess ${counterbase}-str-access.c
set counterDatastrAppLibDef lib${counterbase}.def

set countermodule_name {Traffic Server}
set countermodule_help {This is the Traffic Server perfmon counter module}
set moduledoc_offset ${counterbase}_module

set perfmontype(gauge) PERF_COUNTER_RAWCOUNT
set perfmontype(counter) PERF_COUNTER_COUNTER

# list of lists:
# item 0: counter structure id
# item 1: short name
# item 2: default scale
# item 3: longer description
# item 4: proxy variable
# item 5: counter type

#set counters {
#    {
#	cur_http_client_trans {Active HTTP client transactions} {1} {
#	    Currently active client transactions (HTTP)
#	} proxy.process.http.current_client_transactions raw
#    }
#    {
#	http_incoming_requests {Count of incoming HTTP client requests} {1} {
#	    Count of incoming HTTP client requests.
#	} proxy.process.http.incoming_requests counter
#    }
#}

# parse out mgmt2/uistats into counters structure
proc assertIfDup {s} {
    global dupinfo
    if [info exists dupinfo($s)] {
	error "$s should not be duplicated"
    } else {
	set dupinfo($s) 1
    }
}

proc warn {s} {
    puts "$s"
}

proc printattrs {v} {
    upvar $v var
    foreach i [array names var] {
	puts "$i : $var($i)"
    }
}

set counters {}
set f [open "mgmt2/uistats" r]
set state ""
while {[gets $f s] >= 0} {
    set again 1
    while {$again} {
	switch $state {
	    "in_var" {
		if {[string match "#*" $s] == 0} {
		    # out of var definition
		    if {[info exists varinfo(perfmonvar)]} {
			#foreach i [array names varinfo] {
			#    puts "$i: $varinfo($i)"
			#}
			#printattrs varinfo
			if ![info exists varinfo(doc)] {
			    warn "$varinfo(proxyvar) - no documentation"
			    set varinfo(doc) "$varinfo(proxyvar)"
			}
			if ![info exists varinfo(shortdoc)] {
			    warn "$varinfo(proxyvar) - no short documentation"
			    set varinfo(shortdoc) "$varinfo(doc)"
			}
			if {[string length $varinfo(shortdoc)]>35} {
			    set d [expr [string length $varinfo(shortdoc)]-35]
			    puts "$varinfo(proxyvar) - doc is $d characters too long"
			}
			lappend counters \
				[list $varinfo(perfmonvar) \
				$varinfo(shortdoc) \
				$varinfo(scale) \
				$varinfo(doc) \
				$varinfo(proxyvar) \
				$varinfo(usage) \
				]
		    } else {
			puts "$varinfo(proxyvar) -- need schema"
		    }
		    catch {unset varinfo}
		    set state ""
		    set again 1
		} else {
		    if {[regexp "perfmonvar: (.*)" $s foo perfmonvar]} {
			set varinfo(perfmonvar) $perfmonvar
			assertIfDup $perfmonvar
		    }
		    if {[regexp "doc: (.*)" $s foo doc]} {
			set varinfo(doc) $doc
		    }
		    if {[regexp "shortdoc: (.*)" $s foo shortdoc]} {
			set varinfo(shortdoc) $shortdoc
		    }
		    if {[regexp "usage: (.*)" $s foo usage]} {
			set varinfo(usage) $usage
		    }
		    if {[regexp "units: (.*)" $s foo units]} {
			set varinfo(units) $units
		    }
		    if {[regexp "scale: (.*)" $s foo scale]} {
			set varinfo(scale) $scale
		    }
		    set again 0
		}
	    }
	    default {
		if {[string match "#*" $s]} {
		    set again 0
		    continue
		}
		# this is a variable.
		#puts "$s"
		set var [lindex $s 0]
		set UIs [lindex $s 1]
		if {[regexp "P" $UIs]==0 && [string match $UIs ""] == 0 } {
		    # doesn't contain a P, and string is not empty
		    # not for us
		    set again 0
		    continue
		}
		if {[regexp "D" $UIs]} {
		    # stat is deprecated -- doesn't appear anywhere.
		    set again 0
		    continue
		}
		if {[regexp "I" $UIs]} {
		    # stat is internal use only -- doesn't appear in UI.
		    set again 0
		    continue
		}
		set varinfo(proxyvar) $var
		assertIfDup $var
		set state "in_var"
		set again 0
	    }
	}
    }
}
close $f

set next_docstring_offset 0
proc stdIncludeWrapBegin {fh fname} {
    set fname "_$fname"
    regsub -all {\-} $fname "_" fname
    regsub -all {\.} $fname "_" fname
    puts $fh "#ifndef $fname"
    puts $fh "#define $fname"
}

proc stdIncludeWrapEnd {fh} {
    puts $fh "#endif"
}

proc alloc_docstring {f name} {
    global next_docstring_offset
    set docstring($name) $next_docstring_offset
    puts $f "#define $name $next_docstring_offset"
    incr next_docstring_offset 2
}


##
## Generate perfmon counter documentation for use with 'lodctr' utility
##

set f [open "$targetdir/$counterbase.ini" w]
puts $f "\[info\]
drivername=$counterbase
symbolfile=$counterOffsetsInc

\[languages\]
009=English

\[text\]"

# 009 is "English"
set language 009

puts $f "${moduledoc_offset}_${language}_NAME=$countermodule_name"
puts $f "${moduledoc_offset}_${language}_HELP=$countermodule_help"

#puts "counters are $counters"
foreach cdsh $counters {
    set var [lindex $cdsh 0]
    set name [lindex $cdsh 1]
    set scale [lindex $cdsh 2]
    set help [lindex $cdsh 3]
    set name [string trim $name]
    set help [string trim $help]

    puts $f "${counterbase}_${var}_${language}_NAME=$name"
    puts $f "${counterbase}_${var}_${language}_HELP=$help"
}
close $f

set  f [open "$targetdir/$counterOffsetsInc" w]
puts $f "/* This is generated automatically"
puts $f " * \$Id\$"
puts $f " */"
alloc_docstring $f $moduledoc_offset
set min_counter $next_docstring_offset
foreach cdsh $counters {
    set var [lindex $cdsh 0]
    alloc_docstring $f "${counterbase}_${var}"
}
set max_counter [expr $next_docstring_offset-2]
close $f

#### END counter documentation

##
## Generate structures for perfmon DLL 
##
set f [open "$targetdir/$counterDatastrInc" w]
puts $f "/* This is generated automatically"
puts $f " * \$Id\$"
puts $f " */"
stdIncludeWrapBegin $f $counterDatastrInc
puts $f "#include <windows.h>"
puts $f "#include <winperf.h>"
puts $f "#pragma pack (8)"

puts $f "#define ${counterbase}_NUM_PERF_OBJECT_TYPES 1"

## structure definitions for counters

## This is perfmon counter documentation
puts $f "typedef struct _${counterbase}_DATA_DEFINITION {"
puts $f "    PERF_OBJECT_TYPE  ${counterbase}ObjectType;"
foreach cdsh $counters {
    set var [lindex $cdsh 0]
    puts $f "    PERF_COUNTER_DEFINITION  ${counterbase}${var}CounterDef;"
}
puts $f "} ${counterbase}_DATA_DEFINITION;"

## This is counter data (which is also used in the shared memory structures)
puts $f "typedef struct _${counterbase}_COUNTER_DATA {"
foreach cdsh $counters {
    set var [lindex $cdsh 0]
    puts $f "    DWORD  dw${counterbase}${var}CounterData;"
}
puts $f "} ${counterbase}_COUNTER_DATA, *P${counterbase}_COUNTER_DATA;"

## This is perfmon counter data
puts $f "typedef struct _${counterbase}_COUNTERS {"
puts $f "    PERF_COUNTER_BLOCK  CounterBlock;"
puts $f "    ${counterbase}_COUNTER_DATA counterdata;"
puts $f "} ${counterbase}_COUNTERS, *P${counterbase}_COUNTERS;"
puts $f "
typedef struct _MgmtVarCounterBindings {
    char *varName;
    int counterId;
} MgmtVarCounterBindings;
#ifdef __cplusplus
extern \"C\"  {
#endif
extern const MgmtVarCounterBindings $counterCounterBindings\[\];
#ifdef __cplusplus
}
#endif
"

puts $f "#pragma pack ()"

puts $f "/* some prototypes */"
puts $f "void Init${counterbase}DataDefinition("
puts $f "  DWORD dwFirstCounter,"
puts $f "  DWORD dwFirstHelp"
puts $f ");"
puts $f "void Clear${counterbase}Counters("
puts $f "  P${counterbase}_COUNTER_DATA pCounterData"
puts $f ");"
stdIncludeWrapEnd $f

close $f

set f [open "$targetdir/$counterDatastrInit" w]
puts $f "/* This is generated automatically"
puts $f " * \$Id\$"
puts $f " */"
puts $f "#include \"$counterOffsetsInc\""
puts $f "#include \"$counterDatastrInc\""
puts $f "// dummy local for size computation"
puts $f "static ${counterbase}_COUNTERS counterstr;"
puts $f "${counterbase}_DATA_DEFINITION $counterDataDefinition = {"
## documentation for object
puts $f "{
    sizeof(${counterbase}_DATA_DEFINITION) + sizeof(${counterbase}_COUNTERS),
    sizeof(${counterbase}_DATA_DEFINITION),
    sizeof(PERF_OBJECT_TYPE),
    $moduledoc_offset,
    0,
    $moduledoc_offset,
    0,
    PERF_DETAIL_NOVICE, // XXX: this is app counter specific
    (sizeof(${counterbase}_DATA_DEFINITION)-sizeof(PERF_OBJECT_TYPE))/
        sizeof(PERF_COUNTER_DEFINITION),
    0,  // XXX: app specific default counter
    0,  // 0 instances to start with
    0,  // unicode instance names
    {0,0},
    {0,0}
    },"
## documentation for counters
foreach cdsh $counters {
    set var [lindex $cdsh 0]
    set scale [lindex $cdsh 2]
    set type [lindex $cdsh 5]
    set perfscale [expr int(log10(100.0/$scale))]
    puts $f "{
    sizeof(PERF_COUNTER_DEFINITION),
    ${counterbase}_${var},
    0,
    ${counterbase}_${var},
    0,
    $perfscale, // XXX: this is app counter specific scale
    PERF_DETAIL_NOVICE, // XXX: this is app counter specific
    $perfmontype($type),
    sizeof(counterstr.counterdata.dw${counterbase}${var}CounterData),
    0,
    },
	"
}

puts $f "};"

puts $f "const MgmtVarCounterBindings $counterCounterBindings\[\] = {"
foreach cdsh $counters {
    set var [lindex $cdsh 0]
    set mgmtVar [lindex $cdsh 4]
    puts $f "{\"$mgmtVar\", ${counterbase}_${var} }, "
}
puts $f "{ NULL, 0 }"
puts $f "};"

puts $f "void Init${counterbase}DataDefinition("
puts $f "  DWORD dwFirstCounter,"
puts $f "  DWORD dwFirstHelp"
puts $f ")"
puts $f "{"
puts $f "   ${counterbase}_COUNTERS counterstr; // for sizing"

## Adjust module definition/help strings
puts $f "
    $counterDataDefinition.${counterbase}ObjectType.ObjectNameTitleIndex += dwFirstCounter;
    $counterDataDefinition.${counterbase}ObjectType.ObjectHelpTitleIndex += dwFirstHelp;
"
## adjust counter definition/help strings
foreach cdsh $counters {
    set var [lindex $cdsh 0]
    puts $f "
    $counterDataDefinition.${counterbase}${var}CounterDef.CounterNameTitleIndex += dwFirstCounter;
    $counterDataDefinition.${counterbase}${var}CounterDef.CounterHelpTitleIndex += dwFirstHelp;
    $counterDataDefinition.${counterbase}${var}CounterDef.CounterOffset +=
        (DWORD)((LPBYTE)&counterstr.counterdata.dw${counterbase}${var}CounterData - (LPBYTE)&counterstr);
    "
}

puts $f "}"

puts $f "void Clear${counterbase}Counters("
puts $f "  P${counterbase}_COUNTER_DATA pCounterData"
puts $f ")"
puts $f "{"
foreach cdsh $counters {
    set var [lindex $cdsh 0]
    puts $f "    pCounterData->dw${counterbase}${var}CounterData = 0;"
}
puts $f "}"
close $f

set f [open "$apptargetdir/$counterDatastrAccessInc" w]
puts $f "/* This is generated automatically"
puts $f " * \$Id\$"
puts $f " */"
stdIncludeWrapBegin $f $counterDatastrAccessInc
puts $f "
#define ${counterbase}_COUNTER_MIN $min_counter
#define ${counterbase}_COUNTER_MAX $max_counter

#include <windows.h>
#include \"$srcreldir/$counterOffsetsInc\"
#include \"dlldata.h\"

#ifdef MGRPERF_USE_DLL
#ifdef _EXPORTING
   #define DECLSPEC    __declspec(dllexport)
#else
   #define DECLSPEC    __declspec(dllimport)
#endif
#else
   #define DECLSPEC
#endif

/* some prototypes */
#ifdef __cplusplus
extern \"C\"  {
#endif
BOOL
DECLSPEC
__stdcall
Set${counterbase}Counter(
  DWORD counterId,
  DWORD value
);
#ifdef __cplusplus
}
#endif
"
stdIncludeWrapEnd $f
close $f

set f [open "$apptargetdir/$counterDatastrAccess" w]
puts $f "/* This is generated automatically"
puts $f " * \$Id\$"
puts $f " */"
puts $f "#include \"$counterDatastrAccessInc\""
puts $f "#include <assert.h>"

puts $f ""
puts $f "
BOOL
#ifdef MGRPERF_USE_DLL
__declspec(dllexport)
#endif
__stdcall
Set${counterbase}Counter(
  DWORD counterId,
  DWORD value
) {
    if (counterId < ${counterbase}_COUNTER_MIN || counterId > ${counterbase}_COUNTER_MAX || ((counterId % 2) == 1)) {
	return FALSE;
    }
    if (pAppData == NULL) {
	return FALSE;
    }
    switch (counterId) {"
foreach cdsh $counters {
    set var [lindex $cdsh 0]
    puts $f "
       case ${counterbase}_${var}:
           pAppData->counterdata.dw${counterbase}${var}CounterData = value;
           return TRUE;
    "
}

puts $f "
       default:
          assert(!  \"should not happen\");
          return FALSE;
    }
    assert(!  \"should not get here\");
    return FALSE;
}
"

close $f
