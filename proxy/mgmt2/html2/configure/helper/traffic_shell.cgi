#!/usr/bin/perl

$ENV {'REQUEST_METHOD'} =~ tr/a-z/A-Z/;
if ($ENV{'REQUEST_METHOD'} eq "POST") {
    read(STDIN, $buffer, $ENV{'CONTENT_LENGTH'});
} else {
    $buffer = $ENV{'QUERY_STRING'};
}
@pairs = split(/&/, $buffer);
foreach $pair (@pairs) {
    ($name, $value) = split(/=/, $pair);
    $value =~ tr/+/ /;
    $value =~ s/%(..)/pack("C", hex($1))/eg;
    $ENV{$name} = $value;
}

# handle post data here!
#foreach $key (keys(%ENV)) {
#    print "$key = $ENV{$key}<br>";
#}

# find out current InktomiHome
my $path = $ENV{ROOT} || $ENV{INST_ROOT};
if (!$path) {
  if (open(fh, "/etc/traffic_server")) {
    while (<fh>) {
      chomp;
      $InktomiHome = $_;
      last;
    }
  } else {
    $InktomiHome = "/home/trafficserver";
  }
} else {
  $InktomiHome = $path;
}

exec("${InktomiHome}/bin/start_traffic_shell -f ${InktomiHome}/share/yts_ui/configure/helper/otwu.tcl");
exit 0;
