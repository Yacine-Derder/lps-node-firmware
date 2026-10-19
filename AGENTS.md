# Working in this repository

This fork of Bitcraze LPS node firmware develops Mobile UWB Localization (MUL).
Use the existing checkout; do not clone a second working repository by default.
Project roadmap and current milestone: [PLAN.md](PLAN.md).
Durable design and current protocol: [docs/mul_architecture.md](docs/mul_architecture.md).

## Source of truth

- The current task defines the authorized scope and hardware checkpoint.
- AGENTS.md defines how Codex works.
- PLAN.md defines what remains to be done and current milestone status.
- docs/mul_architecture.md explains why the system is designed this way.
- Source code defines what is actually implemented; inspect it before editing.
- Git history records what changed previously; do not duplicate it in documents.
- If documentation disagrees with code, inspect and report the discrepancy;
  do not silently invent an explanation.

## Preserve working behavior

- Keep original mode IDs and behavior: 0 TWR Anchor, 1 TWR Tag, 2 Sniffer,
  3 TDoA Anchor V2, and 4 TDoA Anchor V3.
- Keep custom IDs: 5 MUL ALR and 6 MUL RLR.
- Preserve EEPROM layout, configured addresses, mode persistence, and USB controls.
- Preserve existing firmware behavior unless the task explicitly authorizes a change.
- Do not reset configurations, alter original packet formats, or change radio
  parameters as an incidental part of another task.
- Follow existing C conventions, source/header layout, and build/test infrastructure.
- Prefer tested Bitcraze driver and protocol mechanisms to new implementations.
- Inspect existing TDoA2/TDoA3, clock-correction, and TWR code before duplicating it.
- Keep shared MUL protocol utilities independent of DW1000-specific operations.
- Do not add a general radio abstraction or dependencies without a concrete need.

## Timing discipline

- Each radio has an independent hardware clock; preserve clock-domain identity.
- Never directly compare timestamps from different nodes without an explicit model.
- Use hardware timestamps and delayed-TX primitives for time-critical radio work.
- Host operating-system timing is not a UWB timestamp or precise scheduling source.
- Treat DW1000 counters as 40-bit modular values, including wraparound.
- Keep raw delayed-TX targets distinct from antenna-referenced TX timestamps.
- Respect the half-period ambiguity of signed modular timestamp differences.
- Do not add unexplained offsets to make measurements agree.

## Implementation and validation

- Make incremental changes with appropriate, meaningful regression tests.
- Build firmware changes with `make`; use `make clean` before `make` when needed
  to exclude stale objects, especially after header changes.
- Run the existing Unity/CMock suite using `./tools/do test` (Docker wrapper),
  or `rake unit summary` in a configured native test environment.
- Do not change dependencies just to accommodate a local tool-path problem.
- Documentation-only edits need content, link, scope, and whitespace checks;
  they do not require rebuilding firmware.
- Software tests do not establish physical radio accuracy or hardware validation.
- Attribute user-reported hardware validation; do not claim to have performed it.
- Never flash physical hardware unless the current task explicitly authorizes it.
- Stop at hardware-validation checkpoints specified by the current task; report
  the implemented state and exact next measurements instead of advancing stages.

## Git and reporting

- Inspect repository root, branch, remotes, and working-tree status before edits.
- Preserve unrelated changes and stage only files relevant to the current task.
- Commit and push only when explicitly authorized by the current task.
- Push authorized work to the user's origin fork, never Bitcraze upstream.
- Never force-push unless explicitly authorized.
- Report changes, tests, unresolved limitations, and commit hash if one was created.

## Persistent documentation

- PLAN.md records current milestones and status, not a history of every action.
- docs/mul_architecture.md records durable decisions and important assumptions,
  not implementation diaries or speculative specifications.
- Update these documents only when their actual content changes.
- Keep AGENTS.md stable during routine implementation tasks.
- Do not create daily logs, test-result archives, completed-plan archives,
  session summaries, or additional memory files unless explicitly requested.
- Keep this three-file structure concise and nonredundant.
