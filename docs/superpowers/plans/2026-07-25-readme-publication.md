# Concise README and public publication Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the README with a compact public entry point and publish the
current ParallaxForge branch as a public GitHub repository.

**Architecture:** The README remains the only public-facing explanation and
contains the product definition, three retained core stages, one compact Build section containing the conventional configure and build commands,
one bake invocation, and output names. Publication creates the repository
under the authenticated GitHub owner, adds `origin`, and pushes the current
branch without a pull request.

**Tech Stack:** Markdown, CMake, PowerShell, Git, GitHub CLI.

## Global Constraints

- Keep the README concise; omit smoke, test, implementation, and history detail.
- Describe only PVS baking, GPU voxel/probe generation, and six-face DXR
  visibility baking.
- Keep the repo Windows x64, DirectX 12, and DXR tier 1.0 only.
- Create the public repository as `yyf0202/ParallaxForge`.
- Preserve the existing deidentified Git identity and never include private
  names, paths, assets, or history.

---

### Task 1: Replace the public README

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: `assets/demo-chamber/bake.json` and the existing
  `parallax-forge-bake` CLI.
- Produces: a concise public entry point with the build and bake commands.

- [ ] **Step 1: Write the target README content**

Replace `README.md` with:

```markdown
# ParallaxForge

ParallaxForge is a Windows DirectX 12 PVS baker: it turns OBJ scene geometry
into per-Probe visibility data.

## Core

- **GPU voxel occupancy** - marks world-space triangle AABBs and applies
  spherical dilation.
- **Free-voxel Probes** - emits the eight delta-inset corners and center of
  each 4 m storage cell.
- **DXR visibility baking** - traces six cube faces per Probe, keeps the
  nearest opaque hit, and emits stable object IDs.

## Build

Requires Windows x64, Visual Studio 2022, CMake 3.28+, the Windows SDK with
`dxc.exe`, and a DXR tier 1.0 adapter for baking.

```powershell
cmake --preset windows-debug
cmake --build build/windows-debug --config Debug
```

## Bake the demo

```powershell
build/windows-debug/apps/forge-bake/Debug/parallax-forge-bake.exe --config assets/demo-chamber/bake.json --bake
```

The bake writes `visibility.json` and `visibility.pfvis` under
`assets/demo-chamber/output`.
```

- [ ] **Step 2: Apply the README replacement**

Use `apply_patch` to replace the README exactly with the content from Step 1.

- [ ] **Step 3: Inspect the public text**

Run:

```powershell
powershell -NoProfile -File scripts/audit_public_tree.ps1 -Root (Get-Location).Path
git diff --check
```

Expected: `public-tree audit passed` and no diff-check output.

- [ ] **Step 4: Commit the README**

```powershell
git add README.md
git commit -m "docs: simplify public README"
```

### Task 2: Create and push the public repository

**Files:**
- No source-file changes.

**Interfaces:**
- Consumes: a clean current branch and the authenticated `gh` account
  `yyf0202`.
- Produces: public repository `yyf0202/ParallaxForge`, `origin`, and a pushed
  current branch.

- [ ] **Step 1: Verify the intended publication state**

Run:

```powershell
git status -sb
gh auth status
git remote -v
```

Expected: a clean `codex/parallax-forge-foundation` branch, authenticated
`yyf0202`, and no existing `origin` remote.

- [ ] **Step 2: Create the public repository and push**

Run:

```powershell
gh repo create yyf0202/ParallaxForge --public --source . --remote origin --push
```

Expected: GitHub creates the public repository, configures `origin`, and
pushes `codex/parallax-forge-foundation`.

- [ ] **Step 3: Verify the remote publication**

Run:

```powershell
git ls-remote --heads origin
gh repo view yyf0202/ParallaxForge --json nameWithOwner,visibility,url,defaultBranchRef
```

Expected: a public `yyf0202/ParallaxForge` repository with the pushed branch
visible at the returned URL.
