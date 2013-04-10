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
}

Vagrant.configure("2") do |config|

  # Default all VMs to 1GB.
  config.vm.provider :virtualbox do |v|
    v.customize ["modifyvm", :id, "--memory", 1024]
  end

  # Mount the Traffic Server source code in a fixed location everywhere. Use NFS
  # because it's faster and vboxfs doesn't support links.
  config.vm.synced_folder ".", "/opt/src/trafficserver.git", :nfs => true

  # Ubuntu 13.04 (Raring Ringtail)
  # Ubuntu 12.10 (Quantal Quetzal)
  # Ubuntu 12.04 LTS (Precise Pangolin)
  ['raring', 'quantal', 'precise'].each { |release|
    config.vm.define "#{release}64" do | config |
      config.vm.box = "#{release}64"
      config.vm.box_url = "http://cloud-images.ubuntu.com/vagrant/#{release}/current/#{release}-server-cloudimg-amd64-vagrant-disk1.box"
      config.vm.network :private_network, ip: $network["#{release}64"]
      config.vm.provision :puppet do |puppet|
        puppet.manifests_path = "contrib/manifests"
        puppet.manifest_file = "debian.pp"
      end
    end
  }

  # Ubuntu 10.04 LTS (Lucid Lynx)
  config.vm.define :lucid64 do | config |
    config.vm.box = "lucid64"
    config.vm.network :private_network, ip: $network["lucid64"]
    config.vm.box_url = "http://files.vagrantup.com/lucid64.box"
  end

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

  config.vm.define :centos63 do |config|
    config.vm.box = "centos63"
    config.vm.network :private_network, ip: $network["centos63"]
    config.vm.box_url = "https://dl.dropbox.com/u/7225008/Vagrant/CentOS-6.3-x86_64-minimal.box"
    config.vm.provision :puppet do |puppet|
      puppet.manifests_path = "contrib/manifests"
      puppet.manifest_file = "redhat.pp"
    end
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

end

# -*- mode: ruby -*-
# vi: set ft=ruby :
