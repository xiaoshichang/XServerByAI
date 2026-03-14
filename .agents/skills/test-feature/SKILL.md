---
name: test-feature
description: test the feature according to feature design note in `docs/` named after the item (e.g., `docs/M1-01.md`). use when ask to test the feature.
---

# Test Feature

## Overview
Execute one test process in a repeatable way for XServerByAI.


## Workflow
1. Check dependencies in `docs/DEVELOPMENT_PLAN.md`. If any dependency is incomplete, report and stop. If the target item is already done, report and stop.
2. A developing feature should has a dev branch. If missing, report and stop.
3. Test the feature according to feature design note in `docs/`.
4. Collect all problems, summary them and commit. If you are making a commit, including the `tester` role tag in the message.
5. If feature pass the test, commit and with a message including string `[Feature Passed]`. Also, change the feature state from "开发中" to "已完成".

## Examples
- `test-feature M1-01`
- `test-feature M1-13`

## import rule
- you are `tester`.
- all relatived documents must be updated if needed.
- use dotnet command to manage dotnet project, see `docs/DOTNET_CLI.md`.