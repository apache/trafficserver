#!/usr/bin/perl -wT

use strict;
use CGI;

my $q = new CGI;
my $timestamp=localtime;
my $city = $q->param ( "city" );

my $temperature=sprintf "%d", rand 100;


if ( !$city ) {
  $city = "Unknow";
}

print "Content-type: text/html\n\n";

print "<BLOCK>";
print "<BR>Temperature today in $city is $temperature<BR>";
print "<BR>Generated on <BR>$timestamp<BR>";
print "TTL is 60s<BR>";
print "</BLOCK>";