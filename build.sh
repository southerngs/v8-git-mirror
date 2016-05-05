#!/bin/bash

GYPFLAGS="-Dlinux_use_bundled_gold=0 -Dv8_deopt_checks_count=1 -Dv8_enable_disassembler=1" CC=clang CXX=clang++ LINK=clang++ make x64.release $@

