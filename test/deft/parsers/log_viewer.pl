#!/inktest/dist/bin/perl

# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#
#  log_viewer.pl
#
#
#   Description:
#
#   
#
#

use IO::Socket;
use IO::Select;
use Tk;

use strict vars;
use parse_dispatcher;

our $num_errors = 0;
our $num_warnings = 0;
our %type_filter = ( "ok" => 1,
		     "warn" => 1,
		     "error"=> 1);

our $mode;
our $file = "";

# Vars to deal with test groups
our $current_test = "";  # Test which we are currently displaying
our $running_test = "";  # Test which is currently running
our $pipe_fd = -1;
our %test_set = ();
our @filter_widgets = ();

if (scalar(@ARGV) == 1) {
    $file = $ARGV[0];
    $mode = "file";
} elsif (scalar(@ARGV) == 2 && $ARGV[0] eq "-s") {
    $pipe_fd = $ARGV[1];
    $mode = "pipe";
} else {
    print("Usage: log_display.pl <filename> \n");
    exit(1);
}

our $file_read_active = 0;

sub start_file_read {
    close(LOG);
    open(LOG, $file ) || die "Failed to open log file : $!\n";
#    print "opened LOG from $file\n";
}

if ($mode eq "file") {
    start_file_read();
} else {
    open(PIPE,"<&=$pipe_fd") || die "Couldn't open input pipe: $!";
}

our $mw = MainWindow->new;

#print "Created main window\n";

$mw->title("Test Log Window");
our $menu_bar = $mw->Frame(-relief => 'groove',
			  -borderwidth => 3)->pack(-side => 'top',
						   -fill => 'x');
our $file_mb = $menu_bar->Menubutton(-text => 'File')->pack(-side => 'left');

our $filter_mb = $menu_bar->Menubutton(-text => 'Instance Filter')->pack(-side => 'left');
our $type_mb = $menu_bar->Menubutton(-text => 'Type Filter')->pack(-side => 'left');

$type_mb->checkbutton(-label => "OK",
		      -variable => \$type_filter{"ok"},
		      -command => [\&filters_update, "type:ok"]);
$type_mb->checkbutton(-label => "Warning",
		      -variable => \$type_filter{"warn"},
		      -command => [\&filters_update, "type:warn"]);
$type_mb->checkbutton(-label => "Error",
		      -variable => \$type_filter{"error"},
		      -command => [\&filters_update, "type:error"]);

$file_mb->command(-label => 'Exit',
		    -command => sub { exit });

our $test_mb;
if ($mode eq "pipe") {
    $test_mb = $menu_bar->Menubutton(-text => 'Test')->pack(-side => 'left');
}

our $bottom_bar = $mw->Frame()->pack(-side => 'bottom',
				     -fill => 'x');
our $exit_b =$bottom_bar->Button(-text => "Exit",
				 -command => sub { exit })->pack(-side => 'left', 
								 -expand => 1,
								 -fill => 'x');

our $status_canvas = $bottom_bar->Canvas(-width => 200,
					 -height => 20)->pack(-side => 'right');
our $s_text_id = $status_canvas->createText(100, 10);

our $t = $mw->Text(-width => 120,
		   -height => 40,
		   -wrap => 'none');
	       
$t->tagConfigure('etag', -foreground => 'red');
$t->tagConfigure('wtag', -foreground => 'blue');

our $scroll_y = $mw->Scrollbar(-orient => 'vertical',
			       -command => ['yview', $t]);
$scroll_y->pack(-side => 'right',
		-fill => 'y');

our $scroll_x = $mw->Scrollbar(-orient => 'horizontal',
			       -command => ['xview', $t]);
$scroll_x->pack(-side => 'bottom',
		-fill => 'x');

$t->configure(-yscrollcommand => ['set', $scroll_y]);
$t->configure(-xscrollcommand => ['set', $scroll_x]);

$t->pack(-side => 'left',
	 -expand => 1,
	 -fill => 'both');

update_status_box();

