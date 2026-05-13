global gdt_flush
gdt_flush:
    ; rdi = pointer to gdt_ptr_struct
    lgdt [rdi]

    ; Reload data segment selectors
    ; 0x10 = GDT entry 2 (kernel data segment)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Far jump to reload CS (code segment)
    ; Pushing 0x08 (GDT entry 1, kernel code) and the address of
    ; flush_label, then retfq pops both and jumps there with CS set.
    push 0x08
    lea rax, [rel flush_label]
    push rax
    retfq

flush_label:

    ; Load the TSS via LTR (Task Register)
    ; 0x28 = GDT entry 5 (TSS descriptor, 5 * 8 = 0x28)
    mov ax, 0x28
    ltr ax

    ret
