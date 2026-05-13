#include <cstdint>
#include <inc/kernel/apic/apic.hpp>

Apic::Apic(KShell *kshell) {
    this->kshell = kshell;

    if (!this->check_apic()) {
        kshell->print_kernel_error("CPU does not support APIC");
        return;
    }

    this->apic_base = this->cpu_get_apic_base();
    this->cpu_set_apic_base(this->apic_base);
}

Apic::~Apic() {}

bool Apic::check_apic() {
    uint32_t eax, ebx, ecx, edx;
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(0x1));
    return edx & (1 << 9);
}

uintptr_t Apic::cpu_get_apic_base() {
    uint32_t eax, edx;
    asm volatile("rdmsr" : "=a"(eax), "=d"(edx) : "c"(IA32_APIC_BASE_MSR));
    return ((uintptr_t)(edx & 0x0f) << 32) | (eax & 0xfffff000);
}

void Apic::cpu_set_apic_base(uintptr_t phys_addr) {
    uint32_t eax = (phys_addr & 0xfffff000) | IA32_APIC_BASE_ENABLE;
    uint32_t edx = (phys_addr >> 32) & 0x0f;
    asm volatile("wrmsr" : : "c"(IA32_APIC_BASE_MSR), "a"(eax), "d"(edx));
}

uintptr_t Apic::get_apic_base() { return this->apic_base; }

uint8_t Apic::get_id() {
    return lapic_read(LAPIC_ID_REG) >> 24;
}

uint32_t Apic::lapic_read(uint16_t reg) {
    return *(volatile uint32_t *)(LAPIC_VIRT_BASE + reg);
}

void Apic::lapic_write(uint16_t reg, uint32_t value) {
    *(volatile uint32_t *)(LAPIC_VIRT_BASE + reg) = value;
}

// Enable the APIC by writing the SVR with the ENABLE bit and a spurious vector.
// Vector 0xFF is recommended for the spurious interrupt.
void Apic::enable() {
    uint32_t svr = lapic_read(LAPIC_SVR);
    svr |= LAPIC_SVR_ENABLE;
    svr |= 0xFF; // spurious vector
    lapic_write(LAPIC_SVR, svr);

    // Dummy read to flush the write buffer before potential VM exit.
    lapic_read(LAPIC_SVR);
}

// Set up the APIC timer in periodic mode.
// The timer fires at the specified vector every (initial_count * divider) bus cycles.
// Divide config defaults to 16 (DCR bits = 0x3) after reset.
void Apic::timer_init(uint8_t vector, uint32_t initial_count) {
    // Divide by 16
    lapic_write(LAPIC_TIMER_DCR, 0x3);

    // Set LVT Timer: periodic, unmasked, with the given vector
    lapic_write(LAPIC_TIMER, vector | LAPIC_TIMER_PERIODIC);

    // Set initial count — timer starts counting down immediately
    lapic_write(LAPIC_TIMER_ICR, initial_count);
}
