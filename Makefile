
all: #
	@echo "Choose target:\n"
	@grep -e ':$$' Makefile | tr -d ':'

copy:
	docker buildx build . --output minified2 --build-arg BUILD_ENV=copy
	mv minified _minified
	mv minified2 minified
	chown 1000:1000 -R minified

vm:
	cd minified && find -name '*.h' -or -name '*.c' -exec clang-format -i {} \; && cloc . > /tmp/cloc && make tinyconfig -j1 && make -j1
	./vmtest.tcl
	ls -lh minified/arch/x86/boot/bzImage*
	cat /tmp/cloc

test:
	cd minified && make mrproper && cd .. && make vm
