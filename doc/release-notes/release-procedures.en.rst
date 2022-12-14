.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../common.defs

.. _release-procedures:

ATS Release Procedure Steps
===========================

   - Make sure all commits are in the branch being worked on
   - Update the configure.ac and trafficserver.spec version numbers
   - Run `make changelog` to generate a new changelog file for the new versrion
   - Commit the new changelog, configure.ac and trafficserver.spec files
   - Run `make rel-candidate`, this will generate the tar files and create a RC git tag
   - Push the new tag, `git push origin <tag_name>`
   - Upload the tar files to your own people.apache.org for hosting
   - Have someone double check the build, sha, etc.
   - Call for a vote on the RC

   - After the vote has passed update the tar files to remove the RC and update the sha value
   - Publish to apache releases

   ::

      svn checkout https://dist.apache.org/repos/dist/release/trafficserver/ 
      cd trafficserver 
      mv ../trafficserver-${version}.tar* . 
      svn add trafficserver-${version}.tar* 
      svn commit . -m "Release $version"
      cd .. 
      rm -rf trafficserver

   - Publish the updated version information to the TrafficServer website
