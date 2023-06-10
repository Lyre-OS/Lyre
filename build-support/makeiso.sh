#!/bin/sh

set -ex

# Build the sysroot with jinx and build limine.
rm -rf sysroot
./jinx sysroot
./jinx host-build limine

# Make an initramfs with the sysroot.
( cd sysroot && tar cf ../initramfs.tar * )

# Prepare the iso and boot directories.
rm -rf iso_root
mkdir -pv iso_root/boot
cp sysroot/usr/share/lyre/lyre iso_root/boot/lyre.elf
cp initramfs.tar iso_root/boot/
cp build-support/limine.cfg iso_root/boot/

# Install the limine binaries.
cp host-pkgs/limine/usr/local/share/limine/limine.sys iso_root/boot/
cp host-pkgs/limine/usr/local/share/limine/limine-cd.bin iso_root/boot/
cp host-pkgs/limine/usr/local/share/limine/limine-cd-efi.bin iso_root/boot/
mkdir -pv iso_root/EFI/BOOT
cp host-pkgs/limine/usr/local/share/limine/BOOT*.EFI iso_root/EFI/BOOT/

# Create the disk image.
xorriso -as mkisofs -b boot/limine-cd.bin -no-emul-boot -boot-load-size 4 \
    -boot-info-table --efi-boot boot/limine-cd-efi.bin -efi-boot-part \
    --efi-boot-image --protective-msdos-label iso_root -o lyre.iso

# Install limine.
host-pkgs/limine/usr/local/bin/limine-deploy lyre.iso
