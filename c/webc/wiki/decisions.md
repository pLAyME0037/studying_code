<!-- EXAMPLE decisions.md structure -->
# Decisions

One entry per architectural decision. Newest at the bottom of each section.
Each entry: **what we chose, why, what we rejected, and what would make us
reverse it.** If you change your mind, add a new entry that supersedes the old
one — do not edit the old entry into agreeing with you.

---

## D1 — C11, not C++ and not Rust

**Chosen.** Strict C11 (built as `gnu11` for `typeof` and statement
expressions).

**Why.** The goal is learning plus full control of the generated code. C is the
smallest toolchain and the easiest to read the assembly of. The mesher in the
original is already written as C structs under Cython, so the performance-
critical part maps across almost literally.

**Rejected.**
- *C++*: better STL, but templates and implicit lifetime make the port harder to
  reason about and reintroduce the abstraction creep simplexity forbids.
- *Rust*: excellent for exactly this workload, but ownership fights the
  "caller frees" model a C port wants, and the brief was strictly C.

**Reverses if:** we need RAII-style cleanup for a subsystem with 50+ live
handles, or GTK interop becomes painful. Not expected.

---

## D2 — GTK4 for the GUI

**Chosen.** GTK4, system-linked (LGPL, dynamic linking).

**Why.** It is the only mature GUI toolkit with a **C API** that ships the
widgets this app actually needs: dialogs, file choosers, tree views (for the
NBT editor), list widgets, notebook tabs (one per open world), menus, and
`GtkGLArea` for the live 3D viewport. Verified installed: **GTK4 4.24.1**.

The original is wxPython — a binding over wxWidgets, which is C++. There is no C
binding. So the realistic choice was GTK4 or hand-rolling every widget in SDL3.
Hand-rolling is roughly +30–50k lines for the dialogs alone.

**Rejected.**
- *SDL3 + custom UI*: pure C and permissive, but Amulet has ~15 non-trivial
  dialogs, a tree-based NBT editor, and dockable panels. Too much work for no
  benefit.
- *wxWidgets from C*: possible via its C++ core, but violates D1.
- *Dear ImGui*: C++.

**Cost accepted.** The UI will not be pixel-identical to Amulet. This is
**feature parity, not visual parity.**

**Reverses if:** GTK4 licensing or cross-platform packaging becomes a real
problem. At that point the honest move is C++ + wxWidgets, which breaks D1 — so
it would be a two-decision reversal.

