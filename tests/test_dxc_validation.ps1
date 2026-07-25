param(
  [Parameter(Mandatory=$true)][string]$SourceRoot,
  [Parameter(Mandatory=$true)][string]$BinaryRoot,
  [Parameter(Mandatory=$true)][string]$Generator,
  [string]$Architecture = ''
)

$ErrorActionPreference = 'Stop'

if (Test-Path -LiteralPath $BinaryRoot) {
  Remove-Item -LiteralPath $BinaryRoot -Recurse -Force
}
$fakeDxcDirectory = Join-Path $BinaryRoot 'not-an-executable'
New-Item -ItemType Directory -Path $fakeDxcDirectory | Out-Null
$fakeDxcFile = Join-Path $BinaryRoot 'not-dxc.exe'
Set-Content -LiteralPath $fakeDxcFile -Value 'not an executable'

function Assert-DxcRejected {
  param(
    [Parameter(Mandatory=$true)][string]$Candidate,
    [Parameter(Mandatory=$true)][string]$BuildName
  )

  $configureArguments = @(
    '-S', $SourceRoot,
    '-B', (Join-Path $BinaryRoot $BuildName),
    '-G', $Generator,
    '-DBUILD_TESTING=OFF',
    "-DPARALLAX_FORGE_DXC_EXECUTABLE=$Candidate"
  )
  if ($Architecture) {
    $configureArguments += @('-A', $Architecture)
  }

  $previousErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  $configureOutput = & cmake @configureArguments 2>&1
  $configureExit = $LASTEXITCODE
  $ErrorActionPreference = $previousErrorActionPreference
  if ($configureExit -eq 0) {
    throw "configuration accepted non-executable DXC candidate $Candidate"
  }
  if (($configureOutput -join "`n") -notmatch
      'PARALLAX_FORGE_DXC_EXECUTABLE must name an executable file') {
    throw "configuration failed without the executable-file instruction:`n$configureOutput"
  }
}

Assert-DxcRejected -Candidate $fakeDxcDirectory -BuildName 'directory-build'
Assert-DxcRejected -Candidate $fakeDxcFile -BuildName 'file-build'

Write-Output 'invalid DXC override was rejected'
