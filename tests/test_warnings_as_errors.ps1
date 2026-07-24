param(
  [Parameter(Mandatory=$true)][string]$SourceRoot,
  [Parameter(Mandatory=$true)][string]$BinaryRoot
)

$ErrorActionPreference = 'Stop'
foreach ($setting in @('ON', 'OFF')) {
  $buildDirectory = Join-Path $BinaryRoot $setting.ToLowerInvariant()
  cmake -S $SourceRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A x64 `
    -DBUILD_TESTING=OFF "-DPARALLAX_FORGE_WARNINGS_AS_ERRORS=$setting" | Out-Null
  if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed for warnings-as-errors=$setting"
  }
  $project = Get-Content -LiteralPath (Join-Path $buildDirectory 'src/parallax_forge_core.vcxproj') -Raw
  $hasWarningsAsErrors = $project.Contains('/WX') -or
      $project.Contains('<TreatWarningAsError>true</TreatWarningAsError>')
  if (($setting -eq 'ON') -ne $hasWarningsAsErrors) {
    throw "MSVC /WX state did not match PARALLAX_FORGE_WARNINGS_AS_ERRORS=$setting"
  }
}
