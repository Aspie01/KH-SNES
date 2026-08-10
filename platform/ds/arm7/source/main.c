/* The ARM7, and why it is here at all.
 *
 * The game runs on the ARM9.  This processor does four things for it, three of
 * which are invisible and one of which the port genuinely needs:
 *
 *   THE TOUCHSCREEN.  The touch panel and the buttons hang off the ARM7's SPI
 *   bus and the ARM9 cannot read either directly.  inputGetAndSend() samples
 *   them once a frame and pushes the result over the FIFO, which is what makes
 *   scanKeys() and touchRead() return anything at all on the other side.
 *   device/touch.cpp is written against exactly that: "`down` is libnds's
 *   KEY_TOUCH; `x` and `y` are the calibrated position".  Without this file
 *   those are always zero and the pen does nothing, silently.
 *
 *   THE USER SETTINGS.  readUserSettings() pulls the owner's name, language and
 *   -- the part that matters -- the touchscreen CALIBRATION out of firmware.
 *   Skip it and touchRead() still returns coordinates; they are just wrong, by a
 *   different amount on every console, which reads as a bug in the hit regions.
 *
 *   THE REAL-TIME CLOCK, sound, power and the card, all through
 *   installSystemFIFO().  Nothing in the port uses them yet -- there is no audio
 *   driver; docs/DS_PORT_PROMPT.md lists it as unwritten -- but the FIFO handler
 *   is what stops an ARM9-side libnds call that expects an answer from hanging
 *   forever waiting for one.
 *
 * IT IS DELIBERATELY THE STOCK TEMPLATE AND NOT A DESIGN.  Every line below is
 * devkitPro's default ARM7 core, and that is the correct amount of originality
 * for this file: an ARM7 that differs from the standard one differs in ways
 * that show up as the touchscreen drifting or the console failing to sleep, and
 * neither is a thing this project has any way to test.  When there is an audio
 * driver, it goes here, and this comment is where the reason will be.
 *
 * Written in C rather than C++ because there is nothing here that wants a type
 * system, and because the C++ runtime's static-initialisation machinery is not
 * something to link into a 96 KiB processor for no reason.
 */

#include <nds.h>

static volatile bool exitflag = false;

static void VcountHandler(void) {
    /* Sample the buttons and the pen and push them over the FIFO.  On VCOUNT
     * rather than VBLANK on purpose: the touch panel needs several SPI reads to
     * settle and doing them in the vblank handler would eat the window the ARM9
     * wants for its own DMA. */
    inputGetAndSend();
}

static void VblankHandler(void) {
}

int main(void) {
    /* Calibration and language, out of firmware.  Before irqInit() because the
     * firmware read is a blocking SPI transaction and an interrupt landing in
     * the middle of one is how it comes back with a corrupted byte. */
    readUserSettings();

    irqInit();
    fifoInit();

    /* Sound, power, the RTC, storage and the sleep handshake.  One call in
     * current libnds; older versions split sound out into installSoundFIFO(),
     * and if this build ever fails to link on that symbol, that is the version
     * difference and not a missing file. */
    installSystemFIFO();

    irqSet(IRQ_VCOUNT, VcountHandler);
    irqSet(IRQ_VBLANK, VblankHandler);
    irqEnable(IRQ_VBLANK | IRQ_VCOUNT);

    while (!exitflag) {
        /* L+R+Start+Select quits, which is the convention every homebrew DS
         * binary follows and the only way off a flashcart menu-less build. */
        if ((REG_KEYINPUT & (KEY_SELECT | KEY_START | KEY_L | KEY_R)) == 0)
            exitflag = true;
        swiWaitForVBlank();
    }
    return 0;
}
