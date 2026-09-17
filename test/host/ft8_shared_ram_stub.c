/*
 * Host-test-only stub: the real firmware defines g_ft8_shared_ram in
 * waterfall.c (see ft8_shared_ram.h for why - the union with the main
 * waterfall's own pixel buffer). The host test harness doesn't link
 * waterfall.c at all (it's GD32/LCD-specific), so it needs its own
 * definition of the same symbol - this file exists only to provide
 * that for test/host builds, never compiled into the real firmware.
 */
#include "ft8_shared_ram.h"

ft8_shared_ram_t g_ft8_shared_ram;
