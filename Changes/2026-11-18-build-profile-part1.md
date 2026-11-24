- Moved the build profile out of the 32 project files into `Directory.Build.props`: the toolset (`v145`), the
  Windows SDK (`10.0.26100.0`), the Unicode character set, the debug-library choice, the runtime library (`/MDd`,
  `/MD`) and the optimization level (`/Od`, `/O2`) (`RedHome` convergence CV-6-04 part 1, register C-11). A project
  file keeps only what is its own: the three projects that link the static runtime and the one that optimizes for
  size still say so. `Build/NativeBuildPolicy.targets` stays equal to the copy in the pinned DxUi commit, which
  `restore-dxui.ps1` requires; it takes DxUi's newer text with the next DxUi pin.
  MSBuild evaluated all 32 projects in all six profiles before and after the move. The build properties
  and the compiler, linker and project-reference metadata matched across all 192 comparisons.
