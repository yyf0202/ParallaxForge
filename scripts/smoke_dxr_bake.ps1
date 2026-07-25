param(
  [Parameter(Mandatory=$true)][string]$BakeExe,
  [Parameter(Mandatory=$true)][string]$Config,
  [string]$WorkingRoot = (Get-Location).Path,
  [string]$ReaderExe = ''
)

$ErrorActionPreference = 'Stop'

function Assert-StrictDescendant {
  param(
    [Parameter(Mandatory=$true)][string]$Child,
    [Parameter(Mandatory=$true)][string]$Parent,
    [Parameter(Mandatory=$true)][string]$Description
  )

  $normalizedChild = [System.IO.Path]::GetFullPath($Child).TrimEnd('\', '/')
  $normalizedParent = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
  $parentPrefix = $normalizedParent + [System.IO.Path]::DirectorySeparatorChar
  if (-not $normalizedChild.StartsWith(
      $parentPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "$Description must be strictly below $normalizedParent`: $normalizedChild"
  }
}

function Assert-NoReparsePoint {
  param(
    [Parameter(Mandatory=$true)][string]$Path,
    [Parameter(Mandatory=$true)][string]$Root
  )

  $currentPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  $rootPath = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  while ($true) {
    if (Test-Path -LiteralPath $currentPath) {
      $item = Get-Item -LiteralPath $currentPath -Force
      if (($item.Attributes -band
          [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "smoke cleanup path contains a reparse point: $currentPath"
      }
    }
    if ([string]::Equals(
        $currentPath, $rootPath,
        [System.StringComparison]::OrdinalIgnoreCase)) {
      break
    }
    $parentPath = Split-Path -Parent $currentPath
    if ([string]::IsNullOrWhiteSpace($parentPath) -or
        [string]::Equals(
          $parentPath, $currentPath,
          [System.StringComparison]::OrdinalIgnoreCase)) {
      throw "smoke cleanup path does not reach the supplied working root"
    }
    $currentPath = $parentPath.TrimEnd('\', '/')
  }
}

$bakePath = (Resolve-Path -LiteralPath $BakeExe).Path
$configPath = (Resolve-Path -LiteralPath $Config).Path
$workingRootPath = (Resolve-Path -LiteralPath $WorkingRoot).Path
$configDocument = Get-Content -LiteralPath $configPath -Raw -Encoding utf8 |
  ConvertFrom-Json

if (-not $configDocument.output.write_json -or
    -not $configDocument.output.write_binary) {
  throw 'DXR smoke requires both JSON and PFVIS output to be enabled'
}

& $bakePath --config $configPath --validate
if ($LASTEXITCODE -ne 0) {
  throw "bake configuration validation failed with exit code $LASTEXITCODE"
}

$configuredOutput = [string]$configDocument.output.directory
if ([string]::IsNullOrWhiteSpace($configuredOutput) -or
    [System.IO.Path]::IsPathRooted($configuredOutput)) {
  throw 'configured output directory must be a non-empty relative path'
}

$configDirectory = Split-Path -Parent $configPath
$outputPath = [System.IO.Path]::GetFullPath(
  (Join-Path $configDirectory $configuredOutput))
Assert-StrictDescendant -Child $outputPath -Parent $workingRootPath `
  -Description 'resolved output directory'
Assert-StrictDescendant -Child $outputPath -Parent $configDirectory `
  -Description 'configured output directory'
Assert-NoReparsePoint -Path $outputPath -Root $workingRootPath

if (Test-Path -LiteralPath $outputPath) {
  $outputItem = Get-Item -LiteralPath $outputPath -Force
  if (-not $outputItem.PSIsContainer) {
    throw "configured output path is not a directory: $outputPath"
  }
  $resolvedOutputPath = (Resolve-Path -LiteralPath $outputPath).Path
  Assert-StrictDescendant -Child $resolvedOutputPath -Parent $workingRootPath `
    -Description 'resolved existing output directory'
  foreach ($generatedName in @('visibility.json', 'visibility.pfvis')) {
    $generatedPath = Join-Path $resolvedOutputPath $generatedName
    if (Test-Path -LiteralPath $generatedPath) {
      $generatedItem = Get-Item -LiteralPath $generatedPath -Force
      if ($generatedItem.PSIsContainer -or
          (($generatedItem.Attributes -band
            [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
        throw "generated output path is not a regular file: $generatedPath"
      }
      Remove-Item -LiteralPath $generatedPath -Force
    }
  }
  if (@(Get-ChildItem -LiteralPath $resolvedOutputPath -Force).Count -eq 0) {
    Remove-Item -LiteralPath $resolvedOutputPath -Force
  }
}

& $bakePath --config $configPath --bake
if ($LASTEXITCODE -ne 0) {
  throw "DXR bake failed with exit code $LASTEXITCODE"
}

$jsonPath = Join-Path $outputPath 'visibility.json'
$pfvisPath = Join-Path $outputPath 'visibility.pfvis'
if (-not (Test-Path -LiteralPath $jsonPath -PathType Leaf)) {
  throw "successful bake did not create JSON: $jsonPath"
}
if (-not (Test-Path -LiteralPath $pfvisPath -PathType Leaf)) {
  throw "successful bake did not create PFVIS: $pfvisPath"
}

$jsonDocument = Get-Content -LiteralPath $jsonPath -Raw -Encoding utf8 |
  ConvertFrom-Json
$configuredObjects = @($configDocument.world.objects |
  Sort-Object { [uint64]$_.object_id })
$jsonObjects = @($jsonDocument.objects)
if ($jsonObjects.Count -ne $configuredObjects.Count) {
  throw 'JSON object count does not match the bake configuration'
}

$configuredIds = @{}
for ($index = 0; $index -lt $configuredObjects.Count; ++$index) {
  $configuredObject = $configuredObjects[$index]
  $jsonObject = $jsonObjects[$index]
  $configuredId = [uint64]$configuredObject.object_id
  $jsonId = [uint64]$jsonObject.object_id
  if ($jsonId -ne $configuredId) {
    throw "JSON object ID $jsonId does not match configured public ID $configuredId"
  }
  $configuredLabel = if ($null -eq $configuredObject.label) {
    ''
  } else {
    [string]$configuredObject.label
  }
  if ([string]$jsonObject.label -cne $configuredLabel) {
    throw "JSON label for object $configuredId was not preserved"
  }
  $configuredIds[[string]$configuredId] = $true
}

$jsonProbes = @($jsonDocument.probes)
if ($jsonProbes.Count -eq 0) {
  throw 'successful bake produced no probes'
}
foreach ($probe in $jsonProbes) {
  foreach ($visibleIdValue in @($probe.visible_object_ids)) {
    $visibleId = [string][uint64]$visibleIdValue
    if (-not $configuredIds.ContainsKey($visibleId)) {
      throw "probe $($probe.probe_id) contains non-public object ID $visibleId"
    }
  }
}

if ([string]::IsNullOrWhiteSpace($ReaderExe)) {
  $buildRoot = $bakePath
  for ($level = 0; $level -lt 4; ++$level) {
    $buildRoot = Split-Path -Parent $buildRoot
  }
  $configurationName = Split-Path -Leaf (Split-Path -Parent $bakePath)
  $ReaderExe = Join-Path $buildRoot (
    "tests\$configurationName\parallax_forge_test_pfvis_codec.exe")
}
$readerPath = (Resolve-Path -LiteralPath $ReaderExe).Path

& $readerPath --compare-visibility $jsonPath $pfvisPath
if ($LASTEXITCODE -ne 0) {
  throw "JSON/PFVIS comparison failed with exit code $LASTEXITCODE"
}

Write-Output (
  "DXR smoke passed: {0} objects, {1} probes" -f
    $jsonObjects.Count, $jsonProbes.Count)
