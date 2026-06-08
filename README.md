# asm-snake

snake in 16-bit x86 asm. runs as a dos .com under dosbox. binary is like 1.5kb.

## run it

```
nasm -f bin snake.asm -o snake.com
dosbox snake.com
```

on windows theres `build.bat` and `play.bat` already. play.bat hardcodes the default dosbox install path, fix it if urs is elsewhere.

controls: arrows or wasd, q quits. theres a yellow `FPS: NN` in the corner because why not

## files

started as one big snake.asm but it got gross so:

- `snake.asm` entry, main loop, the frame timing thing
- `consts.inc` all the magic numbers (screen size, colors, etc)
- `video.inc` anything that pokes the framebuffer at B800
- `input.inc` keyboard polling + direction queue
- `game.inc` init, rng, food, the step function

nasm `%include` is just text substitution so its still one flat .com when its built. no linker, no makefile, nothing fancy

## what makes it not laggy

couple things, mostly stuff i fixed after the first version felt sluggish:

**ring buffer for the snake body.** v1 was an array i memmoved every frame to slide the snake forward by one. completely dumb in hindsight. now its a circular buffer with head/tail indices and moving the snake is two `inc`s. the array is exactly 256 bytes so the 8-bit ring index wraps mod 256 for free when you `inc bl`. length 4 vs length 200, same per-tick cost.

**incremental redraw.** v1 repainted the entire 80x25 screen every tick which is like 2000 cell writes. now its 3: erase the old tail, recolor the old head from bright `O` to dim `o`, paint the new head. looks identical.

**input polling decoupled from the sleep.** v1 slept 120ms then read one key. press anything 5ms after the read and it sits there for 115ms before being noticed. now i sleep in 10ms chunks and drain the keyboard buffer between every chunk. snake still moves at the same speed, it just doesnt feel like youre typing through molasses.

**2-deep direction queue.** if ur going right and tap down-then-left real fast for a corner, v1 just lost one of them. now they both go in a tiny fifo. the "no 180 turn" check is one xor + one compare because the direction encoding was picked so opposites are exactly bit 1 apart (R=0 L=2, D=1 U=3, xor those and you get 2). this is one of those things that feels really clever for 5 minutes and then you forget about it

**gets faster as you grow.** every food shaves 10ms off the tick down to a floor of 60ms. so endgame is actually scary instead of a chill walk

## fps counter

just increments a counter every tick. once 18 BIOS clock ticks pass (thats ~1 sec since BIOS ticks at 18.2Hz dont ask me why) it copies the count into the display variable and resets. so the number you see is "ticks per second over the last second" not some jittery instantaneous reading

## food bug i hit

place_food walks the snake and rejects if the new food lands on a body cell. for a while i was calling it BEFORE writing the new head to the ring so it could spawn directly on the cell the snake was about to move into and get insta-eaten invisibly. moved the call to after the head is committed. classic off-by-one-step

## old hardware

should run on basically any ibm pc compatible from 1984 onward. uses `int 15h AH=86h` for the sleep which is an AT-era thing (286+). on a 1981 8088 XT youd swap that for a busy loop and itd work too.

doesnt run native on 64-bit windows because they killed 16-bit support in the kernel forever ago. hence dosbox. or [js-dos.com](https://js-dos.com), drag the .com onto the page and it works in the browser

## tweaks worth trying

mess with consts.inc:
- `POLL_MS=5` for snappier input
- `MIN_POLLS=3` for an unplayable endgame
- `VW=78` for full screen width
- recolor by changing the ATTR_* values

## license

mit, do whatever
