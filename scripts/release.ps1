# =============================================================================
#  release.ps1  --  builds, tests and packages a TcpTools release
#
#    scripts\release.ps1                 build Release|x64, run the tests, write
#                                        dist\TcpTools-<version>-windows-x64.zip
#    scripts\release.ps1 -SkipTests      the same without the end-to-end tests
#    scripts\release.ps1 -SignThumbprint <thumbprint>
#                                        ... and sign TcpTools.exe (see "Sign" below)
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
    [string] $Toolset = "",       # e.g. v143 to build with an older Visual Studio
    [string] $SignThumbprint = "",# sign with this certificate of the user's store
    [int]    $MaxLagMs = 1500     # passed to the tests (slower machines: raise it)
)

$script:Signed = $false

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
# Before signing and packaging: the file that is tested is the file that ships
# (signing only adds the signature).
if (-not $SkipTests) {
    Write-Host "Running the end-to-end tests ..." -ForegroundColor Cyan
    & (Join-Path $root "Test\RunTests.ps1") -Config Release -MaxLagMs $MaxLagMs
    if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }
}

# ---- Sign (when a certificate is given) ---------------------------------------------
# Either  -SignThumbprint <thumbprint of a code-signing certificate in the user's store>
# or the environment variables TCPTOOLS_SIGN_PFX_BASE64 (a .pfx, base64) and
# TCPTOOLS_SIGN_PFX_PASSWORD - the release workflow fills them from the repository
# secrets of the same names. Without a certificate the exe stays unsigned and
# Windows SmartScreen asks before its first start.
$pfxFile = $null
try {
    $signArgs = $null
    if ($SignThumbprint) {
        $signArgs = @("/sha1", $SignThumbprint)
    } elseif ($env:TCPTOOLS_SIGN_PFX_BASE64) {
        $pfxFile = Join-Path ([System.IO.Path]::GetTempPath()) ("tcptools-sign-" + [Guid]::NewGuid().ToString("N") + ".pfx")
        [System.IO.File]::WriteAllBytes($pfxFile, [Convert]::FromBase64String($env:TCPTOOLS_SIGN_PFX_BASE64))
        $signArgs = @("/f", $pfxFile)
        if ($env:TCPTOOLS_SIGN_PFX_PASSWORD) { $signArgs += @("/p", $env:TCPTOOLS_SIGN_PFX_PASSWORD) }
    }
    if ($signArgs) {
        $signtool = Get-ChildItem (Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin") -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
                    Where-Object { $_.FullName -match "\\x64\\" } | Sort-Object FullName -Descending | Select-Object -First 1
        if (-not $signtool) { throw "signtool.exe not found (Windows SDK)" }
        Write-Host "Signing TcpTools.exe ..." -ForegroundColor Cyan
        & $signtool.FullName sign /fd SHA256 /td SHA256 /tr "http://timestamp.digicert.com" /d "TcpTools" @signArgs $exe
        if ($LASTEXITCODE -ne 0) { throw "signtool failed ($LASTEXITCODE)" }
        & $signtool.FullName verify /pa $exe
        if ($LASTEXITCODE -ne 0) { throw "The signature does not verify ($LASTEXITCODE)" }
        $script:Signed = $true
    } else {
        Write-Host "No signing certificate given: TcpTools.exe is NOT signed." -ForegroundColor Yellow
    }
} finally {
    if ($pfxFile -and (Test-Path $pfxFile)) { Remove-Item $pfxFile -Force }
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
