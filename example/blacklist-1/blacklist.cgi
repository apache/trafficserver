#!/usr/local/bin/perl
############################################################################
#
# blacklist.cgi
# 
# A perl script to generate the html form for adding to and removing
# sites from the blacklist.
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
#
############################################################################

# CHANGE: specify the installed directory of traffic server here
$ts_dir = "/home/inktomi/ts-3.5";

$blacklist_file = "$ts_dir/conf/yts/plugins/blacklist.txt";

sub parse_form {
  local($pairs, $buffer);

  $request_method = $ENV{"REQUEST_METHOD"};  # These are global, but that
  $content_length = $ENV{"CONTENT_LENGTH"};  # might not be all bad.

# If the method is the empty string, figure we're being invoked
# from the shell for testing and so use the string from ARGV[0]

  if ($request_method eq "") {	
    $buffer = $ARGV[0];
  } elsif ($request_method eq "POST") {
    read(STDIN, $buffer, $content_length);  # POST => read from STDIN
  } elsif ($request_method eq "GET") {
    $buffer = $ENV{"QUERY_STRING"};         # GET = > read from ENV variable
  } else {
    &ReportError("Unrecognized CGI method \"$request_method\".");
  }

# The following is the standard hack for decoding the pairs

   # Split the name-value pairs
   @pairs = split(/&/, $buffer);

   foreach $pair (@pairs) {
      ($name, $value) = split(/=/, $pair);

      # Un-Webify plus signs and %-encoding
      $value =~ tr/+/ /;
      $value =~ s/%([a-fA-F0-9][a-fA-F0-9])/pack("C", hex($1))/eg;
      $value =~ s/\r\n/\n/g;

      $FORM{$name} = $value;
   }
}

&parse_form;

sub printForm {

  print "<html>\n";
  print "<head><title>Inktomi Blacklist Plugin</title></head>\n";
  print "<body>\n";
  print "<h2>Inktomi Blacklist Plugin</h2>\n";
  print "<form method=post action=/plugins/blacklist.cgi>\n";
  print "<input type=hidden name=INK_PLUGIN_NAME value=\"Inktomi Blacklist Plugin\">\n";
  print "<table border=0>\n";
  print "<tr><td colspan=2><hr size=1></td></tr>\n";
  print "<tr><th align=left colspan=2>Select the site that you want to be removed from the block list</th></tr>\n";
  print "<tr><td align=left valign=top><input type=submit name=submit value=Remove></td>\n";
  print "    <td valign=top align=left><select name=remove_site size=8>\n";

  open(BLACKLIST, $blacklist_file);
  while (<BLACKLIST>) {
    $site = $_;
    chop $site;
    print "<option value=\"$site\">$site</option>\n";
  }
  close(BLACKLIST);

  print "</select></td></tr>\n";
  print "<tr><td colspan=2><hr size=1></td></tr>\n";
  print "<tr><th align=left colspan=2>Enter the site that you want to be added to the block list</th></tr>\n";
  print "<tr><td><input type=submit name=submit value=Add></td>\n";
  print "    <td><input type=text name=add_site size=20></td></tr>\n";
  print "<tr><td colspan=2><hr size=1></td></tr>\n";
  print "<tr><td align=right colspan=2><img src=/plugins/PoweredByInktomi.gif alt=\"Powered by Inktomi\"></td></tr>\n";
  print "</table>\n";
  print "</form>\n";
  print "</body>\n";
  print "</html>\n";
}

if ($FORM{'submit'} eq "Remove") {

  $remove_site = $FORM{'remove_site'};

  open(BLACKLIST, $blacklist_file);
  while (<BLACKLIST>) {
    $site = $_;
    chop $site;
    if ($site ne $remove_site) {
      push(@site_list, $site);
    }
  }
  close(BLACKLIST);

  open(BLACKLIST, ">$blacklist_file");
  foreach $site (@site_list) {
    print BLACKLIST "$site\n";
  }
  close(BLACKLIST);

} elsif ($FORM{'submit'} eq "Add") {

  open(BLACKLIST, ">>$blacklist_file");
  print BLACKLIST "$FORM{'add_site'}\n";
  close(BLACKLIST);

}

&printForm();

exit(0);


