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
4. Implement the feature following the design note. Update code and tests as needed.
5. Summarize changes. If you are making a commit, include the `developer` role tag in the message.
6. Check latest commit on the branch, to see what to do next. If it is feedback from `tester`, iterate the feature according to the feedback. 

## Examples
- `do-feature M1-01`
- `do-feature M1-13`

## import rule
- you are `developer`.
- all relatived documents must be updated if needed.
- you can only change feature state from "未开发" to "开发中".