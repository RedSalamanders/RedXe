Reviewed statistics: use only runtime-*-reviewed.json/md. Earlier runtime-* summaries are retained as superseded because the original Holm adjustment rounded probabilities. Raw native samples remain unchanged. [Study method sources](study-method-sources.zip) include the frozen collection sources, corrected analysis, exact-probability controls and independent verifier. BuildDurations.md records descriptive build times without a speedup claim.

# RedXe MSVC 14.52 Release evidence

Active qualification. [Settings](Settings.md), sizes.json and the compressed original MSBuild snapshots retain the original/candidate policy, artifact hashes and metadata. Both sides use MSVC 14.52.37025; no compiler-version performance claim is made. Runtime measurements belong to this application and do not qualify other products or native ARM64 execution.

full-tool-metadata.csv.gz and full-tool-metadata-summary.json extend the initial selected-field summary to every captured compiler/resource/MIDL/MASM/shader/link/archive metadata field, preserving per-file overrides and explicit unset values. Intrinsic file identities/timestamps are retained in the raw snapshots and excluded from setting comparisons. The original evidence remains preserved.

Runtime study [20261023T195451-3b044a17725d4c52ad361153b686386b](runtime-20261023T195451-3b044a17725d4c52ad361153b686386b.md) retains the comparison and raw native receipts. Its interpretation explicitly preserves unstable/compilation-contaminated results as inconclusive.

Runtime study [20261023T195451-3b044a17725d4c52ad361153b686386b](runtime-20261023T195451-3b044a17725d4c52ad361153b686386b-reviewed.md) retains the comparison and raw native receipts. Its interpretation explicitly preserves unstable/compilation-contaminated results as inconclusive.

Final local repair-phase qualification is green: six builds and Full x64 Debug, Release and ASan Debug, with final tooling/prose checks in LocalValidation.json. The completed plan is under Specs/Plans/Done. Native ARM64 execution and real hosted provisioning remain separate gates. The preserved optimizer-only study and its performance limitations remain unchanged.
