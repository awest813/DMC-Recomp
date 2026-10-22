$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$fixture = Join-Path $repo ('build\launcher-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$profile = Join-Path $fixture 'settings [test].json'
$discPath = Join-Path $fixture 'disc & image [test].iso'
Set-Content -LiteralPath $discPath -Value 'test disc'
$cardPath = Join-Path $fixture 'save & card [test].ps2'
@{ iso = $discPath; memcard = $cardPath; scale = 1; mode = 'native' } | ConvertTo-Json | Set-Content -LiteralPath $profile
$before = Get-Content -LiteralPath $profile -Raw
$dryOutput = . (Join-Path $repo 'launcher\launcher.ps1') -Play native -DryRun -SettingsFile $profile
function Assert($ok, $message) { if (-not $ok) { throw $message } }
Assert ($dryOutput -match '--disc') 'Native dry run omitted disc argument'
Assert ((Get-Content -LiteralPath $profile -Raw) -eq $before) 'Dry run changed settings'
Assert (-not (Test-Path -LiteralPath $cardPath)) 'Dry run created a memory card'
$Game = $fixture
$ControlsPath = Join-Path $fixture 'controls.txt'
$DryRun = $true
function Test-Port { throw 'Dry run attempted a network operation' }
$browserOutput = Start-Browser
Assert ($browserOutput -match 'PS2_SCALE=1') 'Browser URL did not preserve selected quality'
Assert ($browserOutput -match 'http://localhost:8620/') 'Browser origin changed, which would hide existing cards'
Assert (-not (Test-Path -LiteralPath $ControlsPath)) 'Dry run wrote controls'
$isoBox.Text = ''
Update-Readiness
Assert (-not $playButton.Enabled) 'Missing disc did not disable Start'
$isoBox.Text = $discPath
Update-Readiness
Assert $playButton.Enabled 'Existing bracketed path was not recognized'
$script:capturedStart = $null
function Start-Process {
    param($FilePath, $ArgumentList, $WorkingDirectory, [switch]$PassThru, $WindowStyle, $RedirectStandardError, $RedirectStandardOutput)
    $script:capturedStart = @{ file = $FilePath; args = $ArgumentList; cwd = $WorkingDirectory }
    if ($PassThru) { return [pscustomobject]@{ HasExited = $false; Id = -1 } }
}
$DryRun = $false
Start-Native
Assert ($script:capturedStart.file -eq $Native) 'Native launch passed through a command shell'
Assert ($script:capturedStart.args -contains ('"' + $discPath + '"')) 'Native launch lost spaces or metacharacters in disc path'
Assert ($script:capturedStart.args -contains ('"' + $cardPath + '"')) 'Native launch lost memory card path'
Assert (Test-Path -LiteralPath $ControlsPath) 'Native launch did not save controls'
$script:gameProcess = $null
Set-Content -LiteralPath (Join-Path $fixture 'quicksave.ps2') -Value 'fixture'
Start-Native $true
Assert ($script:capturedStart.args -contains '--load-state') 'Resume did not select the quick save'
$script:gameProcess = $null
$WebDir = Join-Path $fixture 'web'
New-Item -ItemType Directory -Path $WebDir | Out-Null
foreach ($name in @('ps2web.html', 'ps2web.js', 'ps2web.wasm', 'ps2web.data')) { Set-Content -LiteralPath (Join-Path $WebDir $name) -Value 'fixture' }
function Test-Port { return $true }
function Find-Browser { return 'browser-test.exe' }
$script:identityDisc = $discPath
function Invoke-RestMethod {
    param($Uri, $TimeoutSec)
    Assert ($Uri -eq 'http://localhost:8620/__ps2web_status') 'Server identity used a different host from the browser'
    return [pscustomobject]@{ application = 'ps2-web-recomp'; build = $WebDir; disc = $script:identityDisc }
}
$browserMode.Checked = $true
Start-Browser $true
Assert ($script:capturedStart.file -eq 'browser-test.exe') 'Matching server was not reused'
Assert (Test-Path -LiteralPath (Join-Path $WebDir 'launcher-quicksave.ps2')) 'Browser resume did not copy its checkpoint'
Assert ($null -eq $script:server) 'Reused server was incorrectly claimed by launcher'
$script:capturedStart = $null
$script:identityDisc = 'different.iso'
Set-Content -LiteralPath (Join-Path $WebDir 'launcher-quicksave.ps2') -Value 'existing checkpoint'
$refused = $false
try { Start-Browser $true } catch { $refused = $_.Exception.Message -match 'Another session' }
Assert $refused 'Launcher reused a server for the wrong disc'
Assert ($null -eq $script:capturedStart) 'Wrong-disc server opened a browser'
Assert ((Get-Content -LiteralPath (Join-Path $WebDir 'launcher-quicksave.ps2') -Raw).Trim() -eq 'existing checkpoint') 'Refused session overwrote a served checkpoint'
$DryRun = $true
$isoBox.Text = Resolve-Path -LiteralPath $discPath -Relative
$relativeOutput = Start-Native
Assert ($relativeOutput -match [regex]::Escape($discPath)) 'Relative disc path was not resolved before changing working directory'
$DryRun = $false
$script:identityDisc = $discPath
$script:portChecks = 0
function Test-Port { $script:portChecks++; return $script:portChecks -gt 1 }
Start-Browser
Assert ($null -ne $script:server) 'Newly started server was not owned'
Assert ($script:capturedStart.file -eq 'browser-test.exe') 'Ready owned server did not open the browser'
$script:server = $null
$script:portChecks = 0
$script:identityDisc = 'wrong-after-start.iso'
$script:stoppedId = $null
function Stop-Process { param($Id, $ErrorAction); $script:stoppedId = $Id }
$refused = $false
try { Start-Browser } catch { $refused = $_.Exception.Message -match 'did not become ready' }
Assert $refused 'New server was accepted without matching identity'
Assert ($script:stoppedId -eq -1) 'Failed startup did not stop its owned process'
Assert ($null -eq $script:server) 'Failed server remained owned'
$timer.Stop()
$form.Dispose()
Write-Output 'Launcher: dry-run isolation, readiness, literal paths, direct launch, resume and server ownership passed'
