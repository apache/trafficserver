# Makefile.am for mrtg
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

webuidir = $(pkgdatadir)/tune

DEFS += @DEFS@

# C Pre-Processor Program.  This is for using the CPP for things
# -----------------------   other than normal.  (CPP) gets a value
#  which leaves extranious data in the file, and this is undesirable
#  for the kind of text-processing I've got in mind.
# If used, note that it does still leave alot of blank lines where
#  #foo's and #include directives existed in the original text.

CUSTOMCPPFLAGS = "-x c -E -P"

# .xml-specific transforms via cpp-program...with kludge to lose leading blank-lines.
#                                         ...extra-kludgy to be safe if no lbl's!
.xmlcpp.xml:
	$(SHELL) -ec '$(CC) $(CUSTOMCPPFLAGS) $(DEFS) $(INCLUDES) $< \
                     | sed '\''1,/-- AND_ME_TOO --/d'\'' > $@'
