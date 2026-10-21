# Devil May Cry launcher: plays the recompiled game in a native window or in the browser.
# Start it with "Play Devil May Cry.bat" in the repository root.
param([ValidateSet('', 'native', 'browser')][string]$Play = '', [switch]$DryRun, [string]$Snapshot = '')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class XInputProbe {
    [StructLayout(LayoutKind.Sequential)]
    public struct State { public uint packet; public ushort buttons; public byte lt, rt; public short lx, ly, rx, ry; }
    [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")]
    static extern uint GetState(uint index, out State state);
    public static int Connected() {
        int n = 0;
        try { for (uint i = 0; i < 4; ++i) { State s; if (GetState(i, out s) == 0) ++n; } } catch (DllNotFoundException) { return -1; }
        return n;
    }
}
'@

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Game = Join-Path $Root 'games\dmc'
$Native = Join-Path $Root 'build\native-dmc\ps2run.exe'
$WebDir = Join-Path $Root 'build\web-dmc'
$Elf = Join-Path $Game 'disc\SLUS_202.16'
$SettingsPath = Join-Path $Game 'launcher.json'
$ControlsPath = Join-Path $Game 'controls.txt'

# Pad input, what it does in DMC, default keys. Key names: letters, digits, Up Down Left Right, Enter, Backspace, Space, Tab, LShift, RShift, LCtrl.
$ControlRows = @(
    @('lup', 'Move up (stick)', 'Up,I'), @('ldown', 'Move down (stick)', 'Down,K'), @('lleft', 'Move left (stick)', 'Left,J'), @('lright', 'Move right (stick)', 'Right,L'),
    @('triangle', 'Triangle - action (jump)', 'S'), @('circle', 'Circle - sword', 'X'), @('square', 'Square - shoot', 'A'), @('cross', 'Cross - shoot / confirm / examine', 'Z'),
    @('r1', 'R1 - aim (lock on)', 'W'), @('l1', 'L1 - devil trigger', 'Q'), @('l2', 'L2 - show map', '1'), @('r2', 'R2', '2'),
    @('start', 'Start - menu screen', 'Enter'), @('select', 'Select - options', 'Backspace'),
    @('rup', 'Right stick up (devil arm)', 'T'), @('rdown', 'Right stick down (devil arm)', 'G'), @('rleft', 'Right stick left (devil arm)', 'F'), @('rright', 'Right stick right (devil arm)', 'H'),
    @('up', 'D-pad up (menus)', 'Up'), @('down', 'D-pad down (menus)', 'Down'), @('left', 'D-pad left (menus)', 'Left'), @('right', 'D-pad right (menus)', 'Right')
)

function Read-Controls {
    $map = [ordered]@{}
    foreach ($row in $ControlRows) { $map[$row[0]] = $row[2] }
    if (Test-Path $ControlsPath) {
        foreach ($line in Get-Content $ControlsPath) {
            if ($line -match '^\s*([a-z0-9]+)\s*=\s*(.*?)\s*$' -and $map.Contains($Matches[1])) { $map[$Matches[1]] = $Matches[2] }
        }
    }
    return $map
}

function Write-Controls($map) {
    $lines = @('# Devil May Cry controls: input=key[,key]. Written by the launcher; read by the native window (PS2_KEYS) and the browser (?keys=).')
    foreach ($k in $map.Keys) { $lines += "$k=$($map[$k])" }
    $lines | Set-Content -Encoding ASCII $ControlsPath
}

function Controls-Param($map) {
    return [uri]::EscapeDataString((($map.Keys | ForEach-Object { "${_}:$($map[$_] -replace '\s', '')" }) -join ';'))
}
$Port = 8620

$settings = [ordered]@{
    iso        = (Join-Path (Split-Path $Root -Parent) 'Devil May Cry.iso')
    memcard    = (Join-Path $Game 'memcard.ps2')
    scale      = 2
    fullscreen = $false
    widescreen = $false
}
if (Test-Path $SettingsPath) {
    try {
        $saved = Get-Content $SettingsPath -Raw | ConvertFrom-Json
        foreach ($key in @($settings.Keys)) { if ($null -ne $saved.$key) { $settings[$key] = $saved.$key } }
    } catch {}
}
function Save-Settings { $settings | ConvertTo-Json | Set-Content -Encoding UTF8 $SettingsPath }

$script:server = $null
$script:gameProcess = $null

function Test-Port {
    $client = New-Object System.Net.Sockets.TcpClient
    try { $client.Connect('127.0.0.1', $Port); return $true } catch { return $false } finally { $client.Dispose() }
}

function Find-Browser {
    foreach ($p in @("$env:ProgramFiles\Google\Chrome\Application\chrome.exe", "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
                     "$env:LocalAppData\Google\Chrome\Application\chrome.exe", "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
                     "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe")) {
        if ($p -and (Test-Path $p)) { return $p }
    }
    return $null
}

# ---- window ----
$form = New-Object System.Windows.Forms.Form
$form.Text = 'Devil May Cry'
$form.ClientSize = New-Object System.Drawing.Size(560, 520)
$form.FormBorderStyle = 'FixedSingle'
$form.MaximizeBox = $false
$form.StartPosition = 'CenterScreen'
$form.BackColor = [System.Drawing.Color]::FromArgb(24, 18, 20)
$form.ForeColor = [System.Drawing.Color]::FromArgb(235, 228, 228)
$form.Font = New-Object System.Drawing.Font('Segoe UI', 9.5)

function Add-Label($text, $x, $y, $w = 120, $font = $null) {
    $l = New-Object System.Windows.Forms.Label
    $l.Text = $text; $l.Location = New-Object System.Drawing.Point($x, $y); $l.Size = New-Object System.Drawing.Size($w, 22)
    if ($font) { $l.Font = $font }
    $form.Controls.Add($l); return $l
}
function Add-Button($text, $x, $y, $w, $h = 30) {
    $b = New-Object System.Windows.Forms.Button
    $b.Text = $text; $b.Location = New-Object System.Drawing.Point($x, $y); $b.Size = New-Object System.Drawing.Size($w, $h)
    $b.FlatStyle = 'Flat'; $b.BackColor = [System.Drawing.Color]::FromArgb(58, 30, 34); $b.ForeColor = $form.ForeColor
    $b.FlatAppearance.BorderColor = [System.Drawing.Color]::FromArgb(140, 40, 48)
    $form.Controls.Add($b); return $b
}
function Add-PathRow($label, $y, $value, $filter, $save) {
    Add-Label $label 20 ($y + 3) 100 | Out-Null
    $t = New-Object System.Windows.Forms.TextBox
    $t.Location = New-Object System.Drawing.Point(120, $y); $t.Size = New-Object System.Drawing.Size(330, 24); $t.Text = $value
    $t.BackColor = [System.Drawing.Color]::FromArgb(40, 32, 34); $t.ForeColor = $form.ForeColor; $t.BorderStyle = 'FixedSingle'
    $form.Controls.Add($t)
    $b = Add-Button 'Browse...' 460 ($y - 2) 80 28
    $b.Add_Click({
        $d = if ($save) { New-Object System.Windows.Forms.SaveFileDialog } else { New-Object System.Windows.Forms.OpenFileDialog }
        $d.Filter = $filter
        if ($save) { $d.OverwritePrompt = $false }
        if ($t.Text) { try { $d.InitialDirectory = Split-Path $t.Text -Parent; $d.FileName = Split-Path $t.Text -Leaf } catch {} }
        if ($d.ShowDialog() -eq 'OK') { $t.Text = $d.FileName }
    }.GetNewClosure())
    return $t
}

$title = Add-Label 'DEVIL MAY CRY' 20 14 400 (New-Object System.Drawing.Font('Georgia', 20, [System.Drawing.FontStyle]::Bold))
$title.Size = New-Object System.Drawing.Size(400, 40)
$title.ForeColor = [System.Drawing.Color]::FromArgb(210, 40, 50)
(Add-Label 'PS2 static recompilation - SLUS_202.16' 22 54 400).ForeColor = [System.Drawing.Color]::FromArgb(160, 150, 150)

$isoBox = Add-PathRow 'Disc image' 92 $settings.iso 'Disc images (*.iso)|*.iso|All files|*.*' $false
$cardBox = Add-PathRow 'Memory card' 126 $settings.memcard 'PS2 memory cards (*.ps2)|*.ps2|All files|*.*' $true

Add-Label 'Resolution' 20 166 100 | Out-Null
$scaleBox = New-Object System.Windows.Forms.ComboBox
$scaleBox.DropDownStyle = 'DropDownList'; $scaleBox.Location = New-Object System.Drawing.Point(120, 163); $scaleBox.Size = New-Object System.Drawing.Size(200, 24)
[void]$scaleBox.Items.AddRange(@('1x  (native, 640x448)', '2x  (1280x896)', '3x  (1920x1344)'))
$scaleBox.SelectedIndex = [Math]::Max(0, [Math]::Min(2, [int]$settings.scale - 1))
$form.Controls.Add($scaleBox)

$fullBox = New-Object System.Windows.Forms.CheckBox
$fullBox.Text = 'Fullscreen (F11 toggles in game)'; $fullBox.Location = New-Object System.Drawing.Point(120, 196); $fullBox.Size = New-Object System.Drawing.Size(300, 24); $fullBox.Checked = [bool]$settings.fullscreen
$form.Controls.Add($fullBox)
$wideBox = New-Object System.Windows.Forms.CheckBox
$wideBox.Text = 'Widescreen 16:9 (DMC draws 4:3, so this stretches)'; $wideBox.Location = New-Object System.Drawing.Point(120, 222); $wideBox.Size = New-Object System.Drawing.Size(400, 24); $wideBox.Checked = [bool]$settings.widescreen
$form.Controls.Add($wideBox)

$playNative = Add-Button 'Play' 20 262 255 44
$playNative.Font = New-Object System.Drawing.Font('Segoe UI', 12, [System.Drawing.FontStyle]::Bold)
$playWeb = Add-Button 'Play in browser' 285 262 255 44
$playWeb.Font = New-Object System.Drawing.Font('Segoe UI', 12)

$controlsButton = Add-Button 'Controls...' 440 196 100 28
$controlsButton.Add_Click({ Show-Controls })

$padLabel = Add-Label '' 20 318 520
$statusLabel = Add-Label 'Ready.' 20 342 520
$statusLabel.ForeColor = [System.Drawing.Color]::FromArgb(170, 160, 160)

$help = New-Object System.Windows.Forms.Label
$help.Location = New-Object System.Drawing.Point(20, 372); $help.Size = New-Object System.Drawing.Size(520, 140)
$help.Font = New-Object System.Drawing.Font('Consolas', 9)
$help.ForeColor = [System.Drawing.Color]::FromArgb(190, 180, 180)
$help.Text = @'
Default keys (change with Controls...)  Controller (Xbox layout)
  Arrows / I J K L  move                  Left stick  move
  S  action / jump (Triangle)             Y  action    B  sword
  X  sword (Circle)                       X  shoot     A  shoot / OK
  A  shoot (Square)    Z  examine / OK    RB aim       LB devil trigger
  W  aim (R1)    Q  devil trigger (L1)    LT map       RS devil arm
  1  map (L2)    T F G H  devil arm       Start menu   Back options
  Enter  menu (Start)   F11 fullscreen   F5 quick-save state (window)
'@
$form.Controls.Add($help)

function Show-Controls {
    $map = Read-Controls
    $dlg = New-Object System.Windows.Forms.Form
    $dlg.Text = 'Controls'; $dlg.FormBorderStyle = 'FixedDialog'; $dlg.MaximizeBox = $false; $dlg.MinimizeBox = $false
    $dlg.StartPosition = 'CenterParent'; $dlg.BackColor = $form.BackColor; $dlg.ForeColor = $form.ForeColor; $dlg.Font = $form.Font
    $dlg.ClientSize = New-Object System.Drawing.Size(420, (60 + 27 * $ControlRows.Count + 50))
    $note = New-Object System.Windows.Forms.Label
    $note.Text = 'Keys per input, comma separated: letters, digits, Up, Down, Left, Right, Enter, Backspace, Space, Tab, LShift, LCtrl.'
    $note.Location = New-Object System.Drawing.Point(14, 10); $note.Size = New-Object System.Drawing.Size(395, 40)
    $dlg.Controls.Add($note)
    $boxes = @{}
    $y = 56
    foreach ($row in $ControlRows) {
        $l = New-Object System.Windows.Forms.Label
        $l.Text = $row[1]; $l.Location = New-Object System.Drawing.Point(14, ($y + 3)); $l.Size = New-Object System.Drawing.Size(200, 22)
        $t = New-Object System.Windows.Forms.TextBox
        $t.Text = $map[$row[0]]; $t.Location = New-Object System.Drawing.Point(220, $y); $t.Size = New-Object System.Drawing.Size(185, 24)
        $t.BackColor = [System.Drawing.Color]::FromArgb(40, 32, 34); $t.ForeColor = $form.ForeColor; $t.BorderStyle = 'FixedSingle'
        $dlg.Controls.Add($l); $dlg.Controls.Add($t)
        $boxes[$row[0]] = $t
        $y += 27
    }
    $ok = New-Object System.Windows.Forms.Button
    $ok.Text = 'Save'; $ok.DialogResult = 'OK'; $ok.Location = New-Object System.Drawing.Point(220, ($y + 10)); $ok.Size = New-Object System.Drawing.Size(90, 30)
    $reset = New-Object System.Windows.Forms.Button
    $reset.Text = 'Defaults'; $reset.Location = New-Object System.Drawing.Point(315, ($y + 10)); $reset.Size = New-Object System.Drawing.Size(90, 30)
    $reset.Add_Click({ foreach ($row in $ControlRows) { $boxes[$row[0]].Text = $row[2] } }.GetNewClosure())
    foreach ($b in @($ok, $reset)) { $b.FlatStyle = 'Flat'; $b.BackColor = [System.Drawing.Color]::FromArgb(58, 30, 34); $dlg.Controls.Add($b) }
    $dlg.AcceptButton = $ok
    if ($dlg.ShowDialog($form) -eq 'OK') {
        foreach ($row in $ControlRows) { $map[$row[0]] = ($boxes[$row[0]].Text -replace '\s', '') }
        Write-Controls $map
        $statusLabel.Text = 'Controls saved to games\dmc\controls.txt (used next time you press Play).'
    }
}

function Read-Form {
    $settings.iso = $isoBox.Text.Trim()
    $settings.memcard = $cardBox.Text.Trim()
    $settings.scale = $scaleBox.SelectedIndex + 1
    $settings.fullscreen = $fullBox.Checked
    $settings.widescreen = $wideBox.Checked
    Save-Settings
}

function Check-Disc {
    if (-not $settings.iso -or -not (Test-Path $settings.iso)) {
        [System.Windows.Forms.MessageBox]::Show('Choose your Devil May Cry disc image (.iso) first.', 'Devil May Cry') | Out-Null
        return $false
    }
    return $true
}

function Start-Native {
    Read-Form
    if (-not (Check-Disc)) { return }
    foreach ($need in @($Native, (Join-Path (Split-Path $Native) 'SDL2.dll'), $Elf)) {
        if (-not (Test-Path $need)) {
            [System.Windows.Forms.MessageBox]::Show("Missing $need`nBuild the native runner first (see HANDOFF.md, 'Native window').", 'Devil May Cry') | Out-Null
            return
        }
    }
    if ($script:gameProcess -and -not $script:gameProcess.HasExited) { $statusLabel.Text = 'The game is already running.'; return }
    New-Item -ItemType Directory -Force (Split-Path $settings.memcard) | Out-Null
    $gameArgs = @("`"$Elf`"", '--disc', "`"$($settings.iso)`"", '--memcard', "`"$($settings.memcard)`"", '--window')
    if ($settings.fullscreen) { $gameArgs += '--fullscreen' }
    if ($settings.widescreen) { $gameArgs += '--widescreen' }
    $env:PS2_SCALE = [string]$settings.scale
    Write-Controls (Read-Controls)
    $env:PS2_KEYS = $ControlsPath
    $env:PS2_QUICKSAVE = Join-Path $Game 'quicksave.ps2'
    if ($DryRun) { Write-Output "native: PS2_SCALE=$env:PS2_SCALE PS2_KEYS=$env:PS2_KEYS $Native $($gameArgs -join ' ')"; return }
    # CreateNoWindow hides the runner's console without touching the game window; -WindowStyle Hidden would hide that too.
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = "$env:SystemRoot\System32\cmd.exe"
    $start.Arguments = "/s /c `"`"$Native`" $($gameArgs -join ' ') 2> `"$(Join-Path $Game 'launcher-game.log')`" > `"$(Join-Path $Game 'launcher-game.out')`"`""
    $start.WorkingDirectory = $Root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $script:gameProcess = [System.Diagnostics.Process]::Start($start)
    $statusLabel.Text = "Running in a window at $($settings.scale)x. Log: games\dmc\launcher-game.log"
}
$playNative.Add_Click({ Start-Native })

function Start-Browser {
    Read-Form
    if (-not (Check-Disc)) { return }
    if (-not (Test-Path (Join-Path $WebDir 'ps2web.html'))) {
        [System.Windows.Forms.MessageBox]::Show("The web build is missing. Build it with:`nscripts\build_web.ps1 dmc", 'Devil May Cry') | Out-Null
        return
    }
    if (-not (Test-Port)) {
        $python = (Get-Command python -ErrorAction SilentlyContinue).Source
        if (-not $python) { [System.Windows.Forms.MessageBox]::Show('Python 3 is needed to serve the browser version.', 'Devil May Cry') | Out-Null; return }
        $script:server = Start-Process -FilePath $python -ArgumentList @("`"$(Join-Path $Root 'scripts\serve.py')`"", "`"$WebDir`"", "`"$($settings.iso)`"", $Port) `
            -WorkingDirectory $Root -PassThru -WindowStyle Hidden
        for ($i = 0; $i -lt 50 -and -not (Test-Port); ++$i) { Start-Sleep -Milliseconds 100 }
    }
    $url = "http://localhost:$Port/?PS2_SCALE=$($settings.scale)&aspect=" + $(if ($settings.widescreen) { '16:9' } else { '4:3' }) + '&keys=' + (Controls-Param (Read-Controls))
    if ($DryRun) { Write-Output "browser: $url"; return }
    $browser = Find-Browser
    if ($browser) { Start-Process -FilePath $browser -ArgumentList @("--app=$url", '--start-maximized') } else { Start-Process $url }
    $statusLabel.Text = "Serving on http://localhost:$Port/ - the server stops when this launcher closes."
}
$playWeb.Add_Click({ Start-Browser })

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 1500
$updatePads = {
    $n = [XInputProbe]::Connected()
    $padLabel.Text = switch ($n) { -1 { 'Controller: XInput not available (keyboard works)' } 0 { 'Controller: none detected - keyboard controls below' } 1 { 'Controller: 1 connected' } default { "Controllers: $n connected (player 1 and 2)" } }
    if ($script:gameProcess -and $script:gameProcess.HasExited) {
        $statusLabel.Text = if ($script:gameProcess.ExitCode -eq 0) { 'Game closed.' } else { "Game exited with code $($script:gameProcess.ExitCode). See games\dmc\launcher-game.log" }
        $script:gameProcess = $null
    }
}
$timer.Add_Tick($updatePads)
& $updatePads
$timer.Start()

$form.Add_Shown({ $playNative.Focus() | Out-Null })
$form.Add_FormClosing({
    Read-Form
    $timer.Stop()
    if ($script:server -and -not $script:server.HasExited) { Stop-Process -Id $script:server.Id -Force -ErrorAction SilentlyContinue }
})
if ($Snapshot) {
    $form.Add_Shown({
        $form.Refresh()
        $bmp = New-Object System.Drawing.Bitmap($form.Width, $form.Height)
        $form.DrawToBitmap($bmp, (New-Object System.Drawing.Rectangle(0, 0, $form.Width, $form.Height)))
        $bmp.Save($Snapshot)
        $form.Close()
    })
    [void]$form.ShowDialog()
    return
}
if ($Play) {
    if ($Play -eq 'native') { Start-Native } else { Start-Browser }
    if ($DryRun) { return }
    if ($script:gameProcess) { $script:gameProcess.WaitForExit() }
    if ($script:server) { Write-Output 'Serving the browser version; close this window to stop.'; $script:server.WaitForExit() }
    return
}
[void]$form.ShowDialog()
