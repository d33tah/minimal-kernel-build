; https://www.muppetlabs.com/~breadbox/software/tiny/teensy.html

BITS 32

              org     0x08048000

ehdr:                                                 ; Elf32_Ehdr
              db      0x7F, "ELF", 1, 1, 1, 0         ;   e_ident
      times 8 db      0
              dw      2                               ;   e_type
              dw      3                               ;   e_machine
              dd      1                               ;   e_version
              dd      _start                          ;   e_entry
              dd      phdr - $$                       ;   e_phoff
              dd      0                               ;   e_shoff
              dd      0                               ;   e_flags
              dw      ehdrsize                        ;   e_ehsize
              dw      phdrsize                        ;   e_phentsize
              dw      1                               ;   e_phnum
              dw      0                               ;   e_shentsize
              dw      0                               ;   e_shnum
              dw      0                               ;   e_shstrndx

ehdrsize      equ     $ - ehdr

phdr:                                                 ; Elf32_Phdr
              dd      1                               ;   p_type
              dd      0                               ;   p_offset
              dd      $$                              ;   p_vaddr
              dd      $$                              ;   p_paddr
              dd      filesize                        ;   p_filesz
              dd      filesize                        ;   p_memsz
              dd      5                               ;   p_flags
              dd      0x1000                          ;   p_align

phdrsize      equ     $ - phdr

_start:

    mov eax, 4
    mov ebx, 1
    mov ecx, hello
    mov edx, hello_len
    int 0x80

    ; read one byte somewhere from stdin so we can test terminal:
    mov eax, 3
    xor ebx, ebx
    mov ecx, hello
    mov edx, 1
    int 0x80

    mov eax, 4
    mov ebx, 1
    mov ecx, still_alive_msg
    mov edx, still_alive_len
    int 0x80

    mov eax, 1
    xor ebx, ebx
    int 0x80

hello:
hello db 'Hello, World!', 0xA
hello_len equ $ - hello

still_alive_msg:
still_alive_msg db 'Still alive', 0xA
still_alive_len equ $ - still_alive_msg

filesize      equ     $ - $$
