param(
[string]$version
)
$file = 'UVAtlasTool\uvatlas.rc'
$versionComma = $version.Replace(".", ",")
(Get-Content $file).replace('1,0,0,0', $versionComma).replace('1.0.0.0', $version) | Set-Content $file
(Get-Content $file)