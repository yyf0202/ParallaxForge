param(
  [Parameter(Mandatory = $true)]
  [string]$Shader
)

$source = Get-Content -LiteralPath $Shader -Raw
if ($source -notmatch
    'float3\s+scaledGridMin\s*=\s*clamp\([\s\S]*?float3\(0\.0f,\s*0\.0f,\s*0\.0f\)[\s\S]*?maximumCoordinate\)') {
  throw 'voxel grid minimum is not clamped in the float domain before narrowing'
}
if ($source -notmatch
    'float3\s+scaledGridMax\s*=\s*clamp\([\s\S]*?float3\(0\.0f,\s*0\.0f,\s*0\.0f\)[\s\S]*?maximumCoordinate\)') {
  throw 'voxel grid maximum is not clamped in the float domain before narrowing'
}
if ($source -notmatch
    'uint3\s+unsignedGridMin\s*=\s*min\(uint3\(scaledGridMin\),\s*GridSize\s*-\s*1\)' -or
    $source -notmatch
    'uint3\s+unsignedGridMax\s*=\s*min\(uint3\(scaledGridMax\),\s*GridSize\s*-\s*1\)') {
  throw 'rounded float bounds are not constrained to the unsigned grid range'
}
if ($source -notmatch 'int3\s+gridMin\s*=\s*int3\(unsignedGridMin\)' -or
    $source -notmatch 'int3\s+gridMax\s*=\s*int3\(unsignedGridMax\)') {
  throw 'voxel grid coordinates are not narrowed after a safe unsigned clamp'
}

Write-Output 'voxel shader clamp contract passed'
