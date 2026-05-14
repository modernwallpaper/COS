#include "inc/kernel/apic/apic.hpp"
#include "inc/kernel/apic/ioapic.hpp"
#include "inc/kernel/pmm/pmm.hpp"
#include "inc/kernel/vmm/vmm.hpp"
#include <cstddef>
#include <cstdint>
#include <inc/kernel/mem/buddy.hpp>
#include <inc/kernel/mem/kmalloc.hpp>
#include <limine.h>

#include <inc/kernel/gdt/gdt.hpp>
#include <inc/kernel/graphics/graphics.hpp>
#include <inc/kernel/mem/page_meta.hpp>
#include <inc/kernel/idt/idt.hpp>
#include <inc/kernel/kshell/kshell.hpp>
#include <inc/kernel/ports/ports.hpp>

// Set the base revision to 5, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

namespace {

__attribute__((used, section(".limine_requests"))) volatile std::uint64_t
    limine_base_revision[] = LIMINE_BASE_REVISION(5);

}

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

namespace {

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

} // namespace

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .cpp file, as seen fit.

namespace {

__attribute__((used, section(".limine_requests_start"))) volatile std::uint64_t
    limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end"))) volatile std::uint64_t
    limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

} // namespace

// Halt and catch fire function.
namespace {

void hcf() {
    for (;;) {
#if defined(__x86_64__)
        asm("hlt");
#elif defined(__aarch64__) || defined(__riscv)
        asm("wfi");
#elif defined(__loongarch64)
        asm("idle 0");
#endif
    }
}

} // namespace

// The following stubs are required by the Itanium C++ ABI (the one we use,
// regardless of the "Itanium" nomenclature).
// Like the memory functions above, these stubs can be moved to a different .cpp
// file, but should not be removed, unless you know what you are doing.
extern "C" {
int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
void __cxa_pure_virtual() { hcf(); }
void *__dso_handle;
}

// Extern declarations for global constructors array.
extern void (*__init_array[])();
extern void (*__init_array_end[])();

constexpr uint64_t KERNEL_STACK_SIZE = 0x4000; // 16 KB
alignas(4096) uint8_t kernel_stack[KERNEL_STACK_SIZE];

// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.
extern "C" void kmain() {
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        hcf();
    }

    // Call global constructors.
    for (std::size_t i = 0; &__init_array[i] != __init_array_end; i++) {
        __init_array[i]();
    }

    serial_init();
#ifdef DEBUGGING
    serial_print("booting...\n");
