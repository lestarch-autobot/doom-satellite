// DoomFlight screen buffers in DTCM on the Teensy 4.1 (rationale in PsramHeap.c and the README).
//
// The engine draws every frame column by column into I_VideoBuffer, then I_FinishUpdate copies it row by row into
// DG_ScreenBuffer, which the frame pipeline reads. Both buffers come from heaps the unmodified doomgeneric submodule
// places in PSRAM: I_VideoBuffer from the zone (Z_Malloc, i_video.c) and DG_ScreenBuffer from malloc (doomgeneric.c).
// Measured at PSRAM 120 MHz the copy alone took 5.5 ms of the 33 ms tick and the strided column writes dominated the
// render phase. The 128 KiB DTCM is otherwise unused and holds both 64,000-byte buffers; this file steers the two
// allocations there at link time (-Wl,--wrap=Z_Malloc -Wl,--wrap=Z_Free, CMakeLists.txt; PsramHeap.c's malloc wrapper)
// so the engine source stays untouched.
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/sys/printk.h>

// SCREENWIDTH x SCREENHEIGHT (doomdef.h) = DOOMGENERIC_RESX x DOOMGENERIC_RESY (Doom/DoomEngine/CMakeLists.txt) =
// Doom.FRAME_WIDTH x Doom.FRAME_HEIGHT (project/config/DoomConfig.fpp).
#define SCREEN_WIDTH 320U
#define SCREEN_HEIGHT 200U
#define SCREEN_BYTES (SCREEN_WIDTH * SCREEN_HEIGHT)
// doomgeneric.c allocates DG_ScreenBuffer as RESX * RESY * 4 bytes for its 32-bit pixel build; with CMAP256 (the
// build used here) pixel_t is one byte and I_FinishUpdate writes exactly SCREEN_BYTES of it.
#define DG_SCREEN_REQUEST (SCREEN_BYTES * 4U)
#define PU_STATIC 1  // z_zone.h

static uint8_t __dtcm_bss_section __aligned(32) video_buffer[SCREEN_BYTES];   // I_VideoBuffer
static uint8_t __dtcm_bss_section __aligned(32) screen_buffer[SCREEN_BYTES];  // DG_ScreenBuffer
/* Latest-wins slots Components::FrameStreamer copies the native frame into on the engine thread (PSRAM: OCRAM has
 * no room for two more frames; the ~1 ms copy is paid by the engine, the send by the streamer thread). */
#define STREAM_SLOTS 2U
static uint8_t Z_GENERIC_SECTION(PSRAM) __aligned(32) stream_slots[STREAM_SLOTS][SCREEN_BYTES];

static int video_buffer_claimed;
static int screen_buffer_claimed;

extern void* __real_Z_Malloc(int size, int tag, void* user);
extern void __real_Z_Free(void* ptr);
extern unsigned char* I_VideoBuffer;  // i_video.c
extern uint8_t* DG_ScreenBuffer;      // doomgeneric.c (pixel_t is uint8_t under CMAP256)

// I_InitGraphics requests I_VideoBuffer as Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL) before the first
// tick; later requests of the same shape (the wipe buffers in f_wipe.c) stay in the zone.
void* __wrap_Z_Malloc(int size, int tag, void* user) {
    if (!video_buffer_claimed && (size == (int)SCREEN_BYTES) && (tag == PU_STATIC) && (user == NULL)) {
        video_buffer_claimed = 1;
        return video_buffer;
    }
    return __real_Z_Malloc(size, tag, user);
}

void __wrap_Z_Free(void* ptr) {
    if (ptr == (void*)video_buffer) {
        return;
    }
    __real_Z_Free(ptr);
}

// Called by PsramHeap.c's malloc wrapper: the one DG_ScreenBuffer request is served from DTCM.
void* screen_buffers_claim(size_t size) {
    if (!screen_buffer_claimed && (size == DG_SCREEN_REQUEST)) {
        screen_buffer_claimed = 1;
        return screen_buffer;
    }
    return NULL;
}

int screen_buffers_owns(const void* ptr) {
    return (ptr == (const void*)video_buffer) || (ptr == (const void*)screen_buffer);
}

// Boot check (Main.cpp): both engine pointers must be the DTCM buffers, otherwise the wrappers missed their request.
void screen_buffers_report(void) {
    printk("ScreenBuffers: I_VideoBuffer=%p (DTCM %p, %s) DG_ScreenBuffer=%p (DTCM %p, %s)\n", (void*)I_VideoBuffer,
           (void*)video_buffer, (I_VideoBuffer == video_buffer) ? "ok" : "MISSED", (void*)DG_ScreenBuffer,
           (void*)screen_buffer, (DG_ScreenBuffer == screen_buffer) ? "ok" : "MISSED");
}

uint8_t* stream_slot(unsigned int index) {
    return (index < STREAM_SLOTS) ? stream_slots[index] : NULL;
}
