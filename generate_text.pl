#!/usr/bin/perl

$lines = $ARGV[0];
$chars = $ARGV[1];

die unless $lines;
die unless $chars;

while ($lines--) {
	print int(rand(9)) x $chars;
	print "\n";
}
