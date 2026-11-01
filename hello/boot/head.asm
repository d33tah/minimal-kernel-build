; 32-bit entry of the hello kernel, linked at code32_start (0x100000 for
; bzImage, 0x10000 for bzImage.low).
; Contract from the setup code: flat 32-bit protected mode, CS/DS/ES/SS
; flat, interrupts off, ESI = boot_params. We only need a stack.

	bits	32
	section	.text.head
	global	startup_32
	extern	hello_main
startup_32:
	cli
	mov	esp, stack_top
	call	hello_main		; does not return

	section	.bss
	align	16
	resb	256			; hello_main needs a few bytes
stack_top:
