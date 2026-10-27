# Live Control Protocol

The running Linux build exposes a per-user Unix socket:

```text
/tmp/tonimcqueen-slicer-$UID.sock
```

Packets are fixed-size little-endian records. They are intentionally numeric,
not arbitrary JSON configuration injection:

```text
u32 magic       = 0x544D5143
u16 version     = 1
u16 opcode
u32 value_bits
```

Opcodes:

| Opcode | Setting | Value |
|---:|---|---|
| 1 | `enable_prime_tower` | `0` or `1` |
| 2 | `wall_loops` | integer `1..20` |
| 3 | `outer_wall_line_width` | IEEE-754 float bits, `0.1..2.0` |
| 4 | `layer_height` | IEEE-754 float bits, `0.01..1.0` |

Accepted commands are posted to the GUI thread, applied through the existing
configuration path, and schedule a background reslice. The protocol does not
mutate an already-running printer job or expose arbitrary configuration keys.

Development sender:

```text
scripts/tonimcqueen-slicer-live-set.py prime-tower on
scripts/tonimcqueen-slicer-live-set.py wall-loops 4
scripts/tonimcqueen-slicer-live-set.py outer-wall-width 0.42
scripts/tonimcqueen-slicer-live-set.py layer-height 0.08
```
