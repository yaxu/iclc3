#!/usr/bin/perl -w

use strict;
use Tk;
use Tk::Canvas;
use IO::Handle;

use Data::Dumper;

my $gridx = 1;
my $gridy = 1;

my $cell_width = 6;
my $cell_height = 6;

my $canvas_height = $cell_height * 17 * 2;
my $canvas_width = $cell_width * 17 * 2;

my $slider_offset = 180;

my $hotx = $cell_width * 10;

my $last_phrase_move;

my $pitch = (shift @ARGV or 40);
my @samplesets = (@ARGV, 'can', 'kurt', 'mouth', 'bassdm');

my $move = shift @ARGV;

my $line_color = 'grey';
my $active_line_color = 'white';

my $main;
my $bpmrepeat;

my @cs;

my %controls = (intensity  => {min => 0,
			       max => 16,
			      },
		pitch     => {min => -100,
			       max => 100,
			       start => 60
			      },
		complexity => {min => 0,
			       max => 16
			      },
		volume     => {min => 0,
			       max => 100,
			       start => 50
			      },
		#disorder  => {max => 100},
		bpm        => {min => 1,
			       max => 400,
			       start => 120,
			      },
	       );
my %values;

init();
MainLoop;

##

sub init {
  init_msg();
 
  # Create main window and canvas
  $main = MainWindow->new();

  #$main->wm("minsize",  400,300);
  $main->wm("geometry", "800x600-5-22");

  my $topframe = $main->Frame;
  $topframe->pack(-side => 'top', -expand => 1, -fill => 'both');
  my $bottomframe = $main->Frame;
  $bottomframe->pack(-side => 'bottom', -expand => 1, -fill => 'both');

  foreach my $frame ($topframe, $bottomframe) {
      foreach my $side ('left', 'right') {
	  my $c = $frame->Canvas(-width => $canvas_width,
				 -height => $canvas_height
				);
	  $c->pack(-expand => 1, -fill => 'both', -side => $side);
	  init_samples($c);
	  %{$values{$c}} = map {$_ => 0} keys %controls;
	  init_sliders($c);
	  make_silence($c, $cell_width, $cell_height, 8);
	  push @cs, $c;
      }
  }

  $values{bpm} = 120;
  #$main->bind('<KeyPress-r>', [\&reverse_phrase, $c]);
}

##

my $sliderset;
sub init_sliders {
    $sliderset ||= 0;
    my $c = shift;
    my $x = $slider_offset - 32;
    my $y = 10;
    foreach my $control (sort keys %controls) {
	if ($control eq 'bpm' and $sliderset > 0) {
	    $y += 16;
	    next;
	}
	$c->create(('text',
		    $x, $y
		   ),
		   -tags    => ["${c}_text_$control",
				'text'
			       ],
		   -fill    => 'black',
		   -text    => $control,
		  );
	$c->create(('text',
		    $x + 145, $y
		   ),
		   -tags    => ["${c}_number_$control",
				'text'
			       ],
		   -fill    => 'black',
		   -text    => ($controls{$control}->{start} or 0),
		  );
	my $tag = make_slide($c, 0, $y, $control);
	$y += 16;
	if (exists $controls{$control}->{start}) {
	    move($c, $tag, 'start');
	}
    }
    ++$sliderset;
}

##

sub make_slide {
    my($c, $x, $y, $label) = @_;
    $x += $slider_offset;
    $c->create(('line',
		$x, $y,
		$x + 99, $y
	       ),
	       -tags    => ["${c}_line_$label",
			    'line'
			   ],
	       -fill    => 'black',
	      );
    my $tag = "${c}_ctrl_$label";
    $c->create(('rectangle',
		$x - 2, $y-2,
		$x + 2, $y+2,
	       ),
	       -tags    => [$tag,
			    'ctrl'
			   ],
	       -fill    => 'black',
	      );

    $c->bind($tag, '<B1-Motion>', 
	     [\&move,
	      $tag,
	      Ev('x')
	     ]
	    );
    return $tag;
    
}

##

sub move {
    my ($c, $tag, $x) = @_;

    my $control = $tag;
    $control =~ s/^.+_ctrl_//;

    if ($x eq 'start') {
	my $start = ($controls{$control}->{start} || 0);
	my $max = $controls{$control}->{max};
	my $min = $controls{$control}->{min};
	my $range = $max - $min;
	
	$x = ((($start - $min) / $range) * 100);

	$x += $slider_offset;
    }
    if ($x > (100 + $slider_offset)) {
	$x = 100 + $slider_offset;
    }
    if ($x < $slider_offset) {
	$x = $slider_offset;
    }

    my @coords = $c->coords($tag);
    $coords[0] = $x - 2;
    $coords[2] = $x + 2;
    $c->coords($tag, @coords);
    my $value = ($x - $slider_offset - 1) / 100;
    my $range = $controls{$control}->{max} - $controls{$control}->{min};
    $value = (($range * $value) + $controls{$control}->{min});
    on_ctrl($c, $control, $value);
}

##

sub init_msg {
    
    my $extra  = ($ENV{MSGR} ? "-record " : "");
    my $nice = ($ENV{MSGN} 
		? "nice -n -$ENV{MSGN}"
		: ""
	       );

    open(MSGFH, "|$nice ./MSG -nobuffer -silent $extra")
      or die "couldn't open ./MSG\n";
}

##

sub MSG {
    my $datum = shift;
    if (not defined syswrite(MSGFH, "$datum\n")) {
	warn("failed MSG\n");
    }
}

##

sub item_move {
  my ($c, $tag, $phrase_tag, $x, $y) = @_;
  $x = ($x - ($x % $gridx));
  $y = ($y - ($y % $gridy));
  return if $y <= 0 or $x <= 0;

  $last_phrase_move = $phrase_tag;

  my @coords = $c->coords($tag);
  my @diff = ($x - $coords[0], $y - $coords[1]);

  foreach my $cell_tag ($c->find('withtag', $phrase_tag)) {
    my @item_coords = $c->coords($cell_tag);
    $item_coords[0] += $diff[0]; 
    $item_coords[1] += $diff[1];
    $item_coords[2] += $diff[0];
    $item_coords[3] += $diff[1];
    
    $c->coords($cell_tag, @item_coords);

    foreach my $line ($c->find('withtag', "from_$cell_tag")) {
      my @line_coords = $c->coords($line);
      $line_coords[0] = $item_coords[0];
      $line_coords[1] = $item_coords[1];
      $c->coords($line, @line_coords);
    }

    foreach my $line ($c->find('withtag', "to_$cell_tag")) {
      my @line_coords = $c->coords($line);
      $line_coords[2] = $item_coords[0];
      $line_coords[3] = $item_coords[1];
      $c->coords($line, @line_coords);
    }
    
    #mung_color($c, $item, $item_coords[0] + $diff[0]);
  }
}

##

sub item_move_end {
  my ($tag, $x, $y) = @_;
}

##

{
  my %samples;

  sub init_samples {
      my $c = shift;
      my $sampleset = shift @samplesets;
      my $sample_dir = "samples/$sampleset";
      
      opendir(DIR, $sample_dir) || die "can't opendir $sample_dir: $!";
      my @files = grep { -f "$sample_dir/$_" } readdir(DIR);
      closedir DIR;
      
      my $inc = 0;
      
      foreach my $file (@files) {
	  next if not $file =~ /wav$/i;
	  my $as = "${sampleset}$inc";
	  MSG("load $sample_dir/$file as $as");
	  push @{$samples{$c}}, $as;
	  $inc++;
    }
  }

  ##

  sub rand_event {
    my $c = shift;
    my $rand_pitch = (rand(16)) - 8;
    
    if (rand(8) > 6) {
      return(255, undef);
    }
    my $sample_no = rand @{$samples{$c}};
    my $sample = $samples{$c}->[$sample_no];
    my $cell_brightness = ((255 - int(($sample_no / @{$samples{$c}}) * 205)) + 50);
    return($cell_brightness,
	   sub {
	     my($c, $x, $y, $maxx, $maxy) = @_;
	     
	     $y = ($y / $maxy);
	     
	     my $rand_cutoff = $y * 10000;
	     $rand_cutoff += 100;

	     my $pan = ($x / $maxx);
	     MSG(
		 'play ' . $sample
		 . ' vol '
		 . $values{$c}->{volume}
		 . ' pitch '
		 . ($rand_pitch + $values{$c}->{pitch})
		 . ' cutoff '
		 . $rand_cutoff
		 . ' res 0.6 '
		 . ' pan '
		 . $pan
		 . ' in chr'
		)
	   }
	  );
  }
}

##

sub on_ctrl {
  my ($c, $control, $value) = @_;
  
  $value = int($value + 0);

  if ($control eq 'complexity') {
    while (int($values{$c}->{$control}) < $value) {
      make_phrase($c);
    }
    while (int($values{$c}->{$control}) > $value) {
      unmake_random_phrase($c);
    }
  }
  elsif ($control eq 'intensity') {
    while ($values{$c}->{$control} < $value) {
      make_thing($c);
      $values{$c}->{$control}++;
    }
    while ($values{$c}->{$control} > $value) {
      unmake_random_thing($c);
      $values{$c}->{$control}--;
    }
  }
  elsif ($control eq 'bpm') {
      $values{bpm} = $value;
      $main->afterCancel($bpmrepeat);
      if ($values{bpm}) {
	  $bpmrepeat = $c->repeat(60000 / ($values{bpm} * 4), \&on_clock);
      }
  }
  else {
    $values{$c}->{$control} = $value;
  }


  my ($cell) = $c->find('withtag', "${c}_number_$control");
  $c->itemconfigure($cell, -text => $value);
}

##
my $foo;
sub on_clock {
    foreach my $c (@cs) {
	move_things($c);
    }
    ++$foo;
}

##

{
  my %things;
  my $thing_id;
  sub make_thing {
    my $c = shift;
    my $first_cell = first_cell($c);
    $thing_id ||= 0;
    my $thing_tag = "${c}_thing_$thing_id";
    my ($x1, $y1) = $c->coords($first_cell);
    $c->create(('oval', 
		$x1, $y1, $x1 + $cell_width, $y1 + $cell_height
	       ),
	       -tags    => ['thing',
			    $thing_tag
			   ],
	       -fill    => 'white',
	       -outline => 'white'
	      );

    $things{$c}->{$thing_tag} = $first_cell;
    ++$thing_id;
    $thing_tag;
  }

  sub unmake_thing {
    my ($c, $thing) = @_;
    $c->delete($thing);
    delete $things{$c}->{$thing};
  }
  
  sub unmake_random_thing {
    my $c = shift;
    my @things = keys %{$things{$c}};
    unmake_thing($c, $things[rand(@things)]) if @things;
  }

  sub move_things {
    my $c = shift;
    foreach my $thing (keys %{$things{$c}}) {
      $c->raise($thing);
      my $to = next_cell($c, $thing, $things{$c}->{$thing});
      $c->coords($thing, $c->coords($to));
      $things{$c}->{$thing} = $to;
      play($c, $to);
    }
  }
}


##

{
  my %silence_id;
  my %silence_ids;
  my %loops;
  my %last_tag;

  my $head_pos;

  my(%incs, %inc);
  my $last;
  my %phrase_starts;
  my $phrase_id;
  my @active_lines;

  sub unmake_phrase {
    my ($c, $phrase_tag) = @_;
    foreach my $cell ($c->find('withtag', $phrase_tag)) {
      $c->delete($cell);
      delete $loops{$c}->{$cell};
    }
    
    $c->delete($phrase_tag . '_line');

    my ($from, $to) = @{$phrase_starts{$c}->{$phrase_tag}};
    delete $phrase_starts{$c}->{$phrase_tag};
    $loops{$c}->{$from} = [grep {$_ ne $to} @{$loops{$c}->{$from}}];
    $values{$c}->{complexity}--;
  }

  sub reverse_phrase {
    my($main, $c) = @_;
    my $phrase = $last_phrase_move;
    return if not $phrase;
    my ($from, $to) = @{$phrase_starts{$c}->{$phrase}};
    my @cells;
    $loops{$c}->{$from} = [grep {$_ ne $to} @{$loops{$c}->{$from}}];
    while ($to !~ /^silence/) {
      push @cells, $to;
      $to = $loops{$c}->{$to}->[0];
    }
    my @rev = reverse @cells;
    
    push @{$loops{$c}->{$from}}, $rev[0];

    my $cell;
    my @colors = map {$c->itemcget($_, '-fill')} @cells;

    while(@rev) {
      $cell = shift @rev;
      $c->itemconfigure($cell, -fill => shift(@colors));
      if(@rev) {
	$loops{$c}->{$cell} = [$rev[0]];
      }
    }
    $loops{$c}->{$cell} = [$to];
    $phrase_starts{$c}->{$phrase} = [$from, $cells[-1]];
  }
  
  sub unmake_random_phrase {
    my $c = shift;
    
    my @phrases = keys %{$phrase_starts{$c}};
    unmake_phrase($c, $phrases[rand @phrases]);
  }

  sub first_cell {
      my $c = shift;
      return $loops{$c}->{begin}->[0];
  }

  sub next_cell {
    my($c, $thing, $tag) = @_;
    
    my $linc = $values{$c}->{independence} ? \$incs{$thing} : \$inc{$c};

    my $next_tag;
    my $next_tags = $loops{$c}->{$tag};
    if (not $next_tags) {
      warn "reverting";
      $next_tags = $loops{$c}->{first_cell($c)};
    }
    $$linc ||= 0;
    
    if (not @$next_tags) {
      $next_tag = first_tag();
      warn("reverting\n");
    }
    else {
      if (@$next_tags == 1) {
	$next_tag = $next_tags->[0];
      }
      else {
	if (rand(1) < (abs($values{$c}->{disorder} || 0) / 100)) {
	  $next_tag = $next_tags->[rand(@$next_tags)];
	}
	else {
	  if ($$linc >= @$next_tags) {
	    $$linc = 0;
	  }
	  $next_tag = $next_tags->[$$linc];
	  ++$$linc;
	}
      }
    }
    
    $last_tag{$c} ||= '';
    $last_tag{$c} = $tag;
    return $next_tag;
  }

  sub deactivate_lines {
    my $c = shift;
    foreach my $active_line (@active_lines) {
      $c->itemconfigure($active_line, 
			'-fill' => $line_color
		       );
    }
    undef @active_lines;
  }
  
  sub start_phrase_link {
      my $c = shift;
      die "last is set and shouldn't be" if $last;
      $phrase_id ||= 0;
      $head_pos = rand @{$silence_ids{$c}};
      my $head = $silence_ids{$c}->[$head_pos];
      $last = $head;
      
      my $phrase_tag = "phrase_$phrase_id";
      
      return($head, $phrase_tag);
  }
    
  sub hack_start_phrase_link {
    my ($c, $phrase_tag, $head, $to) = @_;
    $phrase_starts{$c}->{$phrase_tag} = [$head, $to];
  }
  
  sub cell_phrase_link {
      my($c, $cell) = @_;
      
      push @{$loops{$c}->{$last}}, $cell;
      $last = $cell;
  }
  
  sub complete_phrase_link {
      my $c = shift;
      my $tail_pos = (int(rand(scalar(@{$silence_ids{$c}}) / 4)) * 4);
      
      $tail_pos += $head_pos;
      $tail_pos = ($tail_pos % scalar(@{$silence_ids{$c}}));
      
      my $tail = $silence_ids{$c}->[$tail_pos];
      push @{$loops{$c}->{$last}}, $tail;
    
      undef $last;
      ++$phrase_id;
      return $tail;
  }
  
  sub silence_link {
      my $c = shift;
      $silence_id{$c} ||= 0;
      my $tag = "${c}_silence_$silence_id{$c}";
      
      $loops{$c}->{$last || 'begin'} = [$tag];
      $loops{$c}->{$tag} = 'last';
      
      $last = $tag;
      
      push @{$silence_ids{$c}}, $tag;
      
      ++$silence_id{$c};
      return $tag;
  }

  sub complete_silence {
      my $c = shift;
      $loops{$c}->{$last} = [first_cell($c)];
      undef $last;
  }
}

##

sub make_silence {
  my ($c, $x1, $y1, $length) = @_;
  my $cell_brightness = 127;
  
  my $width = $cell_width * 2;
  my $height = $cell_height * 2;
  
  foreach my $cell (0 .. ($length - 1)) {
    my $silence_tag = silence_link($c);
    make_cell($c, 
	      $x1 + ($cell * $width), 
	      $y1,
	      $cell_brightness,
	      'silence',
	      $silence_tag
	     );
  }

  foreach my $cell (0 .. ($length - 1)) {
    my $silence_tag = silence_link($c);
    make_cell($c, 
	      $x1 + ($length * $width),
	      $y1 + ($cell * $height),
	      $cell_brightness,
	      'silence',
	      $silence_tag
	     );
  }

  foreach my $cell (0 .. ($length - 1)) {
    my $silence_tag = silence_link($c);
    make_cell($c, 
	      $x1 + ($length * $width) - ($cell * $width), 
	      $y1 + ($length * $height),
	      $cell_brightness,
	      'silence',
	      $silence_tag
	     );
  }

  foreach my $cell (0 .. ($length - 1)) {
    my $silence_tag = silence_link($c);
    make_cell($c, 
	      $x1, 
	      ($y1 + ($length * $height)) - ($cell * $height),
	      $cell_brightness,
	      'silence',
	      $silence_tag
	     );
  }
  complete_silence($c);
}

