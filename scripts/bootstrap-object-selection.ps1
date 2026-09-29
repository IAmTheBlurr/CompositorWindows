param([switch]$Offline)
$ErrorActionPreference='Stop'
$objectRoot=Split-Path $PSScriptRoot -Parent
$objectLock=Get-Content -LiteralPath (Join-Path $objectRoot 'dependencies/object-selection.lock.json') -Raw | ConvertFrom-Json
$objectDirectory=Join-Path $objectRoot 'dependencies/imaging/model'
New-Item -ItemType Directory -Force -Path $objectDirectory | Out-Null
foreach($objectFile in $objectLock.files){
    $objectPath=Join-Path $objectDirectory $objectFile.name
    if(!(Test-Path -LiteralPath $objectPath)){
        if($Offline){throw "Object selection model is absent: $($objectFile.name)"}
        Invoke-WebRequest "$($objectLock.repository)/resolve/$($objectLock.revision)/$($objectFile.source)" -OutFile $objectPath
    }
    if((Get-FileHash -LiteralPath $objectPath -Algorithm SHA256).Hash -ine $objectFile.sha256){throw "Object model checksum mismatch: $($objectFile.name)"}
}
