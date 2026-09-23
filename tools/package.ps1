# Package the plugin for a nppPluginList pull request.
#
# Produces build\BatPathIntelliSense_x64.zip and prints the JSON entry to paste
# into nppPluginList's src/pl.x64.json.
#
# The version is read back OUT of the built DLL rather than duplicated here, so
# the JSON entry can never drift from the binary -- that mismatch is exactly what
# nppPluginList's validator rejects.
#
# ASCII only: Windows PowerShell reads a BOM-less .ps1 in the OEM codepage.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

# ---------------------------------------------------------------- identity --
# EDIT THESE to match where you actually publish. The repository URL below is
# built from GitHubOwner + RepoName + version, and must point at the release
# asset, not at the repo page.
$FolderName  = 'BatPathIntelliSense'
$DisplayName = 'BAT Path IntelliSense'
$Author      = 'nicehero'
$GitHubOwner = 'nicehero'
$RepoName    = 'NppBatPathIntelliSense'
$Description = 'File and directory path completion for Windows .bat / .cmd files. Typing .\ ..\ C:\ or \\server\share\ pops up a candidate list, inserts a trailing backslash for directories, and drills into the next level. Ported from the VS Code extension bat-path-intellisense.'
# ---------------------------------------------------------------------------

$root     = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'build'
$dll      = Join-Path $buildDir "$FolderName.dll"
$stage    = Join-Path $buildDir 'package'
$zipName  = "${FolderName}_x64.zip"
$zip      = Join-Path $buildDir $zipName

if (-not (Test-Path $dll)) {
    throw "DLL not found: $dll -- run build.bat first."
}

# ------------------------------------------------------------- version ----
# validator.py reads FileVersionMS/FileVersionLS and compares against the JSON
# "version" padded with ".0" up to four components. Derive the JSON form so the
# two always agree.
$v = (Get-Item $dll).VersionInfo.FileVersionRaw
if ($null -eq $v) {
    throw "The DLL carries no version resource. nppPluginList requires one."
}

$dllVersion  = $v.ToString()
$jsonVersion = if ($v.Revision -eq 0) {
    "$($v.Major).$($v.Minor).$($v.Build)"
} else {
    "$($v.Major).$($v.Minor).$($v.Build).$($v.Revision)"
}

# Mirror validator.py: version + (3 - version.count('.')) * ".0"
# Keep the string on the LEFT of the repeat -- "1 * '.0'" would go numeric and
# coerce ".0" to 0, silently producing a wrong padding count.
$padCount = 3 - ($jsonVersion.Split('.').Count - 1)
if ($padCount -lt 0) { $padCount = 0 }
$expectedFromJson = $jsonVersion + ('.0' * $padCount)

if ($expectedFromJson -ne $dllVersion) {
    throw "Version mismatch: DLL is $dllVersion, JSON form $jsonVersion would validate as $expectedFromJson."
}

Write-Output "DLL version : $dllVersion  (JSON field: $jsonVersion)"

# ------------------------------------------------------------------ zip ----
# The DLL must sit at the ROOT of the zip. validator.py matches the entry name
# against "<folder-name>.dll" with no path prefix, so nesting it in a folder
# fails the check.
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $dll (Join-Path $stage "$FolderName.dll") -Force

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force

# Verify the layout the same way the validator does.
$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try {
    $entries = @($archive.Entries | ForEach-Object { $_.FullName })
}
finally {
    $archive.Dispose()
}

Write-Output "zip entries : $($entries -join ', ')"

$want = "$FolderName.dll"
if (-not ($entries | Where-Object { $_ -ieq $want })) {
    throw "The zip does not contain $want at its root. Validator would reject it."
}
if ($entries.Count -ne 1) {
    Write-Output "NOTE: zip has more than one entry -- only the DLL is expected."
}

# ----------------------------------------------------------------- hash ----
# Plain .NET rather than Get-FileHash: when launched from cmd via powershell.exe
# the Utility module was not resolving here, and this has no module dependency.
$sha256 = [System.Security.Cryptography.SHA256]::Create()
try {
    $stream = [System.IO.File]::OpenRead($zip)
    try { $digest = $sha256.ComputeHash($stream) }
    finally { $stream.Dispose() }
}
finally { $sha256.Dispose() }

$hash = -join ($digest | ForEach-Object { $_.ToString('x2') })
Write-Output "sha256      : $hash"

# ------------------------------------------------------------- json entry --
$repository = "https://github.com/$GitHubOwner/$RepoName/releases/download/v$jsonVersion/$zipName"
$homepage   = "https://github.com/$GitHubOwner/$RepoName"

$entry = [ordered]@{
    'folder-name'  = $FolderName
    'display-name' = $DisplayName
    'version'      = $jsonVersion
    'id'           = $hash
    'repository'   = $repository
    'description'  = $Description
    'author'       = $Author
    'homepage'     = $homepage
}

$json = $entry | ConvertTo-Json -Depth 3
$jsonPath = Join-Path $buildDir 'pl.x64.entry.json'
[IO.File]::WriteAllText($jsonPath, $json, (New-Object Text.UTF8Encoding $false))

Write-Output ""
Write-Output "=== paste into nppPluginList src/pl.x64.json ==="
Write-Output $json
Write-Output ""
Write-Output "written to: $jsonPath"
Write-Output ""
Write-Output "=== next steps ==="
Write-Output "1. Upload $zipName as an asset on GitHub release tag v$jsonVersion"
Write-Output "   verify the URL below returns the zip, not an HTML page:"
Write-Output "   $repository"
Write-Output "2. Fork notepad-plus-plus/nppPluginList, add the entry above to"
Write-Output "   src/pl.x64.json (inside the \"npp-plugins\" array), then open a PR."
Write-Output "3. x64-only is fine -- the lists are independent."
