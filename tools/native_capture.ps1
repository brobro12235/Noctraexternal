param([string]$Output = "artifacts/menu.png")
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class NoctraWindowCapture {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
}
'@
$process = Get-Process -Name Noctra -ErrorAction Stop | Where-Object { $_.MainWindowTitle -eq "NOCTRA - External" } | Select-Object -First 1
if ($null -eq $process -or $process.MainWindowHandle -eq [IntPtr]::Zero) { throw "NOCTRA menu is not open" }
$rect = New-Object NoctraWindowCapture+RECT
if (-not [NoctraWindowCapture]::GetWindowRect($process.MainWindowHandle, [ref]$rect)) { throw "Cannot locate native window" }
$bitmap = New-Object System.Drawing.Bitmap(($rect.Right-$rect.Left), ($rect.Bottom-$rect.Top))
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$dc = $graphics.GetHdc()
try {
    if (-not [NoctraWindowCapture]::PrintWindow($process.MainWindowHandle, $dc, 2)) { throw "Native window capture failed" }
} finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
try {
    $parent = Split-Path -Parent $Output
    if ($parent) { [System.IO.Directory]::CreateDirectory((Join-Path (Get-Location) $parent)) | Out-Null }
    $bitmap.Save((Join-Path (Get-Location) $Output), [System.Drawing.Imaging.ImageFormat]::Png)
} finally { $bitmap.Dispose() }
Write-Output "Captured NOCTRA menu only."
