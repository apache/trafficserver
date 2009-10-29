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

# The statements below will be executed by tcl \
exec tclsh "$0" "$@"
# ftp.tcl --
#
#	FTP library package for Tcl 8.2+.  Originally written by Steffen
#	Traeger (Steffen.Traeger@t-online.de); modified by Peter MacDonald
#	(peter@pdqi.com) to support multiple simultaneous FTP sessions;
#	Modified by Steve Ball (Steve.Ball@zveno.com) to support
#	asynchronous operation.
#
# Copyright (c) 1996-1999 by Steffen Traeger <Steffen.Traeger@t-online.de>
# Copyright (c) 2000 by Ajuba Solutions
# Copyright (c) 2000 by Zveno Pty Ltd
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.
# 
# RCS: @(#) 
#
#   core ftp support: 	ftp::Open <server> <user> <passwd> <?options?>
#			ftp::Close <s>
#			ftp::Cd <s> <directory>
#			ftp::Pwd <s>
#			ftp::Type <s> <?ascii|binary|tenex?>	
#			ftp::List <s> <?directory?>
#			ftp::NList <s> <?directory?>
#			ftp::FileSize <s> <file>
#			ftp::ModTime <s> <from> <to>
#			ftp::Delete <s> <file>
#			ftp::Rename <s> <from> <to>
#			ftp::Put <s> <(local | -data "data"> <?remote?>
#			ftp::Append <s> <(local | -data "data"> <?remote?>
#			ftp::Get <s> <remote> <?(local | -variable varname)?>
#			ftp::Reget <s> <remote> <?local?>
#			ftp::Newer <s> <remote> <?local?>
#			ftp::MkDir <s> <directory>
#			ftp::RmDir <s> <directory>
#			ftp::Quote <s> <arg1> <arg2> ...
#

package provide ftp [lindex {Revision: 2.2 } 1]

namespace eval ftp {

namespace export DisplayMsg Open Close Cd Pwd Type List NList FileSize ModTime\
		 Delete Rename Put Append Get Reget Newer Quote MkDir RmDir 
	
set serial 0
set VERBOSE 0
set DEBUG 0
}

proc bgerror { mess } {
    global errorInfo;
    puts "ERROR: $mess"
    exit -1;
}


#############################################################################
#
# DisplayMsg --
#
# This is a simple procedure to display any messages on screen.
# Can be intercepted by the -output option to Open
#
#	namespace ftp {
#		proc DisplayMsg {msg} {
#			......
#		}
#	}
#
# Arguments:
# msg - 		message string
# state -		different states {normal, data, control, error}
#
proc ftp::DisplayMsg {s msg {state ""}} {

    upvar ::ftp::ftp$s ftp
    variable VERBOSE 
    
    if { ([info exists ftp(Output)]) && ($ftp(Output) != "") } {
        eval [concat $ftp(Output) {$s $msg $state}]
        return
    }
        
    switch -exact -- $state {
        data {
            if { $VERBOSE } {
                puts $msg
            }
        }
        control	{
            if { $VERBOSE } {
                puts $msg
            }
        }
        error {
            error "ERROR: $msg"
        }
        default	{
            if { $VERBOSE } {
                puts $msg
            }
        }
    }
    return
}

#############################################################################
#
# Timeout --
#
# Handle timeouts
# 
# Arguments:
#  -
#
proc ftp::Timeout {s} {
    upvar ::ftp::ftp$s ftp

    after cancel $ftp(Wait)
    set ftp(state.control) 1

    DisplayMsg "" "Timeout of control connection after $ftp(Timeout) sec.!" error
    Command $ftp(Command) timeout
    return
}

#############################################################################
#
# WaitOrTimeout --
#
# Blocks the running procedure and waits for a variable of the transaction 
# to complete. It continues processing procedure until a procedure or 
# StateHandler sets the value of variable "finished". 
# If a connection hangs the variable is setting instead of by this procedure after 
# specified seconds in $ftp(Timeout).
#  
# 
# Arguments:
#  -		
#

proc ftp::WaitOrTimeout {s} {
    upvar ::ftp::ftp$s ftp

    set retvar 1

    if { ![string length $ftp(Command)] && [info exists ftp(state.control)] } {

        set ftp(Wait) [after [expr {$ftp(Timeout) * 1000}] [list [namespace current]::Timeout $s]]

        vwait ::ftp::ftp${s}(state.control)
        set retvar $ftp(state.control)
    }

    if {$ftp(Error) != ""} {
        set errmsg $ftp(Error)
        set ftp(Error) ""
        #DisplayMsg $s $errmsg error
        puts "ERROR: $errmsg"
    }

    return $retvar
}

#############################################################################
#
# WaitComplete --
#
# Transaction completed.
# Cancel execution of the delayed command declared in procedure WaitOrTimeout.
# 
# Arguments:
# value -	result of the transaction
#			0 ... Error
#			1 ... OK
#

proc ftp::WaitComplete {s value} {
    upvar ::ftp::ftp$s ftp

    if {![info exists ftp(Command)]} {
	set ftp(state.control) $value
	return $value
    }
    if { ![string length $ftp(Command)] && [info exists ftp(state.data)] } {
        vwait ::ftp::ftp${s}(state.data)
    }

    catch {after cancel $ftp(Wait)}
    set ftp(state.control) $value
    return $ftp(state.control)
}

#############################################################################
#
# PutsCtrlSocket --
#
# Puts then specified command to control socket,
# if DEBUG is set than it logs via DisplayMsg
# 
# Arguments:
# command - 		ftp command
#

proc ftp::PutsCtrlSock {s {command ""}} {
    upvar ::ftp::ftp$s ftp
    variable DEBUG
	
    if { $DEBUG } {
        DisplayMsg $s "---> $command"
    }

    puts $ftp(CtrlSock) $command
    flush $ftp(CtrlSock)
    return
}

#############################################################################
#
# StateHandler --
#
# Implements a finite state handler and a fileevent handler
# for the control channel
# 
# Arguments:
# sock - 		socket name
#			If called from a procedure than this argument is empty.
# 			If called from a fileevent than this argument contains
#			the socket channel identifier.

