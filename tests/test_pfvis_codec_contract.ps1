param(
  [Parameter(Mandatory = $true)]
  [string]$Source
)

$contents = Get-Content -LiteralPath $Source -Raw
$objectCountOffset = $contents.IndexOf('const auto object_count = ReadU32(bytes, offset);')
$reserveOffset = $contents.IndexOf('objects.reserve(object_count);')
$minimumObjectTableCheck = $contents.IndexOf(
  'RequireElements(bytes, offset, object_count, 8, "truncated PFVIS object data");')

if ($objectCountOffset -lt 0 -or $reserveOffset -lt 0 -or
    $minimumObjectTableCheck -lt $objectCountOffset -or
    $minimumObjectTableCheck -gt $reserveOffset) {
  throw 'PFVIS must bound-check every v2 object minimum before reserving untrusted object_count'
}

Write-Output 'PFVIS v2 object-count allocation contract passed'
