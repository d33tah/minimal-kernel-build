
all: #
	@echo "Choose target:\n"
	@grep -E '^[a-z-]+:' Makefile | cut -d: -f1

copy:
	docker buildx build . --output minified2 --build-arg BUILD_ENV=copy
	mv minified _minified
	mv minified2 minified
	chown 1000:1000 -R minified

vm:
	cd minified && make LLVM=1 -j6
	./vmtest.tcl
	ls -lh minified/arch/x86/boot/bzImage*

fmt-check:
	cd minified && git ls-files '*.c' '*.h' | xargs clang-format --dry-run -Werror

test: fmt-check
	cd minified && make mrproper && cd .. && make vm