proc ftp::StateHandler {s {sock ""}} {
    upvar ::ftp::ftp$s ftp
    variable DEBUG 
    variable VERBOSE

    # disable fileevent on control socket, enable it at the and of the state machine
    # fileevent $ftp(CtrlSock) readable {}
		
    # there is no socket (and no channel to get) if called from a procedure

    set rc "   "
    set msgtext {}

    if { $sock != "" } {

        set number [gets $sock bufline]

        if { $number > 0 } {

            # get return code, check for multi-line text
            
            regexp "(^\[0-9\]+)( |-)?(.*)$" $bufline all rc multi_line msgtext
            set buffer $bufline
			
            # multi-line format detected ("-"), get all the lines
            # until the real return code

            while { [string compare $multi_line "-"] == 0 } {
                set number [gets $sock bufline]	
                if { $number > 0 } {
                    append buffer \n "$bufline"
                    regexp "(^\[0-9\]+)( |-)?(.*)$" $bufline all rc multi_line
                }
            }
        } elseif { [eof $ftp(CtrlSock)] } {
            # remote server has closed control connection
            # kill control socket, unset State to disable all following command
            
            set rc 421
            if { $VERBOSE } {
                DisplayMsg $s "C: 421 Service not available, closing control connection." control
            }
            set ftp(Error) "Service not available!"
            CloseDataConn $s
            WaitComplete $s 0
	    Command $ftp(Command) terminated
            catch {unset ftp(State)}
            catch {close $ftp(CtrlSock); unset ftp(CtrlSock)}
            return
        }
	
    } 
	
    if { $DEBUG } {
        DisplayMsg $s "-> rc=\"$rc\"\n-> msgtext=\"$msgtext\"\n-> state=\"$ftp(State)\""
    }

    # In asynchronous mode, should we move on to the next state?
    set nextState 0
	
    # system status replay
    if { [string compare $rc "211"] ==0 } {
        return
    }

    # use only the first digit 
    regexp "^\[0-9\]?" $rc rc
	
    switch -exact -- $ftp(State) {
        user { 
            switch -exact -- $rc {
                2 {
                    PutsCtrlSock $s "USER $ftp(User)"
                    set ftp(State) passwd
		    Command $ftp(Command) user
                }
                default {
                    set errmsg "Error connecting! $msgtext"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        passwd {
            switch -exact -- $rc {
                2 {
                    set complete_with 1
		    Command $ftp(Command) password
                }
                3 {
                    PutsCtrlSock $s "PASS $ftp(Passwd)"
                    set ftp(State) connect
		    Command $ftp(Command) password
                }
                default {
                    set errmsg "Error connecting! $msgtext"
                    set complete_with 0
		    Command $ftp(Command) error $msgtext
                }
            }
        }
        connect {
            switch -exact -- $rc {
                2 {
		    # The type is set after this, and we want to report
		    # that the connection is complete once the type is done
		    set nextState 1
		    if {[info exists ftp(NextState)] && ![llength $ftp(NextState)]} {
			Command $ftp(Command) connect $s
		    } else {
			set complete_with 1
		    }
                }
                default {
                    set errmsg "Error connecting! $msgtext"
                    set complete_with 0
		    Command $ftp(Command) error $msgtext
                }
            }
        }   
	connect_last {
	    Command $ftp(Command) connect $s
	    set complete_with 1
	}
        quit {
            PutsCtrlSock $s "QUIT"
            set ftp(State) quit_sent
        }
        quit_sent {
            switch -exact -- $rc {
                2 {
                    set complete_with 1
		    set nextState 1
		    Command $ftp(Command) quit
                }
                default {
                    set errmsg "Error disconnecting! $msgtext"
                    set complete_with 0
		    Command $ftp(Command) error $msgtext
                }
            }
        }
        quote {
            PutsCtrlSock $s $ftp(Cmd)
            set ftp(State) quote_sent
        }
        quote_sent {
            set complete_with 1
            set ftp(Quote) $buffer
	    set nextState 1
	    Command $ftp(Command) quote $buffer
        }
        type {
            if { [string compare $ftp(Type) "ascii"] ==0 } {
                PutsCtrlSock $s "TYPE A"
            } elseif { [string compare $ftp(Type) "binary"] ==0 } {
                PutsCtrlSock $s "TYPE I"
            } else {
                PutsCtrlSock $s "TYPE L"
            }
            set ftp(State) type_sent
        }
        type_sent {
            switch -exact -- $rc {
                2 {
                    set complete_with 1
		    set nextState 1
		    Command $ftp(Command) type $ftp(Type)
                }
                default {
                    set errmsg "Error setting type \"$ftp(Type)\"!"
                    set complete_with 0
		    Command $ftp(Command) error "error setting type \"$ftp(Type)\""
                }
            }
        }
	type_change {
	    set ftp(Type) $ftp(type:changeto)
	    set ftp(State) type
	    StateHandler $s
	}
        nlist_active {
            if { [OpenActiveConn $s] } {
                PutsCtrlSock $s "PORT $ftp(LocalAddr),$ftp(DataPort)"
                set ftp(State) nlist_open
            } else {
                set errmsg "Error setting port!"
            }
        }
        nlist_passive {
            PutsCtrlSock $s "PASV"
            set ftp(State) nlist_open
        }
        nlist_open {
            switch -exact -- $rc {
                1 {}
		2 {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        if { ![OpenPassiveConn $s $buffer] } {
                            set errmsg "Error setting PASSIVE mode!"
                            set complete_with 0
			    Command $ftp(Command) error "error setting passive mode"
                        }
                    }   
                    PutsCtrlSock $s "NLST$ftp(Dir)"
                    set ftp(State) list_sent
                }
                default {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        set errmsg "Error setting PASSIVE mode!"
                    } else {
                        set errmsg "Error setting port!"
                    }  
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        list_active {
            if { [OpenActiveConn $s] } {
                PutsCtrlSock $s "PORT $ftp(LocalAddr),$ftp(DataPort)"
                set ftp(State) list_open
            } else {
                set errmsg "Error setting port!"
		Command $ftp(Command) error $errmsg
            }
        }
        list_passive {
            PutsCtrlSock $s "PASV"
            set ftp(State) list_open
        }
        list_open {
            switch -exact -- $rc {
                1 {}
		2 {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        if { ![OpenPassiveConn $s $buffer] } {
                            set errmsg "Error setting PASSIVE mode!"
                            set complete_with 0
			    Command $ftp(Command) error $errmsg
                        }
                    }   
                    PutsCtrlSock $s "LIST$ftp(Dir)"
                    set ftp(State) list_sent
                }
                default {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        set errmsg "Error setting PASSIVE mode!"
                    } else {
                        set errmsg "Error setting port!"
                    }  
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        list_sent {
            switch -exact -- $rc {
                1 -
		2 {
                    set ftp(State) list_close
                }
                default {  
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        unset ftp(state.data)
                    }    
                    set errmsg "Error getting directory listing!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        list_close {
            switch -exact -- $rc {
                1 {}
		2 {
		    set nextState 1
		    if {[info exists ftp(NextState)] && ![llength $ftp(NextState)]} {
			Command $ftp(Command) list [ListPostProcess $ftp(List)]
		    } else {
			set complete_with 1
		    }
                }
                default {
                    set errmsg "Error receiving list!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
	list_last {
	    Command $ftp(Command) list [ListPostProcess $ftp(List)]
	    set complete_with 1
	}
        size {
            PutsCtrlSock $s "SIZE $ftp(File)"
            set ftp(State) size_sent
        }
        size_sent {
            switch -exact -- $rc {
                2 {
                    regexp "^\[0-9\]+ (.*)$" $buffer all ftp(FileSize)
                    set complete_with 1
		    set nextState 1
		    Command $ftp(Command) size $ftp(File) $ftp(FileSize)
                }
                default {
                    set errmsg "Error getting file size!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        } 
        modtime {
            PutsCtrlSock $s "MDTM $ftp(File)"
            set ftp(State) modtime_sent
        }  
        modtime_sent {
            switch -exact -- $rc {
                2 {
                    regexp "^\[0-9\]+ (.*)$" $buffer all ftp(DateTime)
                    set complete_with 1
		    set nextState 1
		    Command $ftp(Command) modtime $ftp(File) [ModTimePostProcess $ftp(DateTime)]
                }
                default {
                    set errmsg "Error getting modification time!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        } 
        pwd {
            PutsCtrlSock $s "PWD"
            set ftp(State) pwd_sent
        }
        pwd_sent {
            switch -exact -- $rc {
                2 {
                    regexp "^.*\"(.*)\"" $buffer temp ftp(Dir)
                    set complete_with 1
		    set nextState 1
		    Command $ftp(Command) pwd $ftp(Dir)
                }
                default {
                    set errmsg "Error getting working dir!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        cd {
            PutsCtrlSock $s "CWD$ftp(Dir)"
            set ftp(State) cd_sent
        }
        cd_sent {
            switch -exact -- $rc {
                1 {}
		2 {
                    set complete_with 1
		    set nextState 1
		    Command $ftp(Command) cd $ftp(Dir)
                }
                default {
                    #set errmsg "Error changing directory to \"$ftp(Dir)\""
                    set errmsg ""
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        mkdir {
            PutsCtrlSock $s "MKD $ftp(Dir)"
            set ftp(State) mkdir_sent
        }
        mkdir_sent {
            switch -exact -- $rc {
                2 {
                    set complete_with 1
		    set nextState 1
		    Command $ftp(Command) mkdir $ftp(Dir)
                }
                default {
                    set errmsg "Error making dir \"$ftp(Dir)\"!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        rmdir {
            PutsCtrlSock $s "RMD $ftp(Dir)"
            set ftp(State) rmdir_sent
        }
        rmdir_sent {
            switch -exact -- $rc {
                2 {
                    set complete_with 1
		    set nextState 1
		    Command $ftp(Command) rmdir $ftp(Dir)
                }
                default {
                    set errmsg "Error removing directory!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        delete {
            PutsCtrlSock $s "DELE $ftp(File)"
            set ftp(State) delete_sent
        }
        delete_sent {
            switch -exact -- $rc {
                2 {
                    set complete_with 1
		    set nextState 1
		    Command $ftp(Command) delete $ftp(File)
                }
                default {
                    set errmsg "Error deleting file \"$ftp(File)\"!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        rename {
            PutsCtrlSock $s "RNFR $ftp(RenameFrom)"
            set ftp(State) rename_to
        }
        rename_to {
            switch -exact -- $rc {
                3 {
                    PutsCtrlSock $s "RNTO $ftp(RenameTo)"
                    set ftp(State) rename_sent
                }
                default {
                    set errmsg "Error renaming file \"$ftp(RenameFrom)\"!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        rename_sent {
            switch -exact -- $rc {
                2 {
                    set complete_with 1
		    set nextState 1
		    Command $ftp(Command) rename $ftp(RenameFrom) $ftp(RenameTo)
                }
                default {
                    set errmsg "Error renaming file \"$ftp(RenameFrom)\"!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        put_active {
            if { [OpenActiveConn $s] } {
                PutsCtrlSock $s "PORT $ftp(LocalAddr),$ftp(DataPort)"
                set ftp(State) put_open
            } else {
                set errmsg "Error setting port!"
		Command $ftp(Command) error $errmsg
            }
        }
        put_passive {
            PutsCtrlSock $s "PASV"
            set ftp(State) put_open
        }
        put_open {
            switch -exact -- $rc {
                1 -
		2 {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        if { ![OpenPassiveConn $s $buffer] } {
                            set errmsg "Error setting PASSIVE mode!"
                            set complete_with 0
			    Command $ftp(Command) error $errmsg
                        }
                    } 
                    PutsCtrlSock $s "STOR $ftp(RemoteFilename)"
                    set ftp(State) put_sent
                }
                default {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        set errmsg "Error setting PASSIVE mode!"
                    } else {
                        set errmsg "Error setting port!"
                    }  
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        put_sent {
            switch -exact -- $rc {
                1 -
		2 {
                    set ftp(State) put_close
                }
                default {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        # close already opened DataConnection
                        unset ftp(state.data)
                    }  
                    set errmsg "Error opening connection!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        put_close {
            switch -exact -- $rc {
		1 {
		    # Keep going
		    return {}
		}
                2 {
                    set complete_with 1
		    set nextState 1
		    Command $ftp(Command) put $ftp(RemoteFilename)
                }
                default {
		    DisplayMsg $s "rc = $rc msgtext = \"$msgtext\""
                    set errmsg "Error storing file \"$ftp(RemoteFilename)\" due to \"$msgtext\""
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        append_active {
            if { [OpenActiveConn $s] } {
                PutsCtrlSock $s "PORT $ftp(LocalAddr),$ftp(DataPort)"
                set ftp(State) append_open
            } else {
                set errmsg "Error setting port!"
		Command $ftp(Command) error $errmsg
            }
        }
        append_passive {
            PutsCtrlSock $s "PASV"
            set ftp(State) append_open
        }
        append_open {
            switch -exact -- $rc {
		1 -
                2 {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        if { ![OpenPassiveConn $s $buffer] } {
                            set errmsg "Error setting PASSIVE mode!"
                            set complete_with 0
			    Command $ftp(Command) error $errmsg
                        }
                    }   
                    PutsCtrlSock $s "APPE $ftp(RemoteFilename)"
                    set ftp(State) append_sent
                }
                default {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        set errmsg "Error setting PASSIVE mode!"
                    } else {
                        set errmsg "Error setting port!"
                    }  
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        append_sent {
            switch -exact -- $rc {
                1 {
                    set ftp(State) append_close
                }
                default {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        # close already opened DataConnection
                        unset ftp(state.data)
                    }  
                    set errmsg "Error opening connection!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        append_close {
            switch -exact -- $rc {
                2 {
                    set complete_with 1
		    set nextState 1
		    Command $ftp(Command) append $ftp(RemoteFilename)
                }
                default {
                    set errmsg "Error storing file \"$ftp(RemoteFilename)\"!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        reget_active {
            if { [OpenActiveConn $s] } {
                PutsCtrlSock $s "PORT $ftp(LocalAddr),$ftp(DataPort)"
                set ftp(State) reget_restart
            } else {
                set errmsg "Error setting port!"
		Command $ftp(Command) error $errmsg
            }
        }
        reget_passive {
            PutsCtrlSock $s "PASV"
            set ftp(State) reget_restart
        }
        reget_restart {
            switch -exact -- $rc {
                2 { 
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        if { ![OpenPassiveConn $s $buffer] } {
                            set errmsg "Error setting PASSIVE mode!"
                            set complete_with 0
			    Command $ftp(Command) error $errmsg
                        }
                    }   
                    if { $ftp(FileSize) != 0 } {
                        PutsCtrlSock $s "REST $ftp(FileSize)"
                        set ftp(State) reget_open
                    } else {
                        PutsCtrlSock $s "RETR $ftp(RemoteFilename)"
                        set ftp(State) reget_sent
                    } 
                }
                default {
                    set errmsg "Error restarting filetransfer of \"$ftp(RemoteFilename)\"!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        reget_open {
            switch -exact -- $rc {
                2 -
                3 {
                    PutsCtrlSock $s "RETR $ftp(RemoteFilename)"
                    set ftp(State) reget_sent
                }
                default {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        set errmsg "Error setting PASSIVE mode!"
                    } else {
                        set errmsg "Error setting port!"
                    }  
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        reget_sent {
            switch -exact -- $rc {
                1 {
                    set ftp(State) reget_close
                }
                default {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        # close already opened DataConnection
                        unset ftp(state.data)
                    }  
                    set errmsg "Error retrieving file \"$ftp(RemoteFilename)\"!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        reget_close {
            switch -exact -- $rc {
                2 {
                    set complete_with 1
		    set nextState 1
		    Command $ftp(Command) get $ftp(RemoteFilename)
                }
                default {
                    set errmsg "Error retrieving file \"$ftp(RemoteFilename)\"!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        get_active {
            if { [OpenActiveConn $s] } {
                PutsCtrlSock $s "PORT $ftp(LocalAddr),$ftp(DataPort)"
                set ftp(State) get_open
            } else {
                set errmsg "Error setting port!"
		Command $ftp(Command) error $errmsg
            }
        } 
        get_passive {
            PutsCtrlSock $s "PASV"
            set ftp(State) get_open
        }
        get_open {
            switch -exact -- $rc {
                1 -
		2 -
                3 {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        if { ![OpenPassiveConn $s $buffer] } {
                            set errmsg "Error setting PASSIVE mode!"
                            set complete_with 0
			    Command $ftp(Command) error $errmsg
                        }
                    }   
                    PutsCtrlSock $s "RETR $ftp(RemoteFilename)"
                    set ftp(State) get_sent
                }
                default {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        set errmsg "Error setting PASSIVE mode!"
                    } else {
                        set errmsg "Error setting port!"
                    }  
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        get_sent {
            switch -exact -- $rc {
                1 {
                    set ftp(State) get_close
                }
                default {
                    if { [string compare $ftp(Mode) "passive"] ==0 } {
                        # close already opened DataConnection
                        unset ftp(state.data)
                    }  
                    set errmsg "Error retrieving file \"$ftp(RemoteFilename)\"!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
        get_close {
            switch -exact -- $rc {
                2 {
                    set complete_with 1
		    set nextState 1
		    if {$ftp(inline)} {
			upvar #0 $ftp(get:varname) returnData
			set returnData $ftp(GetData)
			Command $ftp(Command) get $ftp(GetData)
		    } else {
			Command $ftp(Command) get $ftp(RemoteFilename)
		    }
                }
                default {
                    set errmsg "Error retrieving file \"$ftp(RemoteFilename)\"!"
                    set complete_with 0
		    Command $ftp(Command) error $errmsg
                }
            }
        }
    }

    # finish waiting 
    if { [info exists complete_with] } {
        WaitComplete $s $complete_with
    }

    # display control channel message
    if { [info exists buffer] } {
        if { $VERBOSE } {
            foreach line [split $buffer \n] {
                DisplayMsg $s "C: $line" control
            }
        }
    }
	
    # Rather than throwing an error in the event loop, set the ftp(Error)
    # variable to hold the message so that it can later be thrown after the
    # the StateHandler has completed.

    if { [info exists errmsg] } {
        set ftp(Error) $errmsg
    }

    # If operating asynchronously, commence next state
    if {$nextState && [info exists ftp(NextState)] && [llength $ftp(NextState)]} {
	# Pop the head of the NextState queue
	set ftp(State) [lindex $ftp(NextState) 0]
	set ftp(NextState) [lreplace $ftp(NextState) 0 0]
	StateHandler $s
    }

    # enable fileevent on control socket again
    #fileevent $ftp(CtrlSock) readable [list ::ftp::StateHandler $ftp(CtrlSock)]

}

#############################################################################
#
# Type --
#
# REPRESENTATION TYPE - Sets the file transfer type to ascii or binary.
# (exported)
#
# Arguments:
# type - 		specifies the representation type (ascii|binary)
# 
# Returns:
# type	-		returns the current type or {} if an error occurs

proc ftp::Type {s {type ""}} {
    upvar ::ftp::ftp$s ftp

    #if { ![info exists ftp(State)] } {
        #if { ![string is digit -strict $s] } {
            #DisplayMsg $s "Bad connection name \"$s\"" error
        #} else {
            #DisplayMsg $s "Not connected!" error
        #}
        #return {}
    #}

    # return current type
    if { $type == "" } {
        return $ftp(Type)
    }

    # save current type
    set old_type $ftp(Type) 
	
    set ftp(Type) $type
    set ftp(State) type
    StateHandler $s
	
    # wait for synchronization
    set rc [WaitOrTimeout $s]
    if { $rc } {
        return $ftp(Type)
    } else {
        # restore old type
        set ftp(Type) $old_type
        return {}
    }
}

#############################################################################
#
# NList --
#
# NAME LIST - This command causes a directory listing to be sent from
# server to user site.
# (exported)
# 
# Arguments:
# dir - 		The $dir should specify a directory or other system 
#			specific file group descriptor; a null argument 
#			implies the current directory. 
#
# Arguments:
# dir - 		directory to list 
# 
# Returns:
# sorted list of files or {} if listing fails

proc ftp::NList {s { dir ""}} {
    upvar ::ftp::ftp$s ftp

    #if { ![info exists ftp(State)] } {
        #if { ![string is digit -strict $s] } {
            #DisplayMsg $s "Bad connection name \"$s\"" error
        #} else {
            #DisplayMsg $s "Not connected!" error
        #}
        #return {}
    #}

    set ftp(List) {}
    if { $dir == "" } {
        set ftp(Dir) ""
    } else {
        set ftp(Dir) " $dir"
    }

    # save current type and force ascii mode
    set old_type $ftp(Type)
    if { $ftp(Type) != "ascii" } {
	if {[string length $ftp(Command)]} {
	    set ftp(NextState) [list nlist_$ftp(Mode) type_change list_last]
	    set ftp(type:changeto) $old_type
	    Type $s ascii
	    return {}
	}
        Type $s ascii
    }

    set ftp(State) nlist_$ftp(Mode)
    StateHandler $s

    # wait for synchronization
    set rc [WaitOrTimeout $s]

    # restore old type
    if { [Type $s] != $old_type } {
        Type $s $old_type
    }

    unset ftp(Dir)
    if { $rc } { 
        return [lsort $ftp(List)]
    } else {
        CloseDataConn $s
        return {}
    }
}

#############################################################################
#
# List --
#
# LIST - This command causes a list to be sent from the server
# to user site.
# (exported)
# 
# Arguments:
# dir - 		If the $dir specifies a directory or other group of 
#			files, the server should transfer a list of files in 
#			the specified directory. If the $dir specifies a file
#			then the server should send current information on the
# 			file.  A null argument implies the user's current 
#			working or default directory.  
# 
# Returns:
# list of files or {} if listing fails

proc ftp::List {s {dir ""}} {

    upvar ::ftp::ftp$s ftp

    if { ![info exists ftp(State)] } {
        #if { ![string is digit -strict $s] } {
            #DisplayMsg $s "Bad connection name \"$s\"" error
        #} else {
            #DisplayMsg $s "Not connected!" error
            puts "Hmmmm..."
        #}
        #return {}
    }

    set ftp(List) {}
    if { $dir == "" } {
        set ftp(Dir) ""
    } else {
        set ftp(Dir) " $dir"
    }

    # save current type and force ascii mode

    set old_type $ftp(Type)
    if { ![string compare "$ftp(Type)" "ascii"] ==0 } {
	if {[string length $ftp(Command)]} {
	    set ftp(NextState) [list list_$ftp(Mode) type_change list_last]
	    set ftp(type:changeto) $old_type
	    Type $s ascii
	    return {}
	}
        Type $s ascii
    }

    set ftp(State) list_$ftp(Mode)
    StateHandler $s

    # wait for synchronization

    set rc [WaitOrTimeout $s]

    # restore old type

    if { ![string compare "[Type $s]" "$old_type"] ==0 } {
        Type $s $old_type
    }

    unset ftp(Dir)
    if { $rc } { 
	return [ListPostProcess $ftp(List)]
    } else {
        CloseDataConn $s
        return {}
    }
}

proc ftp::ListPostProcess l {

    # clear "total"-line

    set l [split $l "\n"]
    set index [lsearch -regexp $l "^total"]
    if { $index != "-1" } { 
	set l [lreplace $l $index $index]
    }

    # clear blank line

    set index [lsearch -regexp $l "^$"]
    if { $index != "-1" } { 
	set l [lreplace $l $index $index]
    }

    return $l
}

#############################################################################
#
# FileSize --
#
# REMOTE FILE SIZE - This command gets the file size of the
# file on the remote machine. 
# ATTENTION! Doesn't work properly in ascii mode!
# (exported)
# 
# Arguments:
# filename - 		specifies the remote file name
# 
# Returns:
# size -		files size in bytes or {} in error cases

proc ftp::FileSize {s {filename ""}} {
    upvar ::ftp::ftp$s ftp

    #if { ![info exists ftp(State)] } {
        #if { ![string is digit -strict $s] } {
            #DisplayMsg $s "Bad connection name \"$s\"" error
        #} else {
            #DisplayMsg $s "Not connected!" error
        #}
        #return {}
    #}
	
    if { $filename == "" } {
        return {}
    } 

    puts $s

    set ftp(File) $filename
    set ftp(FileSize) 0
	
    set ftp(State) size
    StateHandler $s

    # wait for synchronization
    set rc [WaitOrTimeout $s]
	
    if {![string length $ftp(Command)]} {
	unset ftp(File)
    }
		
    if { $rc } {
        return $ftp(FileSize)
    } else {
        return {}
    }
}


#############################################################################
#
# ModTime --
#
# MODIFICATION TIME - This command gets the last modification time of the
# file on the remote machine.
# (exported)
# 
# Arguments:
# filename - 		specifies the remote file name
# 
# Returns:
# clock -		files date and time as a system-depentend integer
#			value in seconds (see tcls clock command) or {} in 
#			error cases

proc ftp::ModTime {s {filename ""}} {
    upvar ::ftp::ftp$s ftp

    #if { ![info exists ftp(State)] } {
        #if { ![string is digit -strict $s] } {
            #DisplayMsg $s "Bad connection name \"$s\"" error
        #} else {
            #DisplayMsg $s "Not connected!" error
        #} 
        #return {}
    #}
	
    if { $filename == "" } {
        return {}
    } 

    set ftp(File) $filename
    set ftp(DateTime) ""
	
    set ftp(State) modtime
    StateHandler $s

    # wait for synchronization
    set rc [WaitOrTimeout $s]
	
    if {![string length $ftp(Command)]} {
	unset ftp(File)
    }
		
    if { ![string length $ftp(Command)] && $rc } {
        return [ModTimePostProcess $ftp(DateTime)]
    } else {
        return {}
    }
}

proc ftp::ModTimePostProcess clock {
    foreach {year month day hour min sec} {1 1 1 1 1 1} break
    scan $clock "%4s%2s%2s%2s%2s%2s" year month day hour min sec
    set clock [clock scan "$month/$day/$year $hour:$min:$sec" -gmt 1]
    return $clock
}

#############################################################################
#
# Pwd --
#
# PRINT WORKING DIRECTORY - Causes the name of the current working directory.
# (exported)
# 
# Arguments:
# None.
# 
# Returns:
# current directory name

proc ftp::Pwd {s } {
    upvar ::ftp::ftp$s ftp

    if { ![info exists ftp(State)] } {
        if { ![string is digit -strict $s] } {
            DisplayMsg $s "Bad connection name \"$s\"" error
        } else {
            DisplayMsg $s "Not connected!" error
        }
        return {}
    }

    set ftp(Dir) {}

    set ftp(State) pwd
    StateHandler $s

    # wait for synchronization
    set rc [WaitOrTimeout $s]
	
    if { $rc } {
        return $ftp(Dir)
    } else {
        return {}
    }
}

#############################################################################
#
# Cd --
#   
# CHANGE DIRECTORY - Sets the working directory on the server host.
# (exported)
# 
# Arguments:
# dir -			pathname specifying a directory
#
# Returns:
# 0 -			ERROR
# 1 - 			OK

proc ftp::Cd {s {dir ""}} {
    upvar ::ftp::ftp$s ftp

    #if { ![info exists ftp(State)] } {
        #if { ![string is digit -strict $s] } {
            #DisplayMsg $s "Bad connection name \"$s\"" error
        #} else {
            #DisplayMsg $s "Not connected!" error
            #puts "$s connected!"
        #}
        #return 0
    #}

    if { $dir == "" } {
        set ftp(Dir) ""
    } else {
        set ftp(Dir) " $dir"
    }

    set ftp(State) cd
    StateHandler $s

    # wait for synchronization
    set rc [WaitOrTimeout $s] 

    if {![string length $ftp(Command)]} {
	unset ftp(Dir)
    }
	
    if { $rc } {
        return 1
    } else {
        return 0
    }
}

#############################################################################
#
# MkDir --
#
# MAKE DIRECTORY - This command causes the directory specified in the $dir
# to be created as a directory (if the $dir is absolute) or as a subdirectory
# of the current working directory (if the $dir is relative).
# (exported)
# 
# Arguments:
# dir -			new directory name
#
# Returns:
# 0 -			ERROR
# 1 - 			OK

proc ftp::MkDir {s dir} {
    upvar ::ftp::ftp$s ftp

    if { ![info exists ftp(State)] } {
        DisplayMsg $s "Not connected!" error
        return 0
    }

    set ftp(Dir) $dir

    set ftp(State) mkdir
    StateHandler $s

    # wait for synchronization
    set rc [WaitOrTimeout $s] 

    if {![string length $ftp(Command)]} {
	unset ftp(Dir)
    }
	
    if { $rc } {
        return 1
    } else {
        return 0
    }
}

#############################################################################
#
# RmDir --
#
# REMOVE DIRECTORY - This command causes the directory specified in $dir to 
# be removed as a directory (if the $dir is absolute) or as a 
# subdirectory of the current working directory (if the $dir is relative).
# (exported)
#
# Arguments:
# dir -			directory name
#
# Returns:
# 0 -			ERROR
# 1 - 			OK

proc ftp::RmDir {s dir} {
    upvar ::ftp::ftp$s ftp

    if { ![info exists ftp(State)] } {
        DisplayMsg $s "Not connected!" error
        return 0
    }

    set ftp(Dir) $dir

    set ftp(State) rmdir
    StateHandler $s

    # wait for synchronization
    set rc [WaitOrTimeout $s] 

    if {![string length $ftp(Command)]} {
	unset ftp(Dir)
    }
	
    if { $rc } {
        return 1
    } else {
        return 0
    }
}

#############################################################################
#
# Delete --
#
# DELETE - This command causes the file specified in $file to be deleted at 
# the server site.
# (exported)
# 
# Arguments:
# file -			file name
#
# Returns:
# 0 -			ERROR
# 1 - 			OK

proc ftp::Delete {s file} {
    upvar ::ftp::ftp$s ftp

    if { ![info exists ftp(State)] } {
        DisplayMsg $s "Not connected!" error
        return 0
    }

    set ftp(File) $file

    set ftp(State) delete
    StateHandler $s

    # wait for synchronization
    set rc [WaitOrTimeout $s] 

    if {![string length $ftp(Command)]} {
	unset ftp(File)
    }
	
    if { $rc } {
        return 1
    } else {
        return 0
    }
}

#############################################################################
#
# Rename --
#
# RENAME FROM TO - This command causes the file specified in $from to be 
# renamed at the server site.
# (exported)
# 
# Arguments:
# from -			specifies the old file name of the file which 
#				is to be renamed
# to -				specifies the new file name of the file 
#				specified in the $from agument
# Returns:
# 0 -			ERROR
# 1 - 			OK

proc ftp::Rename {s from to} {
    upvar ::ftp::ftp$s ftp

    if { ![info exists ftp(State)] } {
        DisplayMsg $s "Not connected!" error
        return 0
    }

    set ftp(RenameFrom) $from
    set ftp(RenameTo) $to

    set ftp(State) rename

    StateHandler $s

    # wait for synchronization
    set rc [WaitOrTimeout $s] 

    if {![string length $ftp(Command)]} {
	unset ftp(RenameFrom)
	unset ftp(RenameTo)
    }
	
    if { $rc } {
        return 1
    } else {
        return 0
    }
}

#############################################################################
#
# ElapsedTime --
#
# Gets the elapsed time for file transfer
# 
# Arguments:
# stop_time - 		ending time

proc ftp::ElapsedTime {s stop_time} {
    upvar ::ftp::ftp$s ftp

    set elapsed [expr {$stop_time - $ftp(Start_Time)}]
    if { $elapsed == 0 } {
        set elapsed 1
    }
    set persec [expr {$ftp(Total) / $elapsed}]
    DisplayMsg $s "$ftp(Total) bytes sent in $elapsed seconds ($persec Bytes/s)"
    return
}

#############################################################################
#
# PUT --
#
# STORE DATA - Causes the server to accept the data transferred via the data 
# connection and to store the data as a file at the server site.  If the file
# exists at the server site, then its contents shall be replaced by the data
# being transferred.  A new file is created at the server site if the file
# does not already exist.
# (exported)
#
# Arguments:
# source -			local file name
# dest -			remote file name, if unspecified, ftp assigns
#				the local file name.
# Returns:
# 0 -			file not stored
# 1 - 			OK

proc ftp::Put {s args} {
    upvar ::ftp::ftp$s ftp

    if { ![info exists ftp(State)] } {
        DisplayMsg $s "Not connected!" error
        return 0
    }
    if {([llength $args] < 1) || ([llength $args] > 4)} {
        DisplayMsg $s "wrong # args: should be \"ftp::Put handle (-data \"data\" | localFilename) remoteFilename\"" error
	return 0    
    }

    set ftp(inline) 0
    set flags 1
    set source ""
    set dest ""
    foreach arg $args {
        if {[string compare $arg "--"] ==0 } {
            set flags 0
        } elseif {($flags) && ([string compare $arg "-data"] ==0 )} {
            set ftp(inline) 1
            set ftp(filebuffer) ""
	} elseif {$source == ""} {
            set source $arg
	} elseif {$dest == ""} {
            set dest $arg
	} else {
            DisplayMsg $s "wrong # args: should be \"ftp::Put handle (-data \"data\" | localFilename) remoteFilename\"" error
	    return 0
        }
    }

    if {($source == "")} {
        DisplayMsg $s "Must specify a valid file to Put" error
        return 0
    }        

    set ftp(RemoteFilename) $dest

    if {$ftp(inline)} {
        set ftp(PutData) $source
        if { $dest == "" } {
            set dest ftp.tmp
        }
        set ftp(RemoteFilename) $dest
    } else {
        set ftp(PutData) ""
        if { ![file exists $source] } {
            DisplayMsg $s "File \"$source\" not exist" error
            return 0
        }
        if { $dest == "" } {
            set dest [file tail $source]
        }
        set ftp(LocalFilename) $source
        set ftp(RemoteFilename) $dest

	# TODO read from source file asynchronously
        set ftp(SourceCI) [open $ftp(LocalFilename) r]
        if { [string compare $ftp(Type) "ascii"] ==0 } {
            fconfigure $ftp(SourceCI) -buffering line -blocking 1
        } else {
            fconfigure $ftp(SourceCI) -buffering line -translation binary -blocking 1
        }
    }

    set ftp(State) put_$ftp(Mode)
    StateHandler $s

    # wait for synchronization
    set rc [WaitOrTimeout $s]
    if { $rc } {
	if {![string length $ftp(Command)]} {
	    ElapsedTime $s [clock seconds]
	}
        return 1
    } else {
        CloseDataConn $s
        return 0
    }
}

#############################################################################
#
# APPEND --
#
# APPEND DATA - Causes the server to accept the data transferred via the data 
# connection and to store the data as a file at the server site.  If the file
# exists at the server site, then the data shall be appended to that file; 
# otherwise the file specified in the pathname shall be created at the
# server site.
# (exported)
#
# Arguments:
# source -			local file name
# dest -			remote file name, if unspecified, ftp assigns
#				the local file name.
# Returns:
# 0 -			file not stored
# 1 - 			OK

proc ftp::Append {s args} {
    upvar ::ftp::ftp$s ftp

    if { ![info exists ftp(State)] } {
        DisplayMsg $s "Not connected!" error
        return 0
    }

    if {([llength $args] < 1) || ([llength $args] > 4)} {
        DisplayMsg $s "wrong # args: should be \"ftp::Append handle (-data \"data\" | localFilename) remoteFilename\"" error
        return 0
    }

    set ftp(inline) 0
    set flags 1
    set source ""
    set dest ""
    foreach arg $args {
        if {[string compare $arg "--"] ==0 } {
            set flags 0
        } elseif {($flags) && ([string compare $arg "-data"] ==0 )} {
            set ftp(inline) 1
            set ftp(filebuffer) ""
        } elseif {$source == ""} {
            set source $arg
        } elseif {$dest == ""} {
            set dest $arg
        } else {
            DisplayMsg $s "wrong # args: should be \"ftp::Append handle (-data \"data\" | localFilename) remoteFilename\"" error
            return 0
        }
    }

    if {($source == "")} {
        DisplayMsg $s "Must specify a valid file to Append" error
        return 0
    }   

    set ftp(RemoteFilename) $dest

    if {$ftp(inline)} {
        set ftp(PutData) $source
        if { $dest == "" } {
            set dest ftp.tmp
        }
        set ftp(RemoteFilename) $dest
    } else {
        set ftp(PutData) ""
        if { ![file exists $source] } {
            DisplayMsg $s "File \"$source\" not exist" error
            return 0
        }
			
        if { $dest == "" } {
            set dest [file tail $source]
        }

        set ftp(LocalFilename) $source
        set ftp(RemoteFilename) $dest

        set ftp(SourceCI) [open $ftp(LocalFilename) r]
        if { [string compare $ftp(Type) "ascii"] ==0 } {
            fconfigure $ftp(SourceCI) -buffering line -blocking 1
        } else {
            fconfigure $ftp(SourceCI) -buffering line -translation binary \
                    -blocking 1
        }
    }

    set ftp(State) append_$ftp(Mode)
    StateHandler $s

    # wait for synchronization
    set rc [WaitOrTimeout $s]
    if { $rc } {
	if {![string length $ftp(Command)]} {
	    ElapsedTime $s [clock seconds]
	}
        return 1
    } else {
        CloseDataConn $s
        return 0
    }
}


#############################################################################
#
# Get --
#
# RETRIEVE DATA - Causes the server to transfer a copy of the specified file
# to the local site at the other end of the data connection.
# (exported)
#
# Arguments:
# source -			remote file name
# dest -			local file name, if unspecified, ftp assigns
#				the remote file name.
# Returns:
# 0 -			file not retrieved
# 1 - 			OK

proc ftp::Get {s args} {
    upvar ::ftp::ftp$s ftp

    if { ![info exists ftp(State)] } {
        DisplayMsg $s "Not connected!" error
        return 0
    }

    if {([llength $args] < 1) || ([llength $args] > 4)} {
        DisplayMsg $s "wrong # args: should be \"ftp::Get handle remoteFile ?(-variable varName | localFilename)?\"" error
	return 0    
    }

    set ftp(inline) 0
    set flags 1
    set source ""
    set dest ""
    set varname "**NONE**"
    foreach arg $args {
        if {[string compare $arg "--"] ==0 } {
            set flags 0
        } elseif {($flags) && ([string compare $arg "-variable"] ==0 )} {
            set ftp(inline) 1
            set ftp(filebuffer) ""
	} elseif {($ftp(inline)) && ([string compare $varname "**NONE**"] ==0 )} {
            set varname $arg
	    set ftp(get:varname) $varname
	} elseif {$source == ""} {
            set source $arg
	} elseif {$dest == ""} {
            set dest $arg
	} else {
            DisplayMsg $s "wrong # args: should be \"ftp::Get handle remoteFile
?(-variable varName | localFilename)?\"" error
	    return 0
        }
    }

    if {($ftp(inline)) && ($dest != "")} {
        DisplayMsg $s "Cannot return data in a variable, and place it in destination file." error
        return 0
    }

    if {$source == ""} {
        DisplayMsg $s "Must specify a valid file to Get" error
        return 0
    }

    if { $dest == "" } {
        set dest $source
    } else {
        if {[file isdirectory $dest]} {
            set dest [file join $dest [file tail $source]]
        }
    }

    set ftp(RemoteFilename) $source
    set ftp(LocalFilename) $dest

    set ftp(State) get_$ftp(Mode)
    StateHandler $s

    # wait for synchronization
    set rc [WaitOrTimeout $s]
    if { $rc } {
	if {![string length $ftp(Command)]} {
	    ElapsedTime $s [clock seconds]
	    if {$ftp(inline)} {
		upvar $varname returnData
		set returnData $ftp(GetData)
	    }
	}
        return 1
    } else {
        if {$ftp(inline)} {
            return ""
	}
        CloseDataConn $s
        return 0
    }
}

#############################################################################
#
# Reget --
#
# RESTART RETRIEVING DATA - Causes the server to transfer a copy of the specified file
# to the local site at the other end of the data connection like get but skips over 
# the file to the specified data checkpoint. 
# (exported)
#
# Arguments:
# source -			remote file name
# dest -			local file name, if unspecified, ftp assigns
#				the remote file name.
# Returns:
# 0 -			file not retrieved
# 1 - 			OK

proc ftp::Reget {s source {dest ""}} {
    upvar ::ftp::ftp$s ftp

    if { ![info exists ftp(State)] } {
        DisplayMsg $s "Not connected!" error
        return 0
    }

    if { $dest == "" } {
        set dest $source
    }

    set ftp(RemoteFilename) $source
    set ftp(LocalFilename) $dest

    if { [file exists $ftp(LocalFilename)] } {
        set ftp(FileSize) [file size $ftp(LocalFilename)]
    } else {
        set ftp(FileSize) 0
    }
	
    set ftp(State) reget_$ftp(Mode)
    StateHandler $s

    # wait for synchronization
    set rc [WaitOrTimeout $s]
    if { $rc } {
	if {![string length $ftp(Command)]} {
	    ElapsedTime $s [clock seconds]
	}
        return 1
    } else {
        CloseDataConn $s
        return 0
    }
}

#############################################################################
#
# Newer --
#
# GET NEWER DATA - Get the file only if the modification time of the remote 
# file is more recent that the file on the current system. If the file does
# not exist on the current system, the remote file is considered newer.
# Otherwise, this command is identical to get. 
# (exported)
#
# Arguments:
# source -			remote file name
# dest -			local file name, if unspecified, ftp assigns
#				the remote file name.
#
# Returns:
# 0 -			file not retrieved
# 1 - 			OK

proc ftp::Newer {s source {dest ""}} {
    upvar ::ftp::ftp$s ftp

    if { ![info exists ftp(State)] } {
        DisplayMsg $s "Not connected!" error
        return 0
    }

    if {[string length $ftp(Command)]} {
	return -code error "unable to retrieve file asynchronously (not implemented yet)"
    }

    if { $dest == "" } {
        set dest $source
    }

    set ftp(RemoteFilename) $source
    set ftp(LocalFilename) $dest

    # get remote modification time
    set rmt [ModTime $s $ftp(RemoteFilename)]
    if { $rmt == "-1" } {
        return 0
    }

    # get local modification time
    if { [file exists $ftp(LocalFilename)] } {
        set lmt [file mtime $ftp(LocalFilename)]
    } else {
        set lmt 0
    }
	
    # remote file is older than local file
    if { $rmt < $lmt } {
        return 0
    }

    # remote file is newer than local file or local file doesn't exist
    # get it
    set rc [Get $s $ftp(RemoteFilename) $ftp(LocalFilename)]
    return $rc
		
}

#############################################################################
#
# Quote -- 
#
# The arguments specified are sent, verbatim, to the remote ftp server.     
#
# Arguments:
# 	arg1 arg2 ...
#
# Returns:
#  string sent back by the remote ftp server or null string if any error
#

proc ftp::Quote {s args} {
    upvar ::ftp::ftp$s ftp

    if { ![info exists ftp(State)] } {
        DisplayMsg $s "Not connected!" error
        return 0
    }

    set ftp(Cmd) $args
    set ftp(Quote) {}

    set ftp(State) quote
    StateHandler $s

    # wait for synchronization
    set rc [WaitOrTimeout $s] 

    unset ftp(Cmd)

    if { $rc } {
        return $ftp(Quote)
    } else {
        return {}
    }
}


#############################################################################
#
# Abort -- 
#
# ABORT - Tells the server to abort the previous ftp service command and 
# any associated transfer of data. The control connection is not to be 
# closed by the server, but the data connection must be closed.
# 
# NOTE: This procedure doesn't work properly. Thus the ftp::Abort command
# is no longer available!
#
# Arguments:
# None.
#
# Returns:
# 0 -			ERROR
# 1 - 			OK
#
# proc Abort {} {
#
# }

#############################################################################
#
# Close -- 
#
# Terminates a ftp session and if file transfer is not in progress, the server
# closes the control connection.  If file transfer is in progress, the 
# connection will remain open for result response and the server will then
# close it. 
# (exported)
# 
# Arguments:
# None.
#
# Returns:
# 0 -			ERROR
# 1 - 			OK

proc ftp::Close {s } {
    variable connections
    upvar ::ftp::ftp$s ftp

    if { ![info exists ftp(State)] } {
        DisplayMsg $s "Not connected!" error
        return 0
    }

    if {[info exists \
            connections($ftp(User),$ftp(Passwd),$ftp(RemoteHost),afterid)]} {
        unset connections($ftp(User),$ftp(Passwd),$ftp(RemoteHost),afterid)
        unset connections($ftp(User),$ftp(Passwd),$ftp(RemoteHost))
    }

    set ftp(State) quit
    StateHandler $s

    # wait for synchronization
    WaitOrTimeout $s

    catch {close $ftp(CtrlSock)}
    catch {unset ftp}
}

proc ftp::LazyClose {s } {
    variable connections
    upvar ::ftp::ftp$s ftp

    if { ![info exists ftp(State)] } {
        DisplayMsg $s "Not connected!" error
        return 0
    }

    if {[info exists connections($ftp(User),$ftp(Passwd),$ftp(RemoteHost))]} {
        set connections($ftp(User),$ftp(Passwd),$ftp(RemoteHost),afterid) \
                [after 5000 [list ftp::Close $s]]
    }
    return
}

#############################################################################
#
# Open --
#
# Starts the ftp session and sets up a ftp control connection.
# (exported)
# 
# Arguments:
# server - 		The ftp server hostname.
# user -		A string identifying the user. The user identification 
#			is that which is required by the server for access to 
#			its file system.  
# passwd -		A string specifying the user's password.
# options -		-blocksize size		writes "size" bytes at once
#						(default 4096)
#			-timeout seconds	if non-zero, sets up timeout to
#						occur after specified number of
#						seconds (default 120)
#			-progress proc		procedure name that handles callbacks
#						(no default)  
#			-output proc		procedure name that handles output
#						(no default)  
#			-mode mode		switch active or passive file transfer
#						(default active)
#			-port number		alternative port (default 21)
#			-command proc		callback for completion notification
#						(no default)
# 
# Returns:
# 0 -			Not logged in
# 1 - 			User logged in

proc ftp::Open {server user passwd args} {
    variable DEBUG 
    variable VERBOSE
    variable serial
    variable connections

    set s $serial
    incr serial
    upvar ::ftp::ftp$s ftp
#    if { [info exists ftp(State)] } {
#        DisplayMsg $s "Mmh, another attempt to open a new connection? There is already a hot wire!" error
#        return 0
#    }

    # default NO DEBUG
    if { ![info exists DEBUG] } {
        set DEBUG 0
    }

    # default NO VERBOSE
    if { ![info exists VERBOSE] } {
        set VERBOSE 0
    }
	
    if { $DEBUG } {
        DisplayMsg $s "Starting new connection with: "
    }
	
    set ftp(User)       $user
    set ftp(Passwd) 	$passwd
    set ftp(RemoteHost) $server
    set ftp(LocalHost) 	[info hostname]
    set ftp(DataPort) 	0
    set ftp(Type) 	{}
    set ftp(Error) 	""
    set ftp(Progress) 	{}
    set ftp(Command)	{}
    set ftp(Output) 	{}
    set ftp(Blocksize) 	4096	
    set ftp(Timeout) 	60	
    set ftp(Mode) 	active	
    set ftp(Port) 	21	

    set ftp(State) 	user
	
    # set state var
    set ftp(state.control) ""
	
    # Get and set possible options
    set options {-blocksize -timeout -mode -port -progress -output -command}
    foreach {option value} $args {
        if { [lsearch -exact $options $option] != "-1" } {
            if { $DEBUG } {
                DisplayMsg $s "  $option = $value"
            }
            regexp {^-(.?)(.*)$} $option all first rest
            set option "[string toupper $first]$rest"
            set ftp($option) $value
        } 
    }
    if { $DEBUG && ([llength $args] == 0) } {
        DisplayMsg $s "  no option"
    }

    if {[info exists \
            connections($ftp(User),$ftp(Passwd),$ftp(RemoteHost),afterid)]} {
        after cancel $connections($ftp(User),$ftp(Passwd),$ftp(RemoteHost),afterid)
	Command $ftp(Command) connect $connections($ftp(User),$ftp(Passwd),$ftp(RemoteHost))
        return $connections($ftp(User),$ftp(Passwd),$ftp(RemoteHost))
    }


    # No call of StateHandler is required at this time.
    # StateHandler at first time is called automatically
    # by a fileevent for the control channel.

    # Try to open a control connection
    if { ![OpenControlConn $s [expr {[string length $ftp(Command)] > 0}]] } {
        return -1
    }

    # waits for synchronization
    #   0 ... Not logged in
    #   1 ... User logged in
    if {[string length $ftp(Command)]} {
	# Don't wait - asynchronous operation
	set ftp(NextState) {type connect_last}
        set connections($ftp(User),$ftp(Passwd),$ftp(RemoteHost)) $s
	return $s
    } elseif { [WaitOrTimeout $s] } {
        # default type is binary
        Type $s binary
        set connections($ftp(User),$ftp(Passwd),$ftp(RemoteHost)) $s
	Command $ftp(Command) connect $s
        return $s
    } else {
        # close connection if not logged in
        Close $s
        return -1
    }
}

#############################################################################
#
# CopyNext --
#
# recursive background copy procedure for ascii/binary file I/O
# 
# Arguments:
# bytes - 		indicates how many bytes were written on $ftp(DestCI)

proc ftp::CopyNext {s bytes {error {}}} {
    upvar ::ftp::ftp$s ftp
    variable DEBUG
    variable VERBOSE

    # summary bytes

    incr ftp(Total) $bytes

    # callback for progress bar procedure
    
    if { ([info exists ftp(Progress)]) && \
	    [string length $ftp(Progress)] && \
	    ([info commands [lindex $ftp(Progress) 0]] != "") } { 
        eval $ftp(Progress) $ftp(Total)
    }

    # setup new timeout handler

    catch {after cancel $ftp(Wait)}
    set ftp(Wait) [after [expr {$ftp(Timeout) * 1000}] [namespace current]::Timeout $s]

    if { $DEBUG } {
        DisplayMsg $s "-> $ftp(Total) bytes $ftp(SourceCI) -> $ftp(DestCI)" 
    }

    if { $error != "" } {
        catch {close $ftp(DestCI)}
        catch {close $ftp(SourceCI)}
        unset ftp(state.data)
        DisplayMsg $s $error error

    } elseif { [eof $ftp(SourceCI)] } {
        close $ftp(DestCI)
        close $ftp(SourceCI)
        unset ftp(state.data)
        if { $VERBOSE } {
            DisplayMsg $s "D: Port closed" data
        }

    } else {
        fcopy $ftp(SourceCI) $ftp(DestCI) -command [list [namespace current]::CopyNext $s] -size $ftp(Blocksize)

    }
    return
}

#############################################################################
#
# HandleData --
#
# Handles ascii/binary data transfer for Put and Get 
# 
# Arguments:
# sock - 		socket name (data channel)

proc ftp::HandleData {s sock} {
    upvar ::ftp::ftp$s ftp

    # Turn off any fileevent handlers

    fileevent $sock writable {}		
    fileevent $sock readable {}

    # create local file for ftp::Get 

    if { [regexp "^get" $ftp(State)]  && (!$ftp(inline))} {
        set rc [catch {set ftp(DestCI) [open $ftp(LocalFilename) w]} msg]
        if { $rc != 0 } {
            DisplayMsg $s "$msg" error
            return 0
        }
	# TODO use non-blocking I/O
        if { [string compare $ftp(Type) "ascii"] ==0 } {
            fconfigure $ftp(DestCI) -buffering line -blocking 1
        } else {
            fconfigure $ftp(DestCI) -buffering line -translation binary -blocking 1
        }
    }	

    # append local file for ftp::Reget 

    if { [regexp "^reget" $ftp(State)] } {
        set rc [catch {set ftp(DestCI) [open $ftp(LocalFilename) a]} msg]
        if { $rc != 0 } {
            DisplayMsg $s "$msg" error
            return 0
        }
	# TODO use non-blocking I/O
        if { [string compare $ftp(Type) "ascii"] ==0 } {
            fconfigure $ftp(DestCI) -buffering line -blocking 1
        } else {
            fconfigure $ftp(DestCI) -buffering line -translation binary -blocking 1
        }
    }	

    # perform fcopy

    set ftp(Total) 0
    set ftp(Start_Time) [clock seconds]
    fcopy $ftp(SourceCI) $ftp(DestCI) -command [list [namespace current]::CopyNext $s] -size $ftp(Blocksize)
    return
}

#############################################################################
#
# HandleList --
#
# Handles ascii data transfer for list commands
# 
# Arguments:
# sock - 		socket name (data channel)

proc ftp::HandleList {s sock} {
    upvar ::ftp::ftp$s ftp
    variable VERBOSE

    if { ![eof $sock] } {
        set buffer [read $sock]
        if { $buffer != "" } {
            set ftp(List) [append ftp(List) $buffer]
        }	
    } else {
        close $sock
        catch {unset ftp(state.data)}
        if { $VERBOSE } {
            DisplayMsg $s "D: Port closed" data
        }
    }
    return
}

#############################################################################
#
# HandleVar --
#
# Handles data transfer for get/put commands that use buffers instead
# of files.
# 
# Arguments:
# sock - 		socket name (data channel)

proc ftp::HandleVar {s sock} {
    upvar ::ftp::ftp$s ftp
    variable VERBOSE

    if {$ftp(Start_Time) == -1} {
        set ftp(Start_Time) [clock seconds]
    }

    if { ![eof $sock] } {
        set buffer [read $sock]
        if { $buffer != "" } {
            append ftp(GetData) $buffer
            incr ftp(Total) [string length $buffer]
        }	
    } else {
        close $sock
        catch {unset ftp(state.data)}
        if { $VERBOSE } {
            DisplayMsg $s "D: Port closed" data
        }
    }
    return
}

#############################################################################
#
# HandleOutput --
#
# Handles data transfer for get/put commands that use buffers instead
# of files.
# 
# Arguments:
# sock - 		socket name (data channel)

proc ftp::HandleOutput {s sock} {
    upvar ::ftp::ftp$s ftp
    variable VERBOSE

    if {$ftp(Start_Time) == -1} {
        set ftp(Start_Time) [clock seconds]
    }

    if { $ftp(Total) < [string length $ftp(PutData)] } {
        set substr [string range $ftp(PutData) $ftp(Total) \
                [expr {$ftp(Total) + $ftp(Blocksize)}]]
        if {[catch {puts -nonewline $sock "$substr"} result]} {
            close $sock
            unset ftp(state.data)
            if { $VERBOSE } {
                DisplayMsg $s "D: Port closed" data
            }
        } else {
            incr ftp(Total) [string length $substr]
        }
    } else {
        fileevent $sock writable {}		
        close $sock
        catch {unset ftp(state.data)}
        if { $VERBOSE } {
            DisplayMsg $s "D: Port closed" data
        }
    }
    return
}

############################################################################
#
# CloseDataConn -- 
#
# Closes all sockets and files used by the data conection
#
# Arguments:
# None.
#
# Returns:
# None.
#
proc ftp::CloseDataConn {s } {
    upvar ::ftp::ftp$s ftp

    catch {after cancel $ftp(Wait)}
    catch {fileevent $ftp(DataSock) readable {}}
    catch {close $ftp(DataSock); unset ftp(DataSock)}
    catch {close $ftp(DestCI); unset ftp(DestCI)} 
    catch {close $ftp(SourceCI); unset ftp(SourceCI)}
    catch {close $ftp(DummySock); unset ftp(DummySock)}
    return
}

#############################################################################
#
# InitDataConn --
#
# Configures new data channel for connection to ftp server 
# ATTENTION! The new data channel "sock" is not the same as the 
# server channel, it's a dummy.
# 
# Arguments:
# sock -		the name of the new channel
# addr -		the address, in network address notation, 
#			of the client's host,
# port -		the client's port number

proc ftp::InitDataConn {s sock addr port} {
    upvar ::ftp::ftp$s ftp
    variable VERBOSE

    # If the new channel is accepted, the dummy channel will be closed

    catch {close $ftp(DummySock); unset ftp(DummySock)}

    set ftp(state.data) 0

    # Configure translation and blocking modes

    set blocking 1
    if {[string length $ftp(Command)]} {
	set blocking 0
    }

    if { [string compare $ftp(Type) "ascii"] ==0 } {
        fconfigure $sock -buffering line -blocking $blocking
    } else {
        fconfigure $sock -buffering line -translation binary -blocking $blocking
    }

    # assign fileevent handlers, source and destination CI (Channel Identifier)

    switch -regexp -- $ftp(State) {
        list {
            fileevent $sock readable [list [namespace current]::HandleList $s $sock]
            set ftp(SourceCI) $sock		  
        }
        get {
            if {$ftp(inline)} {
                set ftp(GetData) ""
                set ftp(Start_Time) -1
                set ftp(Total) 0
                fileevent $sock readable [list [namespace current]::HandleVar $s $sock]
	    } else {
                fileevent $sock readable [list [namespace current]::HandleData $s $sock]
                set ftp(SourceCI) $sock
	    }			  
        }
        append -
        put {
            if {$ftp(inline)} {
                set ftp(Start_Time) -1
                set ftp(Total) 0
                fileevent $sock writable [list [namespace current]::HandleOutput $s $sock]
	    } else {
                fileevent $sock writable [list [namespace current]::HandleData $s $sock]
                set ftp(DestCI) $sock
	    }			  
        }
    }

    if { $VERBOSE } {
        DisplayMsg $s "D: Connection from $addr:$port" data
    }
    return
}

#############################################################################
#
# OpenActiveConn --
#
# Opens a ftp data connection
# 
# Arguments:
# None.
# 
# Returns:
# 0 -			no connection
# 1 - 			connection established

proc ftp::OpenActiveConn {s } {
    upvar ::ftp::ftp$s ftp
    variable VERBOSE

    # Port address 0 is a dummy used to give the server the responsibility 
    # of getting free new port addresses for every data transfer.
    
    set rc [catch {set ftp(DummySock) [socket -server [list [namespace current]::InitDataConn $s] 0]} msg]
    if { $rc != 0 } {
        DisplayMsg $s "$msg" error
        return 0
    }

    # get a new local port address for data transfer and convert it to a format
    # which is useable by the PORT command

    set p [lindex [fconfigure $ftp(DummySock) -sockname] 2]
    if { $VERBOSE } {
        DisplayMsg $s "D: Port is $p" data
    }
    set ftp(DataPort) "[expr {$p / 256}],[expr {$p % 256}]"

    return 1
}

#############################################################################
#
# OpenPassiveConn --
#
# Opens a ftp data connection
# 
# Arguments:
# buffer - returned line from server control connection 
# 
# Returns:
# 0 -			no connection
# 1 - 			connection established

proc ftp::OpenPassiveConn {s buffer} {
    upvar ::ftp::ftp$s ftp

    if { [regexp {([0-9]+),([0-9]+),([0-9]+),([0-9]+),([0-9]+),([0-9]+)} $buffer all a1 a2 a3 a4 p1 p2] } {
        set ftp(LocalAddr) "$a1.$a2.$a3.$a4"
        set ftp(DataPort) "[expr {$p1 * 256 + $p2}]"

        # establish data connection for passive mode

        set rc [catch {set ftp(DataSock) [socket $ftp(LocalAddr) $ftp(DataPort)]} msg]
        if { $rc != 0 } {
            DisplayMsg $s "$msg" error
            return 0
        }

        InitDataConn $s $ftp(DataSock) $ftp(LocalAddr) $ftp(DataPort)
        return 1
    } else {
        return 0
    }
}

#############################################################################
#
# OpenControlConn --
#
# Opens a ftp control connection
# 
# Arguments:
#	s	connection id
#	block	blocking or non-blocking mode
# 
# Returns:
# 0 -			no connection
# 1 - 			connection established

proc ftp::OpenControlConn {s {block 1}} {
    upvar ::ftp::ftp$s ftp
    variable DEBUG
    variable VERBOSE
    global connect
    set connect 0
    set peer ""
    set msg ""

    # open a control channel

    while { $connect == 0 } {
      #set rc [catch {set ftp(CtrlSock) [socket $ftp(RemoteHost) $ftp(Port)]} msg]
      #set rc [catch {set ftp(CtrlSock) [socket -async $ftp(RemoteHost) $ftp(Port)]} msg]
      catch { set ftp(CtrlSock) [socket -async $ftp(RemoteHost) $ftp(Port)] } msg
      if { ![info exists ftp(CtrlSock)] } {
        if { [string compare $msg ""] != 0 } {
          puts "ERROR: $msg."
          return 0
        }
      }
      #after 10000 { set connect 1 }
      set afterID [after [expr { $ftp(Timeout) * 1000}] { set connect 2 }]
      fileevent $ftp(CtrlSock) writable { set connect 1 }
      vwait connect
      fileevent $ftp(CtrlSock) writable {}
      catch {set peer [fconfigure $ftp(CtrlSock) -peername]}
      if { $connect == 1} {
        if { [string compare $peer ""] == 0 } {
          set connect 0
        } 
      }
      after cancel afterID
    }

    if { $connect == 0 } {
      puts "ERROR: Connection failed. Please try again."
      unset ftp(State)
      return 0
    } elseif { $connect == 2} {
      puts "ERROR: Connection timeout after $ftp(Timeout) secs."
      unset ftp(State)
      return 0
    }

    # configure control channel

    fconfigure $ftp(CtrlSock) -buffering line -blocking $block -translation {auto crlf}
    fileevent $ftp(CtrlSock) readable [list [namespace current]::StateHandler $s $ftp(CtrlSock)]
	
    # prepare local ip address for PORT command (convert pointed format
    # to comma format)

    set ftp(LocalAddr) [lindex [fconfigure $ftp(CtrlSock) -sockname] 0]
    regsub -all "\[.\]" $ftp(LocalAddr) "," ftp(LocalAddr) 

    # report ready message


    set peer [fconfigure $ftp(CtrlSock) -peername]
    if { $VERBOSE } {
        DisplayMsg $s "C: Connection from [lindex $peer 0]:[lindex $peer 2]" control
    }
	
    return 1
}

# ftp::Command --
#
#	Wrapper for evaluated user-supplied command callback
#
# Arguments:
#	cb	callback script
#	msg	what happened
#	args	additional info
#
# Results:
#	Depends on callback script

proc ftp::Command {cb msg args} {
    if {[string length $cb]} {
	uplevel #0 $cb [list $msg] $args
    }
}

# ?????? Hmm, how to do multithreaded for tkcon?
# added TkCon support
# TkCon is (c) 1995-1999 Jeffrey Hobbs, http://www.purl.org/net/hobbs/tcl/script/tkcon/
# started with: tkcon -load ftp
#if { [string compare [uplevel "#0" {info commands tkcon}] "tkcon"] ==0 } {

    # new ftp::List proc makes the output more readable
    #proc ::ftp::__ftp_ls {args} {
        #foreach i [::ftp::List_org $args] {
            #puts $i
        #}
    #}

    # rename the original ftp::List procedure
    #rename ::ftp::List ::ftp::List_org

    #alias ::ftp::List	::ftp::__ftp_ls
    #alias bye		catch {::ftp::Close; exit}	

    #set ::ftp::VERBOSE 1
    #set ::ftp::DEBUG 0
#}




#Open FTP Connection
#  normal -- return the ftp_code ( >=0)
#  error  -- return -1

proc proc_ftp_open { server_ip user password } {
   
    set ftp_code [ftp::Open $server_ip $user $password]
    if { $ftp_code == -1 } {
	#puts "FTP Connection refused."
	return -1
    }
    return $ftp_code
}

# Close Ftp Connection identified by ftp_code 

proc proc_ftp_close { ftp_code } {

    ftp::Close $ftp_code
    #puts "FTP Connection Closed."
}

# Get Files in Binary mode by FTP,
#   - Input:  ftp_code, local_dir, filename
#   - normal -- return 0
#   - error  -- return -1

proc proc_ftp_get { ftp_code filename {local_dir ""}} {

    ftp::Type $ftp_code binary
    #puts "getting $filename by FTP ..."
    if { [string compare $local_dir ""] == 0} {
      if { ![ftp::Get $ftp_code $filename] } { 
	puts "FTP file get failed!"
	return -1
      }
    } else {
      if { ![ftp::Get $ftp_code $filename $local_dir] } { 
	puts "FTP file get failed!"
	return -1
      }
    }
    return 0
} 


# Put Files in Binary mode by FTP,
#   - Input:  ftp_code, local_dir, filename
#   - normal -- return 0
#   - error  -- return -1



proc proc_ftp_put { ftp_code filename } {

    ftp::Type $ftp_code binary
    if { ![ftp::Put $ftp_code $filename] } { 
      puts "FTP file put failed!"
      return -1
    }
    return 0
} 

#proc proc_ftp_put { ftp_code local_dir filename } {

    #ftp::Type $ftp_code binary
    #puts "getting $filename by FTP ..."
    #if { ![ftp::Put $ftp_code $filename] } { 
	#puts "FTP file put failed!"
	#return -1
    #}
    #return 0
#} 

#
# Get file from the remote site by FTP
#
proc proc_get_file { server user password local remote }  {
set return_code 0

    if { [file exists $local] == 0 } {
      puts "ERROR: FTP Get:: \'$local\' does not exist. Please create the directory"
      exit -1
    }

    set ftp_code [proc_ftp_open $server $user $password]
    if { $ftp_code  == -1 } {
       puts "ERROR: FTP Get:: Ftp Connection Failed"
	exit -1
    } else {
        ftp::Type $ftp_code binary
	 if { [string compare [ftp::NList $ftp_code $remote] $remote] == 0 } {
           # Remote is a file...
           if { [file isdirectory $local] } {
             # Local is a directory
             set local_current_directory pwd
             cd $local
	     if { [proc_ftp_get $ftp_code $remote] == -1 } {
	       puts "ERROR: FTP Get:: Could not fetch \'$remote\'"
               cd $local_current_directory
	       proc_ftp_close $ftp_code
               exit -1
	     }
           } else {
             #local is a file
	     if { [proc_ftp_get $ftp_code $remote $local] == -1 } {
	       puts "ERROR: FTP Get:: Could not get \'$remote\'"
               cd $local_current_directory
	       proc_ftp_close $ftp_code
               exit -1
	     }
           }
         } elseif { [ftp::Cd $ftp_code $remote] == 1 } {
             # remote is a directory
             ftp::Cd $ftp_code $remote
             if { [file isdirectory $local] } {
               # Local is a directory
               set local_current_directory [pwd]
               cd $local
               #puts [ftp::NList $ftp_code]
               foreach remote_file [ftp::NList $ftp_code] {
	         if { [proc_ftp_get $ftp_code $remote_file] == -1 } {
	           puts "ERROR: FTP Get:: Could not get \'$remote_file\'"
                   cd $local_current_directory
	           proc_ftp_close $ftp_code
                   exit -1
	         }
               }
               cd $local_current_directory
             } else {
                 puts "ERROR: FTP Get:: $local is a file --YAAHHHHHH"
	         proc_ftp_close $ftp_code
                 exit -1
	     }
          } else {
            puts "ERROR: FTP Get:: \'$remote\' does not exist"
	    proc_ftp_close $ftp_code
            exit -1
          }
         }
	proc_ftp_close $ftp_code
}



#
# Put file from the local site by FTP
#
proc proc_put_file { server user password local remote }  {
set return_code 0

    if { [file exists $local] == 0 } {
      puts "ERROR: FTP Put:: \'$local\' does not exist. Please create the directory"
      exit -1
    }

    #if { [file isdirectory $local] } {
      #puts " $local is a directory"
    #} else {
      #puts " $local is a file"
    #}
    #puts "Opening Connection to $server ..."
    set ftp_code [proc_ftp_open $server $user $password]
    if { $ftp_code  == -1 } {
       puts "ERROR: FTP Put:: Ftp Connection Failed"
       exit -1
    } else {
         if { [ftp::Cd $ftp_code $remote] == 1 } {
             puts ""
          } else {
            #puts "Directory $remote does not exist"
            set remote_path [file dirname $remote]
            #puts "remote_path = $remote_path"
            set remote_snapdir [file tail $remote]
            #puts "remote_snapdir = $remote_snapdir"
            if { [ftp::Cd $ftp_code $remote_path] == 1 } {
              if { [ftp::MkDir $ftp_code $remote_snapdir] == 1 } {
                if { [ftp::Cd $ftp_code $remote] == 0} {
	           puts "ERROR: FTP Put:: Could not changing dir to \'$remote\'"
	           proc_ftp_close $ftp_code
	           exit -1
                }
              } else {
                 puts "ERROR: FTP Put:: permission to write to \'$remote_path\' denied"
                 exit -1
              }
            } else {
	      puts "ERROR: FTP Put:: Directory path \'$remote_path\' does not exist"
	      proc_ftp_close $ftp_code
	      exit -1
            }
          }

          if { [file isdirectory $local] } {
            #puts "Local is a directory"
            set local_current_directory [pwd]
            cd $local
		# Don't wish to fill the FTP dir with garbage :)
	      if { $local != [pwd] } {
	          puts "ERROR: FTP Put:: Could not cd to local dir \'$local\'"
                  exit -1
              }
              foreach local_file [glob -nocomplain *] {
                #puts $local_file
	        if { [proc_ftp_put $ftp_code $local_file] == -1 } {
	            puts "ERROR: FTP Put:: Error Putting File \'$local_file\'"
                    cd $local_current_directory
	            proc_ftp_close $ftp_code
	            exit -1
	        }
              } 
            cd $local_current_directory
          } else {
            #puts " $local is a file --YAAHHHHHH"
	    if { [proc_ftp_put $ftp_code $local] == -1 } {
	        puts "ERROR: FTP Put:: Error putting file \'$local\'"
	        proc_ftp_close $ftp_code
	        exit -1
	    }
	    proc_ftp_close $ftp_code
	  }
    }
}



#
# List available files from the remote site by FTP
#
proc proc_list_files { server user password remote }  {
set return_code 0

    #puts "$remote"
    ##puts "Opening Connection to $server ..."
    set ftp_code [proc_ftp_open $server $user $password]
    if { $ftp_code  == -1 } {
       puts "ERROR: FTP List:: Ftp Connection Failed"
	exit -1
    } else {
        ftp::Type $ftp_code binary
        if { [ftp::Cd $ftp_code $remote] == 0} {
              puts "ERROR: FTP List:: Remote directory \'$remote\' does not exist"
              proc_ftp_close $ftp_code
              exit -1
        }
        set remote_directory_list [ftp::List $ftp_code $remote]
        set files_list [list]
        foreach remote_directory_name $remote_directory_list {
          if { [regexp {^d(.*)} $remote_directory_name match dir_remote_name] } {
            scan $match %s%s%s%s%s%s%s%s%s s1 s2 s3 s4 s5 s6 s7 s8 s9
             if { [string compare $s9 "."] != 0 } {
              if { [string compare $s9 ".."] != 0 } {
               set remote_subdirectory_list [ftp::List $ftp_code $s9]
               if { [regexp {records.config} $remote_subdirectory_list match] } {
                 lappend files_list $s9
               }
              }
             }
          }
        }
        puts $files_list
	proc_ftp_close $ftp_code
    }
}
	

#-----
#
# Main
#
#-----

set i 0
#puts "Program: $argv0"
#puts "Number of arguments: $argc"
# 
#foreach arg $argv {
        #puts "Arg $i: $arg"
        #incr i
#}

#puts "###############################################"
#puts "  Please do not interrupt the upgrade process"
#puts "###############################################"

set Cmd [lindex $argv 0]
set Hostname [lindex $argv 1]
set User [lindex $argv 2]
set Password [lindex $argv 3]
set Local [lindex $argv 4]
set Remote [lindex $argv 5]

set return_code 0

set EncodedPassword $Password
set EncodedPasswordList [split $EncodedPassword %]
set EncodedPasswordListLength [llength $EncodedPasswordList]
set ii 1
set jj 0
set unm_len [string length $User]
set Password ""
while { [string compare $ii "$EncodedPasswordListLength"] != 0 } {
  set EncodedPasswordListElement [lindex $EncodedPasswordList $ii]
  scan $EncodedPasswordListElement "%d" EncodedPasswordListElement_int
  set user_arg [string index $User [expr {$jj}]]
  scan $user_arg "%c" user_arg_int
  set decode [ format "%c" [expr {$EncodedPasswordListElement_int ^ $user_arg_int} ]]
  append Password $decode
  incr ii
  incr jj
  if { $jj == $unm_len } {
    set jj 0
  }
}

    # Note: These two functionality test are fairly unix-centric (using /bin)

    # Testing out the functionality of 'readdir' will detect presence of
    #  a broken(?) tcl installation. (Finished and works, but commented out.)
#if { [catch {readdir /bin} lsting] } {
#    puts "ERROR: Invalid 'tcl' installation."
#    exit -1
# }

    # And a further test will detect issues associated with another form of
    #  breakage...namely, a sun4 .tcl compiled against ucb headers.
    #  When this happens, it will result in truncated listing elements.
    #  Turns out that I did *not* run into this problem, so no checking code
    #  for now, but perhaps later...
#set tst_glob [ glob /bin/* ]
#if { <some test like 't' in /bin glob?> } {
#    puts "ERROR: Invalid 'tcl' installation."
#    exit -1
# }


if { [ string compare $Remote "." ] == 0 || [string compare $Remote ".."] == 0} {
    puts "ERROR: Invalid Remote directory \'$Remote\'"
    exit -1
 }
 

regexp "^(-)?" $Hostname all hyphen 
if { ([ info exists hyphen ]) && ([string compare $hyphen "-"] == 0) } {
  puts "ERROR: Illegal Hostname \'$Hostname\'. URI cannot start with a -"
  exit -1
}

regexp "(-)?$" $Hostname all hyphen 
if { ([ info exists hyphen ]) && ([string compare $hyphen "-"] == 0) } {
  puts "ERROR: Illegal Hostname \'$Hostname\'. URI cannot end with a -"
  exit -1
}

regexp "(\[^-a-zA-Z0-9.\]+)" $Hostname illegalChars
if { [ info exists illegalChars ] } {
  puts "ERROR: Illegal characters \'$illegalChars\' in Hostname \'$Hostname\'."
  exit -1
}


switch -regexp $Cmd {
  {^g(e(t)?)?$} {
    proc_get_file $Hostname $User $Password $Local $Remote
  }
  {^p(u(t)?)?$} {
    proc_put_file $Hostname $User $Password $Local $Remote
  }
  {^l(i(s(t)?)?)?$} {
    proc_list_files $Hostname $User $Password $Remote
  }
  default {
    #puts "###############################################"
    return -1
  }
}

