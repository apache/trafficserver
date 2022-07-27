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

.. _roadmap:

ATS Release Roadmap
===================

.. toctree::
   :maxdepth: 1

Apache Traffic Server is currently on a two year major release cycle. At any
given time, we guarantee to have two major versions supported. Please refer to
our `download page <https://trafficserver.apache.org/downloads>`_ to see the
current supported versions.


For details on the actual developer release process, See

Versions, compatibility and schedules
-------------------------------------

1. We promise to make 1 major release every two years, but the RM and
   community can of course make more as necessary.
2. We only make releases off the LTS branches, which are cut every 2 years
   off the master branch.
3. Master is always open, for any type of change (including incompatible
   changes). But don't break compatibility just for fun!
4. Master is always stable, i.e. commits should be properly tested and
   reviewed before committed to master.
5. All releases are stable releases, following strict Semantic Versioning.
6. Minor and patch releases are made at the discretion of the community and
   the RM.
7. Minor releases can include new (small / safe) features, but must be
   compatible within the LTS major version.
8. The LTS cycle, 4 years, does not reset when we make a minor release.
9. The goal is that within a major LTS version, only one minor version is
   continuously supported. For example, if we have made a v9.1.2, and the
   RM makes a v9.2.0 release, do not expect any more releases of v9.1.x.
   The exception here would be serious issues, or security problems.

Current Release Schedule and support
------------------------------------

.. figure:: images/roadmap.png
   :align: center

**Note:** These are examples, only the first minor release number of each
major LTS branch is guaranteed to be made. The dates for point releases
are also for illustration.

How?
----

As you can see, we no longer make any sort of development releases. Also,
there's no longer a backport voting process! The latter means that all
developers and users must make a higher commitment to reviewing changes as
they go into master and the incompatible release branch. Other than that,
it's pretty much business as usual, but easier for everyone involved.
This git branch diagram shows an example how the git tree will be managed:

.. figure:: images/git-versions.svg
   :align: center

Burning release numbers, or how our release process works
---------------------------------------------------------

When we upload a tar ball to VOTE on as a new release and it does not work
out, because something is broken and needs a code-change we will not reuse
the version number. The rationale behind this is the process which guarantees
that what we release and what's in the tree is also what everyone has seen so
far and no code is sneaked in.

If for instance we had a release candidate trafficserver-4.1.4-rc1.tar.bz2
(note the rc1 at the) end, and that vote passed, we'd re-roll the tar-ball to
make sure it will simply be called trafficserver-4.1.4.tar.bz2. But now all
sha1 and md5 sums as well as the GPG signature would also be different.
That's the perfect opportunity to smuggle in some code that no one will
bother to review any more.

Therefore when creating a new release the first thing we do is create a
signed tag and push it. That way everyone can compare that signed tag with
the signed tar-ball that we create from the tag and upload it (usually to
people.apache.org).

Now, when we notice an issue that needs a code-change, we make that on
master, cherry-pick it to the release-branch (optional), and create the new
tag.

Release Managers
----------------

=======  =======  =========  ===========  =========
Version  Primary  Secondary  1st Release  Supported
=======  =======  =========  ===========  =========
8.x      Bryan    Leif       8/2018         1/2022
9.x      Leif     Bryan      1/2020         1/2024
10.x     TBD      TBD        1/2022         1/2026
=======  =======  =========  ===========  =========


Each release manager is responsible for the primary, minor release, as well
as any patch releases within that minor release. Note that patch releases are
primarily for truly critical bugs, and security issues. Don't expect minor
fixes or feature additions in a patch release, those happens on each
quarterly release.
