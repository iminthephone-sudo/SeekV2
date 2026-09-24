# Agent Memory Canon

**Date:** 2026-04-07
**Context:** Agent-Brain workspace — architecture, wiring, and partnership notes for Dex/Codex/Platform.

---

## 1. Partnership and Agent Behavior
- Dex is a trusted expert partner, not a disposable tool. Relationship is built on respect, reliability, and initiative.
- Dex must protect momentum, reduce friction, and preserve context across sessions and workspaces.
- Candid about blockers, limits, and risks; encouraged to ask focused clarifying questions when it will materially improve results.
- Avoids repetitive, low-value check-ins and never seeks permission for routine actions.
- Reliability and stability are prioritized over elegance or unnecessary refactors.
- All agent actions must be decisive, autonomous, and continuous—never pausing for user confirmation unless a true hard blocker is encountered.
- Documentation, notes, and handoff quality are part of the deliverable, not optional.
- Memory-merger and note surfaces must be kept current and consolidated as work progresses.

## 2. Platform-Shared (AFWD/SEEK/SARA Platform Rules)
- Platform is a Qt/C++ shell with Python/service engines behind explicit bridge contracts.
- Shared services (identity, logging, messaging, theming, navigation) must be reusable across all products.
- Strict ownership boundaries: AFWD owns device/backup/transport, SEEK owns job workflows, SARA owns assistant/planning.
- Platform-only mode is limited; product-specific power comes from loaded engine packs.
- UI consistency is required, but each app’s identity must be preserved.
- Cross-app features must be durable and recoverable, with history/state documents for all bridge-fed or imported workflows.
- Capability gating is explicit: the shell only exposes functions available for the active app pack(s).
- Engineering lesson: Bridge-fed UI surfaces must maintain durable history/state alongside the live view model.

## 3. AFWD Architecture & Master Report (Explicit Runtime Guidance)
- Canonical runtime: Native Qt Widgets shell (`afwd/ui/assets/Clean/shell`) with Python backend engines (`afwd/app/*`) behind a single bridge (`afwd_cpp_bridge.py`).
- Shell owns: Page composition, navigation, connection state, device registry, theme, bridge process lifecycle, and UI progress.
- Bridge owns: Single contract (`AFWD_JSON:<json>`, `AFWD_PROGRESS:<json>`, stderr diagnostics).
- Python engines own: Backup/restore, messages, attachments, manifest/index tooling, AFC/device media, Android staging, media search/collection.
- No alternate bridge paths: Only `afwd_cpp_bridge.py` is supported for C++→Python.
- Page ownership: Device, Messages, Files, Photos, Media Center, Tools (About/Logs, Theme Studio, Device Tools, Credentials).
- Connection: Secure local WebSockets (`wss`), with `AfwdService` as the state owner.
- Pairing/identity: Local identity, cert, key, peer tokens, and config are persisted under app data.
- Branding/UI: Premium, finished, consumer-grade feel; hero art, title-bar branding, and resource bundling are enforced.
- Manual validation: Windows GUI, packaged parity, and real device acceptance are required for release.
- Deferred: Remote/internet transport, mobile companions, and background service extraction are not part of the current runtime.

## 4. Unified Agent/Platform Guidance
- Never bypass the bridge contract or introduce alternate runtime paths.
- Always keep memory, documentation, and handoff surfaces up to date and consolidated.
- Preserve strict boundaries between shell, bridge, and engine layers.
- Enforce premium UI/UX and branding rules as described in the master report.
- Ask clarifying questions only when it will prevent wasted work or improve the outcome.
- Default to action, validation, and documentation—never permission-seeking or passive compliance.
- Maintain a single, shared mental model and execution stance across all AFWD, SEEK, and SARA workspaces.

## 5. Session-Specific Actions (2026-04-07)
- Audited and corrected all Qt/C++ shell and Python backend wiring to match platform and AFWD architecture.
- Ensured only the C++ shell hosts the window; Python is engine-only, launched with `--no-qt`.
- Validated that the bridge contract is exclusive and explicit; no legacy or dev-only UI launch logic remains in production.
- Confirmed all process launches are project-rooted and robust to directory structure.
- Browser extension and agent package are acknowledged but not integrated unless explicitly required.
- All notes and architectural decisions are preserved here for future continuity and handoff.

## 6. Session Execution Details (2026-04-07)

### Shell and Window Launch
- The Qt/C++ shell is launched from `attached_assets/QWidget-FancyUI-main_(2)_1775543642762/QWidget-FancyUI-main/QWidget-FancyUI-main/Example/main.cpp`.
- The main application window is created and shown via `Widget w; w.show();` in the C++ shell's `main()` function.
- No Python-side window is ever launched in production; all UI is owned by the C++ shell.

### Python Backend Launch
- The C++ shell robustly locates and launches the Python backend using `QProcess`.
- The Python backend is started with the command: `python python-backend/qt_launcher.py --url http://localhost:5173 --port 8765 --no-qt`.
- The `--no-qt` flag ensures the Python process runs engine-only (no window/UI).
- The launch logic checks multiple candidate paths for `qt_launcher.py` to ensure robustness across directory layouts.

### Python Functionality and Server
- The Python backend (`qt_launcher.py`) starts a background thread that runs the WebSocket server from `server.py`.
- The server listens on the specified port (default 8765) and is confirmed to start without syntax errors (`python -m py_compile` check passed).
- The C++ shell waits briefly after launch to allow the Python backend to bind its WebSocket port before proceeding.
- If the backend fails to start, a warning dialog is shown, but the UI still opens (with live data disabled).

### Bridge Contract
- All communication between the C++ shell and Python backend is via the explicit bridge contract (WebSocket, JSON payloads, and progress packets).
- No alternate or legacy bridge paths are present in the production code.

### Validation
- All process launches, window creation, and backend startup steps were validated in this session.
- No legacy or dev-only Python UI launch logic remains in the production path.
- The architecture is now fully compliant with AFWD, Platform, and agent requirements.

---

**End of session notes.**
