$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$source = Join-Path $PSScriptRoot 'historical_transfer_bench.cpp'
$compiler = Get-Command g++.exe -ErrorAction Stop
$outDir = Join-Path $PSScriptRoot 'results'
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("umrw-history-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
New-Item -ItemType Directory -Path $tempRoot | Out-Null

try {
    $revisions = @(
        @{ Sha = '307aad3cc410559f2808723fb835cb23b10157fc'; Label = '307aad3' },
        @{ Sha = '38732bab803482a6ae17214fa236cfee36b91ea6'; Label = '38732ba' }
    )

    foreach ($revision in $revisions) {
        & git -C $repoRoot cat-file -e "$($revision.Sha):memory.hpp" 2>$null
        if ($LASTEXITCODE -ne 0) {
            & git -C $repoRoot fetch --depth=1 origin $revision.Sha
            if ($LASTEXITCODE -ne 0) { throw "Could not fetch historical revision $($revision.Sha)." }
        }

        $revisionDir = Join-Path $tempRoot $revision.Label
        New-Item -ItemType Directory -Path $revisionDir | Out-Null
        & git -C $repoRoot show "$($revision.Sha):memory.hpp" | Set-Content -LiteralPath (Join-Path $revisionDir 'memory.hpp') -Encoding ascii
        if ($LASTEXITCODE -ne 0) { throw "Could not extract historical header $($revision.Sha)." }
        Copy-Item -LiteralPath $source -Destination $revisionDir
        $exe = Join-Path $revisionDir 'historical_transfer_bench.exe'
        $revisionDefine = if ($revision.Label -eq '38732ba') { '-DHISTORICAL_387=1' } else { '-DHISTORICAL_307=1' }
        & $compiler.Source -std=c++17 -O3 -DNDEBUG $revisionDefine -I $revisionDir `
            (Join-Path $revisionDir 'historical_transfer_bench.cpp') -lpsapi -o $exe
        if ($LASTEXITCODE -ne 0) { throw "Historical build failed for $($revision.Sha)." }

        $resultPath = Join-Path $outDir ("history-$($revision.Label)-gcc-$((& $compiler.Source -dumpfullversion).Trim()).csv")
        & $exe | Tee-Object -FilePath $resultPath
        if ($LASTEXITCODE -ne 0) { throw "Historical benchmark failed for $($revision.Sha) with exit $LASTEXITCODE." }
    }
}
finally {
    $tempPrefix = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    $fullTempRoot = [System.IO.Path]::GetFullPath($tempRoot)
    if (-not $fullTempRoot.StartsWith($tempPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to remove a historical benchmark directory outside the system temporary directory.'
    }
    if (Test-Path -LiteralPath $fullTempRoot) { Remove-Item -LiteralPath $fullTempRoot -Recurse -Force }
}