##

{
  my %events;
  sub attach_event {
    my ($c, $event, $tag) = @_;
    
    $events{$c}->{$tag} = $event;
  }

  sub clear_event {
    my ($c, $tag) = @_;
    
    delete $events{$c}->{$tag};
  }
  
  sub play {
    my($c, $tag) = @_;
    if (my $event = $events{$c}->{$tag}) {
      my @coords = $c->coords($tag);
      $event->($c, $coords[0], $coords[1], $canvas_width, $canvas_height);
    }
  }
}  

sub make_phrase {
  my $c = shift;
  
  my($head, $phrase_tag) = start_phrase_link($c);

  my($first_cell_tag, $last_cell_tag);
  
  my ($x1, $y1) = rand_position();
    
  my $length = int(rand(4) + 1);
  foreach my $cell_id (0 .. ($length - 1)) {
    my($cell_brightness, $event) = rand_event($c);

    my($tagid, $cell_tag) = make_cell($c, $x1, $y1, $cell_brightness,
				      $phrase_tag
				     );
    $first_cell_tag ||= $tagid;
    $last_cell_tag = $tagid;

    cell_phrase_link($c, $tagid);

    attach_event($c, $event, $tagid);
    
    
    $x1 += $cell_width;
  }

  hack_start_phrase_link($c, $phrase_tag, $head, $first_cell_tag);
  my $tail = complete_phrase_link($c);

  make_line($c, $phrase_tag, $head, $first_cell_tag);
  make_line($c, $phrase_tag, $last_cell_tag, $tail);
  

  $c->bind($phrase_tag, '<B1-Motion>', [\&item_move,
					$first_cell_tag,
					$phrase_tag,
					Ev('x'), Ev('y'), 
				       ]
	  );
  $c->bind($phrase_tag, '<ButtonRelease-1>', [\&item_move_end, 
					      $first_cell_tag,
					      $phrase_tag,
					      Ev('x'), Ev('y'), 
					     ]
	  );
  $c->bind($phrase_tag, '<Button-3>', [\&unmake_phrase,
				       $phrase_tag
				      ]
	  );
  $values{$c}->{complexity}++;
}

##

sub make_line {
  my ($c, $phrase_tag, $from, $to) = @_;
  my ($fx, $fy) = $c->coords($from);
  my ($tx, $ty) = $c->coords($to);
  my $line = $c->createLine($fx, $fy, $tx, $ty, 
			    -tags => ["from_$from", "to_$to", "line",
				      "${phrase_tag}_line"
				     ], 
			    -arrow => 'last', 
			    -fill => $line_color,
			    -dash => [2, 1, 1, 2]
			   );
  $c->lower($line);
  return $line;
}

##

my $cellcount;
sub make_cell {
  my ($c, $x1, $y1, $cell_brightness, @tags) = @_;
  $cellcount ||= 0;
  my $tag = "${c}_cell_$cellcount";
  my $tagid = $c->create(('rectangle', 
			  $x1, $y1, $x1 + $cell_width, $y1 + $cell_height
			 ),
			 -tags    => ['cell',
				      @tags,
				      $tag
				     ],
			 -outline    => sprintf("#%02x%02x%02x",
						$cell_brightness,
						$cell_brightness,
						$cell_brightness
					       ),
			 -fill    => sprintf("#%02x%02x%02x",
					     $cell_brightness,
					     $cell_brightness,
					     $cell_brightness
					    ),
			 -width => 1,
			);
  ++$cellcount;
  return($tagid, $tag);
}


##

{
  my $inc;
  sub rand_position {
    my ($x, $y);
    $inc ||= 0;
    $inc = ($cell_height * 3) if $inc < ($cell_height * 3);
    if ($inc > $canvas_height) {
      $inc = $cell_height * 3;
    }
    my $offsetx = int(rand($canvas_width - ($cell_width * 4)));
    $offsetx -= $offsetx % $cell_width;
    $x = $cell_width * 2 + $offsetx;
    
    $y = $inc;
    
    $inc += $cell_height * 3;
    return($x, $y);
  }
}

##

exit 0;

