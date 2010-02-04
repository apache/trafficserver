#!/bin/sh

#--------------------------------------------------------------------------
# installer.cgi
#
# Agent Installer CGI Shell Script 
#
# This script is invoked from traffic_manager and does
# the following
#    1. Download upgrade_info file from ftp host to /tmp
#    2. Check if upgrade_type is ADDON.  If not quit.
#    3. Download upgrade_file from ftp host to /tmp/agent-installer
#    4. Unzip and untar upgrade_file
#    5. Execute upgrade.sh as a background process
#    6. Store output of upgrade.sh in $HTMLDIR/agent-installer/Mmm-DD-HH-MM-SS.txt file
#

#-

echo "<HTML>"
echo "<H3>Agent Installer Results</H3><P>"

if [ -r etc/trafficserver/records.config.shadow ]
then
    HTMLDIR=`grep "proxy\.config\.admin\.html_doc_root " etc/trafficserver/records.config.shadow | awk '{print $4}'`
    SERVERPORT=`grep "proxy\.config\.http\.server_port " etc/trafficserver/records.config.shadow | awk '{print $4}'`
elif [ -r conf/yts/records.config ]
then
    HTMLDIR=`grep "proxy\.config\.admin\.html_doc_root " etc/trafficserver/records.config | awk '{print $4}'`
    SERVERPORT=`grep "proxy\.config\.http\.server_port " etc/trafficserver/records.config | awk '{print $4}'`
else
    HTMLDIR="../ui"
    SERVERPORT=8080
fi

# Find out what OS we are running on
OS=`uname`

case $HTMLDIR in
    [!/]*) HTMLDIR=`pwd`\/$HTMLDIR
    echo "<p>"
esac


TSDIR="$ROOT"
[ -z "$TSDIR" ] && TSDIR="$INST_ROOT"
if [ "$OS" = "IRIX64" ]
then
    [ -z "$TSDIR" ] && TSDIR=`/usr/bsd/head -1 /etc/traffic_server 2>/dev/null`
else
    [ -z "$TSDIR" ] && TSDIR=`/usr/bin/head -1 /etc/traffic_server 2>/dev/null`
fi
[ -z "$TSDIR" ] && TSDIR="/home/trafficserver"

echo Traffic Server is running in $TSDIR
echo "<p>"

#parse out parameters from  QUERY_STRING 
eval `echo $QUERY_STRING | sed -e 's/'"'"'/%27/g' | \
      awk 'BEGIN { RS="&"; FS="=" }
        $1 ~ /^[a-zA-Z][a-zA-Z0-9_]*$/ {
          printf "QS_%s=%c%s%c\n", $1, 39, $2, 39 }' | 
      sed -e 's/+/ /g' `

echo FTP Server is $QS_ftp_server 
echo "<p>"

QS_ftp_directory=`echo $QS_ftp_directory | sed -e 's/\%2F/\//g'`
QS_ftp_directory=`echo $QS_ftp_directory | sed -e 's/\%7E/\~/g'`

if [ "$QS_ftp_server" = "" ]
then
    echo "<B>Please go back using Back Button and fill in all four fields.</B><P>"
    echo "</HTML>"
    exit
fi

if [ "$QS_ftp_directory" = "" ]
then
    echo "<B>Please go back using Back Button and fill in all four fields.</B><P>"
    echo "</HTML>"
    exit
fi

if [ "$QS_ftp_username" = "" ]
then
    echo "<B>Please go back using Back Button and fill in all four fields.</B><P>"
    echo "</HTML>"
    exit
fi

if [ "$QS_ftp_password" = "" ]
then
    echo "<B>Please go back using Back Button and fill in all four fields.</B><P>"
    echo "</HTML>"
    exit
fi

rm /tmp/upgrade_info > /dev/null
cd /tmp
date >> agent-installer.stamp
echo "<B>"
echo "Spawning ftp to download upgrade_info <p>"
echo "</B>"

echo "<Pre>"

