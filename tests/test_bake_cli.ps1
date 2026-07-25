param(
  [Parameter(Mandatory=$true)][string]$BakeExe,
  [Parameter(Mandatory=$true)][string]$DemoConfig
)

$ErrorActionPreference = 'Stop'

& $BakeExe --config $DemoConfig --validate
if ($LASTEXITCODE -ne 0) {
  throw 'validate command failed'
}

Write-Output 'forge-bake validate CLI contract passed'
