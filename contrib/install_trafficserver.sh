#!/bin/bash
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
# Quick Build/Test for TrafficServer
# Tested on Ubuntu Karmic 9.10, EC2 Friendly with official Canonical AMIs
# Author: Jason Giedymin
# Desc: The intent with this script is to unify a single build script with
#       standard options for testing.
#
# Version Information:
#    v0.1.1a - Added Debug
#    v1.0.0  - Stable Release
#    v1.0.1  - Added sqlite dev lib.
#    v1.0.2  - EC2 Note about ephemeral storage
#            - Create ephemeral storage by mimicking on non EC2 systems.
#    v1.0.3  - Added fedora to list of supported distros
#    v1.0.4  - Added fedora EC2 compatibility
#            - Added EC2 detection, option

# It's safe to use this in a non-ec2 environment. This directory will be
# created if it doesn't exist. If your running this on EC2, it's best not
# to change this location as this is the ephemeral drive setup by Amazon.
# For non-ec2 environments, you may change this to any location you
# desire.
EC2_EPHEMERAL=/mnt
PROJECT=trafficserver

BRANCH=traffic/trunk
SVN_LOC=traffic-trunk.svn
SVN_HOME=http://svn.apache.org/repos/asf/incubator

FALSE=0
TRUE=1
DEBUG=$FALSE
USING_EC2=$FALSE
BUILD_HOME=/usr/local
PREFIX=--prefix=$BUILD_HOME
BUILD_OPTIONS="$PREFIX"
BUILD_OPTIONS_DEBUG="--with-user=root --with-group=root --enable-debug"
BUILD_OPTIONS_FC8_EC2="--disable-eventfd"
CONFIGURE_OPTIONS=""
FULL_BUILD_PATH=$EC2_EPHEMERAL/$PROJECT/$SVN_LOC

SUSE="suse"
FEDORA="fedora"
REDHAT="redhat" # also exists on Fedora
UBUNTU="ubuntu"
DEBIAN="debian" # also exists on Ubuntu
SLACKWARE="slackware"


function killAll() {
    killall traffic_cop
    killall traffic_manager
    killall traffic_server
}

function updateInstall() {
    if [ "$DISTRIB_ID" = "$UBUNTU" ]; then
        apt-get update
        apt-get install -y g++ autoconf \
        make \
        libtool \
        libssl-dev \
        tcl-dev \
        libpcre3-dev \
        curl
        apt-get install -y subversion git git-svn
    elif [ "$DISTRIB_ID" = "$FEDORA" ]; then
        yum update
        yum upgrade

        # Chose not to use kernel version here as FC8 xen needs more than just eventfd work
        if [ $USING_EC2 = $TRUE ]; then
            yum -y install subversion \
                git \
                autoconf \
                automake \
                libtool \
                gcc-c++ \
                glibc-devel \
                openssl-devel \
                tcl-devel \
                db4-devel \
                pcre \
                pcre-devel
        elif [ $USING_EC2 = $FALSE ]; then
            yum -y install subversion \
                git \
                autoconf \
                automake \
                libtool \
                gcc-c++ \
                glibc-devel \
                openssl-devel \
                tcl-devel \
                pcre \
                pcre-devel
        fi
    fi
}

function cleanUp() {
    if [ -e $EC2_EPHEMERAL/$PROJECT ]; then
            rm -R $EC2_EPHEMERAL/$PROJECT
    fi

    if [ ! -d $EC2_EPHEMERAL ]; then
        mkdir -p $EC2_EPHEMERAL
        cd $EC2_EPHEMERAL
    fi
}

function svnCheckout() {
    #----------------SVN Only------------------------------
    svn checkout   $SVN_HOME/$PROJECT/$BRANCH   $FULL_BUILD_PATH
    cd $FULL_BUILD_PATH
    #------------------------------------------------------
}

#This is just for some dev/testing, and still in the 'works'
function dev() {
    #----------------Git Only------------------------------
    git clone git://git.apache.org/trafficserver.git
    cd $EC2_EPHEMERAL/$trafficserver

    #swtich to dev build
    git checkout -b remotes/origin/dev
    #------------------------------------------------------
}

function handleGroups() {

    # maybe someday some extra security can be put in around here
    # and yeah, force add the group
    if [ "$DISTRIB_ID" = "$UBUNTU" ]; then
        addgroup nobody
    elif [ "$DISTRIB_ID" = "$FEDORA" ]; then
        groupadd nobody
    fi
}

function getConfigureOptions() {
    configureOptions="$BUILD_OPTIONS"

    if [ $DEBUG = $TRUE ]; then
        configureOptions="$configureOptions $BUILD_OPTIONS_DEBUG"
    fi

    if [ $USING_EC2 = $TRUE ]; then
        if [ "$DISTRIB_ID" = "$FEDORA" ]; then
            configureOptions="$configureOptions $BUILD_OPTIONS_FC8_EC2"
        fi
    fi

    CONFIGURE_OPTIONS=$configureOptions
}

function rebuild() {
    # remake, clean, uninstall first

    if [ ! -d $FULL_BUILD_PATH ]; then
        echo "Can't find $FULL_BUILD_PATH, cannot continue!";
        exit 1;
    fi

    handleGroups

    cd $FULL_BUILD_PATH
    autoreconf -i --force
    ./configure $CONFIGURE_OPTIONS

    make clean

    # Here is where things are dumb.  We don't check for
    # successful builds yet.  Thats in the next release.
    # This is why I call it dumb.
    make
    make uninstall
    make install
}

