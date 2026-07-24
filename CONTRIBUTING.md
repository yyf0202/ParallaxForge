# Contributing to ParallaxForge

Keep changes focused, self-contained, and suitable for a public repository.

Before opening a pull request, run the Windows x64 configure, build, and test
route:

```powershell
cmake --preset windows-debug
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug --output-on-failure
powershell -NoProfile -File scripts/audit_public_tree.ps1 -Root $PWD
```

The audit rejects private path and host markers as well as common tracked
binary artifacts. A local release-only deny-list may be supplied with
`-DenyListFile`; do not commit that file or its private identifiers.

Pull requests must include the relevant tests and keep the public-tree audit
passing.
