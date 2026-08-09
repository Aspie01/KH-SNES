// The ARM9's frame glue.  The one file in the port that cannot be tested here.
//
// EVERYTHING THAT DECIDES ANYTHING IS SOMEWHERE ELSE, and that is the whole
// design of this file.  device/boot.cpp runs the game, device/oam.cpp decides
// what is drawn, device/oam_pack.cpp decides what the hardware is told,
// device/ground.cpp streams the map, device/hud.cpp and device/box.cpp build
// their tilemaps, device/fx.cpp maps the screen effects onto registers, and
// every one of those is compiled and asserted on by the host suite on a machine
// with no toolchain.  What is left here is: read the pad, read the pen, copy
// bytes to addresses vram_map.h names, and wait for the vertical blank.
//
// The split is drawn where it is because of what this container can and cannot
// do.  nds.h does not exist here (tools/check_device.py is the gate), so every
// line in this file is a line no host run will ever compile again -- which
// makes it the one place in the port where a mistake survives.  So it is kept
// small enough to read in one sitting and it makes no decisions.
//
// WHAT IT IS NOT.  Not a demo, not a menu, not a title screen: it boots
// straight into the first Station of Awakening, which is where the game starts.
// SELECT restarts the scene and L/R step through the nine scenes, because a
// person testing a build on hardware needs to reach the town without playing to
// it, and that is the smallest thing that gets them there.

#include <nds.h>

#include <cstring>

#include "boot.h"
#include "box.h"
#include "constants.h"
#include "fx.h"
#include "gen/assets.h"
#include "ground.h"
#include "hud.h"
#include "init.h"
#include "mmio.h"
#include "oam.h"
#include "oam_pack.h"
#include "text.h"
#include "touch.h"
#include "vram_map.h"

using namespace kh;
using namespace kh::device;

