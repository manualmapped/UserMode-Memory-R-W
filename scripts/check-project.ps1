$ErrorActionPreference = 'Stop'

$compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
$compilerKind = 'msvc'
if (-not $compiler) {
    $compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
    $compilerKind = 'mingw'
    if (-not $compiler) {
        Write-Error 'Neither MSVC cl.exe nor MinGW-w64 g++.exe was found. Install Visual Studio 2022 C++ tools or GCC 12+ targeting x86_64-w64-mingw32.'
        exit 2
    }

    $target = (& $compiler.Source -dumpmachine).Trim()
    $version = (& $compiler.Source -dumpfullversion -dumpversion).Trim()
    $majorVersion = 0
    if ($target -ne 'x86_64-w64-mingw32' -or -not [int]::TryParse($version.Split('.')[0], [ref]$majorVersion) -or $majorVersion -lt 12) {
        Write-Error "MinGW compiler does not meet the documented Windows x64 requirement (target=$target, version=$version)."
        exit 2
    }
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$tempRoot = [System.IO.Path]::GetTempPath()
$token = [guid]::NewGuid().ToString('N')
$source = Join-Path $tempRoot "codex-memory-header-$token.cpp"
$object = Join-Path $tempRoot "codex-memory-header-$token.obj"
$executable = Join-Path $tempRoot "codex-memory-header-$token.exe"
$sourceText = @'
#include "memory.h"
static_assert(sizeof(void*) == 8, "This project smoke check targets Windows x64.");
int main() {
    return mem::is_valid(mem::MIN_ADDRESS) && !mem::is_valid(mem::MIN_ADDRESS - 1) ? 0 : 1;
}
'@

try {
    Set-Content -LiteralPath $source -Value $sourceText -Encoding ascii
    Push-Location $projectRoot
    try {
        if ($compilerKind -eq 'msvc') {
            & $compiler.Source /nologo /std:c++17 /EHsc /W4 /TP "/I$projectRoot" "/Fo$object" "/Fe:$executable" $source
        }
        else {
            & $compiler.Source -std=c++17 -Wall -Wextra -I $projectRoot $source -o $executable
        }
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    finally {
        Pop-Location
    }

    & $executable
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Output "$($compilerKind.ToUpperInvariant()) x64 C++17 header smoke check passed."
}
finally {
    foreach ($path in @($source, $object, $executable)) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
}
