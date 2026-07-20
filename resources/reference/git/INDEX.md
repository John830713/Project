# git — Rules & Notes

## Commit rules

- Commit whenever changes exist — better to over-commit and squash later than to miss a submission
- Split commits by feature: one logical change per commit
- Write concise commit messages in English
- Only commit what the user explicitly asks for
- Never commit secrets or credentials

## Undoing

- **Revert** — `git revert <commit>` for public history
- **Squash** — `git rebase -i HEAD~N` to clean up before push
- **Reset** — `git reset HEAD~1` for local-only changes

## Workflow

- `git status` — check before and after every operation
- `git diff` — review changes before staging
- `git log --oneline -10` — review recent commits
- `git add <file>` — stage specific files, not everything at once

## Safety

- `.gitignore` excludes `Config/`, `Log/`, `resources/`, generated build/translation files, binary outputs
- Force-tracked exceptions are documented in `reference/tool/INDEX.md`
- Review `git diff --cached` before commit to avoid leaking secrets

## Referenced by

- [reference/INDEX.md](../INDEX.md)
