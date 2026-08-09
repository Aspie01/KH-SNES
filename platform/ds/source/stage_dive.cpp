#include "stage.h"

namespace kh {

void DiveMachine::begin() {
    stage_ = DiveStage::Intro;
    shatter_ = 0;
    fall_ = 0;
    fade_ = 0;
    taken_ = ActType::None;
    given_ = ActType::None;
    pendActor_ = -1;
    pendWeapon_ = ActType::None;
}

// Shatter, and Shatter2: the same effect one station further down.
//
// Done with hardware rather than art -- MOSAIC coarsens the ground into ever
// larger blocks while the brightness falls and the screen shakes, which reads as
// glass breaking up without a second tileset for the debris.
StageStep DiveMachine::shatterStep(SceneView& view, ScreenFx& fx) {
    if (shatter_ == 0) {
        // Landed.  Which station the glass drops him onto depends on which broke.
        fx.shakeX = 0;
        fx.mosaic = 0;
        fx.forcedBlank = true;      // while VRAM is rewritten
        const bool second = (stage_ == DiveStage::Shatter2);
        stage_ = second ? DiveStage::S3Intro : DiveStage::S2Intro;
        return StageStep{second ? SceneAction::EnterStation3
                                : SceneAction::EnterStation2,
                         second ? ScriptId::DiveStation3 : ScriptId::DiveStation2};
    }
    --shatter_;

    // elapsed = SHATTER_LEN - remaining, scaled to the 0..15 the registers take.
    // The clamp is `cmp #16 / bcc` then 15, so 96/8 = 12 is the most it reaches:
    // the platform never fully blacks out before it lands.
    int step = (SHATTER_LEN - shatter_) >> 3;
    if (step >= 16) step = 15;
    fx.mosaic = uint8_t(step);
    fx.brightness = uint8_t(15 - step);
    // Alternating either side of centre on a four-frame period.  +/-2 here; the
    // night's Tear is +/-3.  Audit finding 6.
    fx.shakeX = (view.frame & 0x02) ? int8_t(-2) : int8_t(2);
    return StageStep{};
}

// Fall: specks of light stream up past him, then the light itself takes over.
StageStep DiveMachine::fallStep(SceneView& view, ScreenFx& fx) {
    (void)view;
    if (fall_ == 0) {
        // BeginFade.  BG1 stays off until the screen is white.
        stage_ = DiveStage::Fade;
        fade_ = FADE_LEN;
        fx.whiteout = 0;
        return StageStep{};
    }
    --fall_;
    // A new mote every fourth frame, tested AFTER the decrement.
    if ((fall_ & 0x03) == 0) return StageStep{SceneAction::SpawnMote};
    return StageStep{};
}

// FadeOut: white out, swap to Destiny Islands at the midpoint, fade back in.
StageStep DiveMachine::fadeStep(ScreenFx& fx) {
    if (fade_ == 0) {
        stage_ = DiveStage::Arrived;
        fx.whiteout = 0;
        return StageStep{SceneAction::Say, ScriptId::DiveWake};
    }
    --fade_;
    if (fade_ == FADE_LEN / 2) {
        // Fully white, so nothing of the swap is visible.  The register takes
        // five bits, so the SNES's 0xFF was a clamp and not a value.
        fx.whiteout = 31;
        fx.forcedBlank = true;
        return StageStep{SceneAction::EnterIsland};
    }
    if (fade_ < FADE_LEN / 2) {
        fx.whiteout = uint8_t(fade_);           // the timer is already 31 down to 0
    } else {
        fx.whiteout = uint8_t(FADE_LEN - fade_); // 1 up to 31
    }
    return StageStep{};
}

StageStep DiveMachine::update(SceneView& view, ScreenFx& fx) {
    // A box is up; nothing else happens.  Every Dive beat waits for dialogue --
    // including Shatter, which is worth stating because the night's Tear and
    // EndFade deliberately run THROUGH the box.  Audit, "what a port must be told".
    if (view.dialogue.busy()) return StageStep{};

    switch (stage_) {
        case DiveStage::Intro:
            // The opening message has been dismissed: the pedestals are live.
            stage_ = DiveStage::Pick;
            return StageStep{};

        case DiveStage::Shatter:
        case DiveStage::Shatter2:
            return shatterStep(view, fx);

        case DiveStage::S2Intro:
            stage_ = DiveStage::S2Fight;
            return StageStep{};

        case DiveStage::S2Fight:
            // WatchShadows: once the last one is gone the floor gives out again.
            if (view.actors.count(ActType::Shadow) != 0) return StageStep{};
            stage_ = DiveStage::Shatter2;
            shatter_ = SHATTER_LEN;
            return StageStep{SceneAction::Say, ScriptId::DiveFloorGoes};

        case DiveStage::S3Intro:
            // BeginBoss: the third station's line has been dismissed.
            stage_ = DiveStage::Boss;
            return StageStep{SceneAction::SpawnBoss, ScriptId::DiveBoss};

        case DiveStage::Boss:
            if (view.actors.count(ActType::Darkside) != 0) return StageStep{};
            stage_ = DiveStage::Done;
            return StageStep{SceneAction::Say, ScriptId::DiveVictory};

        case DiveStage::Done: {
            // BeginFall: the last station goes with the shadow, and Sora drops.
            stage_ = DiveStage::Fall;
            fall_ = FALL_LEN;
            fx.shakeX = 0;
            fx.mosaic = 0;
            fx.bgVisible = false;   // the void needs no art; the backdrop is black
            return StageStep{SceneAction::BeginFall};
        }

        case DiveStage::Fall:
            return fallStep(view, fx);

        case DiveStage::Fade:
            return fadeStep(fx);

        case DiveStage::Pick:
        case DiveStage::Drop:
            // Pressing A next to a weapon asks about it.  Which weapon, and the
            // REACH test that finds it, is the caller's -- this machine's only
            // interest is that an answer moves Pick to Drop and Drop to Shatter.
            return StageStep{};

        case DiveStage::Arrived:
            return StageStep{};
    }
    return StageStep{};
}

// A death, and the checkpoint it rewinds to.  Which stage that is depends on
// which station is loaded, so this is a per-scene rewind and not a single one.
StageStep DiveMachine::restart(SceneId scene, ScreenFx& fx) {
    fx.mosaic = 0;
    fx.shakeX = 0;
    fx.whiteout = 0;
    fx.brightness = 15;
    fx.forcedBlank = false;
    fx.bgVisible = true;
    shatter_ = 0;
    fall_ = 0;
    fade_ = 0;

    if (scene == SceneId::Dive3) {
        stage_ = DiveStage::Boss;
        return StageStep{SceneAction::SpawnBoss};
    }
    if (scene == SceneId::Dive2) {
        stage_ = DiveStage::S2Fight;
        return StageStep{SceneAction::EnterStation2};
    }
    // The first station: back to choosing, with the pedestals live again.  The
    // choice is inert, so it is deliberately NOT cleared -- a retry does not
    // un-choose what was already picked.
    stage_ = DiveStage::Pick;
    return StageStep{};
}

}  // namespace kh
