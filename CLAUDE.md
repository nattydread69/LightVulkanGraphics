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

## Header Layout
**Headers always go in `include/`, never in `src/`.** `src/` holds only `.cpp` files.

- Public API headers go in `include/` (or `include/lightVulkanGraphics/`); they are installed.
- Private implementation headers go in `include/internal/` (GUI internals in
  `include/internal/ui/`). That directory is on the library's PRIVATE include path and is
  excluded from `install()`, so its headers never ship and are included by bare name
  (`#include "VkAppExtras.h"`).
- Vendored third-party headers stay in `external/`.

## CI Must Pass After Every Implementation
**Every new implementation (feature, bug fix, refactor, build change) must leave the CI test
suite passing.** Do not report work as done until this has been checked.

After making code changes, and before reporting back:
1. Build everything and run the **full** suite, not just the tests you touched:

       cmake --build build -j8 && ctest --test-dir build --output-on-failure

2. Add or update tests for the new behaviour so CI actually exercises it.
3. Mirror what CI does where it differs from the default `build/` configuration. CI
   (`.github/workflows/ci.yml`) configures Linux jobs with `-DLVG_USE_SYSTEM_ASSIMP=ON
   -DLVG_WARNINGS_AS_ERRORS=ON` (g++ and clang++), plus a clang++ sanitizer job
   (`-DLVG_ENABLE_SANITIZERS=ON`). The default `build/` uses the bundled Assimp, so a
   change can pass locally and fail in CI (or the reverse). For changes touching model
   loading, warnings-sensitive code, or memory handling, also configure and test a
   separate build directory (in the scratchpad, not the repo) with those flags.
4. If any test fails, fix it or report it with the failing output. Never skip, disable or
   loosen a test just to get green, and never describe a red suite as passing.
5. Say in the report which tests/configurations were run and which were not.

Once the user has pushed, check the result with `gh run list` / `gh run view <id>`
(per-job logs: `gh api repos/{owner}/{repo}/actions/jobs/<job-id>/logs`). Pushing and
committing remain the user's job (see Git Workflow above).
