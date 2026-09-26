# Development checks

From the repository root:

```powershell
node protocol/scripts/validate_protocol_artifacts.mjs
node protocol/scripts/generate_protocol_docs.mjs
wsl bash protocol/scripts/run_native_tests_wsl.sh
wsl bash node/scripts/run_native_tests_wsl.sh
```

Web UI tests and build are in the [gateway README](gateway/README.md#web-ui-filesystem).

Enable the tracked pre-commit hook once per clone:

```powershell
git config core.hooksPath .githooks
```

The hook validates changed protocol artifacts. It regenerates diagrams only when the staged manifest or diagram generator changed, and stops the commit if the generated SVG files need to be staged.
