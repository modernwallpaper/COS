#pragma once

#include <cstdint>
#include <inc/kernel/kshell/kshell.hpp>

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_ENABLE 0x800
#define IA32_APIC_BASE_BSP 0x100

#define LAPIC_VIRT_BASE 0xFEE00000ULL
#define LAPIC_ID_REG 0x020
#define LAPIC_SVR 0x0E0
#define LAPIC_TPR 0x080
#define LAPIC_TIMER 0x320
#define LAPIC_TIMER_ICR 0x380
#define LAPIC_TIMER_CCR 0x390
#define LAPIC_TIMER_DCR 0x3E0

#define LAPIC_SVR_ENABLE (1 << 8)
#define LAPIC_TIMER_PERIODIC (1 << 17)
#define LAPIC_TIMER_MASKED (1 << 16)

class HPET;

class Apic {
  private:
    uintptr_t apic_base;
    KShell *kshell;

    bool check_apic();
    uintptr_t cpu_get_apic_base();
    void cpu_set_apic_base(uintptr_t phys_addr);

    uint32_t lapic_read(uint16_t reg);
    void lapic_write(uint16_t reg, uint32_t value);

  public:
    uint32_t calibrated_10ms;
    Apic(KShell *kshell);
    ~Apic();

    uintptr_t get_apic_base();
    uint8_t get_id();
    void enable();
    void timer_init(uint8_t vector, uint32_t initial_count);
    void calibrate(HPET* hpet);
    uint32_t ticks_per_ms() { return calibrated_10ms / 10; }
};
