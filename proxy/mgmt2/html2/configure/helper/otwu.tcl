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

source $tcl_library/base64.tcl
source $tcl_library/tls.tcl
load   $tcl_library/libtls1.4.so
source $tcl_library/md5.tcl
source $tcl_library/ftp.tcl
source $tcl_library/http.tcl
source $tcl_library/otw_upgrade.tcl
package require otwu


#######################################################################################
# HtmlRndr Procedures
#######################################################################################

proc HtmlRndr { strings } {
    puts "$strings\n";
}

proc HtmlRndrError { msg } {
    global Server
    HtmlRndr "<tr>";
    HtmlRndr "<td class=bodyText colspan=6 nowrap>Error: $msg</td>";
    HtmlRndr "</tr>";
}


#######################################################################################
# Stage I - Get & Display package_list
#######################################################################################

proc HtmlRndrHiddenInputI { url username password action } {
    HtmlRndr "<input type=hidden name=Server value=\"$url\">";
    HtmlRndr "<input type=hidden name=Username value=\"$username\">";
    HtmlRndr "<input type=hidden name=Password value=\"$password\">";
    HtmlRndr "<input type=hidden name=UpgradeType value=\"$action\">";
}

proc HtmlRndrNoPackage { } {
    HtmlRndr "<tr>";
    HtmlRndr "<td class=monitorLabel colspan=6>Package</td>";
    HtmlRndr "</tr>";
    HtmlRndr "<tr>";
    HtmlRndr "<td class=bodyText colspan=6 nowrap>No package is available.</td>";
    HtmlRndr "</tr>";
}

proc HtmlRndrHelpText { } {
    HtmlRndr "<tr>";
    HtmlRndr "  <td class=helpBg colspan=2>";
    HtmlRndr "    <font class=configureBody>";
    HtmlRndr "Please choose a package to download.";
    HtmlRndr "Press 'Next' to start downloading the package and this may take a few minutes."
    HtmlRndr "    </font>";
    HtmlRndr "  </td>";
    HtmlRndr "</tr>";
}

proc HtmlRndrSelectionHeader { } {
    HtmlRndr "<tr align=center>";
    HtmlRndr "  <td class=monitorLabel>Package</td>";
    HtmlRndr "  <td class=monitorLabel>Version</td>";
    HtmlRndr "  <td class=monitorLabel>File</td>";
    HtmlRndr "  <td class=monitorLabel>Required Size</td>";
    HtmlRndr "  <td class=monitorLabel width=100%>Description</td>";
    HtmlRndr "  <td class=monitorLabel>&nbsp;</td>";
    HtmlRndr "</tr>";
}

proc HtmlRndrSelection { plist ptype } {
    # if no package available
    if { [llength $plist] == 0 } {
	HtmlRndrNoPackage;
    } else {
	set i 0
	foreach pkg $plist {
	    if { [ string compare $ptype "application" ] == 0 } {
		if { [regexp {^(TS|TS/MIXT):([-0-9\.]+):(.*):([0-9]+):(.*)} $pkg match title version fname size desc] != 1 } {
		    continue
		}
	    } else {
		if { [regexp {^(TS|TS/MIXT|RedHat):([-0-9\.]+):(.*):([0-9]+):(.*)} $pkg match title version fname size desc] != 1 } {
		    continue
		}
	    }
	    HtmlRndr "<tr>";
	    HtmlRndr "  <td class=bodyText>$title</td>";
	    HtmlRndr "  <td class=bodyText>$version</td>";
	    HtmlRndr "  <td class=bodyText>$fname</td>";
	    HtmlRndr "  <td class=bodyText>$size</td>";
	    HtmlRndr "  <td class=bodyText>$desc</td>";
	    
	    if { $i == 0 } {
		HtmlRndr "  <td><input type=radio name=TargetPackage value=\"$pkg\" checked></td>";
	    } else {
		HtmlRndr "  <td><input type=radio name=TargetPackage value=\"$pkg\"></td>";
	    }
	    HtmlRndr "</tr>";
	    incr i
	}
    }
}

proc HtmlRndrButtonI { } {
    HtmlRndr ""
    HtmlRndr "<table width=100% border=0 cellspacing=0 cellpadding=3>"
    HtmlRndr "  <tr class=\"secondaryColor\">"
    HtmlRndr "    <td width=100% nowrap>&nbsp;</td>"
    HtmlRndr "    <td>&nbsp;</td>"
    HtmlRndr "    <td><input class=\"configureButton\" type=submit name=\"next\" value=\"Next\" onclick=\"Status()\"></td>"
    HtmlRndr "  </tr>"
    HtmlRndr "</table>"
}

