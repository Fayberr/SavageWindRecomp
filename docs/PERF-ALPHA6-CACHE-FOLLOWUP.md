# Alpha 6 cache follow-up: source and existing-capture analysis

No new native build, launch, benchmark, settings change or runtime patch in
this investigation. The playing executable and its two draft PRs are unchanged.
These are candidate designs and measured cost pools, not verified speedups.

## Evidence and method

Existing owner capture `swprof-20261116-073713.swprof`, exact combined runtime
SHA256 `7b2bffbe85bac4898bd5c98d038842f6169ed302d6ed5cea9476efd83dba2725`.
27.212 seconds, 2,674 frames, 19,728 render-thread stack samples. Runtime source
is the material-cache experiment based on Alpha 6 plus PR824/858. The current
public draft port is PR890; this report does not add that patch again.

The ordinary symbol-only sampler puts 47.4% of samples in `draw_`. This build
has DWARF inline information. Re-symbolizing the existing sample addresses with
`llvm-symbolizer --inlining --output-style=JSON` separates inlined helpers from
`draw_`, without collecting a new profile. Exact executable/load-bias matching
is necessary. Only host-runtime symbols were examined; no game code is included.

| Innermost runtime function, including inlined functions | Share of all render-thread samples |
| --- | ---: |
| `draw_` after resolving inlined helpers | 11.37% |
| `stage_set` | 4.18% |
| `set_finish` | 4.10% |
| `guest_copy` | 3.03% |
| `texture_info` | 3.30% |
| `texture_info_` | 0.42% |
| `stage_set_reuse` | 1.06% |
| `fnv_sample` | 3.58% |
| Full texture/content hashing, both compiled names | 4.78% |

These rows are exclusive leaf attribution. Some runtime leaves and all driver
leaves are omitted, so this is not a complete frame budget. Around 4.05% of the
thread has a `draw_` leaf with no useful line, so line-level coverage is partial.
`prof_ticks` alone accounts for 9.81%, and `prof_generation` another 2.77%; these
capture costs must not be sold as normal-play optimization opportunities.
Compiler scheduling and samples on instructions after a call also limit precise
source attribution. Sampling fractions are not predicted FPS gains.

A particularly useful distinction: 2,201 samples, 11.16% of the thread, have an
inlined descriptor/preparation helper under the vertex `stage_set` call. Of those,
575 are `set_finish`, 551 `stage_set`, 468 `guest_copy`, 245 `texture_info`, and
178 `cbuf_info`. This is a lower-bound subset with the relevant inline context,
not a full measurement of the vertex stage. The fragment cache does not cover it.

## 1. Cache vertex descriptor identity separately from per-draw uniform offsets

**First structural candidate.** `draw_` still calls the original vertex
`stage_set` for every draw. In `stage_set`, a dynamic uniform binding obtains
its current snapshot, appends its current offset to `dv`, then sets the
buffer descriptor's offset to zero before calling `set_finish`.

Consequently, object transforms or constant-buffer addresses can change while
the descriptor set's normalized buffer handles/ranges remain identical.
Caching an entire vertex preparation result by all guest buffer addresses is
unlikely to hit well. A narrower descriptor template can retain the static
binding metadata and normalized descriptor identity while regenerating only
snapshots and dynamic offsets for the current draw.

This could bypass repeated construction/double hashing for the common ordinary
vertex layout, rather than merely make `setc` larger. The global hashing pool
is 4.10% of this capture; only part belongs to an eligible vertex path. It
cannot honestly promise recovery of the entire 11.16% vertex cost pool.

Required guards: exact layout/binding types, actual returned buffer handles and
ranges, non-dynamic offsets, image/sampler descriptors, special storage paths,
submission/pool lifetime and device limits. Dynamic offsets must still be
produced in binding order, with their current values. HUD patches and global
memory paths should retain the original implementation until covered.

**Overlap:** descriptor identity reuse, dynamic UBOs and binding plans are known
approaches. PR761 cached whole preparation; PR128 retained sets/constants across
submissions; previous local candidates partitioned binding loops and delayed
writes until misses. This is a narrower separation of normalized vertex set
identity from changing offsets, not a claim to invent descriptor caching.

## 2. Stop evicting an unchanged fragment material for unrelated uniform writes

**Concrete refinement to PR890.** The material cache currently requires
`last->serial == uub_serial`. `uub_note` increments this global serial on every
UpdateUniformBuffer, including an update to a vertex-only range or an unrelated
fragment binding. The cache therefore misses even if every input that its
fragment shader actually reads stayed unchanged.

The ordinary `guest_copy` cache already has a more precise mechanism: entries
retain a mask of known update ranges and their snapshot serial, and only reject
when an overlapping range was updated. Adding/growing/evicting a range advances
`gcopy_gen`, retaining the conservative fallback when range membership changes.

A material entry could use equivalent dependency tracking for the uniform and
compiler-constant ranges it actually reads. Retain all existing checks for
`gcopy_gen`, GPU writes, submission, descriptor pool, shader/layout, resolved
textures, viewport/tint and special-path fallbacks. Simply deleting the global
serial check would be incorrect. An implementation must preserve range-table
replacement/growth invalidation and test effective buffer lengths, compiler
constants and a relevant write after an irrelevant one.

This is a specific avoidable miss visible in the source, but the capture does
not record the miss reasons or data dependencies. Its frequency and recovered
CPU time are unknown. It may overlap the first candidate's cost pool.

## 3. Reuse an entire prepared texture-binding bundle

**Refinement of the existing per-texture memo.** `texture_info` already caches
individual answers. Repeating a material still walks all textures, computes
hash slots, compares view/state keys, copies image descriptors/scales, then
compares the resulting arrays again in the material cache. Its 3.30% leaf share
is largely in that fast-path work; another generic per-texture table duplicates
what exists.

A stage/material bundle could retain the resolved descriptors plus scale/size
tables and a dependency signature for the used texture/sampler entries. Refresh
only changed entries, or at least reuse the bundle for successive draws with an
unchanged used-slot generation. Use a cheap exact last-bundle path first, before
introducing a large associative table.

**Validity work is substantial:** `tex_gen` alone is insufficient. Texture and
sampler pool entries can be registered again without rebinding the handles;
`objects.c` currently updates them directly. Track those registrations as well
as used handle changes, image/view retirement, feedback/depth twins, scene
composites, blend/anisotropy/mip-bias settings and per-frame upload requirements.
Do not skip CPU-written texture upload checks or reuse a stale feedback layout.
Pool registration visibility and playback order need explicit treatment.

The entire texture lookup leaf pool is only 3.72%, not all of the 1.289 ms
texture zone. Upload/change detection is separate. A bundle is worth examining,
but there is no evidence here of another large standalone FPS win.

## Existing alternatives and rejected leads

- Pipeline fast-cache and less aggressive shader-switch invalidation are already
  [PR817](https://github.com/Revivedx/SavageWindRecomp/pull/817). Around 1.43% of
  this thread's samples are in the inlined pipeline-key `hash_words` call. Do
  not report this PR's approach as a newly found optimization.
- Persistent cross-submission descriptors are already
  [PR128](https://github.com/Revivedx/SavageWindRecomp/pull/128), with a prior
  no-FPS-gain result on RADV. Descriptor indexing and parallel immutable draw
  preparation also appear in the earlier CPU-candidate report. They remain
  larger known directions, not discoveries from this pass.
- Replacing texture hashing with per-texture Windows GetWriteWatch was already
  investigated and rejected for query overhead. Sharing a correctly versioned
  write journal would be a different, larger design; existing geometry queries
  do not prove every texture page clean. Never skip hashing merely because a
  texture object or binding is unchanged.
- The full-hash path also refreshes a sampled hash even for permanently dynamic
  textures. This is redundant while dynamic, but this capture has only 1.0
  dynamic full hash/frame versus 60.3 sampled checks/frame. It is cleanup, not
  evidence of a significant win. There are also 5.7 ordinary full hashes/frame.
- Zero-filled fallback UBO/vertex copies could share immutable backing, but
  existing capture attribution does not establish a useful budget for them.
- Larger `setc` associativity, binding-loop partitioning and SIMD index scans
  overlap [PR862](https://github.com/Revivedx/SavageWindRecomp/pull/862) and prior
  experiments. They are not fresh candidates just because caches are involved.

## Recommendation and limits

Prioritize the vertex descriptor-identity split as the next substantial design,
with the fragment write-dependency refinement as the smaller focused follow-up.
The texture bundle is third because its remaining lookup budget is smaller and
its invalidation surface is wider. These candidates overlap and their cost
pools must not be added as predicted savings. Correctness tests can establish
valid reuse, but only eventual controlled native timings could establish FPS
benefit. No such timings or new implementation were authorized or performed
in this analysis. Existing play files and PRs were left unchanged.
