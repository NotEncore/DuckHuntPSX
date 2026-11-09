function Resolve-ToolChildPath([string]$Root, [string]$Relative) {
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\','/')
    if ([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or $Relative -match ':') {
        throw 'Tool entry must use a relative path.'
    }
    $child = [IO.Path]::GetFullPath([IO.Path]::Combine($rootPath,$Relative))
    if (!$child.StartsWith($rootPath + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Tool entry escapes its package directory.'
    }
    return $child
}

function Assert-RequiredToolFiles($Package, [string]$Root) {
    if (!$Package.required) { return }
    foreach ($file in $Package.required.PSObject.Properties) {
        $path = Resolve-ToolChildPath $Root $file.Name
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-FileHash -LiteralPath $path).Hash -ne $file.Value) {
            throw "Tool integrity check failed for $($Package.id): $($file.Name)"
        }
    }
}
function Get-PortableTool($Package, [string]$Cache, [string]$OfflinePackages) {
    if ($Package.id -notmatch '^[a-z0-9][a-z0-9._-]+$' -or
        $Package.archiveSha256 -notmatch '^[0-9a-fA-F]{64}$' -or
        $Package.entrySha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw 'Tool lock requires a package ID and two SHA-256 pins.'
    }
    $key = "$($Package.id)-$($Package.archiveSha256.ToLowerInvariant())"
    $root = Resolve-ToolChildPath $Cache $Package.archiveSha256.Substring(0,16)
    $entry = Resolve-ToolChildPath $root $Package.entry
    $ready = Join-Path $root '.verified-archive'
    if (Test-Path -LiteralPath $root) {
        if (!(Test-Path -LiteralPath $ready -PathType Leaf) -or (Get-Content -LiteralPath $ready -Raw).Trim() -ne $Package.archiveSha256) { throw "Incomplete tool cache for $($Package.id). Move $root aside before retrying." }
        if (!(Test-Path -LiteralPath $entry -PathType Leaf) -or
            (Get-FileHash -LiteralPath $entry).Hash -ne $Package.entrySha256) {
            throw "Integrity check failed for cached $($Package.id). Move that cache entry aside before retrying."
        }
        Assert-RequiredToolFiles $Package $root
        return $entry
    }
    [IO.Directory]::CreateDirectory($Cache) | Out-Null
    $work = Join-Path $Cache ([Guid]::NewGuid().ToString('N').Substring(0,12))
    [IO.Directory]::CreateDirectory($work) | Out-Null
    $zip = Join-Path $work 'tool.zip'
    Write-Host "Preparing $($Package.id)..."
    try {
        if ($OfflinePackages) {
            $inputFile = Resolve-ToolChildPath $OfflinePackages "$key.zip"
            if (!(Test-Path -LiteralPath $inputFile -PathType Leaf)) { throw "Missing offline archive $key.zip." }
            Copy-Item -LiteralPath $inputFile -Destination $zip
        } else {
            $uri = $null
            if (![Uri]::TryCreate([string]$Package.url,[UriKind]::Absolute,[ref]$uri) -or $uri.Scheme -ne 'https') {
                throw "No approved HTTPS download is configured for $($Package.id)."
            }
            $curl = Join-Path $env:SystemRoot 'System32\curl.exe'
            if (!(Test-Path -LiteralPath $curl -PathType Leaf)) {
                throw 'Windows curl.exe is required for online setup. Use a current Windows installation or the offline packages.'
            }
            $size = if ($Package.archiveBytes) { ' ({0:N1} MiB)' -f ($Package.archiveBytes / 1MB) } else { '' }
            Write-Host "Downloading $($Package.id)$size..."
            & $curl --disable --fail --silent --location --proto '=https' --proto-redir '=https' --max-redirs 5 --connect-timeout 15 --max-time 600 --speed-limit 1024 --speed-time 45 --output $zip --url $uri.AbsoluteUri
            $downloadExit = $LASTEXITCODE
            if ($downloadExit -eq 28) { throw "Download timed out for $($Package.id). Check your connection and run the builder again." }
            if ($downloadExit -ne 0) { throw "Download failed for $($Package.id) (curl exit $downloadExit). Check your internet connection and run the builder again." }
        }
        if ((Get-FileHash -LiteralPath $zip).Hash -ne $Package.archiveSha256) {
            throw "Archive integrity check failed for $($Package.id). The download was not extracted."
        }
        Write-Host "Verified $($Package.id). Extracting..."
    } catch {
        if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip }
        Remove-Item -LiteralPath $work
        throw
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    # Claim the destination exclusively before extraction. A partial cache
    # remains unusable until every required check passes and the marker exists.
    $claim = [IO.File]::Open("$root.installing", [IO.FileMode]::CreateNew)
    try {
        if (Test-Path -LiteralPath $root) { throw 'Another setup already created this tool cache. Run setup again.' }
        [IO.Compression.ZipFile]::ExtractToDirectory($zip,$root)
        if (!(Test-Path -LiteralPath $entry -PathType Leaf) -or
            (Get-FileHash -LiteralPath $entry).Hash -ne $Package.entrySha256) {
            throw "Extracted tool integrity check failed for $($Package.id)."
        }
        Assert-RequiredToolFiles $Package $root
        [IO.File]::WriteAllText($ready, $Package.archiveSha256)
    } finally {
        $claim.Dispose()
        Remove-Item -LiteralPath "$root.installing"
        Remove-Item -LiteralPath $zip
    }
    return $entry
}

