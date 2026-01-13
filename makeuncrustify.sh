#!/bin/sh

uncrustify -c uncrustify.cfg --replace --no-backup cmdline/*.c cmdline/*.h
