
all: #
	@echo "Choose target:\n"
	@grep -e ':$$' Makefile | tr -d ':'

copy:
	docker buildx build . --output minified2 --build-arg BUILD_ENV=copy
	mv minified _minified
	mv minified2 minified
	chown 1000:1000 -R minified

vm:
	mkdir -p minified/elo/dev
	cd minified && make LLVM=1 tinyconfig && make LLVM=1 -j1
	-script -c  'timeout 10s  qemu-system-x86_64 -kernel minified/arch/x86/boot/bzImage   -display curses -m 32M'
	ls -lh minified/arch/x86/boot/bzImage*
