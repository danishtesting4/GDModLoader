param(
    [string]$GameDirectory
)

$ErrorActionPreference = "Stop"

$sdkRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$buildDirectory = Join-Path $PSScriptRoot "build"
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($null -eq $cmake) {
    $visualStudioCmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $visualStudioCmake) {
        $cmakePath = $visualStudioCmake
    } else {
        throw "CMake was not found. Install CMake or add it to PATH."
    }
} else {
    $cmakePath = $cmake.Source
}

& $cmakePath -S $PSScriptRoot -B $buildDirectory -A x64 "-DGDML_SDK_ROOT=$sdkRoot"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE."
}

& $cmakePath --build $buildDirectory --config Release
if ($LASTEXITCODE -ne 0) {
    throw "Mod build failed with exit code $LASTEXITCODE."
}

$modPath = Join-Path $buildDirectory "Release\my_first_mod.dll"
if (-not (Test-Path $modPath)) {
    throw "Build completed without producing $modPath."
}

if (-not [string]::IsNullOrWhiteSpace($GameDirectory)) {
    $gamePath = (Resolve-Path $GameDirectory).Path
    if (-not (Test-Path (Join-Path $gamePath "GeometryDash.exe"))) {
        throw "Game directory does not contain GeometryDash.exe: $gamePath"
    }

    $modsDirectory = Join-Path $gamePath "GDMods"
    New-Item -ItemType Directory -Force -Path $modsDirectory | Out-Null
    Copy-Item $modPath (Join-Path $modsDirectory "my_first_mod.dll") -Force
    Write-Output "Installed my_first_mod.dll into $modsDirectory"
} else {
    Write-Output "Built mod: $modPath"
    Write-Output "Pass -GameDirectory to install it next to GeometryDash.exe."
}
