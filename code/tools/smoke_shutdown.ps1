$ErrorActionPreference = "Stop"

$tempRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd([IO.Path]::DirectorySeparatorChar) +
    [IO.Path]::DirectorySeparatorChar
$dir = Join-Path $env:TEMP "helmx-close-test"
$fullDir = [IO.Path]::GetFullPath($dir)
if (-not $fullDir.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "unsafe temp path"
}

if (Test-Path -LiteralPath $fullDir) {
    Remove-Item -LiteralPath $fullDir -Recurse -Force
}
New-Item -ItemType Directory -Path $fullDir | Out-Null

$sourceConfig = Join-Path $HOME ".codex\config.toml"
$testConfig = Join-Path $fullDir "config.toml"
Copy-Item -LiteralPath $sourceConfig -Destination $testConfig
$before = (Get-FileHash -LiteralPath $testConfig -Algorithm SHA256).Hash

$exe = Join-Path $PSScriptRoot "..\build\helmx.exe"
$exe = [IO.Path]::GetFullPath($exe)
$psi = [Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $exe
$psi.Arguments = "--autostart"
$psi.UseShellExecute = $false
$psi.Environment["CODEX_HOME"] = $fullDir
$psi.Environment["PATH"] = "C:\Windows\System32"
$process = [Diagnostics.Process]::Start($psi)

$ready = $false
for ($i = 0; $i -lt 80; $i++) {
    Start-Sleep -Milliseconds 250
    $ports = Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue |
        Where-Object { $_.OwningProcess -eq $process.Id -and $_.LocalPort -in 1800, 8090 }
    if (($ports.LocalPort -contains 1800) -and ($ports.LocalPort -contains 8090)) {
        $ready = $true
        break
    }
}

if (-not $ready) {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    throw "services did not become ready"
}

$status = Invoke-RestMethod -Uri "http://127.0.0.1:8090/api/proxy"
$shutdown = Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:8090/api/shutdown"
$exited = $process.WaitForExit(10000)
$after = (Get-FileHash -LiteralPath $testConfig -Algorithm SHA256).Hash
$remaining = Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue |
    Where-Object { $_.LocalPort -in 1800, 8090 }

[pscustomobject]@{
    Ready = $ready
    Pid = $process.Id
    Exited = $exited
    ConfigRestored = ($before -eq $after)
    ProxyStatus = ($status | ConvertTo-Json -Compress)
    Shutdown = ($shutdown | ConvertTo-Json -Compress)
    PortsRemaining = (($remaining | ForEach-Object LocalPort) -join ",")
}

& $exe startup off | Out-Host
Remove-Item -LiteralPath $fullDir -Recurse -Force
