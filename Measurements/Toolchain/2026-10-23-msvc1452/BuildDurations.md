# Release build-duration observations

| Before seconds | After seconds | Scope |
| ---: | ---: | --- |
| 186.25 | 171.15 | Captured build-wrapper wall time |

Descriptive single-build observations only. Build caches, restore costs, concurrent workloads and clean/incremental state were not controlled across this pair. No build-time regression, improvement or compiler-version effect is established.

Both builds use MSVC 14.52.37025. Exact receipt SHA256 values and source paths are retained in build-duration-observations.json in this owner''s external report folder. These times do not replace the frozen paired runtime fixture experiment.

Captured command outcomes: before=True; after=True.
