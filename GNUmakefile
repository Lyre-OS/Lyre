QEMUFLAGS ?= -M q35,smm=off -m 8G -boot order=dc -cdrom lyre.iso -serial stdio 

.PHONY: all
all: jinx
	rm -f builds/kernel.built builds/kernel.installed
	./jinx build base-files kernel
	./build-support/makeiso.sh

.PHONY: debug
debug:
	JINX_CONFIG_FILE=jinx-config-debug $(MAKE) all

jinx:
	curl -o jinx https://raw.githubusercontent.com/mintsuki/jinx/trunk/jinx
	chmod +x jinx

.PHONY: distro-full
distro-full: jinx
	./jinx build-all

.PHONY: distro-base
distro-base: jinx
	./jinx build bash coreutils utils

.PHONY: run-kvm
run-kvm: lyre.iso
	qemu-system-x86_64 -enable-kvm -cpu host $(QEMUFLAGS) -smp 4

.PHONY: run-hvf
run-hvf: lyre.iso
	qemu-system-x86_64 -accel hvf -cpu host $(QEMUFLAGS) -smp 4

ovmf:
	mkdir -p ovmf
	cd ovmf && curl -o OVMF-X64.zip https://efi.akeo.ie/OVMF/OVMF-X64.zip && 7z x OVMF-X64.zip

.PHONY: run-uefi
run-uefi: ovmf
	qemu-system-x86_64 -enable-kvm -cpu host $(QEMUFLAGS) -smp 4 -bios ovmf/OVMF.fd

.PHONY: run
run: lyre.iso
	qemu-system-x86_64 $(QEMUFLAGS) -cpu qemu64,level=11,+sep  -no-shutdown -no-reboot -d int -smp 1

.PHONY: kernel-clean
kernel-clean:
	rm -rf builds/kernel* pkgs/kernel*

.PHONY: base-files-clean
base-files-clean:
	rm -rf builds/base-files* pkgs/base-files*

.PHONY: clean
clean: kernel-clean base-files-clean
	rm -rf iso_root sysroot lyre.iso initramfs.tar

.PHONY: distclean
distclean: jinx
	cd kernel && ./bootstrap && ./configure && make maintainer-clean
	./jinx clean
	rm -rf iso_root sysroot lyre.iso initramfs.tar jinx ovmf
