#pragma once
#include <cstdint>

// === Port I/O ===

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

// === 8259 PIC ===

#define PIC_MASTER_CMD  0x20
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_CMD   0xA0
#define PIC_SLAVE_DATA  0xA1
#define PIC_EOI         0x20

static inline void pic_init() {
    // ICW1: begin initialization (edge triggered, cascade, ICW4 needed)
    outb(PIC_MASTER_CMD, 0x11);
    outb(PIC_SLAVE_CMD,  0x11);

    // ICW2: remap IRQ base vectors to 0x20 (master) and 0x28 (slave)
    outb(PIC_MASTER_DATA, 0x20);
    outb(PIC_SLAVE_DATA,  0x28);

    // ICW3: master has slave on IRQ2; slave cascade identity
    outb(PIC_MASTER_DATA, 0x04);
    outb(PIC_SLAVE_DATA,  0x02);

    // ICW4: x86 mode
    outb(PIC_MASTER_DATA, 0x01);
    outb(PIC_SLAVE_DATA,  0x01);

    // Mask all IRQs (enable them one-by-one later)
    outb(PIC_MASTER_DATA, 0xFF);
    outb(PIC_SLAVE_DATA,  0xFF);
}

static inline void pic_unmask(uint8_t irq) {
    if (irq < 8)
        outb(PIC_MASTER_DATA, inb(PIC_MASTER_DATA) & ~(1 << irq));
    else
        outb(PIC_SLAVE_DATA,  inb(PIC_SLAVE_DATA)  & ~(1 << (irq - 8)));
}

static inline void pic_send_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC_SLAVE_CMD, PIC_EOI);
    outb(PIC_MASTER_CMD, PIC_EOI);
}