#endif

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == nullptr ||
        framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    // Fetch the first framebuffer.
    limine_framebuffer *framebuffer =
        framebuffer_request.response->framebuffers[0];

    // Initrialize Graphics
    Graphics graphics(framebuffer);

    // graphics.draw_pixel(0xffffff, 30, 30);
    // graphics.draw_line(0xffffff, 0, 0, 500, 60);
    // graphics.k_draw_char('T', 0xffffff, 50, 50);
    // => All working

    KShell kshell(&graphics);

    kshell.print_kernel_success("Initialized graphics");
    kshell.print_kernel_success("KShell initialized, before memmap loop");

    // Loop through the memory map and print it out
    for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
        auto entry = memmap_request.response->entries[i];
        if (i % 2 == 0) {
            kshell.print("base=%x, length=%x, type=%x", entry->base,
                         entry->length, entry->type);
        } else {
            kshell.print("\tbase=%x, length=%x, type=%x\n", entry->base,
                         entry->length, entry->type);
        }
    }

    // --- PMM setup: carve the 8 MB stack buffer from usable memory ---
    // We use the HHDM (higher-half direct map) to convert physical
    // addresses to virtual ones, since the kernel is in higher-half space.
    uint64_t hhdm_offset = hhdm_request.response->offset;

    // Compute where the kernel binary sits in physical memory.
    extern uint8_t _end[];
    uint64_t kernel_virtual_base = exec_addr_request.response->virtual_base;
    uint64_t kernel_phys_base = exec_addr_request.response->physical_base;
    uint64_t kernel_size = (uint64_t)_end - kernel_virtual_base;
    uint64_t kernel_phys_end = kernel_phys_base + kernel_size;

    // Find a USABLE region that has 8 MB of contiguous free space.
    // The kernel is often placed near the end of a region by Limine,
    // so we check both before and after the kernel in each region.
    uint64_t pmm_stack_phys = 0;
    uint64_t pmm_stack_size = 1048576; // enough for ~4 GB of RAM
    uint64_t needed = pmm_stack_size * sizeof(uintptr_t); // 8 MB
    for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
        auto entry = memmap_request.response->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE)
            continue;

        uint64_t region_start = (entry->base + 0xFFF) & ~0xFFF;
        uint64_t region_end = (entry->base + entry->length) & ~0xFFF;
        if (region_start >= region_end)
            continue;

        if (region_start < kernel_phys_end && region_end > kernel_phys_base) {
            // This region overlaps the kernel — check space before it
            if (region_start + needed <= kernel_phys_base) {
                pmm_stack_phys = region_start;
                break;
            }
            // Also check space after the kernel
            uint64_t after_kernel = (kernel_phys_end + 0xFFF) & ~0xFFF;
            if (after_kernel + needed <= region_end) {
                pmm_stack_phys = after_kernel;
                break;
            }
        } else {
            // Region doesn't overlap the kernel — use its start
            if (region_start + needed <= region_end) {
                pmm_stack_phys = region_start;
                break;
            }
        }
    }

    // Convert the physical stack address to a virtual pointer via HHDM.
    uintptr_t *pmm_stack_virt = (uintptr_t *)(pmm_stack_phys + hhdm_offset);

    if (pmm_stack_phys == 0) {
        kshell.print_kernel_error("Could not find space for PMM stack");
        kshell.print("needed %d bytes, kernel phys end at %x\n", needed,
                     kernel_phys_end);
        for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
            auto e = memmap_request.response->entries[i];
            kshell.print("  entry %d: base=%x len=%x end=%x type=%x%s\n", i,
                         e->base, e->length, e->base + e->length, e->type,
                         e->type == LIMINE_MEMMAP_USABLE ? " USABLE" : "");
        }
        hcf();
    }

    // Create the PMM. It will populate its free-frame stack by walking
    // the memory map, skipping the kernel range AND its own stack buffer.
    PMM pmm(pmm_stack_virt, pmm_stack_size, kernel_phys_base, kernel_phys_end,
            pmm_stack_phys, memmap_request.response->entries,
            memmap_request.response->entry_count);

    // Create the Buddy allocator on top of PMM
    Buddy buddy(&pmm, hhdm_offset);
    kshell.print_kernel_success("Initialized Buddy allocator (%d pages free)",
                                buddy.total_free());

    // Compute max PFN from memory map and initialize page metadata
    uint64_t max_pfn = 0;
    for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
        uint64_t end = memmap_request.response->entries[i]->base +
                       memmap_request.response->entries[i]->length;
        uint64_t end_pfn = (end + 0xFFF) >> 12;
        if (end_pfn > max_pfn)
            max_pfn = end_pfn;
    }
    page_meta_init(&buddy, hhdm_offset, max_pfn);
    kshell.print_kernel_success("Initialized page metadata");

    // Initialize kmalloc/SLAB allocator
    kmalloc_init((void *)&buddy, hhdm_offset);
    kshell.print_kernel_success("Initialized kmalloc/SLAB allocator");

    // Create VMM and map APIC MMIO regions with uncacheable flags
    VMM vmm(&buddy, hhdm_offset);
    kshell.print_kernel_success("Initialized VMM");

    // Map LAPIC (0xFEE00000) and I/O APIC (0xFEC00000) as uncacheable MMIO
    // The identity-mapped virtual addresses are fine since we're in ring 0.
    uint64_t mmio_flags = VMM::PAGE_RW | VMM::PAGE_CACHEDIS;
    vmm.map_range(0xFEE00000, 0xFEE00000, 0x1000, mmio_flags);
    vmm.map_range(0xFEC00000, 0xFEC00000, 0x1000, mmio_flags);

    kshell.print_kernel_success("Mapped APIC MMIO regions");

    // kshell.print("[  ");
    // kshell.set_foreground_color(0x00FF00);
    // kshell.print("OK");
    // kshell.set_foreground_color(0xFFFFFF);
    // kshell.print("   ]");

    // kshell.print("Hello %s!\n", "World");
    // kshell.print("Number: %d, Unsigned: %u, Big: %llu\n", -42, 42u,
    // 123456789012345ULL); kshell.print("100%% done\n");

    GDT gdt(&kshell, KERNEL_STACK_SIZE, kernel_stack);
    gdt.init_gdt();
    gdt.check_entries();

    kshell.print_kernel_success("Initialized GDT");

    IDT idt(&kshell);
    kshell.print_kernel_success("Initialized IDT");

    pic_init();
    kshell.print_kernel_success("Initialized PIC");

    pic_disable();
    kshell.print_kernel_success("Disabled PIC, Wow, how long that stayed");

    Apic apic(&kshell);
    kshell.print_kernel_success("Initialized APIC");

    apic.enable();
    kshell.print_kernel_success("APIC enabled via SVR");

    // Start APIC timer in periodic mode with vector 48.
    // The bus frequency varies; 0x100000 at divide-by-16 gives roughly
    // 100-200 ms on QEMU/KVM. Tune as needed.
    apic.timer_init(48, 0x100000);
    kshell.print_kernel_success("APIC timer started");

    // Initialize I/O APIC via ACPI MADT
    IOAPIC ioapic(&kshell, &buddy, hhdm_offset);
    uint8_t lapic_id = apic.get_id();
    uintptr_t rsdp_addr =
        rsdp_request.response ? (uintptr_t)rsdp_request.response->address : 0;
#ifdef DEBUGGING
    serial_print("rsdp_addr=");
    serial_print_hex(rsdp_addr);
    serial_print("\n");
#endif
    if (ioapic.init(lapic_id, rsdp_addr))
        kshell.print_kernel_success("I/O APIC initialized");
    else
        kshell.print_kernel_error("I/O APIC initialization failed");

#ifdef DEBUGGING
    serial_print("ioapic done\n");
#endif

    // We're done, just hang...
    hcf();
}
