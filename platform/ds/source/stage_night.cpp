#include "stage.h"

namespace kh {

// ArmLightning, night.s:233.  One draw from the LFSR, masked to seven bits and
// added to the floor.  It is a named routine here for the same reason it is one
// over there: it is called from THREE places -- NightBegin, NightRestart and the
// end of a wait -- and the whole of the night's determinism is that those three
// draw exactly once each.
void NightMachine::armLightning(Rng& rng) {
    wait_ = FLASH_GAP_MIN + (rng.next() & FLASH_GAP_VAR);
}

void NightMachine::begin(Rng& rng) {
    stage_ = NightStage::Intro;
    timer_ = 0;
    // NightBegin sets spawnTimer to the gap before anything runs, so the first
    // Shadow of the search arrives a full gap in and not on the opening frame.
    spawn_ = shadowGap_;
    // The night is the ONLY scene that clears keyGot, and it does so itself
    // after LoadScene -- which sets it to 1.  Audit, LoadScene's side effects.
    key_ = false;
    armLightning(rng);
    // "No fade back in: the storm arrives with the first flash of lightning."
    flash_ = FLASH_LEN;
}

// One frame of the storm.
//
// Two strikes in the one flash: bright, out, brighter, out.  The ramp is read
// off the timer so it costs no extra state, and it reads the timer AFTER the
// decrement -- `dec flashTimer` then `lda flashTimer`.  With FLASH_LEN 8 the
// eight frames are 16, 16, 0, 0, 26, 26, 0, 0.
void NightMachine::lightning(SceneView& view, ScreenFx& fx) {
    if (flash_ != 0) {
        --flash_;
        uint8_t amount = 0;
        if (flash_ >= FLASH_LEN - 2) amount = 16;           // the first stab
        else if (flash_ >= FLASH_LEN - 4) amount = 0;        // dark again
        else if (flash_ < 2) amount = 0;                     // falling away
        else amount = 26;                                    // lights the island
        fx.whiteout = amount;
        // A non-zero amount switches the unit to additive white; zero puts it
        // back to translucent shadows.  ScreenFx carries the amount and the
        // device tier derives the mode, which is the whole point of arbitrating
        // in one place -- see the header.
        return;
    }
    if (wait_ != 0) {
        --wait_;
        return;
    }
    flash_ = FLASH_LEN;
    // Armed for the NEXT flash while this one runs: 150 plus a random 0..127.
    armLightning(view.rng);
}

// Shadows arriving, for as long as the search lasts.  Unlike the Second
// District's wave this has no cap and no counter: they keep coming until the
// stage moves on, which is what makes the night a search under pressure rather
// than a fight with an end.
void NightMachine::spawnShadows(SceneView& view) {
    if (spawn_ != 0) {
        --spawn_;
        return;
    }
    spawn_ = shadowGap_;
    // The cap is tested BEFORE the draw on both machines (night.s:359 `bcs
    // @out`), so a night at its ceiling advances the LFSR not at all -- which is
    // what keeps the sequence the same however many Shadows are standing.
    const SpotOutcome r = spawnAtSpot(view, spots_, nSpots_, shadowMax_);
    if (r.tooClose) spawn_ = SPAWN_RETRY;
}

// He is gone, and a column of dark is standing where he was.
//
// The first pass is detected by the timer still holding its entry value, which
// is how one counter serves both "replace them" and "hold for DARK_HOLD".  Note
// the column REPLACES the actor rather than being spawned beside it, so the
// caller must delete and spawn, not just spawn.
StageStep NightMachine::column(SceneView& view, SceneAction make,
                               NightStage next, ScriptId say) {
    if (timer_ == DARK_HOLD) {
        --timer_;
        // Kairi's column arrives on a flash of its own; Riku's does not.
        if (make == SceneAction::ColumnForKairi) flash_ = FLASH_LEN;
        return StageStep{make};
    }
    if (timer_ != 0) {
        --timer_;
        // THE COLUMN FLICKERS, AND ONLY RIKU'S DOES.
        //
        // night.s:576-593, TakeRiku's @wait branch: every frame of the hold it
        // writes actTile on every ACT_DARK, alternating the column's two cels.
        // LoseKairi's @wait (night.s:731-735) is `dec nightTimer; rts` and does
        // not -- so hers stands still.  That asymmetry is almost certainly an
        // oversight in the original, and it is reproduced rather than tidied
        // because the oracle is the oracle; if it is ever to be corrected that
        // is a divergence with a document, not a quiet improvement here.
        //
        // `and #$04` TESTS BIT TWO, so the period is EIGHT frames -- four on
        // each cel -- not four.  Writing `& 1` would double the rate, which is
        // the kind of difference nothing in this tree could catch: THE TRACE
        // CARRIES NO TILE COLUMN.  trace.cpp:74-77 emits
        // actor=idx/type/x/y/state/timer/hp, so a cel is invisible to all eight
        // scenarios and this line is held up by test_stage2.cpp alone.  The
        // column's TYPE and POSITION are in the trace; what it looks like is not.
        //
        // This whole branch was `--timer_; return` with `(void)view;` at the top
        // of the function, which is what a dropped routine looks like when the
        // parameter it needed is still in the signature.
        if (make == SceneAction::ColumnForRiku) {
            const uint8_t cel = (view.frame & 0x04u)
                                    ? uint8_t(sprite::Dark + 4)
                                    : uint8_t(sprite::Dark);
            for (int i = 0; i < MAX_ACTORS; ++i)
                if (view.actors.type[i] == ActType::Dark)
                    view.actors.tile[i] = cel;
        }
        return StageStep{};
    }
    stage_ = next;
    if (next == NightStage::Tear) {
        timer_ = TEAR_LEN;
        // The flash must not be left switched on under what follows.
        flash_ = 0;
    }
    return StageStep{SceneAction::Say, say};
}

// The island comes apart, the same way the platform did.  Unlike the platform,
// TEAR_LEN is 120, so 120/8 reaches mosaic 15 and brightness 0 -- the Dive's 96
// only reaches 12 and 3.  The shake is +/-3 here and +/-2 there.
StageStep NightMachine::tear(SceneView& view, ScreenFx& fx) {
    if (timer_ == 0) {
        fx.shakeX = 0;
        fx.mosaic = 0;
        fx.forcedBlank = true;
        stage_ = NightStage::Boss;
        key_ = true;                    // the fragment re-asserts it
        return StageStep{SceneAction::EnterFragment, ScriptId::NightFragment};
    }
    --timer_;
    int step = (TEAR_LEN - timer_) >> 3;
    if (step >= 16) step = 15;
    fx.mosaic = uint8_t(step);
    fx.brightness = uint8_t(15 - step);
    fx.shakeX = (view.frame & 0x02) ? int8_t(-3) : int8_t(3);
    return StageStep{};
}

// The dark takes the rest.  Subtractive on BG1, the sprites and the backdrop,
// but NOT on BG3, so the world goes black while the closing line stays readable.
StageStep NightMachine::endFade(ScreenFx& fx) {
    if (timer_ == 0) {
        fx.whiteout = 31;               // the register's five bits, saturated
        stage_ = NightStage::Over;
        return StageStep{SceneAction::Say, ScriptId::NightCard};
    }
    --timer_;
    int amount = (END_FADE - timer_) >> 1;
    if (amount >= 32) amount = 31;
    fx.whiteout = uint8_t(amount);
    return StageStep{};
}

StageStep NightMachine::talkToRiku() {
    if (stage_ != NightStage::Seek) return StageStep{};
    stage_ = NightStage::Riku;
    timer_ = DARK_HOLD;
    return StageStep{SceneAction::Say};
}

StageStep NightMachine::talkToKairi() {
    if (stage_ != NightStage::Kairi) return StageStep{};
    stage_ = NightStage::Door;
    timer_ = DARK_HOLD;
    // The door on the wall is RETYPED, not replaced: a second actor would leave
    // the old one standing behind it, and the retyped one falls outside the
    // examinable range so it can no longer be looked at.  Audit finding 13.
    return StageStep{SceneAction::OpenTheDoor, ScriptId::NightKairi};
}

StageStep NightMachine::update(SceneView& view, ScreenFx& fx) {
    // The storm runs through whatever else is happening -- but not past the end
    // of it.  From END on, the colour-math unit belongs to the fade, and a flash
    // resetting it back to translucent shadows would undo the fade every time
    // one landed.  THIS SUPPRESSION IS THE REASON ScreenFx EXISTS.
    if (stage_ < NightStage::End) lightning(view, fx);

    // The two effects stages own the screen and ignore the dialogue box, so the
    // line about the island coming apart is on screen while it does.
    if (stage_ == NightStage::Tear) return tear(view, fx);
    if (stage_ == NightStage::End) return endFade(fx);

    if (view.dialogue.busy()) return StageStep{};

    switch (stage_) {
        case NightStage::Intro:
            stage_ = NightStage::Seek;
            return StageStep{SceneAction::HudChanged};

        case NightStage::Seek:
            // Shadows arrive, and the one thing worth doing is finding Riku.
            // Before the Keyblade they can neither be hurt nor hurt Sora.
            spawnShadows(view);
            return StageStep{};

        case NightStage::Riku:
            return column(view, SceneAction::ColumnForRiku, NightStage::Key,
                          ScriptId::NightRikuGone);

        case NightStage::Key:
            // The dark thins out, and there is something in his hand.
            key_ = true;
            stage_ = NightStage::Kairi;
            flash_ = FLASH_LEN;         // it arrives on a flash of its own
            return StageStep{SceneAction::ClearColumns, ScriptId::NightKey};

        case NightStage::Kairi:
            // She is in the Secret Place, and the Shadows can be answered now.
            spawnShadows(view);
            return StageStep{};

        case NightStage::Door:
            return column(view, SceneAction::ColumnForKairi, NightStage::Tear,
                          ScriptId::NightTorn);

        case NightStage::Boss:
            if (view.actors.count(ActType::Darkside) != 0) return StageStep{};
            stage_ = NightStage::End;
            timer_ = END_FADE;
            fx.whiteout = 0;
            // Its crater Shadows go with it -- and ONLY those.  WatchBoss tests
            // ACT_SHADOW alone, so an orb already in flight survives the sweep
            // with up to ORB_LIFE frames of travel left and no gate on the boss
            // being alive.  BEHAVIOUR.md §12 credits the night with a cleanup
            // the Dive lacks; the cleanup is narrower than it reads.
            return StageStep{SceneAction::SweepCraterShadows};

        case NightStage::Over:
            // The card has been dismissed.  There is somewhere for him to wash up.
            return StageStep{SceneAction::EnterTown};

        default:
            return StageStep{};
    }
}

// A death, and the checkpoint it rewinds to.
//
// The screen-wide effects go back FIRST, before anything else, because a death
// can land in the middle of the island coming apart -- mid-Tear the mosaic is
// coarse, the screen is shaking and the brightness is most of the way to black,
// and none of that belongs to the retry.  This is the clearest case for
// arbitrating effects in one place: the reset is three assignments here instead
// of three register writes scattered across a restart path.
StageStep NightMachine::restart(Rng& rng, bool onFragment, ScreenFx& fx) {
    fx.mosaic = 0;
    fx.shakeX = 0;
    fx.whiteout = 0;            // ShadowMath: back to translucent shadows
    fx.brightness = 15;
    fx.forcedBlank = false;
    fx.bgVisible = true;

    flash_ = 0;
    timer_ = 0;
    spawn_ = shadowGap_;        // NightRestart sets it explicitly
    // ArmLightning: a fresh wait, DRAWN from the same LFSR, so a retry does not
    // reproduce the run that killed him.  This line used to set the floor and
    // draw nothing, under this same comment; the draw is the load-bearing half,
    // because skipping it leaves the LFSR one step behind the oracle's for the
    // whole of the rest of the night.
    armLightning(rng);

    // LoadScene sets keyGot to 1 unconditionally on the way in -- it is one of
    // its scene-independent side effects -- so the Keyblade is BACK before the
    // rewind decides anything, and the early branch is what takes it away again.
    // Modelling it the other way round works for the late branch by accident and
    // is wrong for a retry that reloads the scene.
    key_ = true;

    if (onFragment) {
        stage_ = NightStage::Boss;
        return StageStep{SceneAction::RespawnFragment};
    }
    if (stage_ >= NightStage::Kairi) {
        // The Keyblade is not taken back, and the door stays open.
        stage_ = NightStage::Kairi;
        return StageStep{SceneAction::RespawnNightCast};
    }
    // He has not earned it, so take it back off him.
    key_ = false;
    stage_ = NightStage::Seek;
    return StageStep{SceneAction::RespawnNightCast};
}

}  // namespace kh
