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
if ($source -match '\buint64_t\b') {
  throw 'voxel dilation must not require native Int64 shader operations'
}
if ($source -notmatch
    'uint2\s+SquareUnsigned\s*\(\s*uint\s+value\s*\)' -or
    $source -notmatch
    'uint2\s+AddUnsigned64\s*\(\s*uint2\s+left\s*,\s*uint2\s+right\s*\)' -or
    $source -notmatch
    'bool\s+IsGreaterUnsigned64\s*\(\s*uint2\s+left\s*,\s*uint2\s+right\s*\)') {
  throw 'voxel dilation must implement exact 64-bit comparisons from 32-bit shader operations'
}
if ($source -notmatch
    'uint2\s+radiusSquared\s*=\s*SquareUnsigned\(DilationRadius\)' -or
    $source -notmatch
    'IsGreaterUnsigned64\(distanceSquared,\s*radiusSquared\)') {
  throw 'voxel dilation must compare exact 32-bit-pair squared distances'
}

Write-Output 'voxel shader clamp contract passed'
