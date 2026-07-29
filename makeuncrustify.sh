#!/bin/sh

uncrustify -c uncrustify.cfg --replace --no-backup cmdline/*.c cmdline/*.h
uncrustify -c linux.cfg --replace --no-backup raid/*.c raid/*.h raid/test/*.c raid/test/*.h
