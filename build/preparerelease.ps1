<#

.SYNOPSIS
Prepares a PR for release

.DESCRIPTION
This script is used to do the edits required for preparing a release PR.

.PARAMETER TargetBranch
This the branch to use as the base of the release. Defaults to 'main'.

.PARAMETER UpdateVersion
This is a $true or $false value that indicates if the library version should be incremented. Defaults to $true. 

.LINK
https://github.com/microsoft/UVAtlas/wiki

#>

param(
    [string]$TargetBranch = "main",
    [bool]$UpdateVersion = $true
)

$reporoot = Split-Path -Path $PSScriptRoot -Parent
$cmake = $reporoot + "\CMakeLists.txt"
$header = $reporoot + "\UVAtlas\inc\UVAtlas.h"
$readme = $reporoot + "\README.md"
$history = $reporoot + "\CHANGELOG.md"

if ((-Not (Test-Path $cmake)) -Or (-Not (Test-Path $header)) -Or (-Not (Test-Path $readme)) -Or (-Not (Test-Path $history)))
{
    Write-Error "ERROR: Unexpected location of script file!" -ErrorAction Stop
}

$branch = git branch --show-current
if ($branch -ne $TargetBranch)
{
    Write-Error "ERROR: Must be in the $TargetBranch branch!" -ErrorAction Stop
}

git pull -q
if ($LastExitCode -ne 0)
{
    Write-Error "ERROR: Failed to sync branch!" -ErrorAction Stop
}

$newreleasedate = Get-Date -Format "MMMM d, yyyy"
$newreleasetag = (Get-Date -Format "MMMyyyy").ToLower()

$version = Get-Content ($cmake) | Select-String -Pattern "set\(UVATLAS_VERSION" -CaseSensitive
$version -match "([0-9]?\.[0-9]?\.[0-9]?)" > $null
$version = $Matches.0
$rawversion = $version.replace('.','')

if($UpdateVersion) {
    [string]$newrawversion = ([int]$rawversion + 1)
}
else {
    $newrawversion = $rawversion
}

$newversion = $newrawversion[0] + "." + $newrawversion[1] + "." + $newrawversion[2]

$rawreleasedate = $(Get-Content $readme) | Select-String -Pattern "\*\*[A-Z][a-z]+\S.\d+,?\S.\d\d\d\d\*\*"
$releasedate = $rawreleasedate -replace '\*',''

Write-Host "     Old Version: " $version
Write-Host "Old Release Date: " $releasedate
Write-Host "->"
Write-Host "    Release Date: " $newreleasedate
Write-Host "     Release Tag: " $newreleasetag
Write-Host " Release Version: " $newversion

if($UpdateVersion) {
    (Get-Content $cmake).Replace("set(UVATLAS_VERSION $version)","set(UVATLAS_VERSION $newversion)") | Set-Content $cmake
    (Get-Content $header).Replace("#define UVATLAS_VERSION $rawversion","#define UVATLAS_VERSION $newrawversion") | Set-Content $header
}

(Get-Content $readme).Replace("$rawreleasedate", "**$newreleasedate**") | Set-Content $readme

Get-ChildItem -Path ($reporoot + "\.nuget") -Filter *.nuspec | Foreach-Object {
    (Get-Content -Path $_.Fullname).Replace("$releasedate", "$newreleasedate") | Set-Content -Path $_.Fullname -Encoding utf8
    }

[System.Collections.ArrayList]$file = Get-Content $history
$inserthere = @()

for ($i=0; $i -lt $file.count; $i++) {
    if ($file[$i] -match "## Release History") {
        $inserthere += $i + 1
    }
}

$file.insert($inserthere[0], "`n### $newreleasedate`n* change history here")
Set-Content -Path $history -Value $file
