#!/usr/bin/perl -wT

use strict;
use CGI;

my $q = new CGI;

my $timestamp=localtime;
my $temlate=$ENV{HTTP_X_TEMPLATE};
my $template="on";

my $ip_addr="209.131.50.200";

if ( $template ) {
  print $q->header ( -type => "text/html", -x_template => "true" );
  print $q->start_html( -title => "Template page" );

  print "<TABLE COLS=2 WIDTH=\"500\" HEIGHTH=\"300\" NOSAVE >";
  print "<TR NOSAVE>";
  print "<TD BGCOLOR=\"#FFCCCC\" NOSAVE><FONT SIZE=+2>";
  print "<DYNAMIC>\n";
  print   "CACHEABLE=true\n";
  print	  "BLOCKNAME=block1\n";
  print   "URL=http://$ip_addr/cgi-bin/block1.cgi?QSTRING\n";
  print   "TTL=3600\n";
  print   "KEY\n";
  print     "QUERY=user\n";
  print"</DYNAMIC>";
  print "</FONT></TD>";
  print "<TD BGCOLOR=\"#FFFFCC\" NOSAVE><FONT SIZE=+2>";
  print "<DYNAMIC>\n";
  print    "CACHEABLE=true\n";
  print    "BLOCKNAME=block2\n";
  print    "URL=http://$ip_addr/cgi-bin/block2.cgi?QSTRING\n";
  print    "TTL=60\n";
  print    "KEY\n";
  print       "QUERY=city\n";
  print "</DYNAMIC>";
  print "</FONT></TD>";
  print "</TR>";
  print "<TR NOSAVE>";
  print "<TD BGCOLOR=\"#CCFFFF\" NOSAVE><FONT SIZE=+2>";
  print "<DYNAMIC>\n";
  print    "CACHEABLE=false\n";
  print    "BLOCKNAME=block3\n";
  print    "URL=http://$ip_addr/cgi-bin/block3.cgi?QSTRING\n";
  print "</DYNAMIC>";
  print "</FONT></TD>";
  print "<TD BGCOLOR=\"#99FFCC\" NOSAVE><FONT SIZE=+2>";
  print "<DYNAMIC>\n";
  print    "CACHEABLE=true\n";
  print    "BLOCKNAME=block4\n";
  print    "URL=http://$ip_addr/cgi-bin/block4.cgi?QSTRING\n";
  print    "TTL=30\n";
  print    "KEY\n";
  print       "QUERY=news\n";
  print "</DYNAMIC>";
  print "</FONT></TD>";
  print "</TR>";
  print "</TABLE>";



} else {
  print $q->header ( -type => "text/html" );
  print $q->start_html( -title => "Regular Html page" );
  print $q->p( "Sorry you're proxy is not template enabled" );

}

print $q->p( "<FONT SIZE=+2>" );
print $q->p( "Template page generated on ", $q->b( $timestamp ) ); 
print $q->p( "</FONT>" );

print $q->end_html;