$TSDIR/bin/ftp_via_ts -s $QS_ftp_server -d $QS_ftp_directory -n upgrade_info -m ascii -u $QS_ftp_username -p $QS_ftp_password -c $SERVERPORT

echo "</Pre>"

# Make sure we get upgrade_info
if [ ! -r /tmp/upgrade_info ]
then
    echo "upgrade_info file is not in /tmp or is not readable.  Exiting.<P>"
    echo "This could be caused by a number of reasons."
    echo "The most common causes include <B>Traffic Server not configured to support"
    echo "ftp protocol, /tmp directory full, and "
    echo "ftp transfer failures such as incorrect username or password,"
    echo "incorrect host name or directory name, and non-existance of upgrade_info text"
    echo "file in remote directory</B>.<P>"
    echo "</HTML>"
    exit
fi

UPGRADE_TYPE=`grep upgrade_type upgrade_info | awk '{ print $2 }'`

# INKqa07453: 
# it's possible that the value of UPGRADE_TYPE is never assigned a value
# if the "upgrade_type" string does not exist in upgrade_info file

if [ $UPGRADE_TYPE ]
then
    if [ ! "$UPGRADE_TYPE" = "ADDON" ]
    then
	echo "upgrade_type is not ADDON.  Exiting.<P>"
	echo "upgrade_type in upgrade_info file must be ADDON, spelled exactly like shown.<P>"
	echo "</HTML>"
	exit
    fi
else 
    echo "<B>FTP Error:</B><BR>"
    FTP_ERROR=`grep Description upgrade_info | awk '{ print $0 }'` 
  
   if [ "$FTP_ERROR" = "Binary file upgrade_info matches" ]
   then
    # if thinks upgrade_info is binary file, -a option forces grep 
    # to regard it as text file, eg. on LINUX
	FTP_ERROR=`grep -a Description upgrade_info | awk '{ print $0 }'` 
    fi

    echo "<I>$FTP_ERROR</I>"
    echo "</HTML>"
    exit
fi


echo "<B>Contents of upgrade_info file</B><P>"
echo "<Pre>"
cat upgrade_info
echo "</Pre>"

UPGRADE_FILE=`grep upgrade_file upgrade_info | awk '{ print $2 }'`
#echo $UPGRADE_FILE

if [ "$UPGRADE_FILE" = " " ]
then
    echo "upgrade_file not defined.  Exiting.<P>"
    echo "Agent Installer looks for filename provided by upgrade_file "
    echo "key word in upgrade_info file.  This field must exist for "
    echo "Agent Installer to work.<P>"
    echo "</HTML>"
    exit
fi

#TEMPDIR=`date | awk '{print $2"-"$3"-"$4}' | sed -e 's/:/-/g'`.dir
#TEMPDIRFULL=/tmp/agent-installer/$TEMPDIR
INSTALL_DIR="$ROOT"
[ -z "$INSTALL_DIR" ] && INSTALL_DIR="$INST_ROOT"
[ -z "$INSTALL_DIR" ] && INSTALL_DIR=`/usr/bin/head -1 /etc/traffic_server 2>/dev/null`
[ -z "$INSTALL_DIR" ] && INSTALL_DIR="/home/trafficserver"

CDS_DIR=$INSTALL_DIR/cds-agent

# make sure /tmp/agent-installer exists
#mkdir /tmp/agent-installer > /dev/null

if [ -d $CDS_DIR ]
then
   rm -rf $CDS_DIR
   mkdir $CDS_DIR
else
   mkdir $CDS_DIR
fi

if [ ! -d $CDS_DIR ]
then
    echo "<B>"
    echo "Cannot create cds-agent directory " 
    echo $CDS_DIR
    echo ". Exiting. <P>"
    echo "<B>"
    echo "</HTML>"
    exit
else
    echo "<B>"
    echo "CDS directory is "
    echo $CDS_DIR
    echo "</B><P>"
fi

cd $CDS_DIR

