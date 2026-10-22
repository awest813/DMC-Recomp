# Launcher presentation. Dot-sourced so actions and settings live in launcher.ps1.
$form = New-Object System.Windows.Forms.Form
$form.Text = 'Devil May Cry - Launcher'
$form.ClientSize = New-Object System.Drawing.Size(880, 650)
$form.MinimumSize = New-Object System.Drawing.Size(820, 680)
$form.StartPosition = 'CenterScreen'
$form.AutoScaleMode = 'Dpi'
$form.AutoScaleDimensions = New-Object System.Drawing.SizeF(96, 96)
$form.BackColor = [System.Drawing.Color]::FromArgb(23, 23, 29)
$form.ForeColor = [System.Drawing.Color]::FromArgb(239, 236, 241)
$form.Font = New-Object System.Drawing.Font('Segoe UI', 10)
$muted = [System.Drawing.Color]::FromArgb(173, 168, 181)

function Add-Label($text, $x, $y, $w = 120, $font = $null, $parent = $form) {
    $l = New-Object System.Windows.Forms.Label
    $l.Text = $text; $l.Location = New-Object System.Drawing.Point($x, $y); $l.Size = New-Object System.Drawing.Size($w, 25)
    if ($font) { $l.Font = $font }
    $parent.Controls.Add($l); return $l
}
function Add-Button($text, $x, $y, $w, $h = 34, $parent = $form) {
    $b = New-Object System.Windows.Forms.Button
    $b.Text = $text; $b.Location = New-Object System.Drawing.Point($x, $y); $b.Size = New-Object System.Drawing.Size($w, $h)
    $b.FlatStyle = 'Flat'; $b.BackColor = [System.Drawing.Color]::FromArgb(45, 43, 53); $b.ForeColor = $form.ForeColor
    $b.FlatAppearance.BorderColor = [System.Drawing.Color]::FromArgb(82, 75, 91)
    $b.FlatAppearance.MouseOverBackColor = [System.Drawing.Color]::FromArgb(64, 56, 72)
    $b.Cursor = 'Hand'
    $parent.Controls.Add($b); return $b
}
function Add-PathRow($label, $y, $value, $filter, $save, $parent) {
    Add-Label $label 24 $y 500 $null $parent | Out-Null
    $t = New-Object System.Windows.Forms.TextBox
    $t.Location = New-Object System.Drawing.Point(24, ($y + 29)); $t.Size = New-Object System.Drawing.Size(674, 28)
    $t.Anchor = 'Top,Left,Right'; $t.Text = $value
    $t.BackColor = [System.Drawing.Color]::FromArgb(35, 34, 43); $t.ForeColor = $form.ForeColor; $t.BorderStyle = 'FixedSingle'
    $parent.Controls.Add($t)
    $b = Add-Button 'Browse...' 710 ($y + 26) 110 32 $parent
    $b.Anchor = 'Top,Right'
    $b.Add_Click({
        $d = if ($save) { New-Object System.Windows.Forms.SaveFileDialog } else { New-Object System.Windows.Forms.OpenFileDialog }
        $d.Filter = $filter
        if ($save) { $d.OverwritePrompt = $false }
        if ($t.Text) { try { $d.InitialDirectory = Split-Path $t.Text -Parent; $d.FileName = Split-Path $t.Text -Leaf } catch {} }
        if ($d.ShowDialog($form) -eq 'OK') { $t.Text = $d.FileName }
        $d.Dispose()
    }.GetNewClosure())
    return $t
}

$tabs = New-Object System.Windows.Forms.TabControl
$tabs.Dock = 'Fill'; $tabs.Padding = New-Object System.Drawing.Point(24, 10)
$tabs.Font = New-Object System.Drawing.Font('Segoe UI', 10)
$pages = @{}
foreach ($name in @('Play', 'Settings', 'Help')) {
    $p = New-Object System.Windows.Forms.TabPage
    $p.Text = if ($name -eq 'Help') { 'Controls & help' } else { $name }
    $p.BackColor = $form.BackColor; $p.ForeColor = $form.ForeColor
    $tabs.TabPages.Add($p); $pages[$name] = $p
}
$form.Controls.Add($tabs)
$header = New-Object System.Windows.Forms.Panel
$header.Dock = 'Top'; $header.Height = 100; $header.BackColor = [System.Drawing.Color]::FromArgb(31, 25, 34)
$form.Controls.Add($header)
$title = Add-Label 'DEVIL MAY CRY' 24 12 600 (New-Object System.Drawing.Font('Segoe UI', 24, [System.Drawing.FontStyle]::Bold)) $header
$title.Height = 46
(Add-Label 'Play in a window or your browser.' 27 62 700 $null $header).ForeColor = $muted

