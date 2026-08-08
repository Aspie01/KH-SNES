#include "stage.h"

namespace kh {

void TownMachine::begin(Rng& rng) {
    stage_ = TownStage::Arrive;
    door_ = 0;
    doorTo_ = 0;
    timer_ = 0;
    spawn_ = 0;
    spawned_ = 0;
    // TownBegin RE-SEEDS.  The island seeded 0xACE1 and the town seeds 0x1D57,
    // so the Second District's arrivals are a fixed sequence that does not
    // depend on how long the player spent on the island.
    rng.seed(Rng::TOWN_SEED);
}

void TownMachine::arriveAtThird() {
    // Walking into the Third District for the first time starts the wait.  The
    // guard is on the STAGE and not on a visited flag: at Third it fires, and
    // afterwards the stage has moved past it, so coming back does not.
    if (stage_ != TownStage::Third) return;
    stage_ = TownStage::Meet;
    timer_ = FALL_WAIT;
}

// One frame of a district change: out, swap, back in.  A door owns the screen
// for as long as it is running -- dialogue, the Heartless and the doors
// themselves are all skipped while doorTimer is non-zero.
StageStep TownMachine::doorStep(ScreenFx& fx) {
    --door_;
    if (door_ == 0) {
        fx.brightness = 15;
        return StageStep{};
    }
    if (door_ == DOOR_FADE) {
        fx.brightness = 0;
        return StageStep{SceneAction::EnterDistrict, ScriptId::None, doorTo_};
    }
    if (door_ < DOOR_FADE) {
        fx.brightness = uint8_t((DOOR_FADE - door_) >> 1);      // back in
    } else {
        fx.brightness = uint8_t((door_ - DOOR_FADE) >> 1);      // still going out
    }
    return StageStep{};
}

// Keep the square populated until the wave is spent, then let the player
// through.  Second District only.
StageStep TownMachine::shadows(SceneView& view) {
    if (spawned_ >= TOWN_WAVE) {
        // The whole wave has been out and none of it is left: the way on opens.
        if (view.actors.count(ActType::Shadow) != 0) return StageStep{};
        if (stage_ != TownStage::Second) return StageStep{};
        stage_ = TownStage::Third;
        return StageStep{SceneAction::Say, ScriptId::TownClear};
    }

    if (spawn_ != 0) {
        --spawn_;
        return StageStep{};
    }
    // Note the period is TOWN_GAP + 1 frames, not TOWN_GAP: the timer is set to
    // 80 and counted DOWN TO zero, and the frame it reaches zero is the frame
    // after the last decrement.  The same N-then-count-to-zero off-by-one the
    // audit records for every animation rate.  §6's "one every 80 frames" is the
    // constant, not the measured period.
    spawn_ = TOWN_GAP;
    if (view.actors.count(ActType::Shadow) >= TOWN_SHADOWS) return StageStep{};
    if (spots_ == nullptr || nSpots_ <= 0) return StageStep{};

    // Somewhere in the square, but not on top of the player.
    const uint8_t spot = view.rng.pick(nSpots_);
    const World sx = tileCentre(spots_[spot].i);
    const World sy = tileCentre(spots_[spot].j);
    const int32_t dx = sx.raw() - view.actors.x[view.player].raw();
    const int32_t dy = sy.raw() - view.actors.y[view.player].raw();
    // 1024 Q12.4 is 64 px.  BOTH axes have to be inside it to count as too
    // close -- the SNES fell through to @far as soon as either was outside.
    if ((dx < 0 ? -dx : dx) < SPAWN_CLEAR && (dy < 0 ? -dy : dy) < SPAWN_CLEAR) {
        spawn_ = SPAWN_RETRY;       // one arriving in your face reads as a bug
        return StageStep{};
    }
    if (view.actors.spawn(ActType::Shadow, sx, sy) < 0) return StageStep{};
    ++spawned_;                     // counted on SUCCESS, exactly as `inc` was
    return StageStep{};
}

// The wait, and then two people landing in the square.
StageStep TownMachine::meet(SceneView& view, ScreenFx& fx) {
    // Gated on Donald existing rather than on a pure timer, so the descent and
    // the wait share one counter without ambiguity.  Audit finding 35.
    if (view.actors.count(ActType::Donald) == 0) {
        if (timer_ != 0) {
            --timer_;
            return StageStep{};
        }
        timer_ = FALL_DROP;
        return StageStep{SceneAction::DropPair};
    }
    if (timer_ != 0) {
        --timer_;
        return StageStep{SceneAction::LowerPair};
    }
    // Landed.
    fx.shakeX = 0;
    stage_ = TownStage::Boss;
    timer_ = 1;                 // ...and the armour has not come down yet
    return StageStep{SceneAction::Say, ScriptId::TownMeet};
}

// Raise it once, then wait for it to stop.
StageStep TownMachine::watchArmor(SceneView& view, ScreenFx& fx) {
    if (timer_ != 0) {
        timer_ = 0;             // a one-shot flag, not a countdown
        return StageStep{SceneAction::RaiseArmor};
    }
    if (view.actors.count(ActType::Armor) != 0) return StageStep{};
    fx.shakeX = 0;
    stage_ = TownStage::Won;
    // Its hands go with it.
    return StageStep{SceneAction::SweepGauntlets, ScriptId::TownWon};
}

StageStep TownMachine::update(SceneView& view, ScreenFx& fx) {
    // A door owns the screen for as long as it is running -- before the dialogue
    // check, so a message cannot hold a transition open.
    if (door_ != 0) return doorStep(fx);

    if (view.dialogue.busy()) return StageStep{};

    switch (stage_) {
        case TownStage::Arrive:
            // Woke: the opening line has been dismissed.
            stage_ = TownStage::Look;
            return StageStep{SceneAction::HudChanged};

        case TownStage::Meet:
            return meet(view, fx);

        case TownStage::Boss:
            return watchArmor(view, fx);

        case TownStage::Won:
            // AfterArmor: the closing card.
            stage_ = TownStage::Over;
            return StageStep{SceneAction::Say, ScriptId::TownCard};

        case TownStage::Over:
            return StageStep{};     // the card stays up

        case TownStage::Look:
        case TownStage::Second:
        case TownStage::Third:
            // The town is walkable and the doors are live.  Only the Second
            // District spawns anything.
            if (district_ == SceneId::Town2) return shadows(view);
            return StageStep{};
    }
    return StageStep{};
}

}  // namespace kh
