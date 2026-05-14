#include "inc/kernel/apic/apic.hpp"
#include "inc/kernel/apic/ioapic.hpp"
#include "inc/kernel/hpet/hpet.hpp"
#include "inc/kernel/pmm/pmm.hpp"
#include "inc/kernel/vmm/vmm.hpp"
#include <cstddef>
#include <cstdint>
#include <inc/kernel/mem/buddy.hpp>
#include <inc/kernel/mem/kmalloc.hpp>
#include <limine.h>

#include <inc/kernel/gdt/gdt.hpp>
#include <inc/kernel/graphics/graphics.hpp>
#include <inc/kernel/idt/idt.hpp>
#include <inc/kernel/kshell/kshell.hpp>
#include <inc/kernel/mem/page_meta.hpp>
#include <inc/kernel/ports/ports.hpp>
#include <inc/kernel/sched/task.hpp>
#include <inc/kernel/smp/smp.hpp>
#include <inc/kernel/tests/selftests.hpp>

namespace
{

__attribute__((used, section(".limine_requests"))) volatile std::uint64_t
    limine_base_revision[] = LIMINE_BASE_REVISION(5);

}

namespace
{

__attribute__((used, section(".limine_requests")))

volatile limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0, .response = nullptr};

__attribute__((used, section(".limine_requests")))

volatile limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0, .response = nullptr};

__attribute__((used, section(".limine_requests")))

volatile limine_executable_address_request exec_addr_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0,
    .response = nullptr};

__attribute__((used, section(".limine_requests")))

volatile limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0, .response = nullptr};

__attribute__((used, section(".limine_requests")))

volatile limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID, .revision = 0, .response = nullptr};

__attribute__((used, section(".limine_requests")))
volatile limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID, .revision = 0, .response = nullptr, .flags = 0};

} // namespace

namespace
{

__attribute__((used, section(".limine_requests_start"))) volatile std::uint64_t
    limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end"))) volatile std::uint64_t
    limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

} // namespace

namespace
{

constexpr uint64_t PAGE_SIZE = 0x1000;

void hcf()
{
    for (;;)
    {
#if defined(__x86_64__)
        asm("hlt");
#elif defined(__aarch64__) || defined(__riscv)
        asm("wfi");
#elif defined(__loongarch64)
        asm("idle 0");
#endif
    }
}

uint64_t align_up_page(uint64_t value)
{
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

uint64_t align_down_page(uint64_t value)
{
    return value & ~(PAGE_SIZE - 1);
}

uint64_t count_usable_pages(limine_memmap_response* memmap)
{
    uint64_t pages = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        auto entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE)
            continue;

        uint64_t start = align_up_page(entry->base);
        uint64_t end = align_down_page(entry->base + entry->length);
        if (start < end)
            pages += (end - start) / PAGE_SIZE;
    }

    return pages;
}

uint64_t find_pmm_stack_region(limine_memmap_response* memmap,
                               uint64_t needed_bytes, uint64_t kernel_phys_base,
                               uint64_t kernel_phys_end)
{
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        auto entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE)
            continue;

        uint64_t region_start = align_up_page(entry->base);
        uint64_t region_end = align_down_page(entry->base + entry->length);
        if (region_start >= region_end)
            continue;

        if (region_start < kernel_phys_end && region_end > kernel_phys_base)
        {
            if (region_start < kernel_phys_base &&
                needed_bytes <= kernel_phys_base - region_start)
                return region_start;

            uint64_t after_kernel = align_up_page(kernel_phys_end);
            if (after_kernel < region_end &&
                needed_bytes <= region_end - after_kernel)
                return after_kernel;
        }
        else if (needed_bytes <= region_end - region_start)
        {
            return region_start;
        }
    }

    return 0;
}

} // namespace

extern "C" {
int __cxa_atexit(void (*)(void*), void*, void*)
{
    return 0;
}
void __cxa_pure_virtual()
{
    hcf();
}
void* __dso_handle;
}

extern void (*__init_array[])();
extern void (*__init_array_end[])();

constexpr uint64_t KERNEL_STACK_SIZE = 0x4000; // 16 KB
alignas(4096) uint8_t kernel_stack[KERNEL_STACK_SIZE];

static void thread_a()
{
    serial_print("[SCHED] thread_a entered\n");
    while (true)
    {
        serial_print("[SCHED] A\n");
        sleep_ms(500);
    }
}

static void thread_b()
{
    serial_print("[SCHED] thread_b entered\n");
    while (true)
    {
        serial_print("[SCHED] B\n");
        sleep_ms(1000);
    }
}

