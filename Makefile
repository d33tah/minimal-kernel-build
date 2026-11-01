
all: #
	@echo "Choose target:\n"
	@grep -E '^[a-z-]+:' Makefile | cut -d: -f1

vm:
	cd minified && make LLVM=1 -j6
	./vmtest.tcl
	ls -lh minified/arch/x86/boot/bzImage*

fmt-check:
	cd minified && git ls-files '*.c' '*.h' | xargs clang-format --dry-run -Werror

provenance-test:
	./syscall-uuid-test.sh

test: fmt-check
	cd minified && make mrproper && cd .. && make vm
