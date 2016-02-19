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
  "trusty_64"  => "192.168.2.101",
  "jessie_64" => "192.168.2.102",
  "centos7_64" => "192.168.2.103",
}

$vmspec = {
  "trusty_64" => [
    "ubuntu/trusty64"
  ],
  "jessie_64" => [
      "debian/jessie64"
  ],
  "centos7_64" => [
      "puppetlabs/centos-7.2-64-nocm"
  ],
}

Vagrant.configure("2") do |config|

  # Default all VMs to 1GB.
  config.vm.provider :virtualbox do |v|
    v.memory = 1024
    v.cpus = 2
  end

  # Mount the Traffic Server source code in a fixed location everywhere
  config.vm.synced_folder ".", "/vagrant"

  config.ssh.forward_agent = false
  config.ssh.shell = "bash -c 'BASH_ENV=/etc/profile exec bash'"

  $vmspec.each do | name, spec |
    config.vm.define name do | config |
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