if ($mode eq "file") {
    $file_read_active = 1;
    $mw->fileevent(LOG, 'readable', [\&read_log, $t]);
} else {
    $mw->fileevent(PIPE, 'readable', [\&read_pipe, ""]);
}

MainLoop;

my $schedule_id = 0;
my %seen_comps = {};


sub update_status_box {
    my $first_index = "0";
    my $last_index = $status_canvas->index($s_text_id, 'end');
#    print "Canvas Index $first_index  $last_index\n";
    $status_canvas->dchars($s_text_id, $first_index, $last_index);
    $status_canvas->insert($s_text_id, 'end', "Errors: $num_errors  ");
    $status_canvas->insert($s_text_id, 'end', "Warnings: $num_warnings");
}

sub reread_file() {

#    print "Reread file called\n";
    $num_errors = 0;
    $num_warnings = 0;

#    print "Clearing widget\n";
    my $first_index = "0.0";
    my $last_index = $t->index('end');
#    print "Indexes $first_index  $last_index\n";
    $t->delete($first_index, $last_index);

    sysseek(LOG, 0, 0);

    if ($file_read_active == 0) {
	$mw->fileevent(LOG, 'readable', [\&read_log, $t]);
	$file_read_active = 1;
    }
}

sub reset_on_eof() {
    sysseek(LOG, 0, 1);
}

sub filters_update {
    my ($comp) = @_;

    my $hash_ref;
    if ($comp =~ /^type:(\w+)/) {
	$comp = $1;
	$hash_ref = \%type_filter;
    } else {
	$hash_ref = \%seen_comps;
    }

#    print "Filters Update Called with $comp $$hash_ref{$comp}\n";

    reread_file();
}

our $switch_button;

# sub test_update
# 
#   Handles user wanting to switch which test is displayed
#
sub test_update {
    my ($arg) = @_;

    # Need to clear out components
    #
    #   First from the menu bar
    #
    $filter_mb->menu()->delete(0, end);

    # Second from seen_comps
    %seen_comps = ();
	

    $file = $test_set{$current_test};

    if ($running_test) {
	if ($current_test eq $running_test) {
	    if ($switch_button) {
		$switch_button->destroy();
		$switch_button = 0;
	    }
	} else {
	    display_switch_button($running_test);
	}
    }

    # Need open the new file
    start_file_read();
    $file_read_active = 0;

    # Redisplay the file
    reread_file();
}

sub display_switch_button {
    my ($test_arg) = @_;

    if ($switch_button) {
	$switch_button->destroy();
	$switch_button = 0;
    }

    if ($test_arg) {
	my $tmp_str = "Switch to current (" . $test_arg . ")";
	$switch_button = $menu_bar->Button(-text => $tmp_str,
					   -command => [\&process_switch_to, $test_arg]);
	$switch_button->pack(-side => 'right');
    }
}

sub process_switch_to {
    my ($switch_to) = @_;

    $current_test = $switch_to;
    test_update($switch_to);
}

