# openastro-guider — Decisions log

> **Note:** This document tracks the **separate** `open-astro/openastro-guider`
> project (a Linux-only, headless, Alpaca-only downstream fork). It is kept here
> as reference/tracking material only and is **not** the roadmap for this
> `openastro-phd2` repo.

Append-only log of every non-obvious decision made during the strip + headless-enable.
Each entry: date, decision, reason, and a file/ref where it's encoded. **Do not edit prior
entries; add new ones at the bottom.**

---

## 2026-06-02 — Repo created

- **Hard-fork of `open-astro/openastro-phd2`.** GitHub forbids forking a repo into the org
  that already owns it, so `openastro-guider` was seeded by cloning `openastro-phd2` and
  pushing its full history (3,590 commits + tags) to a fresh repo. This yields a fully
  independent repo (own issues/PRs, not in PHD2's fork network). `openastro-phd2` is kept as
  the `upstream` git remote for selectively pulling future fixes.
- **Scope set:** Linux-only, headless, Alpaca-only guiding daemon; keep wxWidgets; drop
  macOS, Windows, and INDI. ARA is the UI and drives the daemon over the event-server API
  (JSON-RPC :4400). See `design/PHD2_HEADLESS_PLAYBOOK.md`.
- **Workflow:** direct-to-master GitHub Flow with `phase/<N>-<name>` branches and the same
  merge-gate used across the OpenAstro org (adopted 2026-06-02).
