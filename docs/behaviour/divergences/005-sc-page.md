---
id: 005
spec_section: "13"
trace_fields: [txtState, txtPtr, txtRow]
platform: ds
reason: SC_PAGE ended the message instead of paging, so 63% of the dialogue was unreachable
---

# `SC_PAGE` pages

This is a **bug fix**, not an adaptation. Every other divergence in this
directory exists because the DS is a different machine. This one exists because
the SNES build does not do what its own source comment says it does.

## What the SNES does

`text.inc` documents `SC_PAGE` as "wait, clear the text area, keep going".

`EmitOne` sets `TS_WAIT` for `SC_PAGE` (text.s:449-455), exactly as it does for
`SC_END` (text.s:432-437). `TextUpdate`'s `TS_WAIT` branch then closes the box
for `TM_MESSAGE` **unconditionally** (text.s:466-473). `ClearTextArea` has one
caller, `TextOpen` (text.s:97). **There is no transition from `TS_WAIT` back to
`TS_REVEAL` anywhere in the file.**

So a page break is an end of message, and every byte after the first `SC_PAGE`
in a script is unreachable.

| | |
| --- | --- |
| scripts | 70 |
| scripts using `SC_PAGE` | **54** |
| dialogue characters reachable | 1831 |
| dialogue characters never shown | **3213 — 63%** |

Worst affected: `island.s:scriptKairiAsk2` (342 characters dead),
`scriptKairiAsk` (194), `town.s:scriptCid` (157), `island.s:scriptChallenge`
(141), `dive.s:scriptIntro` (118).

## Verified, not deduced

Read carefully enough, the assembly says this plainly — but the whole point of
having a frozen oracle is not to trust a reading. Booting the ROM under
hardware-accurate emulation and pressing A once at the opening line:

- **before the press:** "SO MUCH TO DO, / SO LITTLE TIME. / / TAKE YOUR TIME."
- **after:** no box. "DON'T BE AFRAID." was never displayed.

## What the DS does

`Dialogue::update()` distinguishes the two reasons for being in `Wait`.
`emitOne()` sets `more_` when it stopped at an `SC_PAGE` and clears it at an
`SC_END`; a press in `Wait` with `more_` set clears the page and returns to
`Reveal` instead of closing. That is one flag and one branch, and it is what
`text.inc` always said should happen.

Everything else about the interpreter is carried across unchanged, including the
parts that look like bugs and are not: the mid-word wrap at column 28, the line
clamp that overwrites the bottom row rather than scrolling, `txtHold`,
`TextClose`'s masking of A and B out of `padPressed`, and the Up-beats-confirm
dispatch order in the prompt.

## The SNES build is deliberately NOT fixed

It is frozen at `b8f2b68` as the behavioural oracle and its ROM must stay
byte-identical — that property is the only thing that makes a trace diff mean
anything. Fixing it there is a one-line change (`TS_WAIT` needs to know why it is
waiting) but it would invalidate `docs/oracle-baseline.sha256` and every
fingerprint in it, so it is a decision for the project owner and not a side
effect of porting.

**If it is ever fixed on the SNES**, this file becomes obsolete and the two
platforms agree again — which is the outcome to prefer, because 3213 characters
of writing exist and nobody has read them.

## Trace consequence

`txtState`, `txtPtr` and `txtRow` diverge from the SNES trace from the first
`SC_PAGE` in any conversation onwards, and so does everything downstream of it:
scenes gate their stage transitions on `TextBusy`, so a DS message that runs
three pages holds its scene for roughly three times as many frames as the SNES's
one page did.

**That makes this the one divergence in this directory that a trace diff cannot
simply be told to ignore.** A dialogue-bearing fixture will desynchronise
wholesale rather than differ in a field. `tools/trace_diff.py` needs one of:

1. **Fixtures that avoid dialogue.** Sufficient for movement, collision and boss
   timing, which is most of what the oracle is for.
2. **A comparison that resynchronises on `TextClose`** — compare the frames
   between conversations and skip the conversations themselves.
3. **A `--snes-page-bug` build flag** on the DS side that reproduces the closing
   behaviour, used only for oracle runs.

(3) is the most faithful and the most dangerous, because a flag that changes
behaviour is a flag someone eventually ships. Prefer (1), and reach for (2) only
when a scene's stage machine genuinely needs a dialogue-bearing trace to be
validated at all.
