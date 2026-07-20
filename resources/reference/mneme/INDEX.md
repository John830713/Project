# mneme — Rules & Notes

## Capacity

- Target under 500 characters per memory
- Hard limit at 10,000 characters — rejected beyond that
- Embedding model determines search quality

## What NOT to store

- Source code (read from disk each session)
- Tool outputs (captured automatically)
- Whole conversations
- Current project state or transient task progress

## Scope isolation

- `switch_scope("<project>")` separates memories per project
- Default scope is `global` — always switch before writing
- Writes without scope filter land in the active scope

## Data directory

- `~/.mneme/` — not synced across machines
- Each machine maintains its own mneme data
- `forget` is permanent — no undo

## Session flow

- `mneme://procedural` — pinned rules load every session start
- `mneme://context` — recent events auto-loaded
- `record_event` for time-anchored events (milestones, messages)
- `summarize_session` + LLM completion for session digests

## Referenced by

- [reference/INDEX.md](../INDEX.md)
