# Merge Branches Into Main Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Merge feature branches into `main`, resolve conflicts, clean up old branches, and push final result upstream.

**Architecture:** Use git to fast-forward/merge feature branches into `main`, resolve conflicts in working tree files, run project tests to validate stability, and ensure remote `origin/main` reflects combined changes before deleting old branches.

**Tech Stack:** git CLI, project-specific build/test tooling

---

### Task 1: Assess Branch State

**Files:**
- N/A (git metadata)

- [ ] **Step 1: List local branches and identify merge targets**

Run: `git branch --format="%(refname:short) %(upstream:short)"`
Expected: Shows `main` plus feature branches `codex-esp32-ble-wifi-provisioning` and `codex-no-network-prompt` (and any others).

- [ ] **Step 2: Inspect commit history for each branch**

Run: `git log --oneline main..codex-esp32-ble-wifi-provisioning`
Expected: Shows unique commits pending merge.

Run: `git log --oneline main..codex-no-network-prompt`
Expected: Shows unique commits pending merge.

- [ ] **Step 3: Check working tree cleanliness**

Run: `git status -sb`
Expected: No uncommitted tracked changes blocking merges.

### Task 2: Merge Branch codex-esp32-ble-wifi-provisioning

**Files:**
- Modify: files touched by branch commits (determine during merge)

- [ ] **Step 1: Ensure on main**

Run: `git checkout main`
Expected: Switched to branch 'main'.

- [ ] **Step 2: Merge branch**

Run: `git merge --no-ff codex-esp32-ble-wifi-provisioning`
Expected: Either fast-forward, or merge commit created; resolve conflicts if reported.

- [ ] **Step 3: Resolve conflicts if any**

- If conflicts occur, inspect with `git status`; edit conflicted files to desired final content.
- After edits, run `git add <file>` per resolved file.

- [ ] **Step 4: Run project tests/build**

Run: `<project test command>` (replace with actual, e.g., `npm test`)
Expected: All tests pass.

- [ ] **Step 5: Commit merge if not auto completed**

Run: `git commit` (only if merge commit pending)
Expected: Merge commit recorded.

### Task 3: Merge Branch codex-no-network-prompt

**Files:**
- Modify: files touched by branch commits (determine during merge)

- [ ] **Step 1: Ensure on main**

Run: `git checkout main`
Expected: Switched to branch 'main'.

- [ ] **Step 2: Merge branch**

Run: `git merge --no-ff codex-no-network-prompt`
Expected: Branch merges cleanly; resolve conflicts as needed.

- [ ] **Step 3: Resolve conflicts if any**

- Use preferred editor to edit conflicted files, ensuring final state matches branch intent.
- Stage resolved files via `git add`.

- [ ] **Step 4: Run project tests/build**

Run: `<project test command>`
Expected: All tests pass.

- [ ] **Step 5: Commit merge if pending**

Run: `git commit`
Expected: Merge commit recorded if required.

### Task 4: Final Verification and Cleanup

**Files:**
- N/A (git metadata)

- [ ] **Step 1: Review git status**

Run: `git status -sb`
Expected: Clean working tree ready to push.

- [ ] **Step 2: Push updated main**

Run: `git push origin main`
Expected: Remote `origin/main` updated with merged commits.

- [ ] **Step 3: Delete merged branches locally**

Run: `git branch -d codex-esp32-ble-wifi-provisioning`
Run: `git branch -d codex-no-network-prompt`
Expected: Branches deleted locally (use `-D` only if not fully merged).

- [ ] **Step 4: Delete merged branches remotely**

Run: `git push origin --delete codex-esp32-ble-wifi-provisioning`
Run: `git push origin --delete codex-no-network-prompt`
Expected: Remote branches removed.

- [ ] **Step 5: Tag or document final state if required**

- If the user needs documentation, update docs in repo or provide summary.

### Task 5: Commit Documentation Updates (if any)

**Files:**
- Modify: docs/requirement/

- [ ] **Step 1: Stage documentation files**

Run: `git add docs/requirement/`
Expected: Files staged.

- [ ] **Step 2: Commit final documentation**

Run: `git commit -m "docs: update requirements after merges"`
Expected: Commit created if docs changed.

- [ ] **Step 3: Push commit if created**

Run: `git push origin main`
Expected: Remote updated.

---

## Self-Review

- Spec coverage: All branches targeted for merge, verification, cleanup, documentation.
- Placeholder scan: Ensured placeholders replaced by instructions (project test command left generic; confirm actual command before execution).
- Type consistency: Branch names and commands consistent.

