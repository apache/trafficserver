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
  "trusty_64"  => "192.168.2.101",
  "trusty_32"  => "192.168.2.102",
  "jessie_64" => "192.168.2.103",
  "jessie_32" => "192.168.2.104",
  "centos7_64" => "192.168.2.105",
  "centos6_64" => "192.168.2.106",
  "centos6_32" => "192.168.2.107",
  "omnios" => "192.168.2.108",
}

$vmspec = {
  "trusty_64" => [
    "ubuntu/trusty64"
  ],
  "trusty_32" => [
    "ubuntu/trusty32"
  ],
  "jessie_64" => [
    "puppetlabs/debian-8.2-64-nocm"
  ],
  "jessie_32" => [
    "puppetlabs/debian-8.2-32-nocm"
  ],
  "centos7_64" => [
    "puppetlabs/centos-7.2-64-nocm"
  ],
  "centos6_64" => [
    "puppetlabs/centos-6.6-64-nocm"
  ],
  "centos6_32" => [
    "puppetlabs/centos-6.6-32-nocm"
  ],
  "omnios" => [
    "omniti/omnios-r151014"
  ],
}

Vagrant.configure("2") do |config|

  # Default all VMs to 1GB and 2 cores
  config.vm.provider :virtualbox do |v|
    v.memory = 1024
    v.cpus = 2
  end

  config.ssh.shell = "bash -c 'BASH_ENV=/etc/profile exec bash'"

  $vmspec.each do | name, spec |
    config.vm.define name do | config |
      if name == 'omnios'
        # nfs seems to be the only way to make this work for omnios
        # this method fails if hostfs is encrypted
        config.vm.synced_folder ".", "/vagrant", type: "nfs"
        config.ssh.username = "root"
        config.ssh.password = "vagrant"
      else
        config.vm.synced_folder ".", "/vagrant"
      end
      config.vm.box = spec[0]
      config.vm.network :private_network, ip: $network[name]
      config.vm.provision "shell" do |s|
        s.path = "contrib/vagrant-setup.sh"
        s.args = name
      end
    end
  end

end

# -*- mode: ruby -*-
# vi: set ft=ruby :
