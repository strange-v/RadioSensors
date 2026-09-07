# Repository agent notes

## Node.js commands on Windows

The user's npm installation is valid, but its system shim resolves the configured
global prefix through `%APPDATA%\npm`. Sandboxed agent commands cannot read that
directory. In PowerShell, run the system npm CLI directly instead of invoking
`npm`, `npm.cmd`, or `pnpm`:

```powershell
& "$env:ProgramFiles\nodejs\node.exe" "$env:ProgramFiles\nodejs\node_modules\npm\bin\npm-cli.js" <arguments>
```

For example, use the same command with `test`, `run typecheck`, `run build`, or
`ci` as its arguments. Do not switch this npm-managed workspace to pnpm.
