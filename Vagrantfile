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

$network = {
  # The VM host is 192.168.100.1
  "raring64"  => "192.168.100.11",
  "quantal64" => "192.168.100.12",
  "precise64" => "192.168.100.13",
  "lucid64"   => "192.168.100.14",
  "centos63"  => "192.168.100.15",
  "freebsd"   => "192.168.100.16",
  "omnios"    => "192.168.100.17",

  "raring32"  => "192.168.200.11",
  "quantal32" => "192.168.200.12",
  "precise32" => "192.168.200.13",
  "lucid32"   => "192.168.200.14",

  "fedora18"  => "192.168.200.15",
  "centos59"  => "192.168.200.16",
  "centos64"  => "192.168.200.17",
  "debian7"   => "192.168.200.18",
  "sles11"    => "192.168.200.19",
  "oel63"     => "192.168.200.20",

  "saucy64"   => "192.168.100.21",
  "saucy32"   => "192.168.100.22",
  "trusty64"  => "192.168.100.23",
  "trusty32"  => "192.168.100.24",
}

$vmspec = {
  "lucid64" => [ # Ubuntu 10.04 LTS (Lucid Lynx)
    "http://files.vagrantup.com/lucid64.box", "debian.pp"
  ],
  "fedora18" => [
    "http://puppet-vagrant-boxes.puppetlabs.com/fedora-18-x64-vbox4210.box", "redhat.pp"
  ],
  "centos63" => [
    "https://dl.dropbox.com/u/7225008/Vagrant/CentOS-6.3-x86_64-minimal.box", "redhat.pp"
  ],
  "centos59" => [
    "http://puppet-vagrant-boxes.puppetlabs.com/centos-59-x64-vbox4210.box", "redhat.pp",
  ],
  "centos64" => [
    "http://puppet-vagrant-boxes.puppetlabs.com/centos-64-x64-vbox4210.box", "redhat.pp",
  ],
  "debian7" => [
    "http://puppet-vagrant-boxes.puppetlabs.com/debian-70rc1-x64-vbox4210.box", "debian.pp",
  ],
  "sles11" => [
    "http://puppet-vagrant-boxes.puppetlabs.com/sles-11sp1-x64-vbox4210.box", "redhat.pp",
  ],
  "oel63" => [
    "http://ats.boot.org/vagrant/vagrant-oel63-x64.box", "redhat.pp",
  ],
}

Vagrant.configure("2") do |config|

  # Default all VMs to 1GB.
  config.vm.provider :virtualbox do |v|
    v.customize ["modifyvm", :id, "--memory", 1024]
  end

  # Mount the Traffic Server source code in a fixed location everywhere. Use NFS
  # because it's faster and vboxfs doesn't support links.
  config.vm.synced_folder ".", "/opt/src/trafficserver.git", :nfs => true

  # Always forward SSH keys to VMs.
  config.ssh.forward_agent = true

  # Ubuntu 14.04 (Trusty Tahr)
  # Ubuntu 13.04 (Raring Ringtail)
  # Ubuntu 12.10 (Quantal Quetzal)
  # Ubuntu 12.04 LTS (Precise Pangolin)
  ["i386", "amd64"].each { |arch|
    ['saucy', 'raring', 'quantal', 'precise', 'trusty' ].each { |release|
      n = { "i386" => "32", "amd64" => "64" }[arch]
      config.vm.define "#{release}#{n}" do | config |
        config.vm.box = "#{release}#{n}"
        config.vm.box_url = "http://cloud-images.ubuntu.com/vagrant/#{release}/current/#{release}-server-cloudimg-#{arch}-vagrant-disk1.box"
        config.vm.network :private_network, ip: $network["#{release}#{n}"]
        config.vm.provision :puppet do |puppet|
          puppet.manifests_path = "contrib/manifests"
          puppet.manifest_file = "debian.pp"
        end
      end
    }
  }

  config.vm.define :freebsd do | config |
    config.vm.box = "freebsd"
    config.vm.synced_folder ".", "/opt/src/trafficserver.git", :nfs => false
    # Force the FreeBSD VM to use a network driver that actually works.
    config.vm.provider :virtualbox do |v|
      v.customize ["modifyvm", :id, "--nictype1", "82543GC"]
      v.customize ["modifyvm", :id, "--nictype2", "82543GC"]
    end
    config.vm.network :private_network, ip: $network["freebsd"]
    config.vm.box_url = "https://github.com/downloads/xironix/freebsd-vagrant/freebsd_amd64_zfs.box"
  end

  # Current OmniOS release, see http://omnios.omniti.com/wiki.php/Installation
  config.vm.define :omnios do | config |
    config.vm.box = "omnios"
    config.vm.guest = :solaris
    config.vm.network :private_network, ip: $network["omnios"]
    config.vm.synced_folder ".", "/opt/src/trafficserver.git", :nfs => false
    config.vm.box_url = "http://omnios.omniti.com/media/omnios-latest.box"
    config.vm.provision :shell, :path => "contrib/manifests/omnios.sh"
  end

  $vmspec.each do | name, spec |
    config.vm.define name do | config |
      config.vm.box = name
      config.vm.box_url = spec[0]
      config.vm.network :private_network, ip: $network[name]
      config.vm.provision :puppet do |puppet|
        puppet.manifests_path = "contrib/manifests"
        puppet.manifest_file = spec[1]
      end
    end
  end

end

# -*- mode: ruby -*-
# vi: set ft=ruby :
