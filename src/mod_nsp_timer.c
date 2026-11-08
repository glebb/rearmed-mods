// RSMods: a custom Non-Stop Play timer (NonStopPlayTimerMod.cpp). Between two songs Non-Stop Play counts down 10.99
// seconds; with UseCustomNSPTimer = on it counts CustomNSPTimeLimit ms instead (2 to 60 seconds, as RSMods' app allows).
//
// RSMods writes the constant the countdown is measured against: a double in the game's read-only data, 10.99f widened
// (00 00 00 40 e1 fa 25 40). Both games read their copy from the mapped image at run time: the Mac's at 0x1013ed448
// (0x1006b084f subtracts the time since the song ended from it, and the countdown's other checks compare with it),
// Learn & Play's at 0x1da5ca8 (RSMods' address). Each image has that value once, so it is found by value, not by
// address, and written. Off again, it gets back what the game had.
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdio.h>
#include <string.h>

#include "rsmods.h"

#define GAME_LIMIT_BITS 0x4025fae140000000ull  // (double)10.99f
#define MIN_MS 2000
#define MAX_MS 60000

static double *g_limit;  // the game's constant, once found
static double g_game;    // what it was
static int g_searched;

// The readable parts of the image, 8 bytes at a time: the constant's one place, or NULL (none, or more than one)
static double *find_limit(void) {
  double *found = NULL;
  int hits = 0;
  mach_vm_address_t a = rc_img_lo;
  while (a < rc_img_hi) {
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    if (mach_vm_region(mach_task_self(), &a, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object))
      break;
    if (a >= rc_img_hi) break;
    uint64_t lo = a, hi = a + size < rc_img_hi ? a + size : rc_img_hi;
    if (info.protection & VM_PROT_READ)
      for (uint64_t p = (lo + 7) & ~7ull; p + 8 <= hi; p += 8)
        if (*(const uint64_t *)(uintptr_t)p == GAME_LIMIT_BITS) {
          found = (double *)(uintptr_t)p;
          hits++;
        }
    a += size;
  }
  if (hits != 1) {
    fprintf(stderr, "[rsmods] Non-Stop Play timer: the game's 10.99 s is in the image %d times, not once: the timer stays the game's\n", hits);
    return NULL;
  }
  rsm_log("Non-Stop Play timer: the game's limit at image +0x%llx", (unsigned long long)((uint64_t)(uintptr_t)found - rc_img_lo));
  return found;
}

// Writes the constant, making its page writable first if it is not (both games map the image read and write)
static void set_limit(double v) {
  mach_vm_address_t page = (uint64_t)(uintptr_t)g_limit & ~(uint64_t)(vm_page_size - 1);
  mach_vm_size_t size = 0;
  mach_vm_address_t a = page;
  vm_region_basic_info_data_64_t info;
  mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
  mach_port_t object = MACH_PORT_NULL;
  if (!mach_vm_region(mach_task_self(), &a, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object) &&
      !(info.protection & VM_PROT_WRITE) &&
      mach_vm_protect(mach_task_self(), page, vm_page_size, FALSE, VM_PROT_READ | VM_PROT_WRITE)) {
    fprintf(stderr, "[rsmods] Non-Stop Play timer: the game's limit can't be written\n");
    g_limit = NULL;
    return;
  }
  *g_limit = v;
}

static void tick(void) {
  int on = rsm_on("UseCustomNSPTimer");
  if (!on && !g_limit) return;
  if (!g_searched) {
    g_searched = 1;
    g_limit = find_limit();
    if (g_limit) g_game = *g_limit;
  }
  if (!g_limit) return;
  long ms = rsm_long("CustomNSPTimeLimit", 10000);
  if (ms < MIN_MS) ms = MIN_MS;
  if (ms > MAX_MS) ms = MAX_MS;
  double want = on ? ms / 1000.0 : g_game;
  if (*g_limit != want) {
    set_limit(want);
    rsm_log("Non-Stop Play timer: %.2f s", want);
  }
}

const rsm_mod rsm_mod_nsp_timer = {"nsp_timer", tick, NULL, NULL, NULL};
