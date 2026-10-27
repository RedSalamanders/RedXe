# RedXe C++ compilation and linkage audit

Date: 2026-10-23. All tracked vcxproj files, x64/ARM64, Debug/Release/ASan Debug; effective MSBuild evaluation with full compressed metadata retained. This is metadata evidence; native build and runtime qualification are separate.

Both baseline and candidate select installed default MSVC 14.52.37025 with v145 and SDK 10.0.26100.0. An older toolset is unavailable locally. The intended experiment changes Release optimization policy, not compiler version or the adopted DxUi API pin.

Release explicitly requires /Gy /GL and final /OPT:REF /LTCG, with ordinary incremental linking disabled. Debug and ASan diagnostic optimization remain unchanged. Root late imports are intentional: setting WPO too early permits the MSVC delayed WPO import to restore incremental LTCG.

## C++ settings after implementation

The table includes every distinct effective value of each compiler field across all projects and both platforms. all-settings.csv.gz maps every value to its exact project/configuration/platform/files and includes all linker, librarian and property fields on both sides. Compressed settings-before/after.json.gz preserve full paths, project references and source inventories.

| Setting | Debug | Release | ASan Debug |
| --- | --- | --- | --- |
| AdditionalOptions | /utf-8 /permissive- /Zc:preprocessor  | /utf-8 /permissive- /Zc:preprocessor  | /utf-8 /permissive- /Zc:preprocessor  |
| BasicRuntimeChecks | EnableFastChecks | Default | Default |
| BufferSecurityCheck | true | true | true |
| ConformanceMode | true | true | true |
| DebugInformationFormat | EditAndContinue | ProgramDatabase | ProgramDatabase |
| EnableASAN | false | false | true |
| EnableEnhancedInstructionSet | NotSet | NotSet | NotSet |
| ExceptionHandling | Sync | Sync | Sync |
| ExternalWarningLevel | InheritWarningLevel | InheritWarningLevel | InheritWarningLevel |
| FavorSizeOrSpeed | Neither | Neither | Neither |
| FloatingPointModel | Precise | Precise | Precise |
| FunctionLevelLinking | <unset> | true | <unset> |
| IntrinsicFunctions | false | false; true | false |
| LanguageStandard | stdcpplatest | stdcpplatest | stdcpplatest |
| MultiProcessorCompilation | true | true | true |
| OmitFramePointers | <unset>; false | <unset>; false | <unset>; false |
| Optimization | Disabled | MaxSpeed; MinSpace | Disabled |
| PrecompiledHeader | NotUsing | NotUsing | NotUsing |
| RuntimeLibrary | MultiThreadedDebug; MultiThreadedDebugDLL | MultiThreaded; MultiThreadedDLL | MultiThreadedDebugDLL |
| SDLCheck | true | true | true |
| TreatWarningAsError | true | true | true |
| WarningLevel | Level4 | Level4 | Level4 |
| WholeProgramOptimization | false | true | false |

Definitions, warning-number suppressions and include search paths are file-specific and retained verbatim in the attached data. They are not flattened into an assertion of identical build behavior.

## Link and archive settings after implementation

| Setting | Debug | Release | ASan Debug |
| --- | --- | --- | --- |
| OptimizeReferences | <unset> | true | <unset> |
| LinkTimeCodeGeneration | <unset> | UseLinkTimeCodeGeneration | <unset> |
| EnableCOMDATFolding | <unset> | true | <unset> |
| GenerateDebugInformation | true | true | true |
| SubSystem | Console; Windows | Console; Windows | Console; Windows |
| TargetMachine | MachineARM64; MachineX64 | MachineARM64; MachineX64 | MachineARM64; MachineX64 |
| DataExecutionPrevention | true | true | true |
| RandomizedBaseAddress | true | true | true |
| HighEntropyVA | <unset> | <unset> | <unset> |
| ControlFlowGuard | <unset> | <unset> | <unset> |
| AdditionalOptions | <unset> | <unset> | <unset> |
| IgnoreAllDefaultLibraries | <unset> | <unset> | <unset> |
| AdditionalDependencies | See exact per-project CSV | See exact per-project CSV | See exact per-project CSV |

A .lib is an archive; /OPT:REF removes unreferenced COMDATs at final EXE/DLL links. LTO cannot optimize through an independent DLL boundary. Referenced factories/exports and /INCLUDE roots can retain otherwise unused functionality. No claim is made that every unused DxUi control disappears.

## Interpretation and sources

Unset Gy metadata can still mean /Gy through /O1 or /O2. Unset REF with /DEBUG instead defaults to NOREF. Existing ICF, warning, ISA, exception, size/speed, CRT and security choices remain project-specific. Runtime speed and final size require measured results; larger GL archives are expected to carry optimizer data.

Microsoft references: [GL](https://learn.microsoft.com/en-us/cpp/build/reference/gl-whole-program-optimization), [LTCG](https://learn.microsoft.com/en-us/cpp/build/reference/ltcg-link-time-code-generation), [OPT](https://learn.microsoft.com/en-us/cpp/build/reference/opt-optimizations), [exact tool compatibility](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017).

## Final ARM64 compiler-host correction

A final 192-profile evaluation after the C1060 correction is retained in settings-final-host64.json.gz and raw-msbuild-final-host64.zip. On this x64 machine, every ARM64 profile selects PreferredToolArchitecture=x64. The x64 target retains its existing host-selection policy; this metadata query uses 64-bit MSBuild, so its evaluated x64 host is also x64. Actual build invocation and dependency identity receipts remain authoritative for the host used by a native build. A native ARM64 machine selects the arm64 host; an explicit caller override is preserved. The restore identity probe and project-reference forwarding use the same host. All three ARM64 native builds subsequently passed. This changes the compiler process used for cross-compilation, not the x64 before/after runtime experiment or the adopted DxUi API pin.
