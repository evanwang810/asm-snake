; ==========================================================================
; consts.inc  --  shared constants for all modules
; ==========================================================================
; Pure %define's; no code, no data.  Included first by snake.asm.

; --- play field (text-mode cells, including the border) ---
%define VW          40              ; width  (cols 0 .. VW-1)
%define VH          25              ; height (rows 0 .. VH-1)

; --- snake ring buffer ---
; MAXLEN must be 256 so an 8-bit ring index wraps automatically on `inc bl`.
%define MAXLEN      256

; --- timing ---
; The frame is subdivided into POLL_MS sleeps.  Each sleep is preceded by
; a non-blocking input poll, so the effective input latency is one POLL_MS,
; not the whole frame.
%define POLL_MS     10              ; granularity of input polling (ms)
%define START_POLLS 12              ; 12 * 10ms = 120ms per tick at start
%define MIN_POLLS   6               ; floor: 60ms per tick at top speed

; --- VGA text-mode attributes (bg<<4 | fg) ---
%define ATTR_BORDER 0x0F            ; bright white on black
%define ATTR_FOOD   0x0C            ; bright red on black
%define ATTR_HEAD   0x0A            ; bright green on black
%define ATTR_BODY   0x02            ; dim   green on black
%define ATTR_BLANK  0x07            ; light gray (irrelevant, char is space)
%define ATTR_OVER   0x4F            ; bright white on red
%define ATTR_FPS    0x0E            ; bright yellow on black

; --- on-screen FPS readout position ---
%define FPS_X       42
%define FPS_Y       1

; --- glyphs ---
%define CH_BORDER   '#'
%define CH_FOOD     '@'
%define CH_HEAD     'O'
%define CH_BODY     'o'
%define CH_SPACE    ' '

; --- direction encoding ---
; Layout chosen so that opposite directions XOR to 2.  That makes the
; "no 180-degree turn" check a single XOR-and-compare.
%define DIR_R       0
%define DIR_D       1
%define DIR_L       2
%define DIR_U       3

; --- direction-queue sentinel ---
%define DIRQ_EMPTY  0xFF
