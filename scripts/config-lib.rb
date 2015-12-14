#!/usr/bin/ruby

# This file defines configuration options to be used instead of hardcoded values.

#require './script_utils.rb'

# The config options
$qt_version = "5.5.1" if OS.windows?
$qt_version = "5.5.1" if OS.mac?
$qt_version = "5.5.1" if OS.linux?

$vs_version = 2012

$llvm_version = "3.4"

$libressl_version = "2.3.0"
$openssl_version = "1.0.2a"
