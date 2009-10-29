#!/usr/bin/perl -wT

use strict;
use CGI;

my $q = new CGI;
my $timestamp=localtime;

my $balance=sprintf "%d", rand 10000;

print "Content-type: text/html\n\n";

print "<BLOCK>";
print "<BR>Bank account: <BR>balance = $balance<BR>";
print "<BR>Generated on <BR>$timestamp<BR>";
print "Not Cached<BR>";
print "</BLOCK>";