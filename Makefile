
all: #
	@echo "Choose target:\n"
	@grep -e ':$$' Makefile | tr -d ':'

copy:
	docker buildx build . --output minified2 --build-arg BUILD_ENV=copy
	mv minified _minified
	mv minified2 minified

vm:
	mkdir -p minified/elo/dev
	[ -c minified/elo/dev/console ] || sudo mknod -m 622 minified/elo/dev/console c 5 1
	cd minified && make tinyconfig && make LLVM=1 -j4
	-script -c  'timeout 10s  qemu-system-x86_64 -kernel minified/arch/x86/boot/bzImage   -display curses -m 32M'
	ls -lh minified/arch/x86/boot/bzImage*
