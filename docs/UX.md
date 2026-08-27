# UX conventions

## Destructive-action confirmation

Two control surfaces — the 172×320 touch panel (`main/ui/ui_touch.cpp`) and the
web dashboard (`main/net/index_html.h`) — and one rule for which destructive
actions get which confirmation gate. Before this was written down, four
different patterns had accumulated (two-tap arm, `confirm()`, fire-on-first-tap,
and an undiscoverable long-press); this page exists so the next destructive
button doesn't get a fifth.

The rule is deliberately not "confirm everything": a confirm on every button
makes a small touch panel materially worse to operate, and most cheap actions
are cheap precisely because their state is re-derivable.

### The rule

| Class | What it costs | Examples | Panel gate | Web gate |
|---|---|---|---|---|
| 1 | Irreversible loss of a physical measurement | Reset Cal | two-tap arm | `confirm()` |
| 2 | Loss of access; recovery costs a trip to the machine | Reset Pwd, Reset WiFi | two-tap arm | `confirm()` |
| 3 | Cheap, re-derivable state | Reset Count, Clear Log | none — but the control must be a visible, labelled button | none |
| 4 | Moves or reboots the press | OTA upload | (not offered on the panel) | `confirm()`, and the text must say a running press is stopped |

### The gates

- **Two-tap arm** (panel): `ConfirmArm` in `main/ui/ui_touch.cpp`. First tap
  turns the button amber and relabels it "Sure? Tap"; a second tap within
  `UI_CONFIRM_ARM_MS` (config.h) commits; the timeout reverts it. Every
  navigation (`go()`) disarms every armed button, so no screen is ever entered
  pre-armed — including jam takeovers, which seize the display from anywhere.
- **`confirm()`** (web): native dialog, free, keyboard-accessible. The text
  states the consequence and the recovery path, not just "are you sure".

### Class 3 forbids hidden gestures

No confirmation does **not** mean no affordance. The counter used to reset on
a long-press of the main-screen counter label: no visible affordance, no
confirmation, undiscoverable — and a duplicate of the visible Settings →
Reset Count button. It was removed rather than confirmed. If an action is
cheap enough to skip confirmation, it still has to look like a control.

## Refused actions

A command the press cannot carry out — no calibration, an unreferenced axis, no
batch target, wrong state — must say so on the surface the tap came from. Both
UIs answer with the same sentence, `autolee::refusalMessage()` in
[`lib/autolee_logic/command_gate.h`](../lib/autolee_logic/command_gate.h).

| Surface | How the reason arrives |
|---|---|
| Web | `409` + reason slug on the request, plus the log line |
| Panel | Refusal banner: the reason over the active screen for `UI_REFUSAL_BANNER_MS` |

### A refusable control stays enabled

Except where the state is transient and self-describing, a control the gate
would refuse is left tappable and normally coloured. The tap *is* received, and
the banner answers it. Greying it out instead trades a green button that does
nothing for a grey button that does nothing — neither says why, and the grey one
also hides that the panel is alive.

RUN on an uncalibrated press was the case that proved it (#52): the gate was
working exactly as designed and the operator's question was still "is this
button broken?".

The one exception is `STOPPING`, where RUN *is* disabled: the state clears
itself within a stroke and the button already reads "STOPPING", so there is
nothing for the operator to act on.
