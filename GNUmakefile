QEMUFLAGS ?= -M q35,smm=off -m 8G -boot order=dc -cdrom lyre.iso -serial stdio -smp 4

.PHONY: all
all:
	rm -f lyre.iso
	$(MAKE) lyre.iso

lyre.iso: jinx
	./build-support/makeiso.sh

kernel:
	git clone https://github.com/Lyre-OS/klyre.git kernel

.PHONY: debug
debug:
	JINX_CONFIG_FILE=jinx-config-debug $(MAKE) all

jinx:
	curl -Lo jinx https://github.com/mintsuki/jinx/raw/7a101a39eb061713f9c50ceafa1d713f35f17a3b/jinx
	chmod +x jinx

.PHONY: run-kvm
run-kvm: lyre.iso
	qemu-system-x86_64 -enable-kvm -cpu host $(QEMUFLAGS)

.PHONY: run-hvf
run-hvf: lyre.iso
	qemu-system-x86_64 -accel hvf -cpu host $(QEMUFLAGS)

ovmf/ovmf-code-x86_64.fd:
	mkdir -p ovmf
	curl -Lo $@ https://github.com/osdev0/edk2-ovmf-nightly/releases/latest/download/ovmf-code-x86_64.fd

ovmf/ovmf-vars-x86_64.fd:
	mkdir -p ovmf
	curl -Lo $@ https://github.com/osdev0/edk2-ovmf-nightly/releases/latest/download/ovmf-vars-x86_64.fd

.PHONY: run-uefi
run-uefi: lyre.iso ovmf/ovmf-code-x86_64.fd ovmf/ovmf-vars-x86_64.fd
	qemu-system-x86_64 \
		-enable-kvm \
		-cpu host \
		-drive if=pflash,unit=0,format=raw,file=ovmf/ovmf-code-x86_64.fd,readonly=on \
		-drive if=pflash,unit=1,format=raw,file=ovmf/ovmf-vars-x86_64.fd \
		$(QEMUFLAGS)

.PHONY: run
run: lyre.iso
	qemu-system-x86_64 $(QEMUFLAGS)

.PHONY: clean
clean:
	rm -rf iso_root sysroot lyre.iso initramfs.tar

.PHONY: distclean
distclean: clean
	make -C kernel distclean
	rm -rf .jinx-cache jinx builds host-builds host-pkgs pkgs sources ovmf
