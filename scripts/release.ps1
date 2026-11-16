# =============================================================================
#  release.ps1  --  builds, tests and packages a TcpTools release
#
#    scripts\release.ps1                 build Release|x64, run the tests, write
#                                        dist\TcpTools-<version>-windows-x64.zip
#    scripts\release.ps1 -SkipTests      the same without the end-to-end tests
#    scripts\release.ps1 -Publish        ... and create the GitHub release
#                                        v<version> with the zip (needs `gh`,
#                                        logged in; the tag must be pushed)
#
#  The version comes from Version.h. The GitHub workflow
#  .github\workflows\release.yml runs this script when a tag v* is pushed, so
#  pushing the tag is normally all a release takes.
# =============================================================================

param(
    [switch] $SkipTests,
    [switch] $Publish,
    [string] $Toolset = ""        # e.g. v143 to build with an older Visual Studio
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

# ---- Version ----------------------------------------------------------------
$m = Select-String -Path (Join-Path $root "Version.h") -Pattern 'TCPTOOLS_VERSION_STR\s+"([^"]+)"'
if (-not $m) { throw "TCPTOOLS_VERSION_STR not found in Version.h" }
$version = $m.Matches[0].Groups[1].Value
$tag     = "v$version"
Write-Host "TcpTools $version" -ForegroundColor Cyan

# ---- MSBuild ------------------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found - is Visual Studio (or its Build Tools) installed?" }
$msbuild = & $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild `
                      -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild.exe not found" }

$buildArgs = @("TcpTools.sln", "-t:Rebuild", "-p:Configuration=Release", "-p:Platform=x64", "-v:minimal", "-nologo")
if (-not $Toolset) {
    # The project asks for v145 (Visual Studio 2026). On a machine that only has
    # an older Visual Studio, build with the newest toolset it does have.
    $vsRoot = & $vswhere -latest -prerelease -products * -property installationPath | Select-Object -First 1
    $has145 = Get-ChildItem (Join-Path $vsRoot "MSBuild\Microsoft\VC") -Recurse -Directory -Filter "v145" -ErrorAction SilentlyContinue
    if (-not $has145) { $Toolset = "v143" }
}
if ($Toolset) { $buildArgs += "-p:PlatformToolset=$Toolset"; Write-Host "Toolset: $Toolset" }

Write-Host "Building Release|x64 ..." -ForegroundColor Cyan
& $msbuild @buildArgs
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }

$exe = Join-Path $root "bin\Release\TcpTools.exe"
if (-not (Test-Path $exe)) { throw "bin\Release\TcpTools.exe was not built" }

# The exe must carry the version it is released as.
$fileVersion = (Get-Item $exe).VersionInfo.ProductVersion
if ($fileVersion -ne $version) { throw "TcpTools.exe says version '$fileVersion', Version.h says '$version'" }

# ---- Tests ----------------------------------------------------------------------
if (-not $SkipTests) {
    Write-Host "Running the end-to-end tests ..." -ForegroundColor Cyan
    & (Join-Path $root "Test\RunTests.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }
}

# ---- Package ----------------------------------------------------------------------
$dist  = Join-Path $root "dist"
$stage = Join-Path $dist "TcpTools-$version"
$zip   = Join-Path $dist "TcpTools-$version-windows-x64.zip"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item $exe                              $stage
Copy-Item (Join-Path $root "LICENSE.txt")   $stage
Copy-Item (Join-Path $root "README.md")     $stage
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -CompressionLevel Optimal
Remove-Item $stage -Recurse -Force

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash
Write-Host ("{0}  ({1:N0} bytes)" -f $zip, (Get-Item $zip).Length) -ForegroundColor Green
Write-Host "SHA256 $hash"

# ---- Publish (optional) -------------------------------------------------------------
if ($Publish) {
    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if (-not $gh) {
        $candidate = Join-Path $env:ProgramFiles "GitHub CLI\gh.exe"
        if (Test-Path $candidate) { $gh = $candidate } else { throw "GitHub CLI (gh) not found" }
    } else { $gh = $gh.Source }

    $notes = Join-Path $root "docs\release-notes\$tag.md"
    $ghArgs = @("release", "create", $tag, $zip, "--title", "TcpTools $version", "--verify-tag")
    if (Test-Path $notes) { $ghArgs += @("--notes-file", $notes) } else { $ghArgs += "--generate-notes" }
    Write-Host "Creating GitHub release $tag ..." -ForegroundColor Cyan
    & $gh @ghArgs
    if ($LASTEXITCODE -ne 0) { throw "gh release create failed ($LASTEXITCODE)" }
}