# Inactive tab pages have a small default size until selected. Establish their
# bounds before anchoring path fields, otherwise the first selection pushes Browse offscreen.
$form.PerformLayout()
foreach ($page in $pages.Values) { $page.Size = $tabs.DisplayRectangle.Size }

$p = $pages.Play
$summaryTitle = Add-Label 'Checking setup...' 24 20 790 (New-Object System.Drawing.Font('Segoe UI', 15, [System.Drawing.FontStyle]::Bold)) $p
$summaryTitle.Height = 32; $summaryTitle.Anchor = 'Top,Left,Right'
$summaryDetail = Add-Label '' 24 58 790 $null $p
$summaryDetail.Height = 45; $summaryDetail.Anchor = 'Top,Left,Right'; $summaryDetail.ForeColor = $muted
$windowMode = New-Object System.Windows.Forms.RadioButton
$windowMode.Text = 'Window (recommended)'; $windowMode.Location = New-Object System.Drawing.Point(24, 115); $windowMode.Size = New-Object System.Drawing.Size(270, 30)
$windowMode.Checked = $settings.mode -ne 'browser'; $p.Controls.Add($windowMode)
$browserMode = New-Object System.Windows.Forms.RadioButton
$browserMode.Text = 'Browser'; $browserMode.Location = New-Object System.Drawing.Point(320, 115); $browserMode.Size = New-Object System.Drawing.Size(180, 30)
$browserMode.Checked = -not $windowMode.Checked; $p.Controls.Add($browserMode)
$playButton = Add-Button 'Start game' 24 163 278 52 $p
$playButton.Font = New-Object System.Drawing.Font('Segoe UI', 13, [System.Drawing.FontStyle]::Bold)
$playButton.BackColor = [System.Drawing.Color]::FromArgb(167, 37, 57)
$playButton.FlatAppearance.MouseOverBackColor = [System.Drawing.Color]::FromArgb(194, 43, 64)
$resumeButton = Add-Button 'Resume quick save' 318 163 230 52 $p
$setupButton = Add-Button 'Game settings' 564 163 200 52 $p
$setupButton.Add_Click({ $tabs.SelectedTab = $pages.Settings })
$resumeInfo = Add-Label '' 24 227 790 $null $p
$resumeInfo.ForeColor = $muted; $resumeInfo.Anchor = 'Top,Left,Right'
$statusLabel = Add-Label 'No game running.' 24 266 790 $null $p
$statusLabel.Height = 50; $statusLabel.Anchor = 'Top,Left,Right'
$padLabel = Add-Label '' 24 330 790 $null $p
$padLabel.ForeColor = $muted; $padLabel.Anchor = 'Top,Left,Right'
$hint = Add-Label 'Default keys:  Arrows: move     S: jump     X: sword     A: shoot     Z: confirm' 24 378 790 $null $p
$hint.Height = 34; $hint.Anchor = 'Top,Left,Right'
(Add-Label 'Enter: menu     F5: quick save in window     F11: fullscreen in window' 24 416 790 $null $p).ForeColor = $muted

