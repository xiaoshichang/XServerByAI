---
name: test-feature
description: test the feature according to the feature design note in `docs/` named after the item (for example `docs/M1-01.md`). Use when asked to test a feature item in XServerByAI, including validating feature-specific behavior, checking the related development commit, and compiling managed projects when the tested change touches the .NET solution or `src/managed/`.
---

# Test Feature

## Overview
Execute one feature test flow in a repeatable way for XServerByAI.

## Workflow
1. Check dependencies in `docs/DEVELOPMENT_PLAN.md`. If any dependency is incomplete, report and stop. If the target item is already done, report and stop.
2. Check that the feature under test has a development branch. If missing, report and stop.
3. Inspect the feature development commit(s) under test. If the diff touches `XServerByAI.Managed.sln`, files under `src/managed/`, or other .NET solution/project files, run `scripts/build-managed.ps1` from this skill before concluding the test.
4. Test the feature according to the feature design note in `docs/`.
5. Collect all problems, summarize them, and commit if required. If you are making a commit, include the `tester` role tag in the message.
6. If the feature passes the test, commit with a message including `[Feature Passed]`. Also update the item state in `docs/DEVELOPMENT_PLAN.md` from the in-progress value to the completed value.

## Bundled Scripts
- `scripts/build-managed.ps1`
  Run this script when the feature development commit modifies the managed solution or projects. It sets `DOTNET_CLI_HOME`, `NUGET_PACKAGES`, and `DOTNET_SKIP_FIRST_TIME_EXPERIENCE` to repo-local paths, then runs `dotnet build .\XServerByAI.Managed.sln -m:1 -c Debug`.

## Examples
- `test-feature M1-01`
- `test-feature M1-13`

## Import Rule
- You are `tester`.
- Update all related documents if needed.
- Use `dotnet` commands to manage .NET projects. See `docs/DOTNET_CLI.md`.
