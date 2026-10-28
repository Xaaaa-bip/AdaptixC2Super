# Extension-Kit Plugin Readiness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Mimikatz and the related Extension-Kit execution/credential plugins fail early with clear diagnostics and provide a repeatable validation path for build artifacts and AxScript references.

**Architecture:** Keep the existing AxScript command and BOF interfaces. Add deterministic static validation for script imports, BOF pack arity, and Mimikatz/No-Consolation artifact layout; improve Mimikatz and fetch-script diagnostics without adding new credential collection primitives.

**Tech Stack:** Bash, Node.js ESM, GNU Make, Go AxScript engine.

## Global Constraints

- Do not add new credential extraction, lateral movement, persistence, or evasion primitives.
- Preserve existing command names and BOF parameter layouts.
- Missing optional Mimikatz PE files must be reported as actionable warnings by validation, while broken script references and malformed pack layouts fail.
- Validate changes with static checks, shell syntax checks, relevant Go tests, and the Extension-Kit build when the local toolchain permits.

---

### Task 1: Add Extension-Kit static validation

**Files:**
- Create: `scripts/verify_extension_kit.mjs`
- Test: run the script against `Extension-Kit`

**Interfaces:**
- Consumes: Extension-Kit `.axs`, `.sh`, `Makefile`, and existing `_bin` artifacts.
- Produces: exit code 0 for structurally valid scripts; nonzero for broken imports, malformed `ax.bof_pack` arity, or invalid required build metadata.

- [x] **Step 1:** Scan every `.axs` file, resolve literal `ax.script_import("...")` paths, and fail when an imported file is missing.
- [x] **Step 2:** Count literal `ax.bof_pack("types", [args])` entries and fail when the type count differs from the argument count.
- [x] **Step 3:** Check Mimikatz's required runtime layout and report missing PE/No-Consolation files as warnings with exact fetch/build commands.
- [x] **Step 4:** Run the validator and record the baseline findings.

### Task 2: Harden Mimikatz runtime diagnostics

**Files:**
- Modify: `Extension-Kit/Creds-BOF/mimikatz/mimikatz.axs`
- Modify: `Extension-Kit/Creds-BOF/mimikatz/README.md`
- Test: `scripts/verify_extension_kit.mjs`

**Interfaces:**
- Consumes: `ax.arch`, `ax.is64`, `ax.file_exists`, and the existing No-Consolation BOF.
- Produces: early, actionable errors before an alias is queued when the agent architecture or required local artifact is unsupported/missing.

- [x] **Step 1:** Normalize supported architectures and reject non-64-bit agents with a clear message matching the current No-Consolation limitation.
- [x] **Step 2:** Check the selected No-Consolation object before packing arguments and include both build and reload instructions in the error.
- [x] **Step 3:** Validate timeout bounds and preserve the current default command behavior.
- [x] **Step 4:** Add a short operator troubleshooting table to the README.

### Task 3: Make the Mimikatz fetch helper deterministic

**Files:**
- Modify: `Extension-Kit/Creds-BOF/mimikatz/fetch_mimikatz.sh`
- Modify: `Extension-Kit/Creds-BOF/mimikatz/README.md`
- Test: `bash -n Extension-Kit/Creds-BOF/mimikatz/fetch_mimikatz.sh`

**Interfaces:**
- Consumes: GitHub release ZIP and optional `MIMIKATZ_VERSION`.
- Produces: exactly named x64/x86 PE files, clear failure when no supported executable is found, and no changes outside the plugin `_bin` directory.

- [x] **Step 1:** Add an explicit supported-version/URL override and validate required tools before downloading.
- [x] **Step 2:** Stage extraction in a temporary directory and copy only recognized x64/x86 executables.
- [x] **Step 3:** Emit a final artifact summary and a reload reminder.
- [x] **Step 4:** Run shell syntax validation and the static validator.

### Task 4: Verify the complete plugin surface

**Files:**
- Modify: none unless a check exposes a regression.
- Test: Extension-Kit build; `go test ./core/axscript ./extenders/agent_beacon/... ./extenders/agent_gopher/... ./extenders/agent_kharon/...` where supported.

**Interfaces:**
- Consumes: the files produced by Tasks 1-3.
- Produces: recorded build/test results and a clean validation report.

- [x] **Step 1:** Run `node scripts/verify_extension_kit.mjs`.
- [x] **Step 2:** Run `bash -n Extension-Kit/Creds-BOF/mimikatz/fetch_mimikatz.sh`.
- [ ] **Step 3:** Run `make -C Extension-Kit` and retain toolchain failures as diagnostics if MinGW components are unavailable. The full build was intentionally not started because the local MinGW toolchain is incomplete and the sub-builds clean existing `_bin` artifacts first; `make -n -C Extension-Kit` passed.
- [x] **Step 4:** Run the relevant Go tests and `git diff --check`.

