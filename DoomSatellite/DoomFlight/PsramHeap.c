// ======================================================================
// \title  PsramHeap.c
// \brief  PSRAM heap for the stock doomgeneric engine's libc allocations on the Teensy 4.1
//
// DOOM's 6 MiB zone and 1 MiB screen buffer only fit the PSRAM, but the unmodified doomgeneric submodule requests
// them straight from libc (I_ZoneBase -> malloc, DG_ScreenBuffer = malloc, lumpinfo calloc) with no allocation hook,
// so an Fw::MemAllocator cannot be handed to it; the libc malloc symbol is the only seam. Zephyr's libc has a single
// malloc arena in the chosen SRAM (768 KiB OCRAM) and the PSRAM only exists after the memc driver (POST_KERNEL), so
// malloc/free/calloc/realloc are wrapped (-Wl,--wrap, CMakeLists.txt): requests >= PSRAM_HEAP_THRESHOLD come from a
// sys_heap in the PSRAM linker region, the rest from the libc heap in OCRAM. F Prime's BufferManagers already use
// Fw::MemAllocator (DoomSubtopologyConfig / ComCcsds::Allocation) with bins below the threshold, so they stay in OCRAM.
// The DoomEngine instance is linked into PSRAM by psram_sections.ld and zeroed here (the kernel only zeroes .bss).
// ======================================================================
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/toolchain.h>

#define PSRAM_HEAP_THRESHOLD (16U * 1024U)
/* 8 MiB PSRAM less the DoomEngine instance (psram_sections.ld, ~504 KiB): DOOM's 6 MiB zone, 1 MiB screen buffer
 * and 256 KiB video buffer leave ~400 KiB for the BufferManager pools and WAD lump tables. The linker reports an
 * overflow of the PSRAM region if the static placement grows past the margin. */
#define PSRAM_HEAP_SIZE ((7U * 1024U * 1024U) + (448U * 1024U))
#define PSRAM_HEAP_ALIGN 32U

extern void* __real_malloc(size_t size);
extern void __real_free(void* ptr);
extern void* __real_calloc(size_t count, size_t size);
extern void* __real_realloc(void* ptr, size_t size);

extern char __psram_bss_start[];
extern char __psram_bss_end[];

static uint8_t Z_GENERIC_SECTION(PSRAM) __aligned(PSRAM_HEAP_ALIGN) psram_arena[PSRAM_HEAP_SIZE];
static struct sys_heap psram_heap;
static struct k_spinlock psram_lock;
static bool psram_ready;
static uint32_t psram_test_errors;
static uint32_t psram_test_words;
static uint32_t psram_fallbacks;
static uintptr_t psram_first_bad_addr;
static uint32_t psram_first_bad_value;

/* Two-pass address-pattern test (write everything, then read everything back) with the data cache flushed and
 * invalidated in between, so an absent or aliased PSRAM is detected rather than the cache answering the reads. */
static uint32_t psram_pattern_test(uint32_t* words, uintptr_t* first_bad_addr, uint32_t* first_bad_value) {
    uint32_t errors = 0;
    *words = 0;
    for (size_t offset = 0; offset < sizeof(psram_arena); offset += 4096U) {
        volatile uint32_t* const word = (volatile uint32_t*)&psram_arena[offset];
        *word = 0xA5000000U ^ (uint32_t)(uintptr_t)word;
    }
    sys_cache_data_flush_and_invd_all();
    for (size_t offset = 0; offset < sizeof(psram_arena); offset += 4096U) {
        volatile uint32_t* const word = (volatile uint32_t*)&psram_arena[offset];
        const uint32_t pattern = 0xA5000000U ^ (uint32_t)(uintptr_t)word;
        const uint32_t value = *word;
        (*words)++;
        if (value != pattern) {
            if (errors == 0) {
                *first_bad_addr = (uintptr_t)word;
                *first_bad_value = value;
            }
            errors++;
        }
    }
    return errors;
}

