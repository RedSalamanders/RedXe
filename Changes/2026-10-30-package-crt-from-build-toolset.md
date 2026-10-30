- Package the Visual C++ runtime of the toolset that compiled the binaries. `package.ps1` took the newest CRT under
  Program Files, so on hosted runners, where the required MSVC 14.52 is installed under `RUNNER_TEMP` beside the image's
  older stock Visual Studio, a production ZIP would have shipped the stock 14.51 runtime with 14.52-built binaries.
  The CRT now comes from the installation of the MSBuild the build runs (`Find-RedXeMSBuild`), and packaging fails when
  it is older than `RequiredMsvcToolsVersion`. `PackagingTests.ps1` covers the default root, the floor and the wiring;
  each check fails against the old lookup. Not yet seen in a hosted production run.
