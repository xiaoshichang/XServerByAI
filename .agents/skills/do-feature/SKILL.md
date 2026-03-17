---
name: do-feature
description: Implement a single feature item from `docs/DEVELOPMENT_PLAN.md` in the XServerByAI project. Use when asked to plan, implement, or validate a specific milestone item, including code changes, tests, and related docs.
---

# Do Feature

## Overview
Execute one development-plan feature item in a repeatable way for XServerByAI.



## Workflow
1. Check dependencies in `docs/DEVELOPMENT_PLAN.md`. If any dependency is incomplete, report and stop. If the target item is already done, report and stop.
2. Prepare a new branch named exactly the feature id (e.g., `M1-01`). If missing, create it. also change the state of this feature to developping in `docs/DEVELOPMENT_PLAN.md`.
3. Write a feature design note in `docs/` named after the item (e.g., `docs/M1-01.md`). Break it into 3-10 subpoints based on complexity.
4. Implement the feature following the design note and `docs/CONVENTIONS.md`. For project-owned C++ source files under `src/` and `tests/`, use Allman brace style so every opening brace `{` starts on a new line, and keep pointers and references attached to the type as `T* value` / `T& value`. Do not restyle vendored code under `3rd/` just to satisfy this rule.
5. Summarize changes. If you are making a commit, include the `developer` role tag in the message.
6. Check latest commit on the branch, to see what to do next. If it is feedback from `tester`, iterate the feature according to the feedback. 

## Examples
- `do-feature M1-01`
- `do-feature M1-13`

## import rule
- you are `developer`.
- all relatived documents must be updated if needed.
- you can only change feature state from "未开发" to "开发中".
- use dotnet command to manage dotnet project, see `docs/DOTNET_CLI.md`.
- follow `docs/CONVENTIONS.md`; for project-owned C++ code in `src/` and `tests/`, every opening brace `{` must be on a new line and pointers/references must be written as `T* value` / `T& value`; this rule must not trigger restyling in `3rd/`.