; ==========================================================================
; video.inc  --  VGA text-mode drawing primitives
; ==========================================================================
; We talk directly to the colour text framebuffer at B800:0000.
; Each cell is two bytes: [ char | attr ].  stosw is perfect for that --
; AL holds the char, AH holds the attribute, and ES:DI is the destination.
;
; Calling convention used by all draw_* helpers:
;     BL = x (column),  BH = y (row).
; cell_di translates (BL, BH) into the byte offset within the framebuffer.

; --------------------------------------------------------------------------
; cell_di:  DI <- (BH * 80 + BL) * 2.   Preserves AX, BX, CX, DX.
; --------------------------------------------------------------------------
cell_di:
        push    ax
        push    cx
        mov     al, bh                  ; AL = y
        xor     ah, ah
        mov     cx, 80
        mul     cx                      ; AX = y * 80
        xor     ch, ch
        mov     cl, bl                  ; CX = x
        add     ax, cx                  ; AX = y*80 + x
        shl     ax, 1                   ; AX = (y*80 + x) * 2
        mov     di, ax
        pop     cx
        pop     ax
        ret

; --------------------------------------------------------------------------
; put_cell:  write AX (char+attr) at cell (BL, BH).  Preserves all GPRs.
; --------------------------------------------------------------------------
put_cell:
        push    es
        push    di
        push    cx
        mov     cx, 0xB800
        mov     es, cx
        call    cell_di
        mov     [es:di], ax
        pop     cx
        pop     di
        pop     es
        ret

; --------------------------------------------------------------------------
; Convenience wrappers.  Each loads AX with the right char+attr and
; tail-calls put_cell.  All take BL=x, BH=y.
; --------------------------------------------------------------------------
draw_head_at:
        mov     ax, (ATTR_HEAD << 8) | CH_HEAD
        jmp     put_cell

draw_body_at:
        mov     ax, (ATTR_BODY << 8) | CH_BODY
        jmp     put_cell

draw_food_at:
        mov     ax, (ATTR_FOOD << 8) | CH_FOOD
        jmp     put_cell

erase_at:
        mov     ax, (ATTR_BLANK << 8) | CH_SPACE
        jmp     put_cell

draw_food:
        mov     bl, [fx]
        mov     bh, [fy]
        jmp     draw_food_at

; --------------------------------------------------------------------------
; draw_field:  clear the screen and paint borders, food, and the initial
; snake.  Called once at start-up; thereafter the game runs with
; *incremental* updates only (erase old tail, recolour old head, draw new
; head), which is what keeps the loop fast even with a long snake.
; --------------------------------------------------------------------------
draw_field:
        ; --- 1. blank the whole 80x25 buffer with a single rep stosw ---
        push    es
        mov     ax, 0xB800
        mov     es, ax
        xor     di, di
        mov     ax, (ATTR_BLANK << 8) | CH_SPACE
        mov     cx, 80 * 25
        rep     stosw
        pop     es

        ; --- 2. top and bottom borders ---
        xor     cl, cl
.tb:
        mov     bl, cl
        xor     bh, bh
        mov     ax, (ATTR_BORDER << 8) | CH_BORDER
        push    cx
        call    put_cell
        pop     cx
        mov     bl, cl
        mov     bh, VH - 1
        mov     ax, (ATTR_BORDER << 8) | CH_BORDER
        push    cx
        call    put_cell
        pop     cx
        inc     cl
        cmp     cl, VW
        jl      .tb

        ; --- 3. left and right side borders ---
        mov     cl, 1
.lr:
        xor     bl, bl
        mov     bh, cl
        mov     ax, (ATTR_BORDER << 8) | CH_BORDER
        push    cx
        call    put_cell
        pop     cx
        mov     bl, VW - 1
        mov     bh, cl
        mov     ax, (ATTR_BORDER << 8) | CH_BORDER
        push    cx
        call    put_cell
        pop     cx
        inc     cl
        cmp     cl, VH - 1
        jl      .lr

        ; --- 4. food and FPS label ---
        call    draw_food
        call    draw_fps

        ; --- 5. snake: walk the ring from tail to head ---
        mov     cx, [slen]
        jcxz    .nosnake
        mov     al, [tail]
        mov     [idx], al
.sd:
        push    cx
        mov     bl, [idx]
        xor     bh, bh
        mov     dl, [sx + bx]
        mov     dh, [sy + bx]
        ; head gets the bright glyph, the rest get the dim body glyph
        mov     ax, (ATTR_BODY << 8) | CH_BODY
        mov     cl, [head]
        cmp     [idx], cl
        jne     .nh
        mov     ax, (ATTR_HEAD << 8) | CH_HEAD
.nh:
        mov     bl, dl
        mov     bh, dh
        call    put_cell
        inc     byte [idx]              ; ring step, wraps mod 256
        pop     cx
        loop    .sd
.nosnake:
        ret

; --------------------------------------------------------------------------
; draw_fps:  paint "FPS: NN" at (FPS_X, FPS_Y) using the current [fps].
; Clamped to 99 because we only reserve two digits.  Called once per second
; by update_fps -- not every tick -- so it doesn't matter that it does a
; small div.
; --------------------------------------------------------------------------
draw_fps:
        push    es
        push    di
        mov     ax, 0xB800
        mov     es, ax
        mov     di, (FPS_Y * 80 + FPS_X) * 2
        mov     ah, ATTR_FPS            ; AH stays put across stosw
        mov     al, 'F'
        stosw
        mov     al, 'P'
        stosw
        mov     al, 'S'
        stosw
        mov     al, ':'
        stosw
        mov     al, ' '
        stosw
        ; turn [fps] into two ASCII digits
        mov     ax, [fps]
        cmp     ax, 99
        jbe     .ok
        mov     ax, 99
.ok:
        xor     dx, dx
        mov     bx, 10
        div     bx                      ; AX = tens, DX = ones
        add     al, '0'
        mov     ah, ATTR_FPS
        stosw
        mov     al, dl
        add     al, '0'
        mov     ah, ATTR_FPS
        stosw
        pop     di
        pop     es
        ret

; --------------------------------------------------------------------------
; show_over:  centred "GAME OVER" banner at row 12.
; --------------------------------------------------------------------------
show_over:
        push    es
        mov     ax, 0xB800
        mov     es, ax
        mov     di, (12 * 80 + 26) * 2
        mov     si, over_msg
.lp:
        lodsb
        or      al, al
        jz      .done
        mov     ah, ATTR_OVER
        stosw
        jmp     .lp
.done:
        pop     es
        ret

over_msg db ' GAME OVER - press any key ', 0
