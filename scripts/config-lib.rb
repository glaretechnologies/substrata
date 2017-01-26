#!/usr/bin/ruby

# This file defines configuration options to be used instead of hardcoded values.

#require './script_utils.rb'

# The config options
$qt_version = "5.7.0" if OS.windows?
$qt_version = "5.7.0" if OS.mac?
$qt_version = "5.7.0" if OS.linux?

$vs_version = 2012

$llvm_version = "3.4"

$libressl_version = "2.3.0"
$openssl_version = "1.0.2a"

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

$libjpgturbo_dir = ""
if OS.unix?
	$libjpgturbo_dir = "#{indigo_libs_dir}/libjpeg-turbo-builds/build"
else
	$libjpgturbo_dir = "#{indigo_libs_dir}/libjpeg-turbo-builds" # vs_#{$vs_version}_64" dir suffix will be appended in CMakeLists.txt.
end