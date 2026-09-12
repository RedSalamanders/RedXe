# Matched AV fixture measurements, 2026-09-12

Four runs per profile use baseline/candidate/candidate/baseline order on the same
x64 machine, without concurrent builds or product processes. Both Debug and Release
use the identical two-view fixture and byte-identical AV production sources. Compiler,
WARP, OS and power-policy identity checks pass. Every raw receipt is retained.

The baseline pins d192e474; the candidate pins ecad707. Each scenario contains five
rounds of 600 frames. `compare_runs.py` compares medians within each run, retaining
both candidate comparisons and baseline/baseline and candidate/candidate repeats.
The raw receipts bind source, binary and archive hashes to each execution.

All eight runs pass the fixture's resource assertions: steady surface allocation is
4,608,000 bytes; hidden surface bytes, preparations and composites are zero.
Private-memory changes do not show a consistent increase across the paired runs.

Timing acceptance remains **open**. Release dirty-view throughput at 144/192 DPI
is 11.9–14.4% lower in the candidate pairs; preparation/composition timings also
increase. However, repeat runs contain large shifts, including a 59.8% baseline-only
throughput drop in the 144-DPI clean scenario. Debug results also vary substantially.
These runs identify a signal to investigate; they do not establish its cause or a
performance waiver. No threshold or baseline has changed. These are offscreen WARP
measurements, not presented FPS, peak-memory, hardware AV or accessibility evidence.
