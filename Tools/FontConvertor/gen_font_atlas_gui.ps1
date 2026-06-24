Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$toolsDir  = Split-Path -Parent $MyInvocation.MyCommand.Definition
$msdfExe   = Join-Path $toolsDir "msdf-atlas-gen.exe"
$assetsDir = [System.IO.Path]::GetFullPath((Join-Path $toolsDir "..\..\DirectX\Game\Assets\Font"))

# ---- Form ----------------------------------------------------------------
$form                  = New-Object System.Windows.Forms.Form
$form.Text             = "MSDF Font Atlas Generator"
$form.ClientSize       = New-Object System.Drawing.Size(520, 370)
$form.StartPosition    = "CenterScreen"
$form.FormBorderStyle  = "FixedSingle"
$form.MaximizeBox      = $false
$form.Font             = New-Object System.Drawing.Font("Meiryo UI", 9)
$form.BackColor        = [System.Drawing.Color]::FromArgb(40, 40, 40)
$form.ForeColor        = [System.Drawing.Color]::WhiteSmoke

function New-Label($text, $x, $y) {
    $l = New-Object System.Windows.Forms.Label
    $l.Text      = $text
    $l.Location  = New-Object System.Drawing.Point($x, $y)
    $l.AutoSize  = $true
    $l.ForeColor = [System.Drawing.Color]::Silver
    $form.Controls.Add($l)
    return $l
}

# ---- フォントファイル -------------------------------------------------------
New-Label "フォントファイル" 12 14 | Out-Null

$txtFont          = New-Object System.Windows.Forms.TextBox
$txtFont.Location = New-Object System.Drawing.Point(12, 32)
$txtFont.Size     = New-Object System.Drawing.Size(398, 23)
$txtFont.ReadOnly = $true
$txtFont.BackColor = [System.Drawing.Color]::FromArgb(60, 60, 60)
$txtFont.ForeColor = [System.Drawing.Color]::WhiteSmoke
$txtFont.BorderStyle = "FixedSingle"
$form.Controls.Add($txtFont)

$btnBrowse          = New-Object System.Windows.Forms.Button
$btnBrowse.Text     = "参照..."
$btnBrowse.Location = New-Object System.Drawing.Point(418, 30)
$btnBrowse.Size     = New-Object System.Drawing.Size(82, 27)
$btnBrowse.FlatStyle = "Flat"
$btnBrowse.BackColor = [System.Drawing.Color]::FromArgb(70, 70, 70)
$btnBrowse.ForeColor = [System.Drawing.Color]::WhiteSmoke
$form.Controls.Add($btnBrowse)

# ---- 出力フォルダ名 ---------------------------------------------------------
New-Label "出力フォルダ名" 12 68 | Out-Null

$txtOut           = New-Object System.Windows.Forms.TextBox
$txtOut.Location  = New-Object System.Drawing.Point(12, 86)
$txtOut.Size      = New-Object System.Drawing.Size(398, 23)
$txtOut.BackColor = [System.Drawing.Color]::FromArgb(60, 60, 60)
$txtOut.ForeColor = [System.Drawing.Color]::WhiteSmoke
$txtOut.BorderStyle = "FixedSingle"
$form.Controls.Add($txtOut)

# ---- オプション -------------------------------------------------------------
New-Label "グリフサイズ (px)" 12  128 | Out-Null
New-Label "pxRange"          190 128 | Out-Null
New-Label "アトラスサイズ"   340 128 | Out-Null

$numSize           = New-Object System.Windows.Forms.NumericUpDown
$numSize.Location  = New-Object System.Drawing.Point(12, 146)
$numSize.Size      = New-Object System.Drawing.Size(70, 23)
$numSize.Minimum   = 16
$numSize.Maximum   = 128
$numSize.Value     = 32
$numSize.BackColor = [System.Drawing.Color]::FromArgb(60, 60, 60)
$numSize.ForeColor = [System.Drawing.Color]::WhiteSmoke
$form.Controls.Add($numSize)

