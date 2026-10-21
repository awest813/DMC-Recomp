# Starts a throwaway headless Chrome with WebGPU and remote debugging on port 9333 for scripts/webprobe.mjs.
# usage: scripts/webprobe.ps1 [-Stop]
param([switch]$Stop)
$profileDir = Join-Path $env:TEMP 'ps2web-probe-profile'
Get-CimInstance Win32_Process -Filter "Name = 'chrome.exe'" | Where-Object { $_.CommandLine -like "*$profileDir*" } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
if ($Stop) { return }
$chrome = 'C:\Program Files\Google\Chrome\Application\chrome.exe'
Start-Process $chrome -ArgumentList @('--headless=new', '--remote-debugging-port=9333', "--user-data-dir=$profileDir", '--enable-unsafe-webgpu',
    '--enable-features=Vulkan', '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--window-size=1280,1100', '--autoplay-policy=no-user-gesture-required', 'about:blank')
Start-Sleep -Seconds 2
(Invoke-WebRequest -UseBasicParsing http://127.0.0.1:9333/json/version).Content
