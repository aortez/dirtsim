# NES Video Smoothness Investigation

## Problem

NES emulator video doesn't feel as smooth as expected, despite the emulator
running well under budget on Pi5.

## Measurement Setup

Instrumentation added to three points in the pipeline:

1. **Server main loop** (`StateMachine.cpp`): min/max/stddev of loop iteration time.
2. **UI FBDEV main loop** (`fbdev.cpp`): per-phase timing of processEvents,
   lv_timer_handler, usleep.
3. **UI frame interval** (`SimRunning.cpp`): time between consecutive
   UiUpdateEvent processing, plus queue delay (WebSocket receive to
   processEvents pickup).

## Pipeline

```
Emulator thread (NTSC 60.1fps)
  -> [condvar] server tick (polls/copies frame)
  -> [serialize] broadcastRenderMessage (zpp_bits binary over WebSocket)
  -> [WebSocket callback thread] deserialize, create UiUpdateEvent, queue
  -> [event queue] waiting...
  -> [UI main loop] processEvents() picks up event
  -> render NES frame to LVGL canvas (nearest-neighbor scale, RGB565->ARGB8888)
  -> lv_timer_handler() flushes dirty region to /dev/fb0 via mmap
```

## Measurements (Pi5, NES self-pacing mode, 1ms server sleep chunks)

### Server (frame production)
```
Total iteration: 17.35ms avg (min=16.77 max=20.07 stddev=0.24)
  Tick: 0.45ms avg
  Sleep: 16.91ms avg (1ms chunks)
```
Server delivers frames with sub-millisecond jitter. Not the bottleneck.

### UI FBDEV Main Loop
```
Loop: 12.62ms avg (min=7.08 max=17.08 stddev=2.96)
  processEvents: 3.94ms avg (max=7.35)
  lv_timer_handler: 6.43ms avg (max=11.42)
  usleep: 2.24ms avg (max=8.08)
```
~793 iterations per 10 seconds = ~79 Hz loop rate.

### UI Frame Interval (what matters for smoothness)
```
Frame interval: 17.34ms avg (min=13.00 max=22.93 stddev=3.29)
Queue delay:     4.40ms avg (min=0.16 max=8.35  stddev=2.13)
```

### LVGL / FBDEV Configuration
```
LV_DEF_REFR_PERIOD            = 8ms (125 Hz target)
LV_LINUX_FBDEV_RENDER_MODE    = PARTIAL (only dirty regions)
LV_LINUX_FBDEV_BUFFER_COUNT   = 2 (double buffered)
LV_LINUX_FBDEV_BUFFER_SIZE    = 1080
LV_LINUX_FBDEV_MMAP           = 1
```
No vsync. Writes to /dev/fb0 are immediate.

## Analysis

The server produces frames with 0.24ms stddev, but by the time the UI
processes them the interval stddev is 3.29ms — 14x worse.

The dominant source of jitter is **queue delay**: frames wait 0-13ms
(avg 4.4ms, stddev 2.2ms) in the event queue for processEvents() to pick
them up.

This happens because the UI main loop spends most of its time in phases
where the queue can't be drained:
- lv_timer_handler: 6.4ms avg (rendering dirty regions + fbdev flush)
- usleep: 2.2ms avg (LVGL-suggested idle time, capped at 33ms)

Combined that's ~8.6ms out of every 12.6ms loop iteration where incoming
frames can only accumulate in the queue.

A frame arriving at a random point in the loop waits on average ~half the
non-processEvents portion of the loop, which matches the observed 4.4ms
average queue delay.

The 3.29ms frame interval stddev means some frames display for ~13ms and
others for ~23ms — a 1.77:1 ratio, which is perceptible as judder.

## Potential Fix Directions

### 1. Process events more frequently
Add a second `sm.processEvents()` call after `lv_timer_handler()` (before
usleep). This would halve the maximum queue wait time from ~12ms to ~6ms.
Simple change, measurable improvement.

### 2. Replace usleep with event-aware wait
Instead of blind `usleep(idle_time)`, wait on a condition variable that the
WebSocket thread signals when a new event arrives. Wake immediately on new
frame, or after idle_time — whichever comes first. Eliminates the usleep
portion of queue delay entirely.

### 3. Decouple NES video from the LVGL render path
The NES frame is a raw pixel buffer that doesn't need LVGL's widget system.
Write NES frames directly to the framebuffer (bypassing LVGL canvas +
lv_timer_handler flush), and only use LVGL for the UI overlay. This would
eliminate the 6.4ms lv_timer_handler cost for the video region.

### 4. Reduce lv_timer_handler cost
The 6.4ms average for lv_timer_handler is high. Investigate whether the NES
canvas invalidation is causing unnecessary full-screen redraws. The NES
frame is 256x224 scaled to maybe 480x420 — partial render should only flush
that region, not the full 800x480 display.

### 5. Server-side: use runFrames(1) in self-pacing mode — DONE

Removed the polling branch in NesSmolnesScenarioDriver::tick(). Now always
calls runFrames(1), which blocks on the emulator's condvar and returns the
instant a frame is ready. Server tick went from 0.45ms (poll) to 13.9ms
(blocking wait), server sleep dropped from 16.9ms to 2.8ms. Server
iteration stddev stayed tight at 0.32ms.

**Result**: Confirmed the server was never the bottleneck. UI-side frame
interval jitter was unchanged (stddev 3.35ms before and after). All
remaining latency is in the UI pipeline.

## Recommended Order

Phase 1 (direct fbdev write, bypass LVGL) and Phase 2 (shared memory) are
where the real latency improvement lives. Phase 3/fix #5 is done.
