#include <cstddef>
#include <cstdint>
#include <inc/kernel/mem/buddy.hpp>
#include <inc/kernel/mem/kmalloc.hpp>
#include <inc/kernel/ports/ports.hpp>
#include <inc/kernel/sched/task.hpp>
#include <inc/kernel/smp/smp.hpp>
#include <inc/kernel/apic/apic.hpp>
#include <inc/kernel/tests/selftests.hpp>

namespace
{

void print_result(const char* name, bool pass)
{
    serial_print("[TEST] ");
    serial_print(pass ? "PASS " : "FAIL ");
    serial_print(name);
    serial_print("\n");
}

bool test_slab_alloc_free(Buddy* buddy)
{
    uint64_t before = buddy->total_free();

    void* a = kmalloc(8);
    void* b = kmalloc(64);
    void* c = kmalloc(512);
    void* d = kmalloc(2048);

    bool pass = a != nullptr && b != nullptr && c != nullptr && d != nullptr &&
                a != b && b != c && c != d;

    kfree(c);
    kfree(a);
    kfree(d);
    kfree(b);

    return pass && buddy->total_free() == before;
}

bool test_buddy_multi_page(Buddy* buddy)
{
    uint64_t before = buddy->total_free();

    uintptr_t block = buddy->alloc(2);
    bool pass = block != 0 && (block & ((Buddy::PAGE_SIZE << 2) - 1)) == 0;

    if (block != 0)
        buddy->free(block, 2);

    return pass && buddy->total_free() == before;
}

bool test_mixed_alloc_free(Buddy* buddy)
{
    uint64_t before = buddy->total_free();

    void* small_a = kmalloc(32);
    void* large_a = kmalloc(9000);
    void* small_b = kmalloc(128);
    void* large_b = kmalloc(17000);

    bool pass = small_a != nullptr && large_a != nullptr &&
                small_b != nullptr && large_b != nullptr;

    kfree(large_a);
    kfree(small_b);
    kfree(small_a);
    kfree(large_b);

    return pass && buddy->total_free() == before;
}

bool test_task_add_limit()
{
    Scheduler test_scheduler;
    task dummy[Scheduler::MAX_TASKS + 8] = {};

    int accepted = 0;
    bool rejected = false;

    for (int i = 0; i < Scheduler::MAX_TASKS + 8; i++)
    {
        dummy[i].name = "dummy";
        dummy[i].state = TASK_READY;
        dummy[i].idle = false;

        if (test_scheduler.add_task(&dummy[i]) >= 0)
            accepted++;
        else
            rejected = true;
    }

    return accepted == Scheduler::MAX_TASKS && rejected;
}

} // namespace

bool run_boot_self_tests(Buddy* buddy)
{
    serial_print("[TEST] boot self-tests start\n");

    bool slab = test_slab_alloc_free(buddy);
    print_result("kmalloc/kfree slab allocations", slab);

    bool buddy_multi = test_buddy_multi_page(buddy);
    print_result("buddy multi-page allocation", buddy_multi);

    bool mixed = test_mixed_alloc_free(buddy);
    print_result("mixed allocation/free order", mixed);

    bool task_limit = test_task_add_limit();
    print_result("scheduler task add limit", task_limit);

    bool pass = slab && buddy_multi && mixed && task_limit;
    print_result("boot self-tests", pass);
    return pass;
}

void sleep_test_thread()
{
    uint64_t start = scheduler.ticks();
    sleep_ms(30);
    uint64_t elapsed = scheduler.ticks() - start;

    print_result("sleep_ms waits at least requested ticks", elapsed >= 3);

    while (true)
        sleep_ms(1000);
}

void smp_info_test_thread()
{
    sleep_ms(100);

    bool cpu_count_ok = cpu_count >= 1;
    print_result("SMP: cpu_count >= 1", cpu_count_ok);

    bool ids_valid = true;
    for (uint64_t i = 0; i < cpu_count; i++)
    {
        if (cpu_infos[i].lapic_id == 0 && !cpu_infos[i].bsp)
        {
            ids_valid = false;
            break;
        }
    }
    print_result("SMP: all CPUs have valid LAPIC IDs", ids_valid);

    uint64_t bsp_count = 0;
    uint64_t bsp_idx = 0;
    for (uint64_t i = 0; i < cpu_count; i++)
    {
        if (cpu_infos[i].bsp)
        {
            bsp_count++;
            bsp_idx = i;
        }
    }
    print_result("SMP: exactly one BSP", bsp_count == 1);

    bool all_online = true;
    for (uint64_t i = 0; i < cpu_count; i++)
        if (!cpu_infos[i].online)
            all_online = false;
    print_result("SMP: all CPUs online", all_online);

    print_result("SMP: BSP has no pre-allocated stack",
                 cpu_infos[bsp_idx].stack_base == 0);

    bool stacks_ok = true;
    for (uint64_t i = 0; i < cpu_count; i++)
    {
        if (!cpu_infos[i].bsp && cpu_infos[i].stack_base == 0)
        {
            stacks_ok = false;
            break;
        }
    }
    print_result("SMP: all APs have pre-allocated stacks", stacks_ok);

    while (true)
        sleep_ms(10000);
}

void smp_ipi_test_thread()
{
    sleep_ms(200);

    // Test self-IPI: send IPI to self and verify it's received.
    // This validates the APIC IPI path used for SMP wakeup.
    cpu_infos[0].ipi_count = 0;
    global_apic->send_self_ipi(50);
    for (int d = 0; d < 100000; d++)
        asm volatile("pause" : : : "memory");
    bool self_ipi_ok = (cpu_infos[0].ipi_count == 1);
    print_result("SMP: self-IPI received", self_ipi_ok);

    while (true)
        sleep_ms(10000);
}