proc StageI { } {
    global env

    ##### import env variables needed #####
    set url          $env(Server)
    set username     $env(Username)
    set password     $env(Password)
    set action       $env(UpgradeType)
    set package_type $env(PackageType)

    # get a list of packages available for reinstall/upgrade
    set list [ otwu::get_package_list $url $username $password $action ]
    if { $list == -1 } {
	# display connection error message
	HtmlRndr "<table width=100% border=0 cellspacing=0 cellpadding=10>"
	HtmlRndrError "Unable to locate or login to download server \"$url\".  Please try again.";
	HtmlRndr "</table>"

    } else {
	# display list of available package
	HtmlRndr "<table width=100% border=0 cellspacing=0 cellpadding=3>"
	HtmlRndr "<tr class=secondaryColor><td width=100% nowrap>&nbsp;</td></tr></table>"
	HtmlRndr "<table width=100% border=0 cellspacing=0 cellpadding=10>"
	HtmlRndrHelpText
	HtmlRndr "<tr><td><table width=100% cellspacing=0 cellpadding=3 border=1 bordercolor=#cccccc>";
	HtmlRndrSelectionHeader
	HtmlRndrSelection $list $package_type
	HtmlRndrHiddenInputI $url $username $password $action
	HtmlRndr "</table></td></tr>";
	HtmlRndr "</table>"
        HtmlRndrButtonI
    }
}

#######################################################################################
# Stage II - Get package_target & Display confirmation
#######################################################################################

proc HtmlRndrHiddenInputII { action working_dir } {
    HtmlRndr "<input type=hidden name=UpgradeType value=\"$action\">";
    HtmlRndr "<input type=hidden name=working_dir value=\"$working_dir\">";
}

proc HtmlRndrConfirmationDefault { } {
    HtmlRndr "  <tr>"
    HtmlRndr "    <td align=center>"
    HtmlRndr "      <pre class=bodyText>"
    HtmlRndr "        When ready, hit 'Apply' to continue or 'Cancel to abort."
    HtmlRndr "      </pre>"
    HtmlRndr "    </td>"
    HtmlRndr "  </tr>"
}

proc HtmlRndrConfirmation { file_handle } {
    HtmlRndr "  <tr>"
    HtmlRndr "    <td align=center>"
    HtmlRndr "      <pre class=bodyText>"
    HtmlRndr [read $file_handle]
    HtmlRndr "      </pre>"
    HtmlRndr "    </td>"
    HtmlRndr "  </tr>"
}

proc HtmlRndrButtonII { } {
    HtmlRndr ""
    HtmlRndr "<table width=100% border=0 cellspacing=0 cellpadding=3>"
    HtmlRndr "  <tr class=\"secondaryColor\">"
    HtmlRndr "    <td width=100% nowrap>&nbsp;</td>"
    HtmlRndr "    <td><input class=\"configureButton\" type=submit name=\"action\" value=\"Apply\"></td>"
    HtmlRndr "    <td><input class=\"configureButton\" type=submit name=\"action\" value=\"Cancel\"></td>"
    HtmlRndr "  </tr>"
    HtmlRndr "</table>"
}

proc StageII { } {
    global env

    ##### import env variables needed #####
    set url          $env(Server)
    set username     $env(Username)
    set password     $env(Password)
    set action       $env(UpgradeType)
    set package      $env(TargetPackage)
    set working_dir /tmp/inktomi/Upgrade

    # get upgrade.info for display
    set info_file [ otwu::get_info_file $url $username $password $package ]
    if { $info_file == -1 } { 
	# display error message
	HtmlRndr "<table width=100% border=0 cellspacing=0 cellpadding=10>"
	HtmlRndrError "Unable to download package.  Please make sure there is enough space in the machine to upgrade."
	HtmlRndr "</table>"

    } else {	
	HtmlRndr "<table width=100% border=0 cellspacing=0 cellpadding=10>"
	HtmlRndrHiddenInputII $action $working_dir
	if { $info_file == "" } {
	    # display default confirmation text
	    HtmlRndrConfirmationDefault
	    HtmlRndr "</table>"
	    HtmlRndrButtonII
	    
	} elseif { [ file exists $info_file ] } {
	    # display confirmation and confirmation button
	    set info_file_handle [ open $info_file r ]
	    HtmlRndrConfirmation $info_file_handle
	    HtmlRndr "</table>"
	    HtmlRndrButtonII
	    
	} else {
	    HtmlRndrError "Unable to locate .info file."
	    HtmlRndr "</table>"
	}
    }
}

#######################################################################################
# Stage III - Start reinstall/upgrade
#######################################################################################

proc StageIII { } {
    global env

    ##### import env variables needed #####
    set action       $env(action)
    set upgrade_type $env(UpgradeType)
    
    otwu::get_started $action $upgrade_type
}

#######################################################################################
# Main - switch between stages based on 'submit_from_page'
#######################################################################################

##### define worldwide globals #####
global env

##### import env variables needed #####
set submit_from_page $env(submit_from_page)
switch -regexp $submit_from_page {

    {^/configure/c_otw_upgrade.ink$} { StageI; }
    {^/configure/c_otw_package.ink$} { StageII; }
    {^/configure/c_otw_confirm.ink$} { StageIII; }

}
return 0;
