#!/usr/bin/perl -wT

use strict;
use CGI;

my $q = new CGI;
my $timestamp=localtime;
my $user = $q->param ( "user" );

if ( !$user ) {
  $user = "Unknow";
}

print "Content-type: text/html\n\n";

print "<BLOCK>";
print "<BR>Hello user $user<BR>";
print "<BR>Generated on <BR>$timestamp<BR>";
print "TTL is 3600s<BR>";
print "</BLOCK>";
