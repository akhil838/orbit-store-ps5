# Notes for agents

## Commits

- Never add `Co-Authored-By:` lines, `Claude-Session:` lines or any other co-author or attribution trailers to commit messages.
- Write a short subject line in plain English that says what changed. Add a body only when the reason isn't obvious.
- Don't commit, push or merge until the owner asks.

## Test builds for issue reporters

- Publish user-facing test builds in `saawant12/orbit-issues-test-builds`: https://github.com/saawant12/orbit-issues-test-builds.
- Use a clearly labelled test release there with packages for the relevant operating systems, checksums, and any required matching receiver and source/licence assets.
- Do not put tester packages in the main public project's Releases or use unrelated file-sharing services.
- Prepare issue reply drafts for the owner to review; post only when authorized.
