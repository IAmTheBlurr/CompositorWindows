param([switch]$Offline)
$ErrorActionPreference='Stop'
$importRoot=Split-Path $PSScriptRoot -Parent
$importDependencies=Join-Path $importRoot 'dependencies'
$importDownloads=Join-Path $importDependencies 'downloads'
$importLock=Get-Content (Join-Path $importDependencies 'imports.lock.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Force $importDownloads | Out-Null
foreach($importProperty in $importLock.PSObject.Properties){
    $importEntry=$importProperty.Value
    foreach($importAsset in @($importEntry,$importEntry.source)){
        if(-not $importAsset){continue}
        $importArchive=Join-Path $importDownloads $importAsset.archive
        if(-not(Test-Path -LiteralPath $importArchive)){
            if($Offline){throw "Missing locked import dependency: $($importAsset.archive)"}
            Invoke-WebRequest -Uri $importAsset.url -OutFile $importArchive
        }
        if((Get-FileHash -LiteralPath $importArchive -Algorithm SHA256).Hash -ine $importAsset.sha256){throw "Import dependency checksum mismatch: $($importAsset.archive)"}
    }
    if(-not(Test-Path -LiteralPath (Join-Path $importDependencies $importEntry.probe))){
        $importDestination=Join-Path $importDependencies $importEntry.destination
        New-Item -ItemType Directory -Force $importDestination | Out-Null
        Push-Location $importDestination
        try { & cmake -E tar xf (Join-Path $importDownloads $importEntry.archive); if($LASTEXITCODE){throw 'Import dependency extraction failed'} }
        finally { Pop-Location }
    }
}
