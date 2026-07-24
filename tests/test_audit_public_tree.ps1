param(
  [Parameter(Mandatory=$true)][string]$AuditScript
)

$ErrorActionPreference = 'Stop'
$fixtureRoot = Join-Path ([System.IO.Path]::GetTempPath()) 'parallax_forge_audit_test'
if (Test-Path -LiteralPath $fixtureRoot) {
  Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null

function Invoke-Audit {
  param(
    [Parameter(Mandatory=$true)][string]$Root,
    [string]$DenyListFile = ''
  )
  $arguments = @('-NoProfile', '-File', $AuditScript, '-Root', $Root)
  if ($DenyListFile) {
    $arguments += @('-DenyListFile', $DenyListFile)
  }
  $previousErrorAction = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  & powershell @arguments 2>&1 | Out-Null
  $exitCode = $LASTEXITCODE
  $ErrorActionPreference = $previousErrorAction
  return $exitCode
}

try {
  $externalRoot = Join-Path $fixtureRoot 'external'
  New-Item -ItemType Directory -Path $externalRoot | Out-Null
  $blockedPath = 'C:' + '\Us' + 'ers\fixture\artifact'
  Set-Content -LiteralPath (Join-Path $externalRoot 'blocked.txt') -Value $blockedPath
  if ((Invoke-Audit -Root $externalRoot) -eq 0) {
    throw 'external non-git fixture was not scanned'
  }

  $repositoryRoot = Join-Path $fixtureRoot 'repository'
  New-Item -ItemType Directory -Path $repositoryRoot | Out-Null
  git -C $repositoryRoot init -b fixture *> $null
  git -C $repositoryRoot config user.name 'ParallaxForge Maintainers'
  git -C $repositoryRoot config user.email 'opensource@parallaxforge.dev'
  git -C $repositoryRoot config commit.gpgsign false

  $denyList = Join-Path $repositoryRoot 'audit-denylist.txt'
  Set-Content -LiteralPath $denyList -Value 'private-marker'
  Set-Content -LiteralPath (Join-Path $repositoryRoot '.gitignore') -Value 'scratch.txt'
  Set-Content -LiteralPath (Join-Path $repositoryRoot 'tracked.txt') -Value 'public text'
  git -C $repositoryRoot add .gitignore audit-denylist.txt tracked.txt
  git -C $repositoryRoot commit -m 'clean fixture' *> $null

  Set-Content -LiteralPath (Join-Path $repositoryRoot 'scratch.txt') -Value $blockedPath
  if ((Invoke-Audit -Root $repositoryRoot -DenyListFile $denyList) -ne 0) {
    throw 'ignored scratch or the deny-list file itself affected a tracked-file audit'
  }

  Set-Content -LiteralPath (Join-Path $repositoryRoot 'tracked.txt') -Value 'private-marker'
  if ((Invoke-Audit -Root $repositoryRoot -DenyListFile $denyList) -eq 0) {
    throw 'another tracked file was not checked against an in-root deny-list'
  }
  Set-Content -LiteralPath (Join-Path $repositoryRoot 'tracked.txt') -Value 'public text'

  Set-Content -LiteralPath (Join-Path $repositoryRoot 'history.txt') -Value 'history metadata fixture'
  git -C $repositoryRoot add history.txt
  $env:GIT_AUTHOR_NAME = 'Different Author'
  $env:GIT_AUTHOR_EMAIL = 'different@example.invalid'
  $env:GIT_COMMITTER_NAME = 'ParallaxForge Maintainers'
  $env:GIT_COMMITTER_EMAIL = 'opensource@parallaxforge.dev'
  git -C $repositoryRoot commit -m 'bad author fixture' *> $null
  Remove-Item Env:GIT_AUTHOR_NAME, Env:GIT_AUTHOR_EMAIL, Env:GIT_COMMITTER_NAME, Env:GIT_COMMITTER_EMAIL
  if ((Invoke-Audit -Root $repositoryRoot -DenyListFile $denyList) -eq 0) {
    throw 'different reachable Git metadata was not rejected'
  }

  $env:GIT_AUTHOR_NAME = 'ParallaxForge Maintainers'
  $env:GIT_AUTHOR_EMAIL = 'opensource@parallaxforge.dev'
  $env:GIT_COMMITTER_NAME = 'Different Committer'
  $env:GIT_COMMITTER_EMAIL = 'different@example.invalid'
  git -C $repositoryRoot commit --amend --no-edit --reset-author *> $null
  Remove-Item Env:GIT_AUTHOR_NAME, Env:GIT_AUTHOR_EMAIL, Env:GIT_COMMITTER_NAME, Env:GIT_COMMITTER_EMAIL
  if ((Invoke-Audit -Root $repositoryRoot -DenyListFile $denyList) -eq 0) {
    throw 'different reachable committer metadata was not rejected'
  }
} finally {
  Remove-Item Env:GIT_AUTHOR_NAME, Env:GIT_AUTHOR_EMAIL, Env:GIT_COMMITTER_NAME, Env:GIT_COMMITTER_EMAIL -ErrorAction SilentlyContinue
  if (Test-Path -LiteralPath $fixtureRoot) {
    Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
  }
}
