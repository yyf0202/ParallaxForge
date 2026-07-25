param(
  [Parameter(Mandatory=$true)][string]$FixtureRoot,
  [Parameter(Mandatory=$true)][string]$BinaryRoot,
  [Parameter(Mandatory=$true)][string]$HlslModule,
  [Parameter(Mandatory=$true)][string]$DxcExecutable,
  [Parameter(Mandatory=$true)][string]$Generator,
  [string]$Architecture = ''
)

$ErrorActionPreference = 'Stop'

if (Test-Path -LiteralPath $BinaryRoot) {
  Remove-Item -LiteralPath $BinaryRoot -Recurse -Force
}
$fixtureSource = Join-Path $BinaryRoot 'source'
$fixtureBuild = Join-Path $BinaryRoot 'build'
New-Item -ItemType Directory -Path $fixtureSource | Out-Null
Copy-Item -Path (Join-Path $FixtureRoot '*') -Destination $fixtureSource -Recurse

$configureArguments = @(
  '-S', $fixtureSource,
  '-B', $fixtureBuild,
  '-G', $Generator,
  "-DPARALLAX_FORGE_HLSL_MODULE=$HlslModule",
  "-DPARALLAX_FORGE_DXC_EXECUTABLE=$DxcExecutable"
)
if ($Architecture) {
  $configureArguments += @('-A', $Architecture)
}
& cmake @configureArguments
if ($LASTEXITCODE -ne 0) {
  throw 'shader fixture configuration failed'
}

& cmake --build $fixtureBuild --config Debug
if ($LASTEXITCODE -ne 0) {
  throw 'shader fixture default build failed'
}

$firstOutput = Join-Path $fixtureBuild 'shaders/fixture_first.dxil'
$secondOutput = Join-Path $fixtureBuild 'shaders/fixture_second.dxil'
foreach ($output in @($firstOutput, $secondOutput)) {
  if (-not (Test-Path -LiteralPath $output -PathType Leaf)) {
    throw "default build did not produce $output"
  }
  $magic = [System.Text.Encoding]::ASCII.GetString(
    [System.IO.File]::ReadAllBytes($output), 0, 4)
  if ($magic -cne 'DXBC') {
    throw "$output is not a binary DXIL container"
  }
}

$firstTimestamp = (Get-Item -LiteralPath $firstOutput).LastWriteTimeUtc
$secondTimestamp = (Get-Item -LiteralPath $secondOutput).LastWriteTimeUtc
Start-Sleep -Milliseconds 1200
Add-Content -LiteralPath (Join-Path $fixtureSource 'shared.hlsli') `
  -Value '// dependency rebuild marker'

& cmake --build $fixtureBuild --config Debug
if ($LASTEXITCODE -ne 0) {
  throw 'shader fixture rebuild failed'
}

$rebuiltFirstTimestamp = (Get-Item -LiteralPath $firstOutput).LastWriteTimeUtc
$rebuiltSecondTimestamp = (Get-Item -LiteralPath $secondOutput).LastWriteTimeUtc
if ($rebuiltFirstTimestamp -le $firstTimestamp) {
  throw 'changing an included .hlsli did not rebuild its HLSL library'
}
if ($rebuiltSecondTimestamp -ne $secondTimestamp) {
  throw 'unrelated HLSL library rebuilt after include-only change'
}

Write-Output 'default HLSL build, unique outputs, and include rebuild passed'
