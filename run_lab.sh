#!/bin/sh
./xc -v -o root/etc/os-$1 -Iroot/lib root/etc/os-$1.c
./xem -f fs.img root/etc/os-$1
