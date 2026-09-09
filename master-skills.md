# JUCE VST3 Plugin Rules

---

**Default target configuration**
Unless the Designer explicitly asks for more, target VST3 only, macOS only, no Standalone build. `juce_add_plugin(... FORMATS VST3 ...)` — do not add `AU`, `Standalone`, or a Windows/Linux target speculatively.

**A failed response looks like:**
- Adding `Standalone`, `AU`, or other formats "for completeness" when only VST3/macOS was asked for
- Building cross-platform CMake logic for a project that only targets macOS

---

**JUCE acquisition and build**
Bring JUCE in via CMake `FetchContent`, pinned to a specific release tag — never a moving branch. Use `juce_add_plugin(... COPY_PLUGIN_AFTER_BUILD TRUE)` so a plain `cmake --build` deploys the compiled `.vst3` straight to `~/Library/Audio/Plug-Ins/VST3/`, with no manual copy step.

**A failed response looks like:**
- Pinning `FetchContent` to `main`/`develop` instead of a release tag
- Leaving out `COPY_PLUGIN_AFTER_BUILD`, forcing a manual copy step after every build
- Suggesting Projucer or a manually-downloaded JUCE install when the Designer has no existing JUCE setup

---

**Verify exact JUCE API against the real FetchContent source, not memory**
Since JUCE is pinned via CMake `FetchContent`, the actual JUCE source is available on disk at `<build-dir>/_deps/juce-src`. When unsure of an exact method name, signature, or class (e.g. a LookAndFeel override or a Font method), grep that real source directly instead of guessing from memory — it is authoritative for the exact pinned version in use.

**A failed response looks like:**
- Guessing a JUCE method name/signature from memory and writing code against it without checking the actual fetched source when uncertain
- Assuming a newer/older JUCE API surface than the version actually pinned by FetchContent

---

**Install location: vendor subfolder, not the bare VST3 root**
Plugins install to a `Slow Pulse Studio` subfolder inside the system VST3 folder, not directly into `~/Library/Audio/Plug-Ins/VST3/`. Set `VST3_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Slow Pulse Studio"` on `juce_add_plugin(...)` alongside `COPY_PLUGIN_AFTER_BUILD TRUE`. This keeps every plugin from this studio grouped together in the DAW's plugin browser instead of mixed in with every other vendor's plugins.

**A failed response looks like:**
- Letting COPY_PLUGIN_AFTER_BUILD install straight to the bare VST3/ root without a vendor subfolder
- Using a different or inconsistent subfolder name across projects instead of Slow Pulse Studio

---

**COMPANY_NAME controls DAW vendor grouping, separate from install path**
`COMPANY_NAME` in `juce_add_plugin()` is the metadata a DAW's own plugin browser uses to group plugins by vendor — this is completely independent of the filesystem install subfolder (`VST3_COPY_DIR`). Setting one without the other will not fix vendor-grouping issues in the DAW.

**A failed response looks like:**
- Assuming the install-path vendor subfolder also controls how the DAW browser groups the plugin — it doesn't, `COMPANY_NAME` does
- Changing `VST3_COPY_DIR` to fix a DAW-browser vendor-grouping complaint instead of `COMPANY_NAME`

---

**A full DAW restart may be required to load a rebuilt plugin**
Overwriting an installed `.vst3` in place and removing/re-adding the plugin instance in the DAW is not always enough to load the new binary — some hosts keep the old plugin module resident in process memory across instance add/remove. If a rebuilt plugin doesn't reflect recent changes after being re-added, fully quit and relaunch the DAW before assuming the build or install step failed.

**A failed response looks like:**
- Concluding the build/install pipeline is broken because a DAW still shows old behavior after only removing/re-adding the plugin instance
- Not mentioning a full DAW restart as a troubleshooting step when a rebuilt plugin appears stale in a host

---

**Real-time audio safety in `processBlock`**
`processBlock` runs on the audio thread and must never allocate, lock, log, or do file/network I/O — any of these can cause audible dropouts/glitches in the DAW. Parameter changes must be smoothed (`juce::SmoothedValue`), never applied as a hard jump, to avoid zipper noise/clicks.

**A failed response looks like:**
- Allocating a `std::vector`, `juce::String`, or any heap object inside `processBlock`
- Calling `DBG`/`std::cout`/file I/O from `processBlock`
- Reading a raw parameter value directly into a filter coefficient every block instead of smoothing it

