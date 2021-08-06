include Makefile.inc

BOOT = boot
KERNEL = kernel
USER = user

xv6.img: subdirs
	dd if=/dev/zero of=xv6.img count=10000
	dd if=$(BOOT)/bootblock of=xv6.img conv=notrunc
	dd if=$(KERNEL)/kernel of=xv6.img seek=1 conv=notrunc

xv6memfs.img: subdirs
	dd if=/dev/zero of=xv6memfs.img count=10000
	dd if=$(BOOT)/bootblock of=xv6memfs.img conv=notrunc
	dd if=$(KERNEL)/kernelmemfs of=xv6memfs.img seek=1 conv=notrunc

$(USER)/fs.img: subdirs

.PHONY: subdirs
subdirs:
	cd $(BOOT) && $(MAKE) bootblock
	cd $(KERNEL) && $(MAKE) kernel
	cd $(KERNEL) && $(MAKE) kernelmemfs
	cd $(USER) && $(MAKE) fs.img

# make a printout
FILES = $(shell grep -v '^\#' runoff.list)
PRINT = runoff.list runoff.spec README toc.hdr toc.ftr $(FILES)

xv6.pdf: $(PRINT)
	./runoff
	ls -l xv6.pdf

print: xv6.pdf

# run in emulators

bochs : $(USER)/fs.img xv6.img
	if [ ! -e .bochsrc ]; then ln -s dot-bochsrc .bochsrc; fi
	/opt/bochs/bin/bochs -q

# If the makefile can't find QEMU, specify its path here
# QEMU = qemu-system-i386

# Try to infer the correct QEMU
ifndef QEMU
QEMU = $(shell if which qemu > /dev/null; \
	then echo qemu; exit; \
	elif which qemu-system-i386 > /dev/null; \
	then echo qemu-system-i386; exit; \
	elif which qemu-system-x86_64 > /dev/null; \
	then echo qemu-system-x86_64; exit; \
	else \
	qemu=/Applications/Q.app/Contents/MacOS/i386-softmmu.app/Contents/MacOS/i386-softmmu; \
	if test -x $$qemu; then echo $$qemu; exit; fi; fi; \
	echo "***" 1>&2; \
	echo "*** Error: Couldn't find a working QEMU executable." 1>&2; \
	echo "*** Is the directory containing the qemu binary in your PATH" 1>&2; \
	echo "*** or have you tried setting the QEMU variable in Makefile?" 1>&2; \
	echo "***" 1>&2; exit 1)
endif

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
ifndef CPUS
CPUS := 2
endif
QEMUOPTS = -drive file=$(USER)/fs.img,index=1,media=disk,format=raw -drive file=xv6.img,index=0,media=disk,format=raw -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu: xv6.img $(USER)/fs.img
	$(QEMU) -serial mon:stdio $(QEMUOPTS)

qemu-memfs: xv6memfs.img
	$(QEMU) -drive file=xv6memfs.img,index=0,media=disk,format=raw -smp $(CPUS) -m 256

qemu-nox: xv6.img $(USER)/fs.img
	$(QEMU) -nographic $(QEMUOPTS)

.gdbinit: .gdbinit.tmpl
	sed "s/localhost:1234/localhost:$(GDBPORT)/" < $^ > $@

qemu-gdb: xv6.img $(USER)/fs.img .gdbinit
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -serial mon:stdio $(QEMUOPTS) -S $(QEMUGDB)

qemu-nox-gdb: xv6.img $(USER)/fs.img .gdbinit
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -nographic $(QEMUOPTS) -S $(QEMUGDB)

gtags:
	gtags

tags:
	cd $(KERNEL) && make tags

clean: 
	cd $(BOOT); make clean
	cd $(KERNEL); make clean
	cd $(USER); make clean
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*.o *.d *.asm *.sym xv6.img .gdbinit

-include *.d
