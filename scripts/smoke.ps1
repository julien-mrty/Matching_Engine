param(
  [string]$ServerExe = ".\build\Release\server.exe",
  [string]$ClientExe = ".\build\Release\client.exe",
  [string]$Addr = "localhost:50051"
)

# Start server in background
$server = Start-Process -FilePath $ServerExe -PassThru
Start-Sleep -Milliseconds 800

function Run-Case($side, $type, $price, $scale, $qty) {
  $args = "$Addr C1 SYM $side $type $price $scale $qty"
  $out = & $ClientExe $args
  if ($LASTEXITCODE -ne 0) {
    Write-Error "Client failed: $out"
    exit 1
  }
  if ($out -notmatch "accepted order_id=") {
    Write-Error "Unexpected client output: $out"
    exit 1
  }
  Write-Host "[OK] $args -> $out"
}

Run-Case BUY LIMIT 10050 8 10
Run-Case BUY LIMIT 10050 9 10
Run-Case BUY LIMIT 10050 2 10
Run-Case BUY LIMIT 10050 0 10

# Stop server
Stop-Process -Id $server.Id -Force
