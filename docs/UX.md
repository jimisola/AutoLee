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
