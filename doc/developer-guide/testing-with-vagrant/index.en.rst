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

.. _developer-testing-with-vagrant:

Using Vagrant to Test |TS|
**************************

The |ATS| project's official repository includes a Vagrantfile, intended to ease
the process of creating environments suitable for building and testing |TS|,
where all the necessary dependencies are installed automatically for a variety
of operating systems and common distribution releases.

.. epigraph::

   Vagrant is a tool for building complete development environments. With an
   easy-to-use workflow and focus on automation, Vagrant lowers development
   environment setup time, increases development/production parity, and makes
   the "works on my machine" excuse a relic of the past.

   -- `VagrantUp website <https://www.vagrantup.com/about.html>`_

Vagrant can be used in combination with any of the popular configurtion
management and automation tools, such as `Chef <https://www.chef.io/chef/>`_,
`Puppet <https://puppetlabs.com/>`_, `Ansible <http://www.ansible.com/home>`_,
and more. The Vagrantfile included in the |TS| repository happens to make use
of Puppet.

Installing Vagrant and Dependencies
===================================

VirtualBox
----------

The virtualization software `VirtualBox <https://www.virtualbox.org/>`_ is
required to create and run the virtual machines created by the included project
Vagrantfile.

VirtualBox can be obtained by free from the official website, and many
distributions provide their own packages as well. No special configuration of
the software is required.

Vagrant
-------

A fairly recent version of `Vagrant <https://www.vagrantup.com/downloads.html>`_
is necessary to use the included Vagrantfile. While older versions of Vagrant
could be installed through the Ruby Gems facility, modern versions are only
provided as distribution specific packages.

NFS Server
----------

The project Vagrantfile uses the NFS shared folders support of VirtualBox to
mount the same directory in which the Vagrantfile is located on your host
machine as a network directory inside the virtual machine. For this to work,
your host machine must have an NFS server installed and running, and the user
under which you run the vagrant commands must have sudo permissions to modify
the NFS exports configuration and restart the NFS service.

The virtual machine created by Vagrant will still function without a working
NFS server on your host machine, but you will not be able to access the shared
folder which includes the entire |TS| source tree. You may opt to modify the
Vagrantfile to use a method other than NFS, as per the `Vagrant documentation
<https://docs.vagrantup.com/v2/synced-folders/basic_usage.html>`_.

Managing Virtual Machines
=========================

Listing Available Machines
--------------------------

The included Vagrantfile defines many variations of operating systems,
releases, and architectures. To see a complete list of the virtual machine
options available to you, run the command ``vagrant status`` from within the
same directory as the Vagrantfile. The command may take a few moments to run as
the configurations defined in the Vagrantfile are evaluated, and calls are made
to the underlying VirtualBox utilities to check for the existence and
operational state of each possibility.  You should expect to see output along
the lines of::

    $ vagrant status
    Current machine states:

    saucy32                   not created (virtualbox)
    raring32                  not created (virtualbox)
    quantal32                 not created (virtualbox)
    precise32                 not created (virtualbox)
    trusty32                  not created (virtualbox)
    saucy64                   not created (virtualbox)
    raring64                  not created (virtualbox)
    quantal64                 not created (virtualbox)
    precise64                 not created (virtualbox)
    trusty64                  running (virtualbox)
    freebsd                   not created (virtualbox)
    omnios                    not created (virtualbox)
    lucid64                   not created (virtualbox)
    fedora18                  not created (virtualbox)
    centos63                  not created (virtualbox)
    centos59                  not created (virtualbox)
    centos64                  not created (virtualbox)
    debian7                   not created (virtualbox)
    sles11                    not created (virtualbox)
    oel63                     not created (virtualbox)

    This environment represents multiple VMs. The VMs are all listed
    above with their current state. For more information about a specific
    VM, run `vagrant status NAME`.

Creating and Destroying
-----------------------

Creation and destruction of virtual machines with Vagrant is very simple. To
bring a new virtual machine into existence, run the following command from the
same directory in which the Vagrantfile is located::

    vagrant up <name>

Where ``<name>`` should be the specific operating system release you wish to
use for the virtual machine. For example, to test |TS| in a CentOS 6.4
environment, you would run::

    vagrant up centos64

