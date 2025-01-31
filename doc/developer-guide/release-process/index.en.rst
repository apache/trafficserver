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

.. include:: ../../common.defs

.. _developer-release-process:

Release Process
***************

Managing a release is easiest in an environment that is as clean as possible.
For this reason, cloning the code base in to a new directory for the release
process is recommended.

Requirements
============

* A system for git and building.

* A cryptographic key that has been signed by at least two other PMC members.
  This should be preferentially associated with your ``apache.org`` email
  address but that is not required.

.. _release-management-release-candidate:

Release Candidate
=================

The first step in a release is making a release candidate. This is distributed
to the community for validation before the actual release.

Document
========


Create or update a page on the Wiki for the release. If it is a major or minor
release it should have its own page. Use the previous release page as a
template. Point releases should get a section at the end of the corresponding
release page.

Write an announcement for the release. This will contain much of the same
information that is on the Wiki page but more concisely. Check the
`mailing list archives <http://mail-archives.apache.org/mod_mbox/trafficserver-dev/>`_
for examples to use as a base.

Build
=====

#. Go to the top level source directory.

#. Check the version in ``CMakeLists.txt``. There is a ``project`` line near the
   top with the version number.  Make sure that is correct for the release.

#. Generate or update the CHANGELOG for the next release.  ::

      ./tools/git/changelog.pl -o apache -r trafficserver -m X.Y.Z >
      CHANGELOG-X.Y.Z

#. Commit this file to the repository and push it to the release branch.

#. Execute the following commands to make the distribution files where A is the
   next release candidate number (start with 0). ::

      RC=A cmake --preset release
      cmake --build build-release -t rel-candidate

#. Push the release tag to the repository. ::

      git push <upstream repo> tag X.Y.Z-rcA

These steps will create the distribution files and sign them using your key.
Expect to be prompted twice for your passphrase unless you use an ssh key agent.
If you have multiple keys you will need to set the default appropriately
beforehand, as no option will be provided to select the signing key. The files
should have names that start with ``trafficserver-X.Y.Z-rcA.tar.bz2`` where
``X.Y.Z`` is the version and ``A`` is the release candidate counter. There
should be three such files, one with no extension and two others with the
extensions ``asc``,  and ``sha512``. This will also create a signed git
tag of the form ``X.Y.Z-rcA``.

Distribute
==========

The release candidate files should be uploaded to apache dists. ::

   svn co https://dist.apache.org/repos/dist/dev/trafficserver <local-directory>

Create a directory for the next release version if necessary and then add and
commit the release files produced above.

Send the release candidate announcement to the *users* and *dev* mailing
lists, noting that it is a release *candidate* and providing a link to the
distribution files you uploaded. This announcement should also call for a vote
on the candidate, generally with a 72 hours time limit.

The distribution files for the release candidate will be available at::

   https://dist.apache.org/repos/dist/dev/trafficserver/

If the voting was successful (at least three "+1" votes and no "-1" votes),
proceed to :ref:`release-management-official-release`. Otherwise, repeat the
:ref:`release-management-release-candidate` process.

.. _release-management-official-release:

Official Release
================

Build the distribution files with the command ::

   cmake --build build-release -t release

Be sure to not have changed anything since the release candidate was built so
the checksums are identical. This will create a signed git tag of the form
``X.Y.Z`` and produce the distribution files.

The distribution files must be added to an SVN repository. This can be accessed
with the command::

   svn co https://dist.apache.org/repos/dist/release/trafficserver <local-directory>

All three (.tar.bz2, .asc and .sha512) of the distribution files go here. If you are making a point release
then you should also remove the distribution files for the previous release.
Allow 24 hours for the files to be distributed through the ASF infrastructure.

The distribution files for the release will be available at::

   https://dist.apache.org/repos/dist/release/trafficserver/

The Traffic Server website must be updated. There is a git repository which you
can access at ::

   https://github.com/apache/trafficserver-site

The files of interest are in the ``source/markdown`` directory.

``index.html``
   This is the front page. The places to edit here are any security
   announcements at the top and the "News" section.

``downloads.mdtext``
   Update the downloads page to adjust the links, version numbers and dates.

Commiting to the `asf-site` branch will deploy to the trafficserver website
here::

   https://trafficserver.apache.org/


Update the announcement, if needed, to refer to the release distribution files
and remove the comments concerning the release candidate. This announcement
should be sent to the *users* and *dev* mailing lists. It should also be sent
to the ASF announcement list, which must be done using an ``apache.org`` email
address.

Prepare for the next minor release
==================================

Update the version in ``CMakeLists.txt`` by incrementing the minor version
number.  This should be the first commit of the point release after the release
tag.