function postMods() {
    # Flag verbose on, we like verbose
    if [ -e /etc/default/rcS ]; then
        sed -i 's/VERBOSE=no/VERBOSE=yes/g' /etc/default/rcS
    fi

    # Link the script for init purposes, makes things nice
    if [ -e $BUILD_HOME/bin/trafficserver ]; then
        ln -s -f $BUILD_HOME/bin/trafficserver /etc/init.d/trafficserver
    fi
}

function freshBuild() {
    clear
    echo "Starting TrafficServer Install (dumb) process..."

    killAll
    updateInstall
    cleanUp
    svnCheckout
    rebuild
    postMods

    echo;
    echo "TrafficServer Install (dumb) process complete."
}

function forceBuild() {
    clear
    echo "Starting Build Only..."

    killAll
    updateInstall
    rebuild
    postMods

    echo "Build complete."
}

function flipDebug() {
    if [ $DEBUG = $TRUE ]; then
        DEBUG=$FALSE
    elif [ $DEBUG = $FALSE ]; then
        DEBUG=$TRUE
    fi

    getConfigureOptions;
}

function flipEC2() {
    if [ $USING_EC2 = $TRUE ]; then
        USING_EC2=$FALSE
    elif [ $USING_EC2 = $FALSE ]; then
        USING_EC2=$TRUE
    fi

    getConfigureOptions;
}

# Crude but it works without complex regex, and some people remove ec2/ami tools for security...
function detectEC2() {
    if [ -e /etc/ec2_version ]; then #UBUNTU
        USING_EC2=$TRUE
    elif [ -e /etc/ec2/release-notes ]; then #FEDORA
        USING_EC2=$TRUE
    fi
}

function askUser() {
usage;
echo;

usageLine;

read -p "" RESPONSE

if [ "$RESPONSE" = "freshBuild" ]; then
    $RESPONSE;
    exit 0;
elif [ "$RESPONSE" = "forceBuild" ]; then
    $RESPONSE;
    exit 0;
elif [ "$RESPONSE" = "flipDebug" ]; then
    $RESPONSE;
    askUser;
elif [ "$RESPONSE" = "flipEC2" ]; then
    $RESPONSE;
    askUser;
elif [ "$RESPONSE" = "EXIT" ]; then
    echo "Exiting NOW!"
    exit 0;
else
    #usageLine;
    askUser;
    return 1;
fi

}

function usage() {
    clear
    echo;
    echo 'This script is used for doing quick builds & Tests for TrafficServer.';
    echo;
    displayInfo;
    echo;
    echo "Commands:";
    echo 'freshBuild: Checkout from svn, build and install.';
    echo 'forceBuild: Do a build from previous checked out source.';
    echo 'flipDebug: Flip the current debug mode.';
    echo 'flipEC2: Flip the current EC2 mode.';
    echo 'EXIT: Exit now!';
    echo;
}

function displayInfo() {
    #Would like to make these editable in the next release.

    echo "-----------------------------------------------------------------------"
    echo "                         Current Options                               "
    echo "-----------------------------------------------------------------------"
    echo "                 OS: $DISTRIB_ID"

    if [ $DEBUG = $TRUE ]; then
        echo "         Debug Mode: ON"
        else
        echo "         Debug Mode: OFF"
    fi

    if [ $USING_EC2 = $TRUE ]; then
        echo "           EC2 Mode: ON"
        else
        echo "           EC2 Mode: OFF"
    fi

    echo "    Source checkout: $EC2_EPHEMERAL/$PROJECT"
    echo "             Branch: $BRANCH"
    echo "         SVN Server: $SVN_HOME"
    echo "  Configure Options: $CONFIGURE_OPTIONS"
    echo "    Full Build Path: $FULL_BUILD_PATH"
    echo "-----------------------------------------------------------------------"
}

function usageLine() {
    echo;
    echo "You can  access the menu by calling this script or by command line."
    echo "Menu usage and choices are: {freshBuild|forceBuild|flipDebug|flipEC2|EXIT}."
    echo "Command line usage choices are: {freshBuild|forceBuild|freshDebugBuild|EXIT}."
    echo;
    echo "Notes:"
    echo " - the command line has a strict debug build option."
    echo " - when using the command line build, EC2 detection is automatic."
    echo;
}

#------------Main------------

if [ $UID != 0 ] ; then
    echo "Must have root permissions to execute."
    exit 1
fi

if [ -e /etc/SuSE-release ]; then
    DISTRIB_ID=$SUSE
elif [ -e /etc/fedora-release ]; then
    DISTRIB_ID=$FEDORA
elif [ -e /etc/redhat-release ]; then
    DISTRIB_ID=$REDHAT # also exists on Fedora
elif [ -e /etc/lsb-release ]; then
    DISTRIB_ID=$UBUNTU
elif [ -e /etc/debian-version ]; then
    DISTRIB_ID=$DEBIAN # also exists on Ubuntu
elif [ -e /etc/slackware-version ]; then
    DISTRIB_ID=$SLACKWARE
fi

detectEC2;
getConfigureOptions;
displayInfo;

case "$1" in
  freshBuild)
    $1;
    ;;
  forceBuild)
    $1;
    ;;
  freshDebugBuild)
    $DEBUG=$TRUE
    freshBuild;
    ;;
  EXIT)
    echo 'Exiting...';
    exit 0;
    ;;
  *)
    askUser;
    ;;
esac

exit 0;
