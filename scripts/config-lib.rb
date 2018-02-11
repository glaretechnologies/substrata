#!/usr/bin/ruby

# This file defines configuration options to be used instead of hardcoded values.
# The version of Qt we use needs to be accessed by both CMake and ruby build_dist script etc..#

#require './script_utils.rb'

# The config options

$vs_version = 2015 # Visual studio option used to build distribution.  Used in build.rb


$qt_version = "5.8.0" if OS.windows?
$qt_version = "5.8.0" if OS.mac?
$qt_version = "5.5.1" if OS.linux?  # Old version because it's what we have built on the Linux builder


$libressl_version = "2.6.4"

# Get Qt path.
indigo_libs_dir = ENV['INDIGO_LIBS']
if indigo_libs_dir.nil?
	puts "INDIGO_LIBS env var not defined."
	exit(1)
end

indigo_qt_base_dir = "#{indigo_libs_dir}/Qt"

$indigo_qt_dir = ""
if OS.unix?
	$indigo_qt_dir = "#{indigo_qt_base_dir}/#{$qt_version}"
else
	$indigo_qt_dir = "#{indigo_qt_base_dir}/#{$qt_version}-vs#{$vs_version}-64"
end
