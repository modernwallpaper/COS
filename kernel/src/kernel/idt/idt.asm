global isr0,isr1,isr2,isr3,isr4,isr5,isr6,isr7
global isr8,isr9,isr10,isr11,isr12,isr13,isr14,isr15
global isr16,isr17,isr18,isr19,isr20,isr21,isr22,isr23
global isr24,isr25,isr26,isr27,isr28,isr29,isr30,isr31

global irq0,irq1,irq2,irq3,irq4,irq5,irq6,irq7
global irq8,irq9,irq10,irq11,irq12,irq13,irq14,irq15

global apic_timer_stub

extern isr_handler

; ------------------------
; CPU EXCEPTIONS
; ------------------------

; Macros: exceptions without error code
%macro ISR_NOERR 1
isr%1:
    push 0              ; fake error code
    push %1             ; vector number
    jmp isr_common
%endmacro

; exceptions with error code
%macro ISR_ERR 1
isr%1:
    push %1             ; vector number
    jmp isr_common
%endmacro

; ------------------------
; IRQs
; ------------------------
%macro IRQ_STUB 1
irq%1:
    push 0              ; fake error code
    push 32+%1          ; vector number
    jmp isr_common
%endmacro

; ------------------------
; COMMON ISR ENTRY
; ------------------------
isr_common:
    ; save all general purpose registers
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp        ; pointer to interrupt_frame
    call isr_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    add rsp,16          ; remove vector + error code
    iretq

; ------------------------
; CPU Exception Stubs
; ------------------------
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; ------------------------
; IRQ Stubs
; ------------------------
IRQ_STUB 0
IRQ_STUB 1
IRQ_STUB 2
IRQ_STUB 3
IRQ_STUB 4
IRQ_STUB 5
IRQ_STUB 6
IRQ_STUB 7
IRQ_STUB 8
IRQ_STUB 9
IRQ_STUB 10
IRQ_STUB 11
IRQ_STUB 12
IRQ_STUB 13
IRQ_STUB 14
IRQ_STUB 15

; ------------------------
; APIC Timer Stub
; ------------------------
%macro APIC_STUB 2
%1:
    push 0
    push %2
    jmp isr_common
%endmacro

APIC_STUB apic_timer_stub, 48
