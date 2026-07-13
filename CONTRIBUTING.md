# Contributing to OpenAstro PHD2

## CI policy: pull requests do not compile the code

PR checks are deliberately fast and compile-free: clang-format,
clang-tidy and cppcheck scoped to the lines/files your PR changes,
ShellCheck, a Unicode trojan-source scan, JavaScript syntax checking,
a zizmor workflow audit, and the Claude code review. The full compile +
test suite (`Build and Test` workflow) runs automatically after merge to
`main`, and the sanitizer (ASan/UBSan/TSan) and CodeQL passes run
post-merge and weekly.

**Known risk, accepted by design:** a PR that introduces a compile or
link error passes all PR checks and breaks `main` only after merge.

What that means in practice:

- **Before merging a large or risky C++ change**, run the build against
  the PR branch yourself: Actions tab → *Build and Test* → *Run
  workflow* → pick the branch. Wait for green, then merge.
- **If a merge breaks `main`** (the post-merge run flags it within the
  hour), revert or fix forward promptly so other merges aren't building
  on a broken base.
- Small or non-C++ PRs can merge on the fast checks alone.

## Releases

Releases are manual: the *Release* workflow (Actions tab, or push a
`v*` tag after running `/release` to bump `version.md` and the
CHANGELOG) builds the Debian amd64/arm64 packages, the Windows x64
installer, and the macOS Apple Silicon DMG, then attaches them to a
**draft** GitHub Release. Nothing is published until a human reviews
the draft and publishes it.
