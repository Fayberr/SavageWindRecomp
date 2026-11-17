# Splitting draw preparation over threads (R3): assessment

Question: can the work the `nvn-queue` thread does per draw be spread over two or three threads, so that the render
side drops from about 13 ms to the 5.5 ms a steady 165 fps needs?

Short answer: not as an incremental change. The draw path is the engine's state machine, not a pipeline of pure
steps, and the parts that are pure are small. Nothing here was benchmarked; the evidence is a zone profile and a
static global-access audit (`tools/audit_globals.py`). Branch `perf/r3-audit`, no runtime code changed.

## Where the render thread's time goes

Capture of 2884 frames, 4431 draws a frame, 14.411 ms average frame, `nvn-queue` about 13.4 ms busy (about 3 us a
draw). Zone self time per frame, profiler overhead included (the zones add up to about 14.8 ms):

| Zone | ms/frame | Calls/frame |
| --- | ---: | ---: |
| exec/draw/sets (descriptor sets) | 3.494 | 8862 |
| exec/draw/pre (draw preparation) | 2.679 | |
| exec self (list walk, state commands) | 2.244 | |
| exec/draw/tex (texture info) | 1.651 | |
| exec/draw self | 1.404 | |
| exec/draw/vtx (vertex and index staging) | 0.935 | |
| vk enqueue (recorder hand-off) | 0.840 | 16147 |
| exec/draw/key (pipeline key) | 0.681 | |
| tex/upload check | 0.570 | 467 |
| exec/draw/dyn (dynamic state) | 0.290 | |

The cost is flat: thousands of small decisions per frame, each a few hundred nanoseconds, spread over the whole draw
path. There is no kernel that dominates. Bulk data work (copies, hashing) is already below about 0.6 ms in total.

## What the audit measures

`tools/audit_globals.py` compiles a C file to LLVM IR at `-O0`, reads every load, store, atomic and memcpy that
touches a global (static locals included, constants excluded), and follows direct calls to build, per function, the
set of globals the function and everything below it reads and writes. Calls to functions outside the file and
indirect calls are listed but not followed. The closure includes cold paths (error handling, debug logging), so it
overstates what one draw really touches; it is the number a thread-safety argument would have to cover, and it is
also the number a refactor has to untangle.

Results for `runtime/nvn/vulkan/exec.c` (functions reached, globals loaded, globals stored):

| Root | Functions | Loaded | Stored |
| --- | ---: | ---: | ---: |
| `draw_` | 282 | 562 | 511 |
| `exec_list` | 305 | 597 | 559 |
| `scene_post` | 89 | 181 | 149 |
| `resolve_feedback` | 76 | 174 | 145 |
| `begin_pass` | 78 | 159 | 157 |
| `gw_check` | 79 | 145 | 110 |
| `stage_set_prepare` | 107 | 168 | 124 |
| `cbuf_info` | 74 | 99 | 72 |
| `texture_info` | 40 | 70 | 46 |
| `guest_fetch` | 41 | 72 | 48 |
| `set_finish` | 29 | 48 | 32 |
| `guest_copy` | 7 | 15 | 8 |
| `copy_dedupe_in` | 7 | 9 | 6 |
| `vk_transient_copy` | 6 | 8 | 5 |

`draw_` alone has 105 global loads and 79 stores in its own body, before any callee. The pieces that are small enough
to move cleanly (`guest_copy`, `copy_dedupe_in`, `vk_transient_copy`, `snapshots_invalidate`, `fg_draw`) are exactly
the ones that cost little.

Limits of the method: it is static, so it cannot prove that two draws never touch the same state at run time; it
tracks accesses through one function's GEP chains, so state reached through a pointer returned by a callee is only
seen as "address passed"; it says nothing about the other files (`textures.c`, `record.c`, the shader cache) that the
outside callees live in. It supports "this is entangled", not "this is safe".

## Candidate cuts

1. **Decision on one thread, emit on another.** The measured ceiling (`SAVAGEWIND_NULL_EMIT=1`, 2026-11-02) is +32%
   at 1440p with TAA and only +9% at 1080p with AA off, so the gain depends on the settings. Splitting needs the roughly
   100 shared mutable globals (pass state, the dynamic-state cache, the set cache, texture backends, transient chunks,
   depth twins, feedback state, HUD patches, gmem, readbacks, the recorder ring) assigned to one side or the other.
   Inside one draw there is also a circular dependency: fragment textures are resolved first because their scales feed
   the viewport and render-scale decision, which in turn feeds the pipeline key and set contents.
2. **Descriptor sets as a pipelined stage.** Sets are the largest zone (3.5 ms), but `texture_info` touches views,
   depth twins, feedback and the scene upscale state, the set cache is shared, and the hash cost is already attacked by
   cpuopts bit 16 and the unchanged-preparation cache (bit 8, PR 890). A thread would move a shrinking cost.
3. **Pure kernels on a worker.** memcpy of staged data is about 0.3 ms, texture hashing is already cut by cpuopts
   bit 32, the write-watch poll is at most about 0.6 ms, `guest_copy` is a table lookup that does not parallelize.
   Together they cap at well under 1 ms and each adds hand-off latency.
4. **Speculative preparation with validation stamps.** Prepare draws ahead, validate against state stamps, redo on
   mismatch. Needs a shadow interpreter of the state machine, so the logic exists twice and has to stay identical.
5. **Parallel command lists or passes.** Passes share the same globals; the guest also reuses state across lists.
6. **Moving work to the recorder thread.** It already runs about 5.3 ms a frame plus 0.37 ms submit, so it has no
   room; it would become the new limit.

## Verdict

R3 as rated (very large, about 45% certain) is revised to: very large, weeks, about 20% certain, and not testable
off the WinPC. The prerequisite is mechanical but large: group the draw-path globals into a render-context struct
(for example per-pass state, set cache, texture backends, transient memory, recorder ring), so that a second context
or a hand-off boundary becomes expressible. Only after that can the decision/emit split be evaluated against its
+32% (1440p, TAA) or +9% (1080p, AA off) ceiling. Doing the refactor first also helps every other render item.

## Order that gets nearer to 165 fps

1. Retrained runtime PGO (PR 889, branch `perf/dist-pgo`): earlier measurement +18..20% at Plateau and Castle with a
   fresh TAA profile.
2. R1 bits (cpuopts 16 and 32, est. -1.0..-1.5 ms together; not benchmarked).
3. R2, whole-draw reuse extending the unchanged-preparation cache (mask 8 / PR 890).
4. R4, draw budget (about 810 of 4400 draws are shadow cascades); changes visuals.
5. Guest items G1 to G6 for the main thread (about 8 ms floor).
6. R3 only after the render-context refactor.

Steady 165 in Plateau and Castle at 1440p with TAA stays roughly a 15 to 20% outcome even with all of this.

## Reproduce

```
python3 tools/audit_globals.py --self-test
python3 tools/audit_globals.py --summary runtime/nvn/vulkan/exec.c draw_ stage_set_prepare guest_copy -- \
    -Ithird_party/vma -Iruntime -Iruntime/nvn -Iruntime/nvn/vulkan -I<SDL3 include dir>
```

Drop `--summary` to list the globals themselves (stored, loaded only, address passed, outside callees).
