#!/usr/bin/perl

use warnings;
use strict;

my @elems_old = {};
while (my $line = <stdin>) {
  my $i;
  my @elems = split ' ', $line;
  if (@elems[0] != @elems_old[0]) {
    print "case ", @elems[0], ":\n";
  }
  if (@elems[1] < 2000000) {
    print "if (size <= ", @elems[1], ") { ";
  }
  print "*copyin_type = ", @elems[2], ";";
  for ($i = 3; $i < @elems; $i = $i + 1) {
    print " copyin_factors[", $i - 3, "] = ", @elems[$i], ";";
  }
  print " copyin_factors[", $i - 3, "] = 0; break;";
  if (@elems[1] < 2000000) {
    print " }";
  }
  print "\n";
  @elems_old = @elems[0];
}