namespace {

// ---------------------------------------------------------------------------
// The linked assets
//
// devkitPro's ds_rules runs bin2s over everything in $(DATA) and emits, for
// each file, a `<name>_bin` symbol and a `<name>_bin_end` one.  It also emits a
// `<name>_bin_size`, and this uses END MINUS START instead: the size symbol has
// been an `.int` in the data section in some versions and an absolute `.equ` in
// others, and the two are read differently from C.  The difference between the
// two labels is the length under both, so this cannot be wrong for a reason
// nobody will think to look for.
// ---------------------------------------------------------------------------
#define KH_BIN(name)                                                          \
    extern "C" const unsigned char name##_bin[];                              \
    extern "C" const unsigned char name##_bin_end[]

#define KH_BLOB(name)                                                         \
    Blob { name##_bin, size_t(name##_bin_end - name##_bin) }

// Per-scene ground, characters, palette and cast.  Nine scenes; the night has
// no ground of its own and borrows the island's, which is gen/assets.h's
// `groundFrom` and the one row below where the names do not line up.
KH_BIN(station1coll);  KH_BIN(station1height); KH_BIN(station1map);
KH_BIN(station1chr);   KH_BIN(station1pal);    KH_BIN(station1cast);
KH_BIN(station2coll);  KH_BIN(station2height); KH_BIN(station2map);
KH_BIN(station2chr);   KH_BIN(station2pal);    KH_BIN(station2cast);
KH_BIN(station3coll);  KH_BIN(station3height); KH_BIN(station3map);
KH_BIN(station3chr);   KH_BIN(station3pal);    KH_BIN(station3cast);
KH_BIN(station3boss);
KH_BIN(islandcoll);    KH_BIN(islandheight);   KH_BIN(islandmap);
KH_BIN(islandchr);     KH_BIN(islandpal);      KH_BIN(islandcast);
KH_BIN(islandday1);    KH_BIN(islandday2);     KH_BIN(islandspots);
KH_BIN(nightpal);      KH_BIN(nightcast);      KH_BIN(nightspots);
KH_BIN(fragmentcoll);  KH_BIN(fragmentheight); KH_BIN(fragmentmap);
KH_BIN(fragmentchr);   KH_BIN(fragmentpal);    KH_BIN(fragmentcast);
KH_BIN(fragmentboss);
KH_BIN(town1coll);     KH_BIN(town1height);    KH_BIN(town1map);
KH_BIN(town1chr);      KH_BIN(town1pal);       KH_BIN(town1cast);
KH_BIN(town1doors);
KH_BIN(town2coll);     KH_BIN(town2height);    KH_BIN(town2map);
KH_BIN(town2chr);      KH_BIN(town2pal);       KH_BIN(town2cast);
KH_BIN(town2doors);    KH_BIN(town2spots);
KH_BIN(town3coll);     KH_BIN(town3height);    KH_BIN(town3map);
KH_BIN(town3chr);      KH_BIN(town3pal);       KH_BIN(town3cast);
KH_BIN(town3doors);    KH_BIN(town3pair);

// Sprites, the font, and the object palettes.  Resident or scene-selected; the
// arrangement is gen/assets.h's SPRITE_ASSETS and PALETTE_ASSETS and not a
// choice made here.
KH_BIN(sorachr);       KH_BIN(objchr);         KH_BIN(obj2chr);
KH_BIN(objtownchr);    KH_BIN(hudchr);
KH_BIN(sorapal);       KH_BIN(heartpal);       KH_BIN(objpal);
KH_BIN(fxpal);         KH_BIN(shadowpal);      KH_BIN(divobjpal);
KH_BIN(islepal);       KH_BIN(nightscenepal);  KH_BIN(townobjpal);
KH_BIN(nightobjpal);   KH_BIN(armorpal);       KH_BIN(hudpal);

// What a scene needs beyond its SceneBlobs: the characters to draw it with, its
// background palette, and which of the two shared object pages goes up.
struct SceneArt {
    Blob chars;
    Blob palette;
    Blob objPage1;      // obj2chr for the islands, objtownchr for the districts
    Blob heartPalette;  // OBJ sub-palette 1, whichever this scene overrides with
    Blob scenePalette;  // OBJ sub-palette 2, likewise
};

SceneBlobs g_blobs[int(SceneId::Count)];
SceneArt g_art[int(SceneId::Count)];

void buildSceneTable() {
    // Written out rather than generated, because it is the one table in the
    // port that maps a scene onto SYMBOL NAMES and there is no way to compute a
    // symbol name.  gen/assets.h's SCENE_ASSETS is the authority on the shapes
    // and the optional tables; this is the authority on which bytes.  A row
    // that named the wrong file would load a scene whose collision map is
    // another scene's, and device/boot.cpp's extent check is what catches it.
    SceneBlobs* b = g_blobs;
    SceneArt* a = g_art;

    for (int i = 0; i < int(SceneId::Count); ++i) {
        b[i].tilesW = int(SCENE_ASSETS[i].tilesW);
        b[i].tilesH = int(SCENE_ASSETS[i].tilesH);
    }

    const int st1 = int(SceneId::Dive), st2 = int(SceneId::Dive2);
    const int st3 = int(SceneId::Dive3), isl = int(SceneId::Island);
    const int ngt = int(SceneId::Night), frg = int(SceneId::Fragment);
    const int t1 = int(SceneId::Town1), t2 = int(SceneId::Town2);
    const int t3 = int(SceneId::Town3);

    b[st1].collision = KH_BLOB(station1coll);
    b[st1].height = KH_BLOB(station1height);
    b[st1].chars = KH_BLOB(station1map);
    b[st1].cast = KH_BLOB(station1cast);
    a[st1].chars = KH_BLOB(station1chr);
    a[st1].palette = KH_BLOB(station1pal);

    b[st2].collision = KH_BLOB(station2coll);
    b[st2].height = KH_BLOB(station2height);
    b[st2].chars = KH_BLOB(station2map);
    b[st2].cast = KH_BLOB(station2cast);
    a[st2].chars = KH_BLOB(station2chr);
    a[st2].palette = KH_BLOB(station2pal);

    b[st3].collision = KH_BLOB(station3coll);
    b[st3].height = KH_BLOB(station3height);
    b[st3].chars = KH_BLOB(station3map);
    b[st3].cast = KH_BLOB(station3cast);
    b[st3].boss = KH_BLOB(station3boss);
    a[st3].chars = KH_BLOB(station3chr);
    a[st3].palette = KH_BLOB(station3pal);

    b[isl].collision = KH_BLOB(islandcoll);
    b[isl].height = KH_BLOB(islandheight);
    b[isl].chars = KH_BLOB(islandmap);
    b[isl].cast = KH_BLOB(islandcast);
    b[isl].day1 = KH_BLOB(islandday1);
    b[isl].day2 = KH_BLOB(islandday2);
    b[isl].spots = KH_BLOB(islandspots);
    a[isl].chars = KH_BLOB(islandchr);
    a[isl].palette = KH_BLOB(islandpal);

    // THE NIGHT IS THE ISLAND AFTER DARK: the same collision, the same heights
    // and the same characters, byte for byte.  What makes it night is one
    // palette upload, which is exactly the row below and is why `groundFrom`
    // exists in the generated table at all.
    b[ngt].collision = KH_BLOB(islandcoll);
    b[ngt].height = KH_BLOB(islandheight);
    b[ngt].chars = KH_BLOB(islandmap);
    b[ngt].cast = KH_BLOB(nightcast);
    b[ngt].spots = KH_BLOB(nightspots);
    a[ngt].chars = KH_BLOB(islandchr);
    a[ngt].palette = KH_BLOB(nightpal);

    b[frg].collision = KH_BLOB(fragmentcoll);
    b[frg].height = KH_BLOB(fragmentheight);
    b[frg].chars = KH_BLOB(fragmentmap);
    b[frg].cast = KH_BLOB(fragmentcast);
    b[frg].boss = KH_BLOB(fragmentboss);
    a[frg].chars = KH_BLOB(fragmentchr);
    a[frg].palette = KH_BLOB(fragmentpal);

    b[t1].collision = KH_BLOB(town1coll);
    b[t1].height = KH_BLOB(town1height);
    b[t1].chars = KH_BLOB(town1map);
    b[t1].cast = KH_BLOB(town1cast);
    b[t1].doors = KH_BLOB(town1doors);
    a[t1].chars = KH_BLOB(town1chr);
    a[t1].palette = KH_BLOB(town1pal);

    b[t2].collision = KH_BLOB(town2coll);
    b[t2].height = KH_BLOB(town2height);
    b[t2].chars = KH_BLOB(town2map);
    b[t2].cast = KH_BLOB(town2cast);
    b[t2].doors = KH_BLOB(town2doors);
    b[t2].spots = KH_BLOB(town2spots);
    a[t2].chars = KH_BLOB(town2chr);
    a[t2].palette = KH_BLOB(town2pal);

    b[t3].collision = KH_BLOB(town3coll);
    b[t3].height = KH_BLOB(town3height);
    b[t3].chars = KH_BLOB(town3map);
    b[t3].cast = KH_BLOB(town3cast);
    b[t3].doors = KH_BLOB(town3doors);
    b[t3].pair = KH_BLOB(town3pair);
    a[t3].chars = KH_BLOB(town3chr);
    a[t3].palette = KH_BLOB(town3pal);

    // The object pages and the two overridden sub-palettes, from
    // gen/assets.h's `when` column.  Sub-palette 1 is the Heartless and
    // sub-palette 2 the scenery, and an override is not a clash: it is what an
    // override IS, and the generator refuses two that would be up at once.
    for (int i = 0; i < int(SceneId::Count); ++i) {
        const bool town = i == t1 || i == t2 || i == t3;
        a[i].objPage1 = town ? KH_BLOB(objtownchr) : KH_BLOB(obj2chr);
        a[i].heartPalette = town      ? KH_BLOB(townobjpal)
                          : i == isl  ? KH_BLOB(islepal)
                          : i == ngt  ? KH_BLOB(nightscenepal)
                                      : KH_BLOB(heartpal);
        a[i].scenePalette = town     ? KH_BLOB(armorpal)
                          : i == ngt ? KH_BLOB(nightobjpal)
                                     : KH_BLOB(objpal);
    }
}

const SceneBlobs* sceneSource(SceneId s) {
    const int i = int(s);
    if (i < 0 || i >= int(SceneId::Count)) return nullptr;
    return &g_blobs[i];
}

// ---------------------------------------------------------------------------
// Copying into VRAM, and the trap that makes this a function
//
// VRAM DOES NOT ACCEPT 8-BIT WRITES.  A byte store to 0x06000000 is silently
// dropped by the memory controller -- not faulted, dropped -- so std::memcpy()
// onto VRAM works for the halfword-aligned middle of a copy and quietly loses
// whatever the compiler decided to do byte-wise at the ends.  The symptom is a
// tileset with two corrupt characters, which reads as a pipeline bug.
//
// And the source may be unaligned: a bin2s blob starts wherever the linker put
// it, and an unaligned halfword LOAD on ARMv5TE does not fault either, it
// returns a ROTATED word.  So the source is read a byte at a time and the
// destination written a halfword at a time, and neither hazard exists.
//
// It is slow.  It runs at boot and on a scene change and never in a frame, and
// the whole resident set is under 64 KiB, so it costs a fraction of one frame
// on a machine that spends most of them waiting for the vertical blank.
// ---------------------------------------------------------------------------
void vramCopy(uint32_t dst, Blob src) {
    volatile uint16_t* d = reinterpret_cast<volatile uint16_t*>(dst);
    for (size_t i = 0; i + 1 < src.size; i += 2)
        *d++ = uint16_t(uint16_t(src.data[i]) | uint16_t(src.data[i + 1] << 8));
}

// A 16-entry sub-palette, into one of the four palette regions.
void palCopy(uint32_t region, int slot, Blob src) {
    vramCopy(region + uint32_t(slot) * 32, src);
}

// ---------------------------------------------------------------------------
// The shadow buffers
//
// Built in main RAM and DMAd across in the vertical blank, rather than written
// straight into VRAM.  For OAM that is not a preference: writing an entry while
// the display controller is fetching it shows a sprite half at its old position
// and half at its new one, once, on the frames it happens, which is the classic
// "why does it flicker only when there are a lot of them" report.
// ---------------------------------------------------------------------------
uint16_t g_oamShadow[vram::OAM_ENTRIES * 4];    // four halfwords an entry
uint16_t g_hudMap[HUD_ENTRIES];
uint16_t g_boxMap[BOX_ENTRIES];
uint16_t g_menuMap[BOX_ENTRIES];                // the diagnostics panel
OamEntry g_oam[OAM_SLOTS];
SpriteSlot g_slots[OAM_SLOTS];

// ---------------------------------------------------------------------------
// The diagnostics panel
//
// MENU_MAP is reserved and empty -- device/hud.h explains why the command menu
// is not designed yet -- and an empty region with a name is the right place for
// the one thing a person testing on hardware cannot get any other way: which
// scene is loaded, and which beat the build could not perform.
//
// device/boot.cpp refuses ten actions by name rather than ignoring them, and
// this is what makes that refusal visible.  Without it the symptom is a stage
// machine whose timer runs out over a beat that never happened -- "invisible,
// silent, and only findable by someone who knows what the scene is supposed to
// look like", which is the note in boot.cpp's own refusal arm.
// ---------------------------------------------------------------------------
void panelClear() {
    for (int i = 0; i < BOX_ENTRIES; ++i) g_menuMap[i] = boxCell(CH_CLEAR);
}

void panelText(int row, const char* s) {
    if (row < 0 || row >= 32) return;
    int k = 0;
    for (int c = 0; c < 32; ++c) {
        // `k` stops at the terminator rather than the loop stopping there, so
        // the rest of the row is BLANKED.  The panel is rebuilt every frame and
        // a short message following a long one would otherwise leave the long
        // one's tail on screen -- the same erase text.s's OPT_W pad exists for.
        const char ch = (s && s[k]) ? s[k++] : ' ';
        g_menuMap[row * 32 + c] = boxCell(glyphOf(ch));
    }
    // glyphOf() folds lower case onto upper and turns anything unmapped into a
    // blank, so a message with a character the font does not have comes out with
    // a hole rather than with garbage.  text.h says so; this is why the panel
    // can print arbitrary strings safely.
}

const char* const SCENE_NAMES[int(SceneId::Count)] = {
    "STATION 1", "STATION 2", "STATION 3", "DESTINY ISLANDS", "THE NIGHT",
    "THE FRAGMENT", "FIRST DISTRICT", "SECOND DISTRICT", "THIRD DISTRICT",
};

// ---------------------------------------------------------------------------
// The pad
//
// pad.h: "The buttons keep their SNES names.  A DS mapping belongs in the
// device tier, beside the code that calls scanKeys()."  This is that place, and
// it is a straight rename: the DS's A is where the SNES's A was and its B is
// where the SNES's B was, so nothing is being reinterpreted.
// ---------------------------------------------------------------------------
uint16_t mapKeys(uint32_t keys) {
    uint16_t out = 0;
    if (keys & KEY_A) out = uint16_t(out | raw(Button::A));
    if (keys & KEY_B) out = uint16_t(out | raw(Button::B));
    if (keys & KEY_X) out = uint16_t(out | raw(Button::X));
    if (keys & KEY_Y) out = uint16_t(out | raw(Button::Y));
    if (keys & KEY_L) out = uint16_t(out | raw(Button::L));
    if (keys & KEY_R) out = uint16_t(out | raw(Button::R));
    if (keys & KEY_START) out = uint16_t(out | raw(Button::Start));
    if (keys & KEY_SELECT) out = uint16_t(out | raw(Button::Select));
    if (keys & KEY_UP) out = uint16_t(out | raw(Button::Up));
    if (keys & KEY_DOWN) out = uint16_t(out | raw(Button::Down));
    if (keys & KEY_LEFT) out = uint16_t(out | raw(Button::Left));
    if (keys & KEY_RIGHT) out = uint16_t(out | raw(Button::Right));
    return out;
}

void uploadSceneArt(SceneId s) {
    using namespace kh::vram;
    const SceneArt& a = g_art[int(s)];
    vramCopy(address(GROUND_CHR, Use::MainBg), a.chars);
    palCopy(PAL_MAIN_BG, 0, a.palette);
    // Sub-palette 1 and 2 of the OBJ region are the two the scenes override.
    palCopy(PAL_MAIN_OBJ, 1, a.heartPalette);
    palCopy(PAL_MAIN_OBJ, 2, a.scenePalette);
    // ...and the second object page, which is SHARED: the town overwrites the
    // islanders rather than joining them (gen/assets.h, main.s:540).
    vramCopy(address(OBJ_RESIDENT, Use::MainObj)
                 + uint32_t(SORA_SHEET_BYTES + OBJ_PAGE_BYTES),
             a.objPage1);
}

void uploadResident() {
    using namespace kh::vram;
    // The font, twice.  vram_map.h: "there are TWO COPIES of it -- one in
    // UI_CHR for the dialogue box over the world, one in SUB_CHR for the bottom
    // screen", because a character region is per engine and the two engines
    // cannot share one.
    vramCopy(address(UI_CHR, Use::MainBg), KH_BLOB(hudchr));
    vramCopy(address(SUB_CHR, Use::SubBg), KH_BLOB(hudchr));
    // ...and its palette, in sub-palette 15 on BOTH, for the reason
    // gen/assets.h gives: "the ground reloads into sub-palette 0 on every
    // scene; a font there would change colour with the scenery".
    palCopy(PAL_MAIN_BG, UI_SUBPALETTE, KH_BLOB(hudpal));
    palCopy(PAL_SUB_BG, UI_SUBPALETTE, KH_BLOB(hudpal));

    // Sora's sheet then the first object page, at the offsets oam_pack.h
    // derives its tile numbers from.  A third page does not fit the ten-bit
    // tile number at boundary 32 and gen/assets.h asserts as much.
    const uint32_t obj = address(OBJ_RESIDENT, Use::MainObj);
    vramCopy(obj, KH_BLOB(sorachr));
    vramCopy(obj + uint32_t(SORA_SHEET_BYTES), KH_BLOB(objchr));

    // The five OBJ sub-palettes no scene overrides.
    palCopy(PAL_MAIN_OBJ, 0, KH_BLOB(sorapal));
    palCopy(PAL_MAIN_OBJ, 3, KH_BLOB(fxpal));
    palCopy(PAL_MAIN_OBJ, 4, KH_BLOB(shadowpal));
    palCopy(PAL_MAIN_OBJ, 5, KH_BLOB(divobjpal));
}

// Every OAM entry is four halfwords: three attributes and one quarter of an
// affine matrix, interleaved.  Nothing here rotates, so the fourth is written
// zero -- which is safe precisely BECAUSE nothing rotates, and would silently
// destroy a matrix the day something does.  vram_map.h's oamAffine() is where
// that day starts.
void packOamShadow() {
    for (int i = 0; i < OAM_SLOTS; ++i) {
        g_oamShadow[i * 4 + 0] = g_oam[i].attr0;
        g_oamShadow[i * 4 + 1] = g_oam[i].attr1;
        g_oamShadow[i * 4 + 2] = g_oam[i].attr2;
        g_oamShadow[i * 4 + 3] = 0;
    }
}

}  // namespace

int main() {
    buildSceneTable();

    Mmio io;
    initScreens(io);
    uploadResident();

    using namespace kh::vram;
    TilemapGround ground(
        reinterpret_cast<uint16_t*>(address(GROUND_MAP, Use::MainBg)), io,
        int(MAIN_GROUND_LAYER));
    TouchState touch;
    Game game(sceneSource);

    // Straight into the first Station of Awakening, which is where the game
    // starts.  A failure here is a failure of the asset link step, so it says so
    // on the bottom screen rather than hanging on a black one.
    const bool started = game.begin(SceneId::Dive);
    int browse = int(SceneId::Dive);

    panelClear();
    if (!started) {
        panelText(0, "SCENE LOAD FAILED");
        panelText(2, "CHECK THE ASSET LINK");
    } else {
        // THE FIRST SCENE'S ART, BEFORE THE FIRST FRAME.  The loop uploads on
        // takeSceneChanged(), which would not be seen until after frame one has
        // already been composited -- one frame of the world drawn out of
        // whatever the banks powered on holding.  It is one frame and it is
        // avoidable, so it is avoided here rather than explained later.
        uploadSceneArt(game.scene());
        ground.setMap(game.charMap());
        ground.load(game.ground());
        game.takeSceneChanged();
    }

    while (true) {
        scanKeys();
        const uint32_t held = keysHeld();
        const uint32_t down = keysDown();

        Pad pad;
        pad.held = mapKeys(held);
        pad.pressed = mapKeys(down);

        touchPosition tp;
        touchRead(&tp);
        touch.update((held & KEY_TOUCH) != 0, int16_t(tp.px), int16_t(tp.py));
        // A TOUCH SYNTHESISES BUTTONS AND DOES NOTHING ELSE.  device/touch.h
        // argues the whole case: the oracle's guarantee is "same input, same
        // state", the recorded scripts have no touch column, and the moment the
        // simulation can be moved by a pen that guarantee becomes a statement
        // about the DS with the pen up.  Folding the tap into the pad here, at
        // the very edge, is what keeps it true.
        applyTouch(pad, touch, ADVANCE_REGION, ADVANCE_REGION_COUNT);

        // THE BRING-UP CONTROLS, and they are deliberately on the shoulders
        // where nothing in the game uses them.  L and R walk the scene list and
        // SELECT restarts the one that is loaded.  A person testing this needs
        // to reach the Third District without playing to it, and there is no
        // save system to do it with.
        if (down & (KEY_L | KEY_R | KEY_SELECT)) {
            if (down & KEY_L) browse = (browse + int(SceneId::Count) - 1)
                                       % int(SceneId::Count);
            if (down & KEY_R) browse = (browse + 1) % int(SceneId::Count);
            game.begin(SceneId(browse));
            pad = Pad{};        // the press that changed scene is not gameplay
        }

        game.frame(pad);
        browse = int(game.scene());

        if (game.takeSceneChanged()) {
            uploadSceneArt(game.scene());
            // setMap before load: the renderer refuses a character map that
            // does not match the collision map's extents, and refusing at load
            // time with a reason beats drawing a map one row short, "which does
            // not look like a bug, it looks like the artist forgot something".
            ground.setMap(game.charMap());
            ground.load(game.ground());
        }
        ground.draw(game.camera());

        const OamBuild ob = buildOam(game.actors(), game.camera(), g_slots);
        packAll(g_slots, ob.used, game.actors(), game.player(), game.soraCel(),
                game.frameCount(), g_oam);
        packOamShadow();

        buildHud(g_hudMap, game.actors(), game.hudState());
        buildBox(g_boxMap, game.dialogue());

        panelClear();
        panelText(0, SCENE_NAMES[int(game.scene())]);
        if (!game.lastActionPerformed()) {
            panelText(2, "BEAT NOT PORTED");
            panelText(3, actionName(game.lastAction()));
        } else if (game.error()) {
            panelText(2, "SCENE ERROR");
        }
        panelText(6, "L R SCENE   SELECT RESTART");

        applyFx(io, dispcntMainValue(), game.fx());

        // EVERYTHING ABOVE IS ARITHMETIC AND EVERYTHING BELOW IS A COPY.  The
        // wait goes between them so the copies land in the blanking interval,
        // which for OAM is the difference between a sprite moving and a sprite
        // tearing.
        swiWaitForVBlank();
        dmaCopy(g_oamShadow, reinterpret_cast<void*>(OAM_MAIN),
                sizeof g_oamShadow);
        dmaCopy(g_hudMap, reinterpret_cast<void*>(address(HUD_MAP, Use::SubBg)),
                sizeof g_hudMap);
        dmaCopy(g_boxMap, reinterpret_cast<void*>(address(BOX_MAP, Use::MainBg)),
                sizeof g_boxMap);
        dmaCopy(g_menuMap,
                reinterpret_cast<void*>(address(MENU_MAP, Use::SubBg)),
                sizeof g_menuMap);
    }
}
