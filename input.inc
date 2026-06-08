; ==========================================================================
; input.inc  --  non-blocking keyboard input with a 2-deep direction queue
; ==========================================================================
; Why a queue?
;     The snake only changes direction once per game tick.  Without a queue,
;     a fast corner -- e.g. you're heading Right, and you press Up then Left
;     within the same tick -- loses the Up press.  A 2-deep queue lets both
;     turns happen on consecutive ticks, which is what makes tight cornering
;     feel responsive.
;
; Why poll many times per tick?
;     We sleep in small POLL_MS chunks (see wait_frame in snake.asm) and
;     drain the keyboard buffer on every chunk.  Effective input latency
;     drops from one whole tick (~120 ms) to one POLL_MS (~10 ms).

; --------------------------------------------------------------------------
; poll_key:  drain every key currently in the BIOS buffer, enqueue valid
; direction changes, and set [dead]=1 on Q.  Always returns.
; --------------------------------------------------------------------------
poll_key:
.next:
        mov     ah, 0x01                ; BIOS: check for keystroke (non-block)
        int     0x16
        jz      .done                   ; no key waiting -- bail
        xor     ah, ah                  ; BIOS: read keystroke
        int     0x16                    ; AL=ascii, AH=scancode

        cmp     al, 'q'
        je      .quit
        cmp     al, 'Q'
        je      .quit

        mov     dl, DIRQ_EMPTY          ; DL = decoded direction (or 0xFF)
        cmp     ah, 0x48                ; up arrow scancode
        jne     .nu
        mov     dl, DIR_U
        jmp     .have
.nu:
        cmp     ah, 0x50                ; down arrow
        jne     .nd
        mov     dl, DIR_D
        jmp     .have
.nd:
        cmp     ah, 0x4B                ; left arrow
        jne     .nl
        mov     dl, DIR_L
        jmp     .have
.nl:
        cmp     ah, 0x4D                ; right arrow
        jne     .nr
        mov     dl, DIR_R
        jmp     .have
.nr:
        cmp     al, 'w'
        jne     .nw
        mov     dl, DIR_U
        jmp     .have
.nw:
        cmp     al, 's'
        jne     .ns
        mov     dl, DIR_D
        jmp     .have
.ns:
        cmp     al, 'a'
        jne     .na
        mov     dl, DIR_L
        jmp     .have
.na:
        cmp     al, 'd'
        jne     .next                   ; unknown key -- ignore, drain next
        mov     dl, DIR_R
.have:
        call    enqueue_dir
        jmp     .next

.quit:
        mov     byte [dead], 1
.done:
        ret

; --------------------------------------------------------------------------
; enqueue_dir:  DL = candidate direction.  Insert into dirq[] if it's a
; legal next move (not a 180-degree reversal, not a repeat of the last
; queued direction).  If the queue is full, the press is dropped.
;
; "Last queued direction" rule:
;     queue empty       -> compare against [dir]      (current heading)
;     one entry queued  -> compare against dirq[0]
;     two entries queued-> compare against dirq[1]
;
; The XOR-with-2 trick relies on DIR_R/DIR_L = 0/2 and DIR_D/DIR_U = 1/3.
; --------------------------------------------------------------------------
enqueue_dir:
        mov     al, [dir]
        cmp     byte [dirq], DIRQ_EMPTY
        je      .haveprev
        mov     al, [dirq]
        cmp     byte [dirq + 1], DIRQ_EMPTY
        je      .haveprev
        mov     al, [dirq + 1]
.haveprev:
        mov     ah, al
        xor     ah, dl
        cmp     ah, 2                   ; opposite of last queued?
        je      .reject
        cmp     al, dl                  ; same as last queued?
        je      .reject

        cmp     byte [dirq], DIRQ_EMPTY
        jne     .slot1
        mov     [dirq], dl
        ret
.slot1:
        cmp     byte [dirq + 1], DIRQ_EMPTY
        jne     .reject
        mov     [dirq + 1], dl
.reject:
        ret
