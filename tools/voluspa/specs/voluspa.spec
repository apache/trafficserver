#
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
#

%{!?_release: %define _release 0}
%{!?_version: %define _version UNKNOWN}
%{!?commit0: %define commit0 UNKNOWN}

Name:    voluspa
Version: %{_version}
Release: 1
Summary: Voluspa
License: Apache Software License 2.0 (AL2)
Group:   Applications/System
URL:     https://github.com/apache/trafficserver/tools/voluspa

%description
CDN -> ATS Configuration Tool

Version: %{commit0}

%prep

%build

%check

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/bin
cp -p %{_topdir}/dist/linux/voluspa $RPM_BUILD_ROOT/usr/bin

%files
%defattr(-, root, root, -)
/usr/bin/voluspa

%clean

%changelog
