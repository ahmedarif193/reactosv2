UIX (Experimental Modern UI Helpers)

Purpose
- Provide a small, self‑contained set of helpers to render a minimal,
  modern, “Windows‑10‑like” look in FreeLDR, without changing existing
  TUI/Minimal UI flows yet.

Scope (initial)
- Pixel surface abstraction for platforms exposing a linear framebuffer
  (e.g., UEFI GOP). Falls back to stubs on non‑UEFI builds.
- Basic drawing primitives: solid rects and simple vertical gradients.
- Theme container with a handful of colors and metrics.
- Tiny demo entry you can call to validate rendering once integrated.

Non‑Goals (for now)
- Full widget toolkit, layout engine, or proportional font rendering.
- Input focus & full event loop; only basic stubs are provided.

Integration
- The helpers compile on all architectures; pixel rendering activates
  only when UEFIBOOT is defined and a framebuffer is available.
- Later we can gate a new INI option (e.g. ModernUI=Yes) to invoke the
  demo painter from UiInitialize() when MinimalUI=No.

