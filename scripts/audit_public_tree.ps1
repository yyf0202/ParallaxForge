param(
  [Parameter(Mandatory=$true)][string]$Root,
  [string]$DenyListFile = ''
)

$pathSeparatorPattern = '[\\/]'
$blockedPatterns = @(
  ('(?i)\b[A-Z]:' + $pathSeparatorPattern),
  ('(?i)' + '\\' + 'Users' + '\\'),
  ('(?i)(corp|internal)' + '\.'),
  ('(?i)' + 'git' + '@')
)

$rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
$denyListPath = $null
if ($DenyListFile) {
  $denyListPath = (Resolve-Path -LiteralPath $DenyListFile).Path
  $blockedPatterns += Get-Content -LiteralPath $denyListPath -Encoding utf8 |
    Where-Object { $_.Length -gt 0 } | ForEach-Object { [regex]::Escape($_) }
}

$gitTopLevel = git -C $rootPath rev-parse --show-toplevel 2>$null
$isRepositoryRoot = $LASTEXITCODE -eq 0 -and
    [string]::Equals(
      (Resolve-Path -LiteralPath $gitTopLevel).Path.TrimEnd('\', '/'),
      $rootPath,
      [System.StringComparison]::OrdinalIgnoreCase)

if ($isRepositoryRoot) {
  $files = git -C $rootPath -c core.quotePath=false ls-files | ForEach-Object {
    Get-Item -LiteralPath (Join-Path $rootPath $_) -ErrorAction SilentlyContinue
  }
} else {
  $files = Get-ChildItem -LiteralPath $rootPath -Recurse -File
}

if ($denyListPath) {
  $rootPrefix = $rootPath + [System.IO.Path]::DirectorySeparatorChar
  $denyListIsInsideRoot = $denyListPath.StartsWith(
    $rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)
  if ($denyListIsInsideRoot) {
    $files = $files | Where-Object {
      -not [string]::Equals(
        $_.FullName, $denyListPath, [System.StringComparison]::OrdinalIgnoreCase)
    }
  }
}

$hits = @(foreach ($file in $files) {
  if ($file.Extension -in @('.exe','.dll','.lib','.pdb','.zip','.7z')) {
    $file.FullName
    continue
  }
  $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding utf8 -ErrorAction SilentlyContinue
  foreach ($pattern in $blockedPatterns) {
    if ($null -ne $text -and $text -match $pattern) {
      "$($file.FullName): $pattern"
    }
  }
})

if ($isRepositoryRoot) {
  $expectedIdentity = 'ParallaxForge Maintainers <opensource@parallaxforge.dev>'
  $metadata = git -C $rootPath log --all --format='%an <%ae>|%cn <%ce>'
  if ($LASTEXITCODE -ne 0) {
    $hits += 'failed to inspect reachable Git history'
  } else {
    foreach ($entry in $metadata) {
      $identities = $entry -split '\|', 2
      if ($identities.Count -ne 2 -or
          $identities[0] -cne $expectedIdentity -or
          $identities[1] -cne $expectedIdentity) {
        $hits += "Git metadata does not match the public maintainer identity: $entry"
      }
    }
  }
}

if ($hits) {
  $hits | ForEach-Object { Write-Error $_ }
  exit 1
}
Write-Output 'public-tree audit passed'
