$path = "D:\Project\音乐显示\cpp\gui\main_gui.cpp"
$content = Get-Content $path -Raw
if ($content -match '(g_tabSlideAnim\.update\(\);)') {
    $replacement = "$1`r`n`r`n    // 显示模式滑动动画`r`n    g_displayModeSlideAnim.update();`r`n"
    $newContent = $content -replace '(g_tabSlideAnim\.update\(\);)', $replacement
    Set-Content $path -Value $newContent -Encoding UTF8 -NoNewline
    Write-Host "Added g_displayModeSlideAnim.update()"
} else {
    Write-Host "Pattern not found"
}