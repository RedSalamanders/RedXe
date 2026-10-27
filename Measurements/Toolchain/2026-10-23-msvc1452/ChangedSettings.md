# RedXe before/after tool metadata changes

This table groups every changed captured field by tool. changed-tool-settings.csv maps each change to project/configuration/platform and full before/after values. The comparison uses each project/profile field value set; full-tool-metadata.csv.gz retains exact per-file assignments. Literal unset/default differences and dependency-output fingerprints are preserved, not replaced with assumed compiler behavior. No native source files changed.

| Tool / field | Changed project-profile rows | Configurations | Before values | After values |
| --- | ---: | --- | --- | --- |
| ClCompile / AdditionalIncludeDirectories | 18 | ASan Debug, Debug, Release | project/file-specific; see CSV | project/file-specific; see CSV |
| ClCompile / ForcedIncludeFiles | 6 | ASan Debug | Z:\src\RedXe\.build\dependencies\DxUi\source\fa4da2cb112e029646835f81f14ee2e0185129a7\src/Support/RequireAddressSanitizer.h;&lt;REPO&gt;\Common\RequireAddressSanitizer.h; | &lt;REPO&gt;\.build\dependencies\DxUi\source\fa4da2cb112e029646835f81f14ee2e0185129a7\src/Support/RequireAddressSanitizer.h;&lt;REPO&gt;\Common\RequireAddressSanitizer.h; |
| ClCompile / FunctionLevelLinking | 8 | Release | &lt;unset&gt; | true |
| ClCompile / WholeProgramOptimization | 60 | Release | false | true |
| Lib / LinkTimeCodeGeneration | 60 | Release | &lt;unset&gt; | true |
| Link / LinkTimeCodeGeneration | 60 | Release | &lt;unset&gt; | UseLinkTimeCodeGeneration |
