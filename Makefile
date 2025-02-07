
all: #
	@echo "Choose target:\n"
	@grep -e ':$$' Makefile | tr -d ':'

copy:
	docker buildx build . --output minified2 --build-arg BUILD_ENV=copy
	mv minified _minified
	mv minified2 minified
	chown 1000:1000 -R minified

vm:
	cd minified && make LLVM=1 tinyconfig && make CC=iwyu LLVM=1 -j1
	./vmtest.tcl
	ls -lh minified/arch/x86/boot/bzImage*
