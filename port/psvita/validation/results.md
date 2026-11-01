# Results (fill on hardware)

One section per matrix ID from PLAN.md 5.1. Each verdict cites its evidence
file in this directory. "Pass" without a cited file is not a pass.

## M0 baseline
- [ ] manifest.json complete
- [ ] SDL1/System VPK reinstallable; assets hashed
- [ ] screenshots + logs: startup, menus, select, preview, quick VS, pause, round end, storyboard, exit
- [ ] preview crash recorded as unresolved, with core dump

## M1 cross-build
- [ ] SDL1 and SDL2 profiles cross-compile cleanly
- [ ] link map: one SDL2 + one vitaGL, no SDL1 archive in SDL2 profile

(Acceptance gates M2-M9: copy the relevant matrix rows from PLAN.md 5.1 here
as they are executed, with file references.)
