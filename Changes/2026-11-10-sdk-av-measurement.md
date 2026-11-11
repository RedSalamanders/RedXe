- Fix offscreen AV measurements after a verified DxUi SDK build by reopening its original dependency inputs and preserving module, archive and measurement provenance checks.
  Freeze the wrapper and local build/provenance helpers before execution and reject changed tooling, binary, fixture or dependency inputs after finalization before publishing provenance.