Running the ``vagrant up`` command for a virtual machine which does not exist
yet (or has previously been deleted) will create a brand new virtual machine,
using the appropriate image (called a *basebox* in Vagrant parlance), as well as
provision the machine according to any configuration management rules specified
in the Vagrantfile.

Similarly, you may destroy the virtual machine when you are finished by running
the command::

    vagrant destroy <name>

Or if you wish to only stop the virtual machine temporarily without deleting it,
you may run::

    vagrant halt <name>

A halted virtual machine is started back up with the same ``vagrant up`` command
shown earlier. The difference is that Vagrant will recognize the box already
exists and do nothing more than start the vm process and configure the virtual
networking interfaces on your host. Any configuration management hooks in the
Vagrantfile will be skipped.

Logging In
----------

Logging into a virtual machine created with Vagrant may be accomplished in a
couple different ways. The easiest is to let Vagrant itself figure out where the
machine is and how to properly authenticate you to it::

    vagrant ssh <name>

Using that command from within the same directory as the Vagrantfile allows you
to skip figuring out what virtual network interface has been attached to the
machine, what local port may be assigned to handle SSH forwarding, and so on.
As long as the virtual machine was already running, you will be quickly dropped
into a local shell in the virtual machine as the ``vagrant`` user.

.. important::

   Vagrant by default uses a widely-shared private RSA key on newly created
   virtual machines (that are built on public basebox images). The default user
   on these baseboxes is also configured for password-less sudo permissions.
   This is very clearly insecure, but Vagrant is designed for local testing and
   development, not production (or other public) uses, so the project has made
   the philosophical trade-off in favor of ease of use.

Alternatively, you may SSH directly to the virtual machine. Because the virtual
machines are configured to use only the private virtual network layer provided
by VirtualBox, you cannot directly. Instead, VirtualBox has created a local port
mapping automatically which should be used. There is no fixed, pre-determined
port mapping that will be universally valid, as Vagrant and VirtualBox may be
used together to run an arbitrary number of virtual machines simultaneously,
each provisioned in any order, and defined by any number and variety of
Vagrantfiles.

The correct way to determine what port Vagrant and VirtualBox have used to map
to a given virtual machine is to run the following command from within the same
directory as your Vagrantfile::

    vagrant ssh-config <name>

That will output a configuration block, suitable for inclusion in your local
``~/.ssh/config`` file. Note specifically, in addition to the port, the path to
the private key you will need to use as your identity when attempting to log
into the virtual machine.

Shared Host Folders
-------------------

VirtualBox provides a facility for mounting directories from your host machine
as filesystems inside the virtual machines. The |TS| Vagrantfile makes use of
this feature to mount its own source tree in a predictable location in the
virtual environment.

Multiple methods are available for this, including NFS, CIFS, and simulated
block devices. The |TS| project opts to use NFS for its simplicity, speed,
support for features such as symlinks, and wide interoperability across the
various guest operating systems included in the Vagrantfile. Within the included
Vagrantfile, you can see the following line::

    config.vm.synced_folder ".", "/opt/src/trafficserver.git", :nfs => true

This directs VirtualBox to mount the directory in which the |TS| Vagrantfile
resides as an NFS mount inside the virtual machine at the path
``/opt/src/trafficserver.git``. Additional host directories may be mounted in
the same manner should the need arise.

Forwarding Custom Ports
-----------------------

.. TODO::

    This is trickier to do than it may seem at first. The easiest method is to
    just rely on the VirtualBox GUI or underlying vboxmanage commands and set
    up custom port mappings by hand. To do it dynamically brings up the problem
    of port conflict resolution, especially with the |TS| Vagrantfile which
    defines so many different virtual machines, any number of which may be run
    simultaneously.

Building |TS| Inside Vagrant
============================

Producing |TS| builds from within the Vagrant managed virtual machines is
effectively no different than in any other environment. The same directory in
which the Vagrantfile exists will be mounted via NFS inside the virtual machine
at the path ``/opt/src/trafficserver.git``.

.. note::

   If you have run ``autoconf`` or ``configure`` from outside the virtual
   machine environment against the same Git working copy as is mounted inside
   the virtual machine, you will encounter failures should you attempt to run
   any of the Make targets inside the VM. Any build related commands should be
   run inside of the virtual machine. Additionally, if you are running more
   than one virtual machine simultaneously, remember that they are each using
   the same NFS export on your host machine.


