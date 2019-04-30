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

Gather up information about the changes for the release. The ``CHANGES`` file
is a good starting point. You may also want to check the commits since the last
release. The primary purpose of this is to generate a list of the important
changes since the last release.

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

#. Check the version in ``configure.ac``. There are two values near the top that
   need to be set, ``TS_VERSION_S`` and ``TS_VERSION_N``. These are the release
   version number in different encodings.

#. Check the variable ``RC`` in the top level ``Makefile.am``. This should be
   the point release value. This needs to be changed for every release
   candidate. The first release candidate is ``0`` (zero).

#. Execute the following commands to make the distribution files. ::

      autoreconf -i
      ./configure
      make rel-candidate

These steps will create the distribution files and sign them using your key.
Expect to be prompted twice for your passphrase unless you use an ssh key agent.
If you have multiple keys you will need to set the default appropriately
beforehand, as no option will be provided to select the signing key. The files
should have names that start with ``trafficserver-X.Y.Z-rcA.tar.bz2`` where
``X.Y.Z`` is the version and ``A`` is the release candidate counter. There
should be four such files, one with no extension and three others with the
extensions ``asc``, ``md5``, and ``sha1``. This will also create a signed git
tag of the form ``X.Y.Z-rcA``.

Distribute
==========

The release candidate files should be uploaded to some public storage. Your
personal storage on *people.apach.org* is a reasonable location to use.

Send the release candidate announcement to the *users* and *dev* mailing
lists, noting that it is a release *candidate* and providing a link to the
distribution files you uploaded. This announcement should also call for a vote
on the candidate, generally with a 72 hours time limit.

If the voting was successful (at least three "+1" votes and no "-1" votes),
proceed to :ref:`release-management-official-release`. Otherwise, repeat the
:ref:`release-management-release-candidate` process.

.. _release-management-official-release:

Official Release
================

Build the distribution files with the command ::

   make release

Be sure to not have changed anything since the release candidate was built so
the checksums are identical. This will create a signed git tag of the form
``X.Y.Z`` and produce the distribution files. Push the tag to the ASF repository
with the command ::

   git push origin X.Y.Z

This presumes ``origin`` is the name for the ASF remote repository which is
correct if you originally clone from the ASF repository.

The distribution files must be added to an SVN repository. This can be accessed
with the command::

   svn co https://dist.apache.org/repos/dist/release/trafficserver <local-directory>

All four of the distribution files go here. If you are making a point release
then you should also remove the distribution files for the previous release.
Allow 24 hours for the files to be distributed through the ASF infrastructure.

The Traffic Server website must be updated. This is an SVN repository which you
can access with ::

   svn co https://svn.apache.org/repos/asf/trafficserver/site/trunk <local-directory>

The files of interest are in the ``content`` directory.

``index.html``
   This is the front page. The places to edit here are any security
   announcements at the top and the "News" section.

``downloads.en.mdtext``
   Update the downloads page to point to the new download objects.

After making changes, commit them and then run ::

   publish.pl trafficserver <apache-id>

On the ``people.apache.org`` host.

If needed, update the Wiki page for the release to point at the release
distribution files.

Update the announcement, if needed, to refer to the release distribution files
and remove the comments concerning the release candidate. This announcement
should be sent to the *users* and *dev* mailing lists. It should also be sent
to the ASF announcement list, which must be done using an ``apache.org`` email
address.

Finally, update various files after the release:

* The ``STATUS`` file for master and for the release branch to include this version.

* The ``CHANGES`` file to have a header for the next version.

* ``configure.ac`` to be set to the next version.

* In the top level ``Makefile.am`` change ``RC`` to have the value ``0``.

