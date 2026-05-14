#pragma once

class Buddy;

bool run_boot_self_tests(Buddy* buddy);
void sleep_test_thread();
void smp_info_test_thread();
void smp_ipi_test_thread();
