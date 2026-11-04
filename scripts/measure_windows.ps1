param(
    [string]$EnvName = "",
    [string]$EnvValue = "",
    [int]$Seconds = 10,
    [string]$Exe = "D:\coding\vofa2\dist\vofa2_app.exe",
    [string[]]$AppArgs = @("--ui-preset", "1"),
    [string]$ProcessName = "vofa2_app"
)

if ($EnvName) {
    Set-Item -Path ("Env:" + $EnvName) -Value $EnvValue
}

Get-Process $ProcessName -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

$p = Start-Process -FilePath $Exe -ArgumentList $AppArgs -PassThru
Start-Sleep -Seconds $Seconds
$p.Refresh()

[pscustomobject]@{
    Env = if ($EnvName) { $EnvName + "=" + $EnvValue } else { "default" }
    WorkingSetMB = [math]::Round($p.WorkingSet64 / 1MB, 1)
    PrivateMB = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
    Threads = $p.Threads.Count
    Handles = $p.HandleCount
} | ConvertTo-Json -Compress

$p.Kill()
