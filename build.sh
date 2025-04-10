#!/usr/bin/env bash
gcc -o tge_test.so tge_test.c $(yed --print-cflags yed --print-ldflags) -Wall -Wextra -Werror
