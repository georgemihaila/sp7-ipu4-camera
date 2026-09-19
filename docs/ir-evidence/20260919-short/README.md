# OV7251 short-run decoded-frame evidence

These PNGs are representative decoded 640x480 Y10 frames from the unchanged
BB8 path. The source logs and raw captures remain in:

```text
/var/tmp/ov7251-ir-scene-a-20260919-181900/
/var/tmp/ov7251-ir-scene-b-20260919-181930/
```

- `frame-window-a.png` and `frame-window-b.png` are separate captures from
  the short-run scene. Both visibly contain the same window, wall, radiator,
  and sofa geometry. Their SHA-256 hashes differ, but their mean absolute
  pixel difference is only 1.0755; no pixel differs by more than 32 levels.
  This is consistent with capture noise, not a demonstrated scene change.
- `frame-historical-tablet.png` is copied from the pre-existing
  `scripts/ir/test/frame0.png` artifact and is explicitly historical, not from
  the short-run timeline. It visibly contains a handheld tablet over the
  room. After orientation alignment, the current window frame differs from it
  by 41.1074 mean absolute levels, with 36,773 of 307,200 pixels differing by
  more than 128 levels. This verifies that decoded content follows a gross
  scene change across captures, while the two current same-scene hashes alone
  do not establish image integrity.

SHA-256:

```text
cdb05c9b9d256e0de6e370b5b71a1af688f6b105ce917abd2a8338c9df483c9e  frame-historical-tablet.png
dca1abb3eb3d89b09765526a9f43034931226e996452cc3096a76b931d72216d  frame-window-a.png
7113228d28f6297cfed748016eff19dfe3ca285b6c51a8bd8a08eb81d04b3db9  frame-window-b.png
```