static bool in_psram(const void* ptr) {
    const uint8_t* const p = ptr;
    return (p >= psram_arena) && (p < (psram_arena + PSRAM_HEAP_SIZE));
}

static void* psram_alloc(size_t size) {
    k_spinlock_key_t key = k_spin_lock(&psram_lock);
    void* ptr = sys_heap_aligned_alloc(&psram_heap, PSRAM_HEAP_ALIGN, size);
    k_spin_unlock(&psram_lock, key);
    return ptr;
}

extern void* screen_buffers_claim(size_t size);  // ScreenBuffers.c
extern int screen_buffers_owns(const void* ptr);

void* __wrap_malloc(size_t size) {
    void* dtcm = screen_buffers_claim(size);
    if (dtcm != NULL) {
        return dtcm;
    }
    if (psram_ready && (size >= PSRAM_HEAP_THRESHOLD)) {
        void* ptr = psram_alloc(size);
        if (ptr != NULL) {
            return ptr;
        }
        psram_fallbacks++;
        printk("PsramHeap: %u B request not served from PSRAM\n", (unsigned)size);
    }
    return __real_malloc(size);
}

void __wrap_free(void* ptr) {
    if (screen_buffers_owns(ptr)) {
        return;
    }
    if (in_psram(ptr)) {
        k_spinlock_key_t key = k_spin_lock(&psram_lock);
        sys_heap_free(&psram_heap, ptr);
        k_spin_unlock(&psram_lock, key);
    } else {
        __real_free(ptr);
    }
}

void* __wrap_calloc(size_t count, size_t size) {
    size_t total = 0;
    if (__builtin_mul_overflow(count, size, &total)) {
        return NULL;
    }
    void* ptr = __wrap_malloc(total);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void* __wrap_realloc(void* ptr, size_t size) {
    if (ptr == NULL) {
        return __wrap_malloc(size);
    }
    if (in_psram(ptr)) {
        k_spinlock_key_t key = k_spin_lock(&psram_lock);
        void* moved = sys_heap_aligned_realloc(&psram_heap, ptr, PSRAM_HEAP_ALIGN, size);
        k_spin_unlock(&psram_lock, key);
        return moved;
    }
    return __real_realloc(ptr, size);
}

// Runs after the FlexSPI2 PSRAM driver (POST_KERNEL, CONFIG_MEMC_INIT_PRIORITY = 0) and before the C++ static
// constructors, which Zephyr runs once the POST_KERNEL level has completed.
static int psram_heap_init(void) {
    psram_test_errors = psram_pattern_test(&psram_test_words, &psram_first_bad_addr, &psram_first_bad_value);
    memset(__psram_bss_start, 0, (size_t)(__psram_bss_end - __psram_bss_start));
    sys_heap_init(&psram_heap, psram_arena, sizeof(psram_arena));
    psram_ready = true;
    return 0;
}
SYS_INIT(psram_heap_init, POST_KERNEL, 1);

/* Console report for main(): the POST_KERNEL init runs before the USB console enumerates */
void psram_heap_report(void) {
    /* Largest block the OCRAM arena still hands out (probe by halving): the margin left for late small allocations */
    size_t probe = 256U * 1024U;
    void* block = NULL;
    while ((probe >= 256U) && ((block = __real_malloc(probe)) == NULL)) {
        probe /= 2U;
    }
    if (block != NULL) {
        __real_free(block);
    }
    printk("OcramHeap: largest free block >= %u B\n", (block != NULL) ? (unsigned)probe : 0U);
    printk("PsramHeap: arena %p..%p (%u B), boot read-back %u/%u words failed (first 0x%08x=0x%08x), ready=%d\n",
           (void*)psram_arena, (void*)(psram_arena + PSRAM_HEAP_SIZE), (unsigned)PSRAM_HEAP_SIZE,
           (unsigned)psram_test_errors, (unsigned)psram_test_words, (unsigned)psram_first_bad_addr,
           (unsigned)psram_first_bad_value, (int)psram_ready);
}
