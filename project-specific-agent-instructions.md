# Project-specific agent instructions

<!-- Add project-specific notes here — design decisions, constraints, what's
     being tested, known issues, personas, edge cases, anything the AI should
     know about this particular project that isn't covered by master-skills.md.
     This file is never overwritten by /skill-me-up. -->

## Preset/Randomise scope

Presets and the Randomise button never touch Mix, or any control that ends up
in an Input or Output section of the GUI (whenever those sections exist) —
only the creative/tunable parameters are captured/applied/randomised. This is
in addition to the existing Mode/Pitch Mode/Singularity exclusion (global mode
toggles) already documented in master-skills.md. See `Source/PresetManager.h`.
