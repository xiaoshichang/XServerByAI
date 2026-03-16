---
name: test-feature
description: test the feature according to the feature design note in `docs/` named after the item (for example `docs/M1-01.md`). Use when asked to test a feature item in XServerByAI, including validating feature-specific behavior, checking the related development commit, fully verifying native changes with `cmake -S . -B build -DXS_BUILD_TESTS=ON`, `cmake --build build --config Debug`, and `ctest --test-dir build -C Debug --output-on-failure`, and compiling managed projects when the tested change touches the .NET solution or `src/managed/`.
---

# Test Feature

## Overview
Execute one feature test flow in a repeatable way for XServerByAI.

## Workflow
1. Check dependencies in `docs/DEVELOPMENT_PLAN.md`. If any dependency is incomplete, report and stop. If the target item is already done, report and stop.
2. Check that the feature under test has a development branch. If missing, report and stop.
3. Run `scripts/build-native.ps1` from this skill before concluding the test. Treat it as the default native verification flow for every feature test, and never replace it with target-only builds or filtered `ctest` runs when the change touches native code.
4. Inspect the feature development commit(s) under test. If the diff touches `XServerByAI.Managed.sln`, files under `src/managed/`, or other .NET solution/project files, run `scripts/build-managed.ps1` from this skill before concluding the test.
5. Test the feature according to the feature design note in `docs/`.
6. Collect all problems, summarize them, and commit if required. If you are making a commit, include the `tester` role tag in the message.
7. If the feature passes the test, commit with a message including `[Feature Passed]`. Also update the item state in `docs/DEVELOPMENT_PLAN.md` from the in-progress value to the completed value.

## Bundled Scripts
- `scripts/build-native.ps1`
  Run this script for every feature test. It executes the complete native verification flow from the repo root: `cmake -S . -B build -DXS_BUILD_TESTS=ON`, `cmake --build build --config Debug`, and `ctest --test-dir build -C Debug --output-on-failure`.
- `scripts/build-managed.ps1`
  Run this script when the feature development commit modifies the managed solution or projects. It sets `DOTNET_CLI_HOME`, `NUGET_PACKAGES`, and `DOTNET_SKIP_FIRST_TIME_EXPERIENCE` to repo-local paths, then runs `dotnet build .\XServerByAI.Managed.sln -m:1 -c Debug`.

## Examples
- `test-feature M1-01`
- `test-feature M1-13`

## Import Rule
- You are `tester`.
- Update all related documents if needed.
- Use `dotnet` commands to manage .NET projects. See `docs/DOTNET_CLI.md`.
