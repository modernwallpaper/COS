#include <cstdint>
#include <inc/kernel/apic/apic.hpp>
#include <inc/kernel/idt/idt.hpp>
#include <inc/kernel/kshell/kshell.hpp>
#include <inc/kernel/mem/kmalloc.hpp>
#include <inc/kernel/ports/ports.hpp>
#include <inc/kernel/smp/smp.hpp>
#include <limine.h>

cpu_info cpu_infos[64];
uint64_t cpu_count = 0;

static uint64_t bsp_lapic_id;

extern IDT* global_idt;

extern "C" void smp_ap_entry(struct limine_mp_info* info)
{
    uint32_t lapic_id = info->lapic_id;

    serial_print("[SMP] AP ");
    serial_print_hex(lapic_id);
    serial_print(" online\n");

    int cpu_idx = -1;
    for (uint64_t i = 0; i < cpu_count; i++)
    {
        if (cpu_infos[i].lapic_id == lapic_id)
        {
            cpu_idx = i;
            break;
        }
    }

    if (cpu_idx < 0)
    {
        serial_print("[SMP] Unknown AP, halting\n");
        while (true)
            asm volatile("hlt");
    }

    cpu_infos[cpu_idx].online = true;

    // Switch to the pre-allocated stack (allocated by BSP in smp_init)
    uint64_t ap_stack_top = cpu_infos[cpu_idx].stack_base + 16384 - 16;
    asm volatile("mov %0, %%rsp" : : "r"(ap_stack_top));

    // Load the kernel's IDT — the bootloader's minimal IDT doesn't have
    // entries for our interrupt vectors (48 APIC timer, 49 yield, etc.).
    // Without this, any interrupt on this AP would triple-fault.
    asm volatile("lidt %0" : : "m"(global_idt->idtr));

    // Enable the local APIC
    uint32_t eax, edx;
    asm volatile("rdmsr" : "=a"(eax), "=d"(edx) : "c"(IA32_APIC_BASE_MSR));
    uintptr_t apic_base = ((uintptr_t)(edx & 0x0f) << 32) | (eax & 0xfffff000);

    uint32_t svr = *(volatile uint32_t*)(apic_base + LAPIC_SVR);
    svr |= LAPIC_SVR_ENABLE;
    svr |= 0xFF;
    *(volatile uint32_t*)(apic_base + LAPIC_SVR) = svr;
    *(volatile uint32_t*)(apic_base + LAPIC_SVR);

    // Mask the APIC timer so it doesn't fire periodic interrupts
    // (only the BSP drives the scheduler timer).
    *(volatile uint32_t*)(apic_base + 0x320) = 0 | (1 << 16);

    // Acknowledge any pending interrupt
    *(volatile uint32_t*)(apic_base + 0x0B0) = 0;

    asm volatile("sti");

    serial_print("[SMP] AP ");
    serial_print_hex(lapic_id);
    serial_print(" ready, entering idle\n");

    while (true)
        asm volatile("hlt");
}

static void delay_approx_ms(uint64_t ms)
{
    // Crude calibrated delay loop — good enough for QEMU KVM.
    // ~10,000,000 pause iterations ≈ 10 ms on a ~3 GHz host with KVM.
    for (uint64_t i = 0; i < ms * 1000000ULL; i++)
        asm volatile("pause");
}

void smp_init(KShell* kshell, Apic* apic, limine_mp_response* mp_response,
              uint64_t hhdm_offset)
{
    (void)hhdm_offset;
    cpu_count = mp_response->cpu_count;
    bsp_lapic_id = mp_response->bsp_lapic_id;

    kshell->print_kernel_info("SMP: %llu CPUs detected", cpu_count);

    if (cpu_count < 1)
        return;

    for (uint64_t i = 0; i < cpu_count; i++)
    {
        auto info = mp_response->cpus[i];
        cpu_infos[i].lapic_id = info->lapic_id;
        cpu_infos[i].acpi_id = info->processor_id;
        cpu_infos[i].bsp = (info->lapic_id == bsp_lapic_id);
        cpu_infos[i].online = cpu_infos[i].bsp; // BSP starts online
        cpu_infos[i].stack_base = 0;
        cpu_infos[i].ipi_count = 0;

        kshell->print("  CPU %llu: LAPIC ID=%u%s\n", i, info->lapic_id,
                       cpu_infos[i].bsp ? " (BSP)" : "");
    }

    // Pre-allocate stacks for all APs from the BSP context.
    // kmalloc is NOT thread-safe so APs must never call it themselves.
    for (uint64_t i = 0; i < cpu_count; i++)
    {
        if (cpu_infos[i].bsp)
            continue;
        cpu_infos[i].stack_base = (uint64_t)kmalloc(16384);
        if (cpu_infos[i].stack_base == 0)
        {
            kshell->print_kernel_error("Out of memory for AP %llu stack", i);
            return;
        }
    }

    // Wake APs via SIPI only (no INIT — INIT resets the AP and destroys
    // the bootloader's trampoline setup).  The bootloader leaves APs halted
    // at the trampoline; a single SIPI is enough to wake them.
    for (uint64_t i = 0; i < cpu_count; i++)
    {
        if (cpu_infos[i].bsp)
            continue;

        auto info = mp_response->cpus[i];
        info->goto_address = smp_ap_entry;

        serial_print("[SMP] starting AP ");
        serial_print_hex(info->lapic_id);
        serial_print("\n");

        apic->send_sipi(info->lapic_id, 0);
        delay_approx_ms(1);
    }

    delay_approx_ms(10);

    uint64_t online = 1;
    for (uint64_t i = 0; i < cpu_count; i++)
    {
        if (cpu_infos[i].online)
        {
            serial_print("[SMP] CPU ");
            serial_print_hex(cpu_infos[i].lapic_id);
            serial_print(" online\n");
            if (!cpu_infos[i].bsp)
                online++;
        }
    }
    serial_print("[SMP] ");

    {
        char buf[64];
        int pos = 0;
        uint64_t n = online;
        if (n == 0) { buf[pos++] = '0'; }
        else { while (n) { buf[pos++] = '0' + (n % 10); n /= 10; }
               for (int i = 0; i < pos / 2; i++) { char t = buf[i]; buf[i] = buf[pos-1-i]; buf[pos-1-i] = t; } }
        buf[pos] = 0;
        serial_print(buf);
    }
    serial_print("/");

    {
        char buf[64];
        int pos = 0;
        uint64_t n = cpu_count;
        if (n == 0) { buf[pos++] = '0'; }
        else { while (n) { buf[pos++] = '0' + (n % 10); n /= 10; }
               for (int i = 0; i < pos / 2; i++) { char t = buf[i]; buf[i] = buf[pos-1-i]; buf[pos-1-i] = t; } }
        buf[pos] = 0;
        serial_print(buf);
    }
    serial_print(" CPUs online\n");
}
