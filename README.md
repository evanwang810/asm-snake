# Snake in 16-bit x86 assembly

A DOS `.COM` snake game written in NASM syntax, split across small modules
so each file owns one concern.

```
asm/
├── snake.asm     entry, main loop, frame-delay loop, state declarations
├── consts.inc    %define-only: sizes, colours, glyphs, direction codes
├── video.inc     B800 framebuffer access, draw_field, draw_*  helpers
├── input.inc     non-blocking keyboard polling + direction queue
├── game.inc      init, RNG, food placement, one game tick
├── build.bat     runs `nasm -f bin snake.asm -o snake.com`
└── play.bat     `dosbox snake.com -exit`
```

## Quick start

Windows (with [NASM](https://nasm.us) and [DOSBox](https://www.dosbox.com) installed):

```powershell
.\build.bat
.\play.bat
```

Linux / macOS:

```bash
nasm -f bin snake.asm -o snake.com
dosbox snake.com
```

Arrow keys or WASD to turn. Q to quit. Any key dismisses the game-over banner.
A yellow `FPS: NN` readout in the top-right shows live game ticks per second.

---

## Why split it up?

The previous single-file version was fine as a curiosity but cramped:
state, drawing, input, and game logic all rubbed elbows in 350 lines. The
split mirrors the natural boundaries of a small game engine:

* **consts.inc** is the dial board. Want a bigger field, a faster game,
  a different colour palette? Edit one file and rebuild.
* **video.inc** owns the framebuffer. Nothing outside it touches `B800`,
  so swapping in a different rendering mode (CGA graphics, snow-avoidance
  for real hardware, etc.) is a localized change.
* **input.inc** owns the keyboard and the input policy (queueing, reverse
  rejection). Game logic just reads `[dir]`; it doesn't care how that byte
  got there.
* **game.inc** owns the rules. It's the most likely file to grow when you
  add features (warps, obstacles, scoring), and it sits behind a small
  interface (`init`, `step`).

NASM's `%include` is purely textual, so the result is still a single flat
`.COM` -- no linker required. The trade-off is global label scope; we keep
that clean by using local labels (`.foo`) inside every function.

---

## Optimizations versus the original

Three real wins, all measurable on a long snake.

### 1. Ring buffer instead of an O(n) body shift

The old `step` shifted the entire body array by one slot every tick:

```
for i = len-1 downto 1: sx[i] = sx[i-1]; sy[i] = sy[i-1]
```

At length 200 that's 400 byte copies per tick, every tick, forever.

The new layout stores segments in a fixed 256-entry ring with a head
index and a tail index:

```
            tail              head
              v                v
sx: [ . . . . X . . . . . . . X . . . ]
              \________________/
                  slen cells
```

Advancing the snake one cell is two register increments:

```
inc byte [head]     ; ring step, wraps mod 256 automatically
inc byte [tail]     ; (skip this one if we just grew)
```

`MAXLEN = 256` was chosen so the 8-bit index wraps for free on `inc bl`,
which means none of the modulo bookkeeping you'd normally have to write.
The whole "move snake" operation is now O(1) instead of O(length).

### 2. Incremental rendering

The old version repainted the entire 80×25 screen every frame: a
`rep stosw` for 2000 cells plus per-cell `mul`/`add`/store for the borders
and snake -- thousands of memory writes per tick.

The new version paints once at startup (`draw_field`), then each tick does
exactly three cell writes:

```
erase_at  (old tail cell)        ; if we didn't grow
draw_body_at (old head cell)     ; recolour 'O' -> 'o'
draw_head_at (new head cell)     ; the actual move
```

Plus one extra `draw_food` when food gets eaten and respawned. Total: a
near-constant ~6--8 bytes of framebuffer touched per tick. Combined with
the ring buffer, the per-tick work no longer depends on the snake's
length at all.

### 3. Decoupled input polling

Responsiveness is dominated by *how soon a keypress is observed*, not
*how fast the snake moves*. The old loop did:

```
draw
sleep 120ms          <-- key pressed here: 120ms latency
read one key
step
```

The new `wait_frame` subdivides the sleep into twelve 10 ms chunks and
drains the keyboard buffer between every chunk:

```
for i in 1..12:
    poll_key            <-- catches any pending keys, enqueues them
    sleep POLL_MS       <-- 10ms
```

Effective input latency is one POLL_MS (~10 ms) instead of one full tick
(~120 ms). The snake still moves at the same rate; it just feels much
snappier because every key is *observed* almost immediately.

### Bonus: two-deep direction queue

A 1-deep "current direction" variable loses presses on tight corners.
Imagine you're heading Right, want to do a quick down-then-left S-bend,
and tap D then L within the same tick. With one variable, D overwrites L
or L overwrites D; the snake never goes Down.

The new code keeps a 2-slot FIFO and pops one entry per tick. As long as
you don't queue a 180-degree reversal (rejected by an XOR-with-2 check
that's only possible because of the direction encoding -- see below), you
get tight, predictable cornering.

### Bonus: speed scaling

`tick_polls` starts at 12 (≈120 ms per tick) and is decremented by 1 every
time the snake eats food, floored at 6 (≈60 ms). The game gets gradually
faster as the snake grows, in line with how classic snake plays.

---

## File-by-file walkthrough

### `consts.inc`

`%define`-only, no code, no data. Every magic number in the game lives
here, including the screen layout, the RNG cap, the colour attributes,
and the direction encoding.

The direction layout is worth noting:

```
DIR_R = 0      ; 00
DIR_D = 1      ; 01
DIR_L = 2      ; 10
DIR_U = 3      ; 11
```

Opposite directions XOR to 2 (`0 ^ 2 == 2`, `1 ^ 3 == 2`). That's why the
"don't allow 180-degree turns" check in `enqueue_dir` is one XOR plus
one compare -- no lookup table, no branching tree.

### `video.inc`

Everything that touches `0xB800` (the colour text-mode framebuffer) lives
here. The framebuffer is laid out as 25 rows × 80 columns, each cell two
bytes: low byte is the ASCII char, high byte is `(bg<<4) | fg`. So one
`stosw` writes a complete styled cell.

* `cell_di` turns `(x, y)` into a framebuffer byte offset:
  `di = (y*80 + x) * 2`. Called by every primitive.
* `put_cell` is the generic write: caller puts `(char | attr<<8)` in AX
  and `(x, y)` in `(BL, BH)`, and the word lands in the framebuffer.
  Preserves all general-purpose registers, so callers don't have to push
  anything around it.
* `draw_head_at`, `draw_body_at`, `draw_food_at`, `erase_at` are 2-line
  wrappers that load the right `(char, attr)` into AX and `jmp put_cell`.
  Tail-calling instead of `call`/`ret` saves a few bytes per wrapper.
* `draw_field` is called once at start-up. It does the only `rep stosw`
  in the whole program, paints the border, draws food, and walks the
  snake ring once.

### `input.inc`

`poll_key` drains every pending key from the BIOS keyboard buffer using
the non-blocking BIOS service:

```
int 16h, AH=01h     ; ZF set if no key
int 16h, AH=00h     ; read AL=ascii, AH=scancode
```

Each key is decoded into a direction code, then handed to `enqueue_dir`,
which:

1. picks the "effective last direction" -- the last queued direction if
   the queue is non-empty, else the current heading;
2. rejects if the new direction is the reverse (XOR == 2) or a duplicate;
3. inserts into the first empty slot, or drops the press if the queue is
   full.

That last step matters: if you're mashing keys faster than the snake can
move, only the first two pending directions are kept. No buffer overrun,
no random old presses getting executed half a second later.

### `game.inc`

Three things live here.

**`rand` / `place_food`.** An LCG (`x = x*25173 + 13849`) provides cheap
pseudo-random words. `place_food` uses `div` to map a 16-bit random into
the playable cell range, then walks the ring to make sure the food
doesn't spawn on the snake. Retries on collision.

**`init`** sets up a 4-segment snake at the centre, heading right, with
the queue empty.

**`step`** is the meat of the game. The numbered comments inside it
match this walkthrough:

1. Promote one queued direction into `[dir]`, shift the queue down.
2. Compute the candidate new head cell based on `[dir]`.
3. Wall collision: any coord outside the play rectangle ends the game.
4. Self collision: walk the ring from `tail+1` for `slen-1` cells.
   We skip the tail itself because it's about to vacate -- otherwise the
   snake would die any time it moved straight into the cell behind itself
   (which it does every tick).
5. Detect food eaten; set the `grew` flag but **don't place new food yet**.
6. If we didn't grow, erase the old tail cell and advance the tail index.
7. Recolour the old head from the bright `O` to the dim `o`.
8. Advance the head index, write the new head's coords into the ring,
   draw the new head.
9. If we did grow, *now* place new food and bump speed. Placing food
   after the head is in the ring means the food's collision check sees
   the snake's final shape, so it can never land on the cell the head
   just moved into.

### `snake.asm`

The entry point. Sets up video mode 3, hides the cursor, seeds the RNG
from the BIOS daily-tick counter (`int 1Ah`), runs `init`, paints the
field once, and enters the main loop:

```
main_loop:
    call wait_frame   ; subdivided sleep + key drains
    call step         ; one logical tick, ~3 cell writes
    cmp byte [dead], 0
    je main_loop
```

`wait_frame` is the responsiveness lever. The whole tick budget is split
into `tick_polls` sleeps of 10 ms each, with a `poll_key` call between
every pair. A `cmp [dead], 0` exits early if Q was pressed mid-frame, so
the quit feels instant rather than waiting out the remainder of the tick.

The state variables sit as `resb`/`resw` reservations at the very end of
the file. NASM under `-f bin` emits zero-fill for those (you'll see a
harmless "uninitialized space declared in .text section" warning per
reservation), and DOS loads the whole thing into a 64 KB segment for us.

---

## How to play it online

If you don't want a local DOSBox: drag the built `snake.com` onto
[js-dos.com](https://js-dos.com) -- it runs DOSBox in the browser, no
install required.

## Tweaks worth trying

* `POLL_MS = 5` for even snappier input (24 polls per tick).
* `MIN_POLLS = 3` for a brutal endgame.
* Change `ATTR_HEAD` / `ATTR_BODY` for a different colour scheme.
* Bump `VW` to 78 to use most of the 80-column screen.
* Replace the LCG with a xorshift if you want a better random sequence
  (the food clumps a bit at the start with this LCG).

## License

MIT -- see [LICENSE](LICENSE).
