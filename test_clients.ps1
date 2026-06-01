param(
    [int]$n = 20,
    [int]$m = 40,
    [int]$d = 300
)

$exe = Join-Path $PSScriptRoot "client.exe"

if (-not (Test-Path $exe)) {
    Write-Host "HATA: client.exe bulunamadi." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Event-Driven Sunucu - Coklu Istemci Testi" -ForegroundColor Cyan
Write-Host "  Istemci sayisi : $n" -ForegroundColor Cyan
Write-Host "  Mesaj/istemci  : $m" -ForegroundColor Cyan
Write-Host "  Bekleme        : $d ms" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

$startTime = Get-Date
$jobs = @()

for ($i = 1; $i -le $n; $i++) {
    $clientId = $i
    $jobs += Start-Job -ScriptBlock {
        param($exe, $id, $msgs, $delay)
        $output = & $exe $id $msgs $delay 2>&1
        [PSCustomObject]@{
            Id     = $id
            Output = $output -join "`n"
        }
    } -ArgumentList $exe, $clientId, $m, $d
}

Write-Host "$n istemci baslatildi, bekleniyor..." -ForegroundColor Yellow

$null = $jobs | Wait-Job

$endTime = Get-Date
$elapsed = [math]::Round(($endTime - $startTime).TotalSeconds, 2)

$results = $jobs | Receive-Job
$jobs | Remove-Job

Write-Host ""
Write-Host "============================================" -ForegroundColor Green
Write-Host "  TEST TAMAMLANDI" -ForegroundColor Green
Write-Host "  Toplam sure  : $elapsed saniye" -ForegroundColor Green
Write-Host "  Toplam mesaj : $($n * $m)" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Green
Write-Host ""

foreach ($r in ($results | Sort-Object Id)) {
    $cid = $r.Id.ToString("000")
    Write-Host "--- Client$cid ---" -ForegroundColor DarkCyan
    Write-Host $r.Output
}