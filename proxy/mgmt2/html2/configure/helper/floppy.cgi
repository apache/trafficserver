#!/bin/sh

#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
FloppyDrive=`echo $QUERY_STRING | gawk 'BEGIN { FS = "&" } {for (i=1;i<=NF;i=i+1) print $i}' | awk -F'=' '/FloppyDrive/{print $2}' | sed "s/%2F/\//g"`

mounted_drives=0
media_count=0
permission_count=0
scanned_drives_count=0

echo "<tr>"
echo "<td nowrap class=\"bodyText\">"
for drive in `ls /dev/fd?`
do
  scanned_drives_count=`expr $scanned_drives_count + 1`
  escaped_drive=`exec echo $drive | sed 's&/&\\\/&g'`
  umount $drive &> /dev/null
  if mount | grep $drive &> /dev/null
  then
     echo "<tr>"
     echo "<td nowrap class=\"bodyText\">"
     mountDir=`mount | grep $drive | awk '{print $3}'`
     if [ "$mountDir" = "$FloppyDrive" ];
     then
       echo "  <input type=\"radio\" name=\"FloppyDrive\" value=\"$mountDir\" checked>  Floppy - $drive mounted on $mountDir"
     else
       echo "  <input type=\"radio\" name=\"FloppyDrive\" value=\"$mountDir\">  Floppy - $drive mounted on $mountDir"
     fi
     mounted_drives=`expr $mounted_drives + 1`
     echo "</td>"
     echo "</tr>"
  else
     drive_permissions=`exec awk "/$escaped_drive/{print $4}" /etc/fstab | awk '{print $4}' | gawk 'BEGIN { FS = "," } {for (i=1;i<=NF;i=i+1) print $i}' | grep -i "user"`
     if [ -z "$drive_permissions" ]
     then
       owner_drive_permissions=`exec awk "/$escaped_drive/{print $4}" /etc/fstab | awk '{print $4}' | gawk 'BEGIN { FS = "," } {for (i=1;i<=NF;i=i+1) print $i}' | grep -i "owner"`
       owner=`ls -l $drive | awk '{print $3}'`
       if [ "$owner_drive_permissions" -a $owner = "inktomi" ]
       then
          drive_permissions=$owner_drive_permissions
       else
          drive_permissions=""
       fi
     fi
     if [ "$drive_permissions" ]
     then
       umount $drive &> /dev/null
       mount $drive &> /dev/null
       if mount | grep $drive &> /dev/null
       then
         echo "<tr>"
         echo "<td nowrap class=\"bodyText\">"
         mountDir=`mount | grep $drive | awk '{print $3}'`
         #echo "  <input type=\"radio\" name=\"FloppyDrive\" value=\"$mountDir\">  Floppy - $drive mounted on $mountDir"
         if [ "$mountDir" = "$FloppyDrive" ];
         then
           echo "  <input type=\"radio\" name=\"FloppyDrive\" value=\"$mountDir\" checked>  Floppy - $drive mounted on $mountDir"
         else
           echo "  <input type=\"radio\" name=\"FloppyDrive\" value=\"$mountDir\">  Floppy - $drive mounted on $mountDir"
         fi
         mounted_drives=`expr $mounted_drives + 1`
         media_count=`expr $media_count + 1`
         echo "</td>"
         echo "</tr>"
       else
         media_count=`expr $media_count + 1`
       fi
     else
        #media_count=`expr $media_count - 1`
        permission_count=`expr $permission_count + 1` 
     fi
  fi
done;

if [ "$mounted_drives" -eq 0 -a "$media_count" -eq "$scanned_drives_count" ]
then
  echo "&nbsp;&nbsp;No floppy drives found or you don't have permission to mount the disk"
elif [ "$mounted_drives" -eq 0 -a "$media_count" -ne 0 ]; then
  echo "&nbsp;&nbsp;No media found in any of the scanned drives"
elif [ "$mounted_drives" -eq 0 -a "$permission_count" -eq "$scanned_drives_count" ]; then
  echo "&nbsp;&nbsp;Mount access denied. Please mount and unmount the floppy manually"
fi
echo "</td>"
echo "</tr>"
