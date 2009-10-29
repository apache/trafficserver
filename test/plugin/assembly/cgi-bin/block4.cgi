#!/usr/bin/perl -wT

use strict;
use CGI;

my $q = new CGI;
my $timestamp=localtime;
my $news = $q->param ( "news" );
my $score = sprintf "%d", rand 60;
my $population = sprintf "%d", rand 10000;
my $quote = sprintf "%d", rand 100;

if ( !$news ) {
  $news = "Unknow";
}

print "Content-type: text/html\n\n";

print "<BLOCK>";

if ( $news eq "sports" ) {
  print "<BR>Sports news:<BR>Raiders / Miami score: $score:0<BR>";
}
else
{
if ( $news eq "intl" ) {
  print "<BR>International news:<BR>$population babies born today<BR>";
}
else {
  print "<BR>Generic news:<BR>INKT stock quote: $quote<BR>";
}
}
print "<BR>Generated on <BR>$timestamp<BR>";
print "TTL is 30s<BR>";
print "</BLOCK>";
