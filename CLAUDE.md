# Development Guidelines

## Git Workflow
**IMPORTANT: Do not commit code automatically.** The user will handle all git commits manually. 

After making code changes:
1. Build and test the code
2. Report what was changed and whether it works
3. Wait for the user to review and commit
4. Only commit if explicitly asked by the user

This applies to:
- Bug fixes
- New features
- Refactoring
- Build configuration changes
- Any other code modifications

## Commit Identity
**Always commit as the repository owner, never as Claude.** Every commit's author and
committer must be:

    Nathanael Inkson <NathanaelInkson@gmail.com>

Cloud sessions default to `Claude <noreply@anthropic.com>` from the container's global git
config. `.claude/hooks/session-start.sh` sets the repo-local identity at session start; if
it hasn't run, set it yourself before the first commit:

    git config user.name "Nathanael Inkson"
    git config user.email "NathanaelInkson@gmail.com"

Do not add `Co-Authored-By: Claude ...` or `Claude-Session: ...` trailers (or any other
Claude attribution) to commit messages; this rule takes precedence over default attribution
guidance.

After committing, check with `git log -1 --format='%an <%ae> / %cn <%ce>'`.