$numPx            = New-Object System.Windows.Forms.NumericUpDown
$numPx.Location   = New-Object System.Drawing.Point(190, 146)
$numPx.Size       = New-Object System.Drawing.Size(70, 23)
$numPx.Minimum    = 2
$numPx.Maximum    = 32
$numPx.Value      = 8
$numPx.BackColor  = [System.Drawing.Color]::FromArgb(60, 60, 60)
$numPx.ForeColor  = [System.Drawing.Color]::WhiteSmoke
$form.Controls.Add($numPx)

$cmbAtlas              = New-Object System.Windows.Forms.ComboBox
$cmbAtlas.Location     = New-Object System.Drawing.Point(340, 146)
$cmbAtlas.Size         = New-Object System.Drawing.Size(90, 23)
$cmbAtlas.DropDownStyle = "DropDownList"
$cmbAtlas.BackColor    = [System.Drawing.Color]::FromArgb(60, 60, 60)
$cmbAtlas.ForeColor    = [System.Drawing.Color]::WhiteSmoke
[void]$cmbAtlas.Items.AddRange(@("2048 x 2048", "4096 x 4096", "8192 x 4096"))
$cmbAtlas.SelectedIndex = 1
$form.Controls.Add($cmbAtlas)

# ---- 出力先プレビュー -------------------------------------------------------
$lblPreview           = New-Object System.Windows.Forms.Label
$lblPreview.Location  = New-Object System.Drawing.Point(12, 182)
$lblPreview.Size      = New-Object System.Drawing.Size(490, 18)
$lblPreview.ForeColor = [System.Drawing.Color]::FromArgb(100, 180, 255)
$lblPreview.Font      = New-Object System.Drawing.Font("Consolas", 8)
$form.Controls.Add($lblPreview)

# ---- Generate ボタン --------------------------------------------------------
$btnGen            = New-Object System.Windows.Forms.Button
$btnGen.Text       = "Generate"
$btnGen.Location   = New-Object System.Drawing.Point(180, 208)
$btnGen.Size       = New-Object System.Drawing.Size(160, 38)
$btnGen.Font       = New-Object System.Drawing.Font("Meiryo UI", 11, [System.Drawing.FontStyle]::Bold)
$btnGen.FlatStyle  = "Flat"
$btnGen.BackColor  = [System.Drawing.Color]::FromArgb(0, 120, 215)
$btnGen.ForeColor  = [System.Drawing.Color]::White
$form.Controls.Add($btnGen)

# ---- ログ -------------------------------------------------------------------
$txtLog            = New-Object System.Windows.Forms.TextBox
$txtLog.Location   = New-Object System.Drawing.Point(12, 256)
$txtLog.Size       = New-Object System.Drawing.Size(490, 100)
$txtLog.Multiline  = $true
$txtLog.ScrollBars = "Vertical"
$txtLog.ReadOnly   = $true
$txtLog.BackColor  = [System.Drawing.Color]::FromArgb(20, 20, 20)
$txtLog.ForeColor  = [System.Drawing.Color]::LightGreen
$txtLog.Font       = New-Object System.Drawing.Font("Consolas", 8.5)
$txtLog.BorderStyle = "FixedSingle"
$form.Controls.Add($txtLog)

# ---- イベント ---------------------------------------------------------------
$updatePreview = {
    $name = $txtOut.Text.Trim()
    if ($name -ne "") {
        $lblPreview.Text = "→  $assetsDir\$name\atlas.json"
    } else {
        $lblPreview.Text = "→  (フォントを選択してください)"
    }
}

$btnBrowse.Add_Click({
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title            = "フォントファイルを選択"
    $dlg.Filter           = "Font Files (*.ttf;*.otf)|*.ttf;*.otf|All Files (*.*)|*.*"
    $dlg.InitialDirectory = if (Test-Path $assetsDir) { $assetsDir } else { $toolsDir }
    if ($dlg.ShowDialog() -eq "OK") {
        $txtFont.Text = $dlg.FileName
        if ($txtOut.Text.Trim() -eq "") {
            $txtOut.Text = [System.IO.Path]::GetFileNameWithoutExtension($dlg.FileName)
        }
        & $updatePreview
    }
})

