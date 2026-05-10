global gdt_flush
gdt_flush:
    ; rdi = pointer to gdt_ptr_struct
    lgdt [rdi]

    ; Reload data segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Far jump to reload CS
    push 0x08
    lea rax, [rel flush_label]
    push rax
    retfq

flush_label:

    mov ax, 0x28   ; TSS selector (entry 5 * 8)
    ltr ax

    ret
