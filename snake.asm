; ==========================================================================
; snake.asm  --  entry point and main loop
; ==========================================================================
; Build:  nasm -f bin snake.asm -o snake.com
; Run:    dosbox snake.com    (or any real-mode DOS environment)
;
; Architecture:
;     snake.asm     entry, video mode setup, main loop, frame-delay loop
;     consts.inc    %define constants used by every module
;     video.inc     B800 framebuffer access + draw_field / draw_*  helpers
;     input.inc     non-blocking keyboard polling + direction queue
;     game.inc      init, RNG, food, one game tick
;
; The state variables (sx, sy, head, tail, dirq, ...) live at the very end
; of this file as `resb` reservations.  Under `-f bin` NASM doesn't write
; them to the .com on disk -- they just consume address space inside the
; 64 KB segment DOS hands the program.

        bits 16
        org 0x100

%include "consts.inc"

; --------------------------------------------------------------------------
; entry
; --------------------------------------------------------------------------
start:
        ; switch to 80x25 colour text mode -- guarantees the framebuffer
        ; sits at B800:0000 in the layout we expect.
        mov     ax, 0x0003
        int     0x10

        ; hide the blinking text-mode cursor (CH=0x26 high cursor scanline
        ; with bit-5 set = "cursor off", CL=0x07 low scanline).
        mov     ah, 0x01
        mov     cx, 0x2607
        int     0x10

        ; seed the RNG from the BIOS daily-tick counter (CX:DX, ~18.2 Hz).
        ; DX alone is plenty random for this game.
        xor     ah, ah
        int     0x1A
        mov     [rng], dx

        call    init
        ; snapshot the BIOS tick counter as the start of the FPS window
        xor     ah, ah
        int     0x1A
        mov     [last_tick], dx
        mov     word [frame_count], 0
        mov     word [fps], 0

        call    draw_field              ; one full paint, then incremental

main_loop:
        call    wait_frame              ; sleep + drain keys
        call    step                    ; advance one cell + partial redraw
        call    update_fps              ; bump counter, refresh readout
        cmp     byte [dead], 0
        je      main_loop

        ; --- game over: banner, wait for keypress, restore terminal ---
        call    show_over
        xor     ah, ah
        int     0x16
        mov     ax, 0x0003              ; restores cursor too
        int     0x10
        mov     ax, 0x4C00
        int     0x21

; --------------------------------------------------------------------------
; wait_frame -- responsive sleep.
;
; Instead of one big int15h/AH=86h call for the whole tick, sleep in
; POLL_MS chunks and poll the keyboard between chunks.  When the player
; presses a direction key, it lands in the queue within at most POLL_MS,
; not within a whole tick.  That's the main responsiveness win.
;
; The chunk count [tick_polls] decreases by one every time a food is
; eaten, so the game speeds up smoothly as the snake grows.
; --------------------------------------------------------------------------
wait_frame:
        mov     al, [tick_polls]
        mov     [polls], al
.l:
        call    poll_key
        cmp     byte [dead], 0          ; Q during the frame -> exit fast
        jne     .end
        ; int 15h / AH=86h: wait CX:DX microseconds
        mov     ah, 0x86
        mov     cx, 0
        mov     dx, POLL_MS * 1000
        int     0x15
        dec     byte [polls]
        jnz     .l
.end:
        ret

; --------------------------------------------------------------------------
; update_fps -- count game ticks; once ~1 second of BIOS time has passed,
; freeze the count as [fps], reset, and repaint the readout.
;
; BIOS int 1Ah / AH=00h returns the day's tick count in CX:DX at ~18.2 Hz.
; We compare only the low word -- it wraps every ~1 hour, which we ignore.
; --------------------------------------------------------------------------
update_fps:
        inc     word [frame_count]
        xor     ah, ah
        int     0x1A                    ; CX:DX = ticks since midnight
        mov     ax, dx
        sub     ax, [last_tick]
        cmp     ax, 18                  ; 18 ticks ~= 0.989 sec
        jb      .ret
        mov     [last_tick], dx
        mov     ax, [frame_count]
        mov     [fps], ax
        mov     word [frame_count], 0
        call    draw_fps
.ret:
        ret

; --------------------------------------------------------------------------
; module bodies
; --------------------------------------------------------------------------
%include "video.inc"
%include "input.inc"
%include "game.inc"

; --------------------------------------------------------------------------
; uninitialised state.  Lives past the on-disk end of the .com file.
; --------------------------------------------------------------------------
sx          resb MAXLEN     ; ring buffer of segment x-coords
sy          resb MAXLEN     ; ring buffer of segment y-coords
head        resb 1          ; index of head segment in sx/sy
tail        resb 1          ; index of tail segment
slen        resw 1          ; live length
dir         resb 1          ; current heading
dirq        resb 2          ; 2-deep FIFO of pending direction changes
dead        resb 1          ; game-over flag
fx          resb 1          ; food x
fy          resb 1          ; food y
newx        resb 1          ; scratch: candidate new head x
newy        resb 1          ;          candidate new head y
grew        resb 1          ; did this tick eat food?
tick_polls  resb 1          ; current frame length in POLL_MS units
polls       resb 1          ; wait_frame countdown
idx         resb 1          ; scratch ring-index used by loops
rng         resw 1          ; LCG state
fps         resw 1          ; last computed fps (game ticks / second)
frame_count resw 1          ; ticks accumulated this measurement window
last_tick   resw 1          ; BIOS tick low word at start of window
