/* DoomFlight memory placement for the Teensy 4.1 (rationale in PsramHeap.c).
 * PSRAM: DoomSubtopology::doom, the Doom::DoomEngine instance, carries full-resolution frame storage that does not
 *        fit OCRAM. The section is zeroed by PsramHeap.c before the C++ constructors run.
 */
SECTION_PROLOGUE(psram_bss,(NOLOAD),)
{
	. = ALIGN(32);
	*(.bss._ZN15DoomSubtopology4doomE)
	/* CdhCore::tlmSend (Svc.TlmPacketizer, ~98 KiB of channel and packet storage): OCRAM ran out of malloc arena for
	 * the comm BufferManager once frameHandoff and frameStreamer were added; its small per-channel writes and the
	 * 2 Hz packet sends do not need tightly coupled RAM. */
	. = ALIGN(32);
	*(.bss._ZN7CdhCore7tlmSendE)
	. = ALIGN(32);
} GROUP_LINK_IN(PSRAM)
__psram_bss_start = ADDR(psram_bss);
__psram_bss_end = ADDR(psram_bss) + SIZEOF(psram_bss);

