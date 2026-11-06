; 32-bit entry of the hello kernel, linked at code32_start (0x100000 for
; bzImage, 0x10000 for bzImage.low).
; Contract from the setup code: flat 32-bit protected mode, CS/DS/ES/SS
; flat, interrupts off, ESI = boot_params. We only need a stack.

	bits	32
	section	.text.head
	; Sized, so debuggers (the page's symbol history and backtraces) know
	; which code is startup_32.
	global	startup_32:function (startup_32_size)
	extern	hello_main
startup_32:
	cli
	mov	esp, stack_top
	call	hello_main		; does not return
startup_32_size equ $ - startup_32

	section	.bss
	align	16
	resb	256			; hello_main needs a few bytes
stack_top:
