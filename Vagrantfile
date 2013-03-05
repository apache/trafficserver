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

Vagrant::Config.run do |config|

  # Default all VMs to 1GB.
  config.vm.customize ["modifyvm", :id, "--memory", 1024]

  # Mount the Traffic Server source code in a fixed location everywhere. Use NFS
  # because it's faster and vboxfs doesn't support links.
  config.vm.share_folder "src", "/opt/src/trafficserver.git", ".", :nfs => true

  # Ubuntu 12.04 LTS (Precise Pangolin)
  config.vm.define :precise64 do | config |
    config.vm.box = "precise64"
    config.vm.box_url = "http://files.vagrantup.com/precise64.box"
    config.vm.network :hostonly, "192.168.100.3"
    config.vm.provision :puppet do |puppet|
      puppet.manifests_path = "contrib/manifests"
      puppet.manifest_file = "debian.pp"
    end
  end

  # Ubuntu 10.04 LTS (Lucid Lynx)
  config.vm.define :lucid64 do | config |
    config.vm.box = "lucid64"
    config.vm.network :hostonly, "192.168.100.2"
    config.vm.box_url = "http://files.vagrantup.com/lucid64.box"
  end

  config.vm.define :freebsd do | config |
    config.vm.box = "freebsd"
    config.vm.share_folder "src", "/opt/src", "src", :nfs => false
    # Force the FreeBSD VM to use a network driver that actually works.
    config.vm.customize ["modifyvm", :id, "--nictype1", "82543GC"]
    config.vm.customize ["modifyvm", :id, "--nictype2", "82543GC"]
    config.vm.network :hostonly, "192.168.100.6"
    config.vm.box_url = "https://github.com/downloads/xironix/freebsd-vagrant/freebsd_amd64_zfs.box"
  end

  config.vm.define :centos63 do |config|
    config.vm.box = "centos63"
    config.vm.network :hostonly, "192.168.100.8"
    config.vm.box_url = "https://dl.dropbox.com/u/7225008/Vagrant/CentOS-6.3-x86_64-minimal.box"
    config.vm.provision :puppet do |puppet|
      puppet.manifests_path = "contrib/manifests"
      puppet.manifest_file = "redhat.pp"
    end
  end

  # Current OmniOS release, see http://omnios.omniti.com/wiki.php/Installation
  config.vm.define :omnios do | config |
    config.vm.box = "omnios"
    config.vm.network :hostonly, "192.168.100.9"
    config.vm.share_folder "src", "/opt/src/trafficserver.git", ".", :nfs => false
    config.vm.box_url = "http://omnios.omniti.com/media/omnios-latest.box"
    config.vm.provision :shell,  :path => "contrib/manifests/omnios.sh"
  end

end

# -*- mode: ruby -*-
# vi: set ft=ruby :