echo "<B>"
echo "Downloading and installing $UPGRADE_FILE<P>"
echo "</B>"

echo "<Pre>"

$TSDIR/bin/ftp_via_ts -s $QS_ftp_server -d $QS_ftp_directory -n $UPGRADE_FILE -m binary -u $QS_ftp_username -p $QS_ftp_password -c $SERVERPORT

echo "</Pre>"

echo "<B>"
echo "Unpacking and installing components <p>"
echo "</B>"
echo "<Pre>"

if [ ! -r $UPGRADE_FILE ]
then
    echo "<B>"
    echo "$UPGRADE_FILE does not exist. Exiting. "
    echo "<B>"
    echo "This could be caused by a number of reasons."
    echo "The most common causes include <B>Traffic Server not configured to support"
    echo "ftp protocol, /tmp directory full, and "
    echo "ftp transfer failures such as incorrect username or password,"
    echo "incorrect host name or directory name, and non-existance of "
    echo "file specified by upgrade_file key in remote directory</B>.<P>"
    echo "</HTML>"
    exit
fi

echo "unziping the package"
if [ "$OS" = "Linux" ]
then
    /bin/gunzip *
elif [ "$OS" = "linux" ]
then
    /bin/gunzip *
elif [ "$OS" = "HP-UX" ]
then
    /usr/contrib/bin/gunzip *
elif [ "$OS" = "IRIX64" ]
then
    /usr/sbin/gunzip *
else
    /usr/local/bin/gunzip *
fi

echo
echo "untaring the package" 
tar -xvf *

echo "</Pre>"


# run upgrade.sh in the gackground, and exit
# INKqa10664: change file exentsion from .txt to .html so that it can be displayed as web page
LOGFILE=`date | awk '{print $2"-"$3"-"$4}' | sed -e 's/:/-/g'`.html
LOGFILEFULL=$HTMLDIR/agent-installer/$LOGFILE
LOGURL="agent-installer/$LOGFILE"

if [ -x upgrade.sh ]
then
    echo "<B>Spawning upgrade.sh</B>"
    ./upgrade.sh > $LOGFILEFULL &
    PID=$!
#    echo "pid: $PID"

    # at most, loops 12 times, for a total of 60 sec. 
    count=0
    while [ $count -le 12 ]
    do
	check1=`ps -ef | grep upgrade.sh |  grep -v grep |  awk '{print $2}'`
#	echo "STATE: $check1"
	# if the string is null, then upgrade.sh process is done; exit loop
	if [ -z "$check1" ]
	then
	    break
	fi
	count=$[count+1]
#	echo "COUNT: $count "
	sleep 5
    done

    # kill the upgrade process if still alive
    check2=`ps -ef | grep upgrade.sh |  grep -v grep |  awk '{print $2}'`
    # if the string is not null, then upgrade.sh process is still alive
    if [ -n "$check2" ]
    then
	kill -9 $PID
	echo "<BR>Unsuccessful installation. The upgrade.sh timed out.<BR>"
    fi
else
    echo "<B>"
    echo There is no upgrade.sh.  Exiting.
    echo "<B>"
    echo "This could be caused by a number of reasons."
    echo "The most common causes include "
    echo $UPGRADE_FILE
    echo " not properly downloaded from the ftp server, "
    echo "non-existance of gunzip and tar tools in proper directories, "
    echo "insufficient space left in /tmp directory, and "
    echo "upgrade.sh file being absent in UPGRADE_FILE package.<P>"
    echo "</HTML>"
    exit
fi

# CLEANUP: remove the cds-agent directory
if [ -d $CDS_DIR ]
then
    rm -rf $CDS_DIR
    echo "<P>"
    echo "<B>Removed <I>$CDS_DIR</I>.<B>"
fi

echo "<P>"
echo "Results of the installation are available in <I>$LOGFILEFULL</I> file"
echo 'and at this <A HREF='
echo $LOGURL
echo '>URL</A>.'
echo "<P>"
echo "</HTML>"

exit 0
