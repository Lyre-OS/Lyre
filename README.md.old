# Lyre

Lyre is an effort to write a modern, fast, and useful operating system.

<h3>*** Lyre is still pre-alpha software not meant for daily or production usage! ***</h3>

Join the [Discord chat](https://discord.gg/2kdk3CbADg).

## What is Lyre all about?

- Keeping the code as simple and easy to understand as possible, while not sacrificing
performance and prioritising code correctness.
- Making a *usable* OS which can *run on real hardware*, not just on emulators or
virtual machines.
- Targetting modern 64-bit architectures, CPU features, and multi-core computing.
- Maintaining good source-level compatibility with Linux to allow to easily port programs over.
- Having fun.

![Screenshot 0](/screenshot0.png?raw=true "Screenshot 0")
![Screenshot 0](/screenshot1.png?raw=true "Screenshot 1")

[Photo by Pixabay](https://www.pexels.com/photo/body-of-water-near-mountains-158385/)

## Download latest nightly image

You can grab a pre-built nightly Lyre image at https://github.com/lyre-os/lyre/releases

Make sure to boot the ISO with enough memory (3+GiB) as, for now, Lyre loads its
entire root filesystem in a ramdisk in order to be able to more easily boot
on real hardware.

## Build instructions

### Distro-agnostic build prerequisites

The following is a distro-agnostic list of packages needed to build Lyre.

Skip to a paragraph for your host distro if there is any.

`GNU make`, `diffutils`, `curl`, `git`, `bsdtar`, `xorriso`, and `qemu` to test it.

### Build prerequisites for Ubuntu, Debian, and derivatives
```bash
sudo apt install make diffutils curl git libarchive-tools xorriso qemu-system-x86
```

### Build prerequisites for Arch Linux and derivatives
```bash
sudo pacman -S --needed make diffutils curl git libarchive xorriso qemu
```

### Building the distro

To build the distro, which includes the cross toolchain necessary
to build kernel and ports, as well as the kernel itself, run:

```bash
make distro-base # Build the base distribution.
make all         # Make filesystem and ISO.
```

This will build a minimal distro image. The `make distro-full` target
is also avaliable to build the full distro; this step will take a while.

### To test

In Linux, if KVM is available, run with

```
make run-kvm
```

In macOS, if hvf is available, run with

```
make run-hvf
```

To run without any acceleration, run with

```
make run
```