# The verified 2.1.1 Windows app embeds its dependency ZIP at this offset.
# Only the pinned core is extracted. The GUI and other resources are unused.
function Get-PinnedMesenCore([string]$App) {
    $appHash = 'D2EB03C2590C648BF329F127EBCFEFD70130E7690A9E2CCDBA8616FAEA1FE96B'
    $coreHash = '65F69A7BDB25C35FDCA77C9C73E71EF3E8EC233380204D94CFC0179933048C74'
    if ((Get-FileHash -LiteralPath $App).Hash -ne $appHash) {
        throw 'The Mesen executable differs from the verified 2.1.1 Windows release.'
    }
    Add-Type -AssemblyName System.IO.Compression
    $image = [IO.File]::ReadAllBytes($App)
    $stream = [IO.MemoryStream]::new($image, 37178007, 7954166, $false)
    try {
        $zip = [IO.Compression.ZipArchive]::new($stream)
        try {
            $entry = $zip.GetEntry('MesenCore.dll')
            if (!$entry) { throw 'The verified Mesen core entry is missing.' }
            $path = Join-Path (Split-Path $App -Parent) 'MesenCore.dll'
            if (!(Test-Path -LiteralPath $path)) {
                $pending = "$path.partial-$([Guid]::NewGuid().ToString('N'))"
                try {
                    $input = $entry.Open()
                    try {
                        $output = [IO.File]::Open($pending, [IO.FileMode]::CreateNew)
                        try { $input.CopyTo($output) } finally { $output.Dispose() }
                    } finally { $input.Dispose() }
                    if ((Get-FileHash -LiteralPath $pending).Hash -ne $coreHash) {
                        throw 'The extracted Mesen core failed its integrity check.'
                    }
                    try { [IO.File]::Move($pending, $path) }
                    catch [IO.IOException] { if (!(Test-Path -LiteralPath $path)) { throw } }
                } finally {
                    if (Test-Path -LiteralPath $pending) { Remove-Item -LiteralPath $pending }
                }
            }
            if ((Get-FileHash -LiteralPath $path).Hash -ne $coreHash) {
                throw 'The cached Mesen core failed its integrity check.'
            }
            return $path
        } finally { $zip.Dispose() }
    } finally { $stream.Dispose() }
}