my $leftovers = "";
sub read_log {

    my($widget) = @_;
    my($buf);

    my $r = sysread(LOG, $buf, 8192);

#    print "read_log read $r\n";

    if ($r > 0) {

	my $processed = 0;

	while ($processed < $r) {
	    my $idx = index($buf, "\n", $processed);
	    my $to_add = "";

	    if ($idx >= 0) {
		$idx += 1;
		$to_add = substr($buf, $processed, $idx - $processed);

		my $entry_bytes = $idx - $processed;
		$processed += $entry_bytes;

		if ($leftovers ne "") {
		    $to_add = $leftovers . $to_add;
		    $leftovers = "";
		}

		$to_add =~ s/0 0//;
		$to_add =~ s/\r$//;
		$to_add =~ s/\\ / /g;

		if ($to_add =~ /^\[\w{3} \w{3}\s{1,2}\d{1,2} \d{1,2}:\d{1,2}:\d{1,2}\.\d{1,3} ([^\]]+) ([^\]]+)\]/) {
		    my $comp = $1;
#		    print "Got instance $1\n";
		    if (!defined($seen_comps{$comp})) {
			$seen_comps{$comp} = 1;
			my $new_cb = $filter_mb->checkbutton(-label => $comp,
							     -variable => \$seen_comps{$comp},
							     -command => [\&filters_update, $comp]);
		    }

		    my $i;
		    my $skip = 0;
		    if ($seen_comps{$comp} == 0) {
			$skip = 1;
		    }

		    if ($skip == 0) {
			my $old_last_index = $widget->index('end');

			# For some reason I can't quite figure out, the 'end'
			# index is really one too big so move it back one
			my ($line, $char) = split(/\./, $old_last_index);
			my $old_last_index = $line - 1 . ".0";

			my $line_code = parse_dispatcher::process_test_log_line($to_add);
			my $text_tag = "";
			my $show_type = 0;
			if ($line_code eq "error") {
			    $text_tag = "etag";
			    $num_errors++;
			    update_status_box();
			    $show_type = $type_filter{"error"};
			} elsif ($line_code eq "warning") {
			    $text_tag = "wtag";
			    $num_warnings++;
			    update_status_box();
			    $show_type = $type_filter{"warn"};
			} else {
			    $show_type = $type_filter{"ok"};
			}

			if ($show_type != 0) {
			    $widget->insert('end', $to_add);
			    if ($text_tag ne "") {
				$widget->tagAdd($text_tag, $old_last_index, "$old_last_index lineend");
			    }
			    $widget->yview('end');
			}
		    }
		}
	    } else {
		$leftovers = substr($buf, $processed, $r - $processed);
		$processed += $r - $processed;
	    }

	    if ($file_read_active == 0) {
		$file_read_active = 1;
		$mw->fileevent(LOG, 'readable', [\&read_log, $t]);
	    }
	}
    } elsif ($r == 0) {

	if ($file_read_active != 0) {
	    $mw->fileevent(LOG, 'readable', "");
	    $file_read_active = 0;
	}

#	print ("EOF - scheduling into future\n");
	reset_on_eof();
	$schedule_id = Tk::After->new($widget,250,'once',[\&read_log, $widget]);
    }
}

our $pipe_buf = "";
sub read_pipe {

    my($widget) = @_;
    my($tmp_buf);

    my $r = sysread(PIPE, $tmp_buf, 1024);

    if ($r <= 0 ) {
	if ($r != 0) {
	    warn "Error received on pipe : $!\n";
	} else {
#	    print "EOF from pipe\n";
	}
	close(PIPE);
    } else  {
#	print "Read $r bytes from pipe\n";
	$pipe_buf = $pipe_buf . $tmp_buf;

	while ($pipe_buf =~ /([^\n]*)\n(.*)/s) {
	    my $command = $1;
	    $pipe_buf = $2;

	    if ($command =~ /^start\s+(\S+)\s+(\S+)/) {
		my $test_name = $1;
		my $test_log = $2;

		$test_set{$test_name} = $test_log;
		$running_test = $test_name;

#		print "Got start notification for $test_name\n";

		# Add a radio button for the test
		$test_mb->radiobutton(-command => [\&test_update, $test_name],
				      -variable => \$current_test,
				      -value => $test_name,
				      -label => $test_name);
		
		if ($current_test eq "") {
		    # start reading the file
		    $current_test = $test_name;
		    $file = $test_log;
		    start_file_read();
		    $mw->fileevent(LOG, 'readable', [\&read_log, $t]);
		} else {
		    #put up a switch button
		    display_switch_button($test_name);
		}
	    } elsif ($command =~ /^roll\s+(\S+)/) {
		my $test_name = $1;
		my $test_log = $test_set{$test_name} . "." . $test_name;

		$test_set{$test_name} = $test_log;
	    } elsif ($command =~ /^done\s*/) {
		$running_test = "";
		display_switch_button("");
	    } else {
		warn ("Bad command on control pipe: $command\n");
	    }
	}
    }
}
   

