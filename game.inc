; ==========================================================================
; game.inc  --  core game logic: init, RNG, food placement, one game step
; ==========================================================================
;
; Snake storage: a fixed 256-entry ring buffer of (x, y) pairs.
;     sx[0..255], sy[0..255]      -- byte arrays
;     [head]                      -- ring index of the head segment
;     [tail]                      -- ring index of the tail segment
;     [slen]                      -- live segment count
;
; Why 256?  An 8-bit ring index wraps for free on every `inc bl`, so the
; mod-MAXLEN bookkeeping disappears.  Combined with O(1) advance/retract,
; each game tick moves the snake in constant time regardless of length.
; (The old version did an O(n) memmove of the body every tick.)

; --------------------------------------------------------------------------
; init:  set up a 4-cell snake heading right in the centre of the board.
; --------------------------------------------------------------------------
init:
        mov     byte [tail], 0
        mov     byte [head], 3
        mov     word [slen], 4
        mov     byte [sx + 0], 17
        mov     byte [sy + 0], 12
        mov     byte [sx + 1], 18
        mov     byte [sy + 1], 12
        mov     byte [sx + 2], 19
        mov     byte [sy + 2], 12
        mov     byte [sx + 3], 20
        mov     byte [sy + 3], 12
        mov     byte [dir], DIR_R
        mov     byte [dirq], DIRQ_EMPTY
        mov     byte [dirq + 1], DIRQ_EMPTY
        mov     byte [dead], 0
        mov     byte [tick_polls], START_POLLS
        call    place_food
        ret

; --------------------------------------------------------------------------
; rand:  16-bit linear congruential generator.  AX = next value.
; Parameters borrowed from Borland's old Turbo Pascal RNG (a=25173, c=13849)
; -- not cryptographic, but cheap and plenty random for food placement.
; --------------------------------------------------------------------------
rand:
        mov     ax, [rng]
        mov     bx, 25173
        mul     bx
        add     ax, 13849
        mov     [rng], ax
        ret

; --------------------------------------------------------------------------
; place_food:  pick a random cell inside the play field that isn't already
; occupied by the snake.  Retries on collision.
; --------------------------------------------------------------------------
place_food:
.try:
        call    rand
        xor     dx, dx
        mov     bx, VW - 2
        div     bx                      ; DX = AX mod (VW-2) -> 0 .. VW-3
        inc     dl                      ; -> 1 .. VW-2
        mov     [fx], dl
        call    rand
        xor     dx, dx
        mov     bx, VH - 2
        div     bx
        inc     dl
        mov     [fy], dl

        ; walk the ring, reject if any segment matches
        mov     cx, [slen]
        jcxz    .ok
        mov     al, [tail]
        mov     [idx], al
.chk:
        push    cx
        mov     bl, [idx]
        xor     bh, bh
        mov     al, [sx + bx]
        cmp     al, [fx]
        jne     .next
        mov     al, [sy + bx]
        cmp     al, [fy]
        jne     .next
        pop     cx
        jmp     .try                    ; collision -- repick
.next:
        inc     byte [idx]
        pop     cx
        loop    .chk
.ok:
        ret

; --------------------------------------------------------------------------
; step:  one game tick.
;   1. promote a queued direction to [dir]
;   2. compute the new head cell, check walls and self-collision
;   3. on food: grow (skip tail retract), pick new food, speed up slightly
;   4. apply incremental drawing: erase old tail, recolour old head as
;      body, draw new head as head.  No full-screen redraw -- this is what
;      keeps the loop snappy at any snake length.
; --------------------------------------------------------------------------
step:
        ; ---- 1. dequeue one direction, shift queue left ----
        cmp     byte [dirq], DIRQ_EMPTY
        je      .nodq
        mov     al, [dirq]
        mov     [dir], al
        mov     al, [dirq + 1]
        mov     [dirq], al
        mov     byte [dirq + 1], DIRQ_EMPTY
.nodq:
        ; ---- 2. compute new head position ----
        mov     bl, [head]
        xor     bh, bh
        mov     al, [sx + bx]           ; AL = head x
        mov     dl, [sy + bx]           ; DL = head y
        mov     dh, [dir]
        cmp     dh, DIR_R
        je      .r
        cmp     dh, DIR_D
        je      .d
        cmp     dh, DIR_L
        je      .l
        dec     dl                      ; up
        jmp     .moved
.r:     inc     al
        jmp     .moved
.d:     inc     dl
        jmp     .moved
.l:     dec     al
.moved:
        ; ---- 3. wall collision ----
        cmp     al, 1
        jl      .die
        cmp     al, VW - 2
        jg      .die
        cmp     dl, 1
        jl      .die
        cmp     dl, VH - 2
        jg      .die

        ; ---- 4. self-collision ----
        ; Walk from (tail+1) for (slen-1) cells -- the tail itself is
        ; about to vacate, so colliding with it is fine.
        mov     cx, [slen]
        dec     cx
        jcxz    .nocol
        mov     bl, [tail]
        inc     bl
        mov     [idx], bl
.scol:
        push    cx
        mov     bl, [idx]
        xor     bh, bh
        mov     dh, [sx + bx]
        cmp     dh, al
        jne     .nc
        mov     dh, [sy + bx]
        cmp     dh, dl
        je      .die_pop
.nc:
        inc     byte [idx]
        pop     cx
        loop    .scol
.nocol:
        mov     [newx], al
        mov     [newy], dl

        ; ---- 5. did we eat the food? ----
        ; We only set the `grew` flag here; food placement is deferred until
        ; AFTER the new head is written into the ring, so place_food's
        ; collision check sees the snake's final shape.
        mov     byte [grew], 0
        cmp     al, [fx]
        jne     .doadvance
        cmp     dl, [fy]
        jne     .doadvance
        cmp     word [slen], MAXLEN - 1
        jge     .doadvance              ; capped: still treat as eaten visually
        inc     word [slen]
        mov     byte [grew], 1

.doadvance:
        ; ---- 6. retract tail (unless we just grew) ----
        cmp     byte [grew], 0
        jne     .skiptail
        mov     bl, [tail]
        xor     bh, bh
        mov     al, [sx + bx]
        mov     ah, [sy + bx]
        mov     bl, al
        mov     bh, ah
        call    erase_at
        inc     byte [tail]             ; ring step, wraps mod 256
.skiptail:
        ; ---- 7. recolour the old head as body ----
        mov     bl, [head]
        xor     bh, bh
        mov     al, [sx + bx]
        mov     ah, [sy + bx]
        mov     bl, al
        mov     bh, ah
        call    draw_body_at

        ; ---- 8. advance head index, store new pos, draw new head ----
        inc     byte [head]             ; ring step
        mov     bl, [head]
        xor     bh, bh
        mov     al, [newx]
        mov     [sx + bx], al
        mov     al, [newy]
        mov     [sy + bx], al
        mov     bl, [newx]
        mov     bh, [newy]
        call    draw_head_at

        ; ---- 9. if we ate, NOW place new food and speed up ----
        cmp     byte [grew], 0
        je      .end
        cmp     byte [tick_polls], MIN_POLLS
        jle     .nospeed
        dec     byte [tick_polls]
.nospeed:
        call    place_food
        call    draw_food
.end:
        ret

.die_pop:
        pop     cx                      ; unwind the self-collision push
.die:
        mov     byte [dead], 1
        ret
