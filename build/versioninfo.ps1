<#
Copyright (c) Microsoft Corporation.
Licensed under the MIT License.
#>

param(
[string]$version
)
$versionComma = $version.Replace(".", ",")
$files = 'UVAtlasTool\uvatlas.rc', 'build\UVAtlas.rc.in'
foreach ($file in $files) { (Get-Content $file).replace('1,0,0,0', $versionComma).replace('1.0.0.0', $version) | Set-Content $file }