---

**Parameters**
Expose all user-facing parameters through a single `AudioProcessorValueTreeState`, defined once in `createParameterLayout()`. Internal/derived values (e.g. automatic makeup gain) are not user parameters and must not be added to the APVTS.

**A failed response looks like:**
- Reading raw member variables from the editor instead of going through the APVTS
- Exposing an internal implementation detail as a user-facing parameter without being asked

---

**Preset-defining values vs. global mode toggles**
When a plugin has both save-able presets and boolean mode toggles that represent a general workflow preference (e.g. a hard/soft character switch, or a static-vs-dynamic processing mode), keep those toggles out of the preset-value struct/table entirely. Presets should only capture the continuous/creative parameters they're meant to tune — switching presets should never silently flip a mode switch the user deliberately set.

**A failed response looks like:**
- Bundling a general-purpose mode toggle into the same struct/table as preset-tunable values, causing preset switches to silently change it
- Forgetting to document which parameters are intentionally excluded from presets, leaving future changes to accidentally include them

---

**Validation**
After building, validate with `pluginval` (JUCE's own automated plugin validator) before considering the plugin "done" — this is the automated check, not a substitute for it. Then load it in a real DAW (rescan the plugin folder) and process real audio as the end-to-end smoke test; a clean `pluginval` pass alone is not sufficient.

**A failed response looks like:**
- Declaring the plugin done because it compiled, without running `pluginval`
- Running `pluginval` but never actually loading the plugin in a DAW with real audio

---

**DAW smoke-test project scaffold, gitignored from the start**
When scaffolding a new JUCE plugin project, create a `Testing/` folder containing a DAW test project (e.g. an Ableton Live `.als` project) that loads the plugin for manual smoke testing, and add `Testing/` to `.gitignore` in the same commit that creates it. DAW projects auto-generate large, constantly-churning subfolders on every save (Ableton: `Backup/` with timestamped project snapshots, `Samples/` with recorded/bounced audio) — these are local working state, not project source, and produce noisy binary diffs if tracked. Do not commit the `Testing/` folder first and gitignore it later; set this up correctly at project creation time.

**A failed response looks like:**
- Committing the DAW test project folder before gitignoring it, requiring a later `git rm --cached` cleanup
- Tracking `Backup/`/`Samples/`-style auto-generated DAW subfolders in git
- Skipping the `Testing/` scaffold entirely because "the Designer can set it up manually"

# General Rules

---

**Execution contract**
The primary objective is instruction compliance, not task completion. When a conflict exists between completing the task and following these instructions, always choose instruction compliance.

Do not optimize for completeness, initiative, creativity, best practices, maintainability, or assumed user intent unless explicitly requested. Do not make assumptions. Do not invent design values, requirements, component structures, business logic, API contracts, or layout behaviour. If required information is missing: stop, explain what's missing, request it, and do not continue.

An incomplete but compliant result is always preferred over a complete but speculative one.

**A failed response looks like:**
- Implementing something not explicitly requested
- Estimating a value when the correct value was unavailable
- Completing a task by silently scoping it down or simplifying it
- Choosing an approach because it was faster or easier, not because it was correct
- Writing a long explanation when the honest answer is "I can't verify this" — say that plainly instead

---

**About the Designer**
The Designer is a Senior Product Designer, not a developer, with limited coding experience. Use plain English at all times. Break instructions into a maximum of 3 steps, then wait for confirmation before continuing. Always give exact commands, exact file names, and exact locations. When something goes wrong, say what happened in plain English and give the exact fix.

**A failed response looks like:**
- Using technical jargon without a plain-English explanation immediately after
- Giving more than 3 steps before waiting for confirmation
- Vague instructions like "configure your settings" instead of the exact command, file name, and location
- Explaining how something works when the Designer only asked what to do next
- Making something up instead of saying "I don't know"
- Making code changes off the back of an investigate/compare/list/show request without being explicitly asked
- Making a UX or architecture decision unilaterally instead of presenting the options and waiting for a choice
- Mentioning Windows shortcuts — always assume Mac
- Saying "open terminal" or "open a new terminal" — the terminal is already open, give the exact command directly
- Suggesting a bypass, workaround, or shortcut instead of diagnosing and fixing the root cause
- Not giving the exact fix when something breaks — never say "something went wrong" without also saying exactly what to do about it
- Using phrases that perform sincerity instead of stating a fact — "my honest take", "the real reason", "to be fair", "frankly", "admittedly", "in all honesty". State the fact directly.

---

**Package manager**
Always use pnpm. Never suggest npm, yarn, npx, or any other package manager.

**A failed response looks like:**
- Suggesting `npm install`, `npm run`, `npx`, or `yarn` for any reason

---

**Secrets**
Secrets are: API keys, tokens, passwords, anon keys, client secrets — anything starting with `sk-`, `eyJ`, `sb_publishable`, or similar.

When a secret needs to be added or changed: tell the Designer exactly what to do, then ask them to close the AI assistant, make the change privately, and reopen it when done.

`.env` must be gitignored. `.env.example` is committed as a template with blank values only — never a real secret. Always verify which file a value was written to before assuming it's safe.

**A failed response looks like:**
- Reading, opening, printing, displaying, or running any command that could expose the contents of a secrets file
- Asking the Designer to paste a secret value into chat
- Embedding a secret in source code
- Committing a real secret value in `.env.example`

---

**Production standards**
Every project ships to real users. There is no "MVP mentality", no "good enough for now", no "we can fix this later". Every decision must be made as if the product ships tomorrow.

"MVP" refers only to the scope of features — never an excuse for technical shortcuts, lazy patterns, or code that will need rewriting.

**A failed response looks like:**
- Cutting corners on security, permissions, or data handling because it "works for now"
- Using a legacy or deprecated API when a modern equivalent exists
- Suggesting a shortcut without considering whether it will cause a refactor later
- Treating architecture, naming, file structure, or patterns as throwaway
- Writing code a seasoned engineer would not ship
- Choosing the simpler version of something when a more correct technical approach exists

---

**No lazy shortcuts**
LLMs optimise for goal success, which can mean failing the actual human goal. These rules correct for that.

**A failed response looks like:**
- Reading only part of a file before editing instead of the full relevant file
- Suggesting a fix without first checking if a similar pattern already exists in the codebase
- Adding placeholder values with intent to fix later
- Giving a partial answer to an investigation — if asked to list something, list everything
- Asking a clarifying question that could be answered by reading the existing code

---

**Mandatory self-verification**
This is the pre-submit check for the Execution Contract above. Before every response, verify:

1. Did I introduce anything not explicitly provided?
2. Did I infer values that were unavailable?
3. Did I simplify a requirement?
4. Did I replace a requested implementation with my preferred one?
5. Did I create abstractions, components, or patterns that were not requested?
6. Did I choose a shortcut instead of executing the requested work?

If the answer to any question is YES: do not proceed. Explain the issue, revert the assumption, and request clarification if necessary.

---

**Drunk mode**
The Designer may activate this by saying "drunk mode" or "I've been drinking". It stays active for the rest of the session unless they say "sober mode" or "back to normal".

When drunk mode is active:
- Before doing anything, restate in one plain sentence what you understood the request to be — wait for confirmation before proceeding
- Assume the instruction is 3× vaguer than it sounds — probe for scope, don't assume
- No commits, pushes, or deploys unless the Designer explicitly says "yes commit" or "yes push" in that exact message
- Do one logical change at a time, show what changed, wait for a thumbs up before the next
- If the request could mean two different things, list both options and ask — don't pick one and run
- Flag any instruction that touches auth, secrets, data storage, or backend functions — these need a sober double-check
- If something the Designer says contradicts a recent decision or the approved plan, point it out before acting on it

# Architecture Rules

---

**Single responsibility per file/module**
Each file or module should do one job. Prefer several small, clearly-named files over one large file handling multiple concerns — this makes it obvious where a change belongs and keeps diffs small and reviewable.

**A failed response looks like:**
- Adding an unrelated concern to an existing file instead of creating a new, appropriately-named one
- One file growing to handle several distinct responsibilities because it was the path of least resistance

---

**Extend, don't duplicate**
When adding a feature, extend the existing implementation rather than writing a parallel version alongside it. Two implementations of the same concern drift apart silently and one of them usually stops being maintained.

**A failed response looks like:**
- Creating a second, slightly-different version of an existing function/class/module instead of modifying the original
- Copy-pasting a block of logic to tweak it, instead of extracting a shared function

---

**Configuration over hardcoding**
Values that are likely to change — tunable parameters, feature flags, thresholds, endpoints — belong in configuration (a config file, environment variable, or CLI flag), not hardcoded inside logic. Business/domain logic itself is not configuration and should stay in code.

**A failed response looks like:**
- Hardcoding a value that the user is likely to want to tune, instead of exposing it via config
- Over-configuring stable, unlikely-to-change logic just to seem flexible

---

**Abstract external dependencies behind an interface**
Any external service (a paid API, a specific vendor SDK, a specific database) should sit behind a narrow interface that the rest of the app depends on — not be called directly from many places. This is what makes a provider swappable later without a rewrite.

**A failed response looks like:**
- Calling a vendor SDK directly from multiple unrelated modules instead of through one interface
- Designing internal data structures that only make sense for one specific provider's API shape

---

**No speculative abstraction**
Build the abstraction that today's requirement needs — not one that anticipates a hypothetical future requirement that hasn't been asked for. Unused flexibility is a maintenance cost, not a benefit.

**A failed response looks like:**
- Adding a plugin system, strategy pattern, or extra configuration layer for a case that doesn't exist yet
- Generalising a function to handle inputs it will never actually receive


# Git Rules

---

**Pushing requires explicit confirmation**
Pushing code is a hard-to-reverse action on a shared repo. Commit locally as normal, but never run `git push` (or `--force`, `git reset --hard`, or amend a published commit) without the human first confirming — even when the change itself is a reasonable, low-risk response to their own request. Reasonable idea does not equal permission to push.

**A failed response looks like:**
- Running `git push` immediately after a commit without asking first
- Force-pushing, hard-resetting, or amending a commit that's already on the remote without explicit confirmation
- Treating "the user asked for this change" as implicit permission to also push it

---

**Commit granularity and messages**
Commit in small, focused increments — one feature or fix per commit, not batched unrelated changes. Write multi-line commit messages: a short imperative summary line, then a bullet list explaining what changed and why (the reasoning/trade-off), not just a restatement of the diff.

**A failed response looks like:**
- Bundling multiple unrelated changes into a single commit
- A commit message that only restates the diff ("update file.py") without explaining why
- A vague summary line like "fixes" or "changes" instead of a specific imperative statement

---

**What never gets committed**
Build output (`dist/`, `build/`), local virtualenvs (`.venv/`), and real secrets never go in a commit. `.env` is gitignored; `.env.example` is a template with blank values only. Verify `.gitignore` covers these before the first commit in a new project.

**A failed response looks like:**
- Committing `dist/`, `build/`, or `.venv/` because `.gitignore` wasn't checked first
- Committing a real secret value, even accidentally, in an example/template file

---

**Before declaring a change committed**
Only commit after the change has been verified locally (build succeeds, tests pass, or a manual smoke test confirms the behaviour) — not on the assumption that the diff looks correct.

**A failed response looks like:**
- Committing a change immediately after editing, without running it or its tests first


# Testing Rules

---

**Real end-to-end verification before "done"**
A passing lint/type-check or a mocked unit test is not sufficient to call a feature done. Run it end-to-end against real or realistic data and confirm the actual output, not just the absence of errors.

**A failed response looks like:**
- Declaring a feature complete because "no errors found" without ever running it
- Relying solely on mocked tests for a feature that touches a real external system or real data

---

**Test components individually, then the full pipeline**
When a change spans multiple stages (e.g. generate → validate → composite), verify each stage in isolation first, then run the full pipeline together. This makes it obvious which stage a failure belongs to, instead of debugging a black-box end-to-end failure.

**A failed response looks like:**
- Only testing the full pipeline and guessing which stage caused a failure
- Skipping isolated component checks because the full run "looked fine"

---

**Automate structural validation**
Wherever a human would otherwise eyeball output for correctness (dimensions, counts, ordering, naming, duplicates, missing files), write an automated check instead. Manual visual inspection should be reserved for genuinely subjective judgement (does this look good?), not structural correctness (is this the right size/order/count?).

**A failed response looks like:**
- Leaving a mechanically-checkable property (file count, dimensions, ordering) to manual inspection
- Adding a validation step that only checks the happy path and never runs against a broken/edge case

---

**Surface failures loudly**
During development, failures (failed requests, failed assertions, unexpected values) should be logged clearly, not swallowed silently. A silent failure inside a loop or background task is far harder to diagnose than a loud one.

**A failed response looks like:**
- Catching an exception and continuing without logging it
- A test or validation step that fails closed (reports success) when it can't actually verify the condition


# DSP Prototyping Rules

---

**Prototype in a throwaway script before porting to real-time code**
When building audio/DSP-heavy functionality (dynamic EQ, envelope followers, filters, saturation, etc.), first validate the algorithm in Python/NumPy against real sample audio, not directly in the final target language/runtime. The prototype is disposable — it exists only to prove the algorithm sounds right before committing to a real-time port.

**A failed response looks like:**
- Writing untested DSP logic directly into the final real-time codebase (C++/JUCE, audio worklet, etc.) without validating the algorithm offline first
- Treating the throwaway prototype script as part of the shipped product

---

**Parameter sweeps over guessing values**
Render one labeled output file per candidate value of each tunable parameter (holding others at a neutral baseline), rather than guessing a single value and asking the Designer to imagine alternatives. Sweep one parameter at a time first to isolate its effect; combine only after individual ranges are known.

**A failed response looks like:**
- Picking one arbitrary value per parameter and asking "does this sound right?" instead of rendering a range to compare
- Sweeping multiple parameters at once before any single-parameter baseline has been established

---

**Visual + measured validation, not just listening**
Alongside audio renders, generate before/after spectrograms and a zoomed waveform view around a representative transient/event, plus basic level metrics (RMS/peak before vs after). Use these to confirm the change actually did what was intended (e.g. reduced energy in a specific frequency band), not just that it "sounds different."

**A failed response looks like:**
- Relying on listening alone with no visual/measured evidence of what changed
- Declaring a perceptual goal (e.g. "reduced harshness") met without a spectrogram or level comparison showing it

---

**Round-based tuning**
Treat tuning as iterative rounds: Round 1 sweeps the full plausible range per parameter (including extremes) to find sane bounds; Round 2 refines within the range the Designer responded well to. Lock in final values only after the Designer has given explicit feedback on renders, not by guessing "reasonable" defaults upfront.

**A failed response looks like:**
- Inventing final parameter defaults without an actual round of Designer feedback on real renders
- Skipping straight to porting the algorithm into the real-time codebase before any round of tuning feedback

---

**Bias tonal judgment calls toward dark, warm, thick-bodied genres**
This studio's DSP work is for UK Bass / Future Garage and related dark, warm, bass-heavy genres. Whenever a tuning decision involves a subjective tonal judgment call (choosing a default value within an already-approved range, picking between two options that both technically satisfy the brief, resolving an ambiguous "does this sound right?"), bias toward a dark, warm, thick-bodied result. Treat shrill, harsh, or thin outcomes as a failure condition to correct, not a neutral stylistic variant — even if no explicit genre reference was given for that specific task. This bias applies to judgment calls only; it never overrides an explicit Designer instruction or an already-established parameter value from real render feedback.

**A failed response looks like:**
- Defaulting to a bright/thin/shrill setting because it was technically simplest or most "neutral," when a tonal judgment call was actually needed
- Treating a shrill or harsh result as acceptable because the Designer didn't explicitly rule it out for that specific parameter
- Applying this bias to override an explicit instruction or a value the Designer already confirmed from a real render

---

**Ad-hoc test renders go in a subfolder, never the Output/ root**
One-off A/B renders (e.g. comparing two DSP approaches, testing a bug fix) must be written to a dedicated subfolder under Output/ (e.g. `Output/<feature-name>/`), matching the existing convention already used for sweep and preset output. Never write loose WAV/PNG files directly into Output/'s root.

**A failed response looks like:**
- Writing a quick comparison render straight to Output/some_test.wav instead of Output/some_test/some_test.wav
- Leaving the Designer to manually clean up/organize stray files the agent wrote to the Output/ root

---

**Random combined-parameter batches for interaction effects**
After one-parameter-at-a-time sweeps establish each parameter's usable low/high range, render a second batch of combinations by randomly sampling several parameters at once within their discovered ranges (not exhaustively grid-searching every combination). This surfaces interaction effects — e.g. two parameters that sound fine individually but clash or reinforce unexpectedly together — that isolated sweeps cannot reveal. Use both methods together: one-at-a-time sweeps to find sane bounds, random combined batches to confirm those bounds still hold once parameters interact.

**A failed response looks like:**
- Only ever testing parameters in isolation and never validating combined settings before locking in defaults
- Exhaustively grid-searching every combination of every parameter instead of random sampling within already-discovered ranges
- Randomly sampling parameter combinations before any individual-parameter bounds have been established
