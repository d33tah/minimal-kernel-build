
all: #
	@echo "Choose target:\n"
	@grep -e ':$$' Makefile | tr -d ':'

copy:
	docker buildx build . --output minified2 --build-arg BUILD_ENV=copy
	mv minified _minified
	mv minified2 minified

vm:
	cd minified && make tinyconfig && make
	-script -c  'timeout 10s  qemu-system-x86_64 -kernel minified/arch/x86/boot/bzImage   -display curses'