$p = $pages.Settings
$isoBox = Add-PathRow 'Devil May Cry disc image (.iso)' 20 $settings.iso 'Disc images (*.iso)|*.iso|All files|*.*' $false $p
(Add-Label 'Use your own US-release disc image. The disc stays on this computer.' 24 82 790 $null $p).ForeColor = $muted
$cardBox = Add-PathRow 'Window memory card' 122 $settings.memcard 'PS2 memory cards (*.ps2)|*.ps2|All files|*.*' $true $p
$cardHelp = Add-Label 'Choose an existing card or a new filename. Browser saves use Import / Export card in the game.' 24 184 790 $null $p
$cardHelp.Height = 45; $cardHelp.Anchor = 'Top,Left,Right'; $cardHelp.ForeColor = $muted
Add-Label 'Picture quality' 24 241 200 $null $p | Out-Null
$scaleBox = New-Object System.Windows.Forms.ComboBox
$scaleBox.DropDownStyle = 'DropDownList'; $scaleBox.Location = New-Object System.Drawing.Point(228, 238); $scaleBox.Size = New-Object System.Drawing.Size(480, 30)
[void]$scaleBox.Items.AddRange(@('1x - Original detail, lightest load', '2x - Sharper picture (recommended)', '3x - Sharpest picture, more demanding'))
$scaleBox.SelectedIndex = [Math]::Max(0, [Math]::Min(2, [int]$settings.scale - 1)); $p.Controls.Add($scaleBox)
$fullBox = New-Object System.Windows.Forms.CheckBox
$fullBox.Text = 'Start fullscreen in window mode'; $fullBox.Location = New-Object System.Drawing.Point(228, 286); $fullBox.Size = New-Object System.Drawing.Size(540, 28)
$fullBox.Checked = [bool]$settings.fullscreen; $p.Controls.Add($fullBox)
$wideBox = New-Object System.Windows.Forms.CheckBox
$wideBox.Text = 'Widescreen (16:9) - stretches the original 4:3 picture'; $wideBox.Location = New-Object System.Drawing.Point(228, 326); $wideBox.Size = New-Object System.Drawing.Size(590, 28)
$wideBox.Checked = [bool]$settings.widescreen; $p.Controls.Add($wideBox)
$saveSettings = Add-Button 'Save settings' 24 400 185 40 $p
$saveSettings.Add_Click({ if (Save-Form) { $tabs.SelectedTab = $pages.Play; $statusLabel.Text = 'Settings saved.' } })
$openSaves = Add-Button 'Open save folder' 228 400 185 40 $p
$openSaves.Add_Click({ New-Item -ItemType Directory -Force $Game | Out-Null; Start-Process explorer.exe -ArgumentList "`"$Game`"" })

$p = $pages.Help
$controlsButton = Add-Button 'Customize keyboard...' 24 22 240 42 $p
$controlsButton.Add_Click({ Show-Controls })
$help = Add-Label @'
DEFAULT KEYBOARD                              CONTROLLER
Arrows or I / J / K / L   Move                Left stick   Move
S   Jump / action       X   Sword            Y   Action   B   Sword
A   Shoot               Z   Examine / OK     X   Shoot    A   Confirm
W   Aim                 Q   Devil trigger    RB  Aim      LB  Devil trigger
1   Map                 Enter   Menu         LT  Map      Start   Menu
'@ 24 86 790 (New-Object System.Drawing.Font('Consolas', 10)) $p
$help.Height = 163; $help.Anchor = 'Top,Left,Right'
$moreHelp = Add-Label @'
QUICK SAVES & GAME SAVES
In a window, F5 creates a quick save. Use Resume quick save on the Play screen to return to it.
In-game saves use your memory card. Browser cards are stored by the browser; export a backup in the game.

IF SOMETHING GOES WRONG
Try 1x picture quality if sound breaks up or the game slows down. Keep the launcher open for browser play.
In a browser, press Alt+Enter for fullscreen. Window mode uses F11.
'@ 24 264 790 $null $p
$moreHelp.Height = 170; $moreHelp.Anchor = 'Top,Left,Right'; $moreHelp.ForeColor = $muted
$logsButton = Add-Button 'Open game log' 24 445 185 34 $p
$logsButton.Add_Click({ $log = Join-Path $Game 'launcher-game.log'; if (Test-Path $log) { Start-Process notepad.exe -ArgumentList "`"$log`"" } else { $statusLabel.Text = 'No game log yet. Start a window game first.'; $tabs.SelectedTab = $pages.Play } })
$form.AcceptButton = $playButton