static void idle_thread()
{
    while (true)
        asm volatile("hlt");
}

extern "C" void kmain()
{
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false)
    {
        hcf();
    }

    for (std::size_t i = 0; &__init_array[i] != __init_array_end; i++)
    {
        __init_array[i]();
    }

    serial_init();
    serial_print("booting...\n");

    if (framebuffer_request.response == nullptr ||
        framebuffer_request.response->framebuffer_count < 1)
    {
        hcf();
    }

    limine_framebuffer* framebuffer =
        framebuffer_request.response->framebuffers[0];

    Graphics graphics(framebuffer);
    KShell kshell(&graphics);

    kshell.print_kernel_success("Initialized graphics");
    kshell.print_kernel_success("KShell initialized, before memmap loop");

    for (uint64_t i = 0; i < memmap_request.response->entry_count; i++)
    {
        auto entry = memmap_request.response->entries[i];
        if (i % 2 == 0)
        {
            kshell.print("base=%x, length=%x, type=%x", entry->base,
                         entry->length, entry->type);
        }
        else
        {
            kshell.print("\tbase=%x, length=%x, type=%x\n", entry->base,
                         entry->length, entry->type);
        }
    }

    uint64_t hhdm_offset = hhdm_request.response->offset;

    extern uint8_t _end[];
    uint64_t kernel_virtual_base = exec_addr_request.response->virtual_base;
    uint64_t kernel_phys_base = exec_addr_request.response->physical_base;
    uint64_t kernel_size = (uint64_t)_end - kernel_virtual_base;
    uint64_t kernel_phys_end = kernel_phys_base + kernel_size;

    uint64_t pmm_stack_size = count_usable_pages(memmap_request.response);
    uint64_t needed = pmm_stack_size * sizeof(uintptr_t);
    uint64_t pmm_stack_phys = find_pmm_stack_region(
        memmap_request.response, needed, kernel_phys_base, kernel_phys_end);

    uintptr_t* pmm_stack_virt = (uintptr_t*)(pmm_stack_phys + hhdm_offset);

    if (pmm_stack_phys == 0)
    {
        kshell.print_kernel_error("Could not find space for PMM stack");
        hcf();
    }

    PMM pmm(pmm_stack_virt, pmm_stack_size, kernel_phys_base, kernel_phys_end,
            pmm_stack_phys, memmap_request.response->entries,
            memmap_request.response->entry_count);

    Buddy buddy(&pmm, hhdm_offset);

    uint64_t pages_imported = 0;
    while (uintptr_t page = pmm.alloc())
    {
        buddy.free(page, 0);
        pages_imported++;
    }
    kshell.print_kernel_success(
        "Imported %llu pages from PMM into buddy (%llu free pages)",
        pages_imported, buddy.total_free());

    uint64_t max_pfn = 0;
    for (uint64_t i = 0; i < memmap_request.response->entry_count; i++)
    {
        auto entry = memmap_request.response->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE)
            continue;
        uint64_t end = entry->base + entry->length;
        uint64_t end_pfn = (end + 0xFFF) >> 12;
        if (end_pfn > max_pfn)
            max_pfn = end_pfn;
    }
    if (!page_meta_init(&buddy, hhdm_offset, max_pfn))
    {
        kshell.print_kernel_error("page_meta_init failed");
        hcf();
    }
    kshell.print_kernel_success("Initialized page metadata");

    kmalloc_init((void*)&buddy, hhdm_offset);
    kshell.print_kernel_success("Initialized kmalloc/SLAB allocator");

    if (!run_boot_self_tests(&buddy))
    {
        kshell.print_kernel_error("Boot self-tests failed");
        hcf();
    }
    kshell.print_kernel_success("Boot self-tests passed");

    VMM vmm(&buddy, hhdm_offset);
    kshell.print_kernel_success("Initialized VMM");

    uint64_t mmio_flags = VMM::PAGE_RW | VMM::PAGE_CACHEDIS;
    vmm.map_range(0xFEE00000, 0xFEE00000, 0x1000, mmio_flags);
    vmm.map_range(0xFEC00000, 0xFEC00000, 0x1000, mmio_flags);
    kshell.print_kernel_success("Mapped APIC MMIO regions");

    GDT gdt(&kshell, KERNEL_STACK_SIZE, kernel_stack);
    gdt.check_entries();
    kshell.print_kernel_success("Initialized GDT");

    IDT idt(&kshell);
    kshell.print_kernel_success("Initialized IDT");

    pic_init();
    kshell.print_kernel_success("Initialized PIC");

    pic_disable();
    kshell.print_kernel_success("Disabled PIC ... Wow, how long that lasted");

    Apic apic(&kshell);
    kshell.print_kernel_success("Initialized APIC");

    apic.enable();
    kshell.print_kernel_success("APIC enabled via SVR");

    uint8_t lapic_id = apic.get_id();
    uintptr_t rsdp_addr =
        rsdp_request.response ? (uintptr_t)rsdp_request.response->address : 0;
    serial_print("rsdp_addr=");
    serial_print_hex(rsdp_addr);
    serial_print("\n");
    IOAPIC ioapic(&kshell, &buddy, hhdm_offset, lapic_id, rsdp_addr);
    if (ioapic.initialized)
        kshell.print_kernel_success("I/O APIC initialized");
    else
        kshell.print_kernel_error("I/O APIC initialization failed");

    serial_print("ioapic done\n");

    serial_print("hpet init...\n");
    HPET hpet(rsdp_addr, &vmm, hhdm_offset);
    kshell.print_kernel_success("Initialized HPET");
    serial_print("hpet init ok\n");

    serial_print("hpet enable...\n");
    hpet.enable();
    kshell.print_kernel_success("Enabled HPET");
    serial_print("hpet enable ok\n");

    serial_print("calibrate...\n");
    apic.calibrate(&hpet);
    kshell.print_kernel_success("APIC timer calibrated (%d ticks/10ms)",
                                apic.calibrated_10ms);
    serial_print("calibrate ok\n");

    // Register the main (kmain) task.
    // We give it a valid stack_base so the scheduler can safely inspect it.
    // Its rsp field starts at 0 and is filled in on the first preemption —
    // that is fine because main_task is RUNNING (not READY) at that point,
    // so the scheduler will never try to restore from rsp=0.
    serial_print("scheduler init...\n");
    task* main_task = (task*)kmalloc(sizeof(task));
    main_task->rsp = 0; // filled on first preemption
    main_task->stack_base = (uint64_t)kernel_stack; // informational only
    main_task->wake_tick = 0;
    main_task->name = "kmain";
    main_task->state = TASK_RUNNING; // already executing
    main_task->idle = false;
    scheduler.add_task(main_task);
    kshell.print_kernel_success("Registered kmain task");

    task* idle = scheduler.create_kthread(idle_thread, "idle", true);
    if (idle)
    {
        scheduler.add_task(idle);
        kshell.print_kernel_success("Created idle task");
    }

    task* ta = scheduler.create_kthread(thread_a, "thread_a");
    if (ta)
    {
        scheduler.add_task(ta);
        kshell.print_kernel_success("Created kernel thread A");
    }

    task* tb = scheduler.create_kthread(thread_b, "thread_b");
    if (tb)
    {
        scheduler.add_task(tb);
        kshell.print_kernel_success("Created kernel thread B");
    }

    task* sleep_test =
        scheduler.create_kthread(sleep_test_thread, "sleep_test");
    if (sleep_test)
    {
        scheduler.add_task(sleep_test);
        kshell.print_kernel_success("Created sleep self-test thread");
    }
    serial_print("scheduler init ok\n");

    // Arm the APIC timer only after all tasks are registered, so the first
    // scheduler tick sees a valid run queue.
    serial_print("timer arm...\n");
    apic.timer_init(48, apic.calibrated_10ms);
    kshell.print_kernel_success("APIC timer armed at 100 Hz");
    serial_print("timer arm ok\n");

    kshell.print_kernel_info("Scheduler running at 100 Hz");

    serial_print("smp init...\n");
    smp_init(&kshell, &apic, mp_request.response, hhdm_offset);
    serial_print("smp init ok\n");

    task* smp_info_test_task =
        scheduler.create_kthread(smp_info_test_thread, "smp_info_test");
    if (smp_info_test_task)
    {
        scheduler.add_task(smp_info_test_task);
        kshell.print_kernel_success("Created SMP info test thread");
    }

    task* smp_ipi_test_task =
        scheduler.create_kthread(smp_ipi_test_thread, "smp_ipi_test");
    if (smp_ipi_test_task)
    {
        scheduler.add_task(smp_ipi_test_task);
        kshell.print_kernel_success("Created SMP IPI test thread");
    }

    kshell.print_kernel_info(
        "Holy shit, we actually got here without tripple-faulting");

    serial_print("kmain entering sleep loop\n");

    while (true)
        sleep_ms(1000);
}
