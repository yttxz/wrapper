#!/bin/sh
set -eu
# The guest resolver cannot see Docker's /etc/resolv.conf after chroot.
mkdir -p /app/rootfs/etc
cp /etc/resolv.conf /app/rootfs/etc/resolv.conf
