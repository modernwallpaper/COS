#include <cstdint>
#include <cstddef>
#include <limine.h>

#include <inc/kernel/graphics/graphics.hpp>
#include <inc/kernel/kshell/kshell.hpp>
#include <inc/kernel/gdt/gdt.hpp>
#include <inc/kernel/idt/idt.hpp>
#include <inc/kernel/ports/ports.hpp>

// Set the base revision to 5, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

namespace {

__attribute__((used, section(".limine_requests")))
volatile std::uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(5);

}

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

namespace {

__attribute__((used, section(".limine_requests")))
volatile limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = nullptr
};

}

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .cpp file, as seen fit.

namespace {

__attribute__((used, section(".limine_requests_start")))
volatile std::uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
volatile std::uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

}

// Halt and catch fire function.
namespace {

void hcf() {
    for (;;) {
#if defined (__x86_64__)
        asm ("hlt");
#elif defined (__aarch64__) || defined (__riscv)
        asm ("wfi");
#elif defined (__loongarch64)
        asm ("idle 0");
#endif
    }
}

}

// The following stubs are required by the Itanium C++ ABI (the one we use,
// regardless of the "Itanium" nomenclature).
// Like the memory functions above, these stubs can be moved to a different .cpp file,
// but should not be removed, unless you know what you are doing.
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

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == nullptr
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    // Fetch the first framebuffer.
    limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    // Initrialize Graphics
    Graphics graphics(framebuffer);

    // graphics.draw_pixel(0xffffff, 30, 30);
    // graphics.draw_line(0xffffff, 0, 0, 500, 60);
    // graphics.k_draw_char('T', 0xffffff, 50, 50);
    // => All working
    
    KShell kshell(&graphics);

    // kshell.print("[  ");
    // kshell.set_foreground_color(0x00FF00);
    // kshell.print("OK");
    // kshell.set_foreground_color(0xFFFFFF);
    // kshell.print("   ]");

    // kshell.print("Hello %s!\n", "World");
    // kshell.print("Number: %d, Unsigned: %u, Big: %llu\n", -42, 42u, 123456789012345ULL);
    // kshell.print("100%% done\n");

    kshell.print_kernel_success("Initialized graphics");

    GDT gdt(&kshell, KERNEL_STACK_SIZE, kernel_stack);
    gdt.init_gdt();
    gdt.check_entries();

    kshell.print_kernel_success("Initialized GDT");

    IDT idt(&kshell);
    kshell.print_kernel_success("Initialized IDT");

    pic_init();
    kshell.print_kernel_success("Initialized PIC");

    // We're done, just hang...
    hcf();
}
