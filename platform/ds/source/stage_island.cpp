#include "stage.h"

namespace kh {

void IslandMachine::begin() {
    state_ = QuestState::Idle;
    day_ = 1;
    dayTimer_ = 0;
    rikuWp_ = 0;
    leg_ = 0;
    raceWon_ = 0;
    raft_ = RaftName::None;
}

// Dim to black, then rebuild the island for the morning.
//
// The brightness formula is worth spelling out because it is easy to get wrong
// by one: `lda dayTimer / beq / dec dayTimer / lsr a` decrements MEMORY, so the
// accumulator still holds the value from BEFORE the decrement and the shift
// halves that.  So the ramp is dayTimer >> 1 read before the step: 15, 14, 14,
// 13, 13 ... 0 over DAY_FADE frames.
StageStep IslandMachine::dayOut(ScreenFx& fx) {
    if (dayTimer_ == 0) {
        fx.forcedBlank = true;      // while the cast is rebuilt
        day_ = 2;
        // The SNES writes Idle, calls HudUpdate, and only THEN writes DayIn --
        // three instructions apart, in this one frame.  The transient Idle is
        // load-bearing: the HUD it draws is the one the player sees 31 frames
        // later when the fade-in finishes, and it has to be day two's empty
        // checklist rather than a day-change state.  island.s:215-218.
        hudState_ = QuestState::Idle;
        state_ = QuestState::DayIn;
        dayTimer_ = DAY_FADE;
        fx.brightness = 0;
        return StageStep{SceneAction::RebuildForDayTwo};
    }
    fx.brightness = uint8_t(dayTimer_ >> 1);
    --dayTimer_;
    return StageStep{};
}

// ...and back up into day two.  This one reads the timer AFTER the decrement --
// `dec dayTimer / lda #DAY_FADE / sec / sbc dayTimer` -- so it ramps 0 up to 15
// rather than mirroring dayOut.
StageStep IslandMachine::dayIn(ScreenFx& fx) {
    if (dayTimer_ == 0) {
        fx.brightness = 15;
        state_ = QuestState::Idle;
        hudState_ = QuestState::Idle;
        return StageStep{SceneAction::Say, ScriptId::IslandNextDay};
    }
    --dayTimer_;
    fx.brightness = uint8_t((DAY_FADE - dayTimer_) >> 1);
    return StageStep{};
}

// The last evening.  Everything is on the raft, so the light simply goes, and
// what comes up is not the morning.  Same pre-decrement read as dayOut.
StageStep IslandMachine::dusk(ScreenFx& fx) {
    if (dayTimer_ == 0) return StageStep{SceneAction::BeginNight};
    fx.brightness = uint8_t(dayTimer_ >> 1);
    --dayTimer_;
    return StageStep{};
}

StageStep IslandMachine::countdown() {
    if (dayTimer_ == 0) {
        state_ = QuestState::RaceRun;
        return StageStep{SceneAction::HudChanged};
    }
    --dayTimer_;
    // The number on the race row changes every 64 frames; the HUD reads the
    // timer, so telling it the timer moved is all this has to do.
    return StageStep{SceneAction::HudChanged};
}

// Watch for either of them reaching the end of the course.
//
// Riku's waypoint count is tested FIRST, so he wins a same-frame tie.  Audit
// finding 8, and it is the kind of thing a port reorders without noticing.
StageStep IslandMachine::raceRun() {
    if (rikuWp_ >= RACE_WPS) {
        raceWon_ = 2;                       // Riku is home
        state_ = QuestState::RaceOver;
        return StageStep{SceneAction::Say, ScriptId::IslandRikuWins};
    }
    return StageStep{};
}

void IslandMachine::reachHome() {
    // Sora's second leg.  Only counts once he has tagged the paopu tree, and
    // only while the race is actually running.
    if (state_ != QuestState::RaceRun || leg_ == 0 || raceWon_ != 0) return;
    raceWon_ = 1;
    state_ = QuestState::RaceOver;
}

void IslandMachine::beginRace() {
    state_ = QuestState::RaceSet;
    hudState_ = state_;
    dayTimer_ = COUNT_LEN;
    rikuWp_ = 0;
    leg_ = 0;
    raceWon_ = 0;
}

// The result line has been dismissed.
StageStep IslandMachine::afterRace() {
    if (raceWon_ == 1) {
        state_ = QuestState::Naming;
        // TM_RAFT: the three names, not yes/no.
        return StageStep{SceneAction::Ask, ScriptId::IslandWhatName};
    }
    // He won, so he names it, and he was never going to pick anything else.
    raft_ = RaftName::Excalibur;
    state_ = QuestState::Named;
    return StageStep{SceneAction::Say, ScriptId::IslandRikuNames};
}

StageStep IslandMachine::talkToKairi(bool haveAll) {
    switch (state_) {
        case QuestState::Named:
            // The raft has a name and everything is on it.  There is nothing
            // left to do, so this line is the last of the day -- dismissing it
            // puts the light out.
            state_ = QuestState::Dusk;
            dayTimer_ = DAY_FADE;
            return StageStep{SceneAction::Say};

        case QuestState::Done:
            // Day two's list handed in and acknowledged: Riku wants his race.
            // Day one's Done never gets here, because the dispatch turns in for
            // the night the moment the previous line is dismissed.
            if (day_ == 2) {
                beginRace();
                return StageStep{SceneAction::HudChanged};
            }
            return StageStep{SceneAction::Say};

        case QuestState::Active:
            if (!haveAll) return StageStep{SceneAction::Say};
            state_ = QuestState::Done;
            return StageStep{SceneAction::Say};

        case QuestState::Idle:
            // First time asking: hand over the list, and the tally row with it.
            state_ = QuestState::Active;
            hudState_ = state_;
            return StageStep{SceneAction::HudChanged};

        default:
            return StageStep{SceneAction::Say};
    }
}

StageStep IslandMachine::update(SceneView& view, ScreenFx& fx) {
    // THE DAY CHANGE RUNS BEFORE THE DIALOGUE CHECK and ignores it, exactly as
    // the night's Tear and EndFade do.  island.s:69-76.
    if (state_ == QuestState::DayOut) return dayOut(fx);
    if (state_ == QuestState::DayIn) return dayIn(fx);

    if (view.dialogue.busy()) return StageStep{};

    switch (state_) {
        case QuestState::RaceSet:
            return countdown();
        case QuestState::RaceRun:
            return raceRun();
        case QuestState::RaceOver:
            return afterRace();

        case QuestState::Naming: {
            // The prompt has closed; its answer is the name Sora chose.  1-based,
            // because 0 has to mean "still choosing".
            const int answer = view.dialogue.result();
            if (answer == 0) return StageStep{};
            raft_ = RaftName(answer);
            state_ = QuestState::Named;
            // namedLines is indexed by the name minus one, because
            // RAFT_HIGHWIND is 1 and the first line.  The table is generated
            // from the assembly's .word run, so the order cannot drift.
            return StageStep{SceneAction::Say, NAMEDLINES[answer - 1],
                             uint8_t(answer)};
        }

        case QuestState::Dusk:
            return dusk(fx);

        case QuestState::Done:
            // Day one finished and its last line dismissed: turn in for the
            // night, with no player action.  Day two's Done waits for Kairi.
            if (day_ == 1) {
                state_ = QuestState::DayOut;
                dayTimer_ = DAY_FADE;
            }
            return StageStep{};

        case QuestState::Idle:
        case QuestState::Active:
        case QuestState::Named:
            // The island is walkable: pickups, the Keyblade and conversation,
            // all of which are the caller's.
            return StageStep{};

        default:
            return StageStep{};
    }
}

}  // namespace kh
