# Quiet matched AV measurements, 13 September 2026

No hosted CI was used. Debug and Release each ran baseline/candidate/candidate/
baseline on the same x64 machine with processor affinity mask 255, no concurrent
builds or other test workloads, and the unchanged five-round, 600-frame fixture.
Baseline pins `d192e474`; candidate pins `ecad707`. All eight receipts match fixture,
AV production source, compiler, WARP, OS and power-policy identity. Each receipt
also binds its own executable and selected archive to the actual linker record.

An earlier attempt stopped before its first candidate measurement because the
candidate Debug executable and linker record were absent. Its single baseline
receipt is preserved in `incomplete-first-attempt` and does not enter this series.
The official candidate Debug build restored the outputs in 1m16s with zero
warnings/errors; its log is retained. The complete paired series restarted only
after checking all four required executable/linker-record combinations.

All eight completed runs retain 4,608,000 bytes of steady surface storage and zero
hidden surface bytes, preparations and composites. `compare_runs.py` reproduces
`av-20260912-comparison.json` from the eight adjacent raw receipts. The filenames
retain the existing comparator convention; metadata records the actual execution
times. Every A/A and B/B comparison is included.

| Across both candidate pairs and all six scenarios | Debug | Release |
|---|---|---|
| Completed offscreen FPS change | -1.40% to +0.52% | -1.76% to +0.17% |
| Frame p95 change | -1.71% to +3.69% | -3.74% to +3.22% |
| Private bytes change | -6.42% to +5.26% | -2.42% to +6.57% |
| Working set change | -0.64% to +26.64% | -0.63% to +2.56% |

The earlier 11.9–14.4% Release dirty-throughput loss does not reproduce under this
controlled sequence. This is evidence against a sustained drop of that magnitude,
not a reason to discard the earlier receipts or infer a hardware/presentation gain.

Separate CPU timing still matters. Release 96-DPI dirty composition increases from
25.3 to 30.5 microseconds in the first pair and 24.3 to 26.9 microseconds in the
second. At 192 DPI it increases from 24.5 to 27.7 and 23.4 to 24.6 microseconds.
The 144-DPI first pair increases from 23.5 to 27.3 microseconds; its second pair
does not show the same increase. The second pair's 144-DPI dirty preparation
increases 5.52% (0.6590 to 0.6954 ms). Total frame throughput remains within the
range above, but it does not erase these component costs.

Process memory also varies: the first Debug candidate's working set is 23.6–26.6%
higher, while the second is slightly below its baseline. Debug baseline private
memory varies by up to 9.2% between repeats. These process-level readings do not
establish a steady resource leak or an accepted memory regression.

Performance acceptance remains open pending review of these measured tradeoffs.
No threshold or baseline changed. This fixture does not measure presented FPS,
peak process memory, allocation counts, hardware AV, or real-client accessibility;
those retain their existing owners and qualification limits.