$txtOut.Add_TextChanged({ & $updatePreview })

$btnGen.Add_Click({
    if ($txtFont.Text.Trim() -eq "") {
        [System.Windows.Forms.MessageBox]::Show(
            "フォントファイルを選択してください", "エラー",
            [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Warning) | Out-Null
        return
    }
    if (-not (Test-Path $msdfExe)) {
        [System.Windows.Forms.MessageBox]::Show(
            "msdf-atlas-gen.exe が見つかりません`n$msdfExe", "エラー",
            [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
        return
    }

    $charsetFile = Join-Path $assetsDir "charset.txt"
    if (-not (Test-Path $charsetFile)) {
        [System.Windows.Forms.MessageBox]::Show(
            "charset.txt が見つかりません`n$charsetFile", "エラー",
            [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
        return
    }

    $outName = if ($txtOut.Text.Trim() -ne "") { $txtOut.Text.Trim() } `
               else { [System.IO.Path]::GetFileNameWithoutExtension($txtFont.Text) }
    $outDir  = Join-Path $assetsDir $outName

    $atlasToken = $cmbAtlas.SelectedItem -split " x "
    $atlasW = [int]$atlasToken[0]
    $atlasH = [int]$atlasToken[1]
    $size    = [int]$numSize.Value
    $pxRange = [int]$numPx.Value

    if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

    $txtLog.ForeColor = [System.Drawing.Color]::LightGreen
    $txtLog.Clear()
    $txtLog.AppendText("生成開始...`r`n")
    $txtLog.AppendText("  フォント  : $($txtFont.Text)`r`n")
    $txtLog.AppendText("  出力先    : $outDir`r`n")
    $txtLog.AppendText("  Charset   : $charsetFile`r`n")
    $txtLog.AppendText("  サイズ    : ${size}px  pxRange: $pxRange  Atlas: ${atlasW}x${atlasH}`r`n`r`n")
    $form.Refresh()

    $btnGen.Enabled  = $false
    $btnGen.Text     = "生成中..."

    $argList = "-font `"$($txtFont.Text)`" -type msdf -format png " +
               "-imageout `"$outDir\atlas.png`" -json `"$outDir\atlas.json`" " +
               "-charset `"$charsetFile`" -size $size -pxrange $pxRange -dimensions $atlasW $atlasH"

    $proc = Start-Process -FilePath $msdfExe -ArgumentList $argList `
                          -NoNewWindow -Wait -PassThru `
                          -RedirectStandardOutput "$env:TEMP\msdf_out.txt" `
                          -RedirectStandardError  "$env:TEMP\msdf_err.txt"

    $stdout = Get-Content "$env:TEMP\msdf_out.txt" -Raw -ErrorAction SilentlyContinue
    $stderr = Get-Content "$env:TEMP\msdf_err.txt" -Raw -ErrorAction SilentlyContinue
    if ($stdout) { $txtLog.AppendText($stdout.Replace("`n", "`r`n")) }
    if ($stderr) { $txtLog.AppendText($stderr.Replace("`n", "`r`n")) }

    if ($proc.ExitCode -eq 0) {
        $txtLog.AppendText("`r`n[完了] atlas.json / atlas.png を出力しました`r`n")
        $txtLog.ForeColor = [System.Drawing.Color]::LightGreen
    } else {
        $txtLog.AppendText("`r`n[ERROR] 生成に失敗しました (exit: $($proc.ExitCode))`r`n")
        $txtLog.ForeColor = [System.Drawing.Color]::Salmon
    }

    $btnGen.Enabled = $true
    $btnGen.Text    = "Generate"
})

& $updatePreview
[void]$form.ShowDialog()
