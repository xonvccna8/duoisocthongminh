# Nhúng giao diện web vào firmware ESP32:
#   web/index.html  -> duoi_soc_web/web_page.h   (trang HTML nén gzip)
#   web/img/*.jpg   -> duoi_soc_web/img_assets.h (ảnh thu nhỏ WebP từ tools/fw_images.py, phục vụ tại /img/<tên>.jpg)
# Nén để firmware vừa phân vùng MẶC ĐỊNH của Arduino IDE (1,25MB) - không cần chọn Partition Scheme.
# Chạy lại mỗi khi sửa giao diện hoặc ảnh:   powershell -ExecutionPolicy Bypass -File tools\build_web.ps1
$root = Split-Path -Parent $PSScriptRoot
$utf8 = New-Object Text.UTF8Encoding $false

function Append-Bytes($sb, [byte[]]$bytes) {
  for ($o = 0; $o -lt $bytes.Length; $o += 32) {
    $n = [Math]::Min(32, $bytes.Length - $o)
    [void]$sb.Append('0x').Append([BitConverter]::ToString($bytes, $o, $n).Replace('-', ',0x')).Append(",`n")
  }
}

# ---- Trang HTML (gzip, trình duyệt tự giải nén) ----
$src = Join-Path $root 'web\index.html'
$dst = Join-Path $root 'duoi_soc_web\web_page.h'
$raw = [IO.File]::ReadAllBytes($src)
$ms = New-Object IO.MemoryStream
$gz = New-Object IO.Compression.GZipStream($ms, [IO.Compression.CompressionLevel]::Optimal)
$gz.Write($raw, 0, $raw.Length); $gz.Close()
$html = $ms.ToArray()
$sb = New-Object Text.StringBuilder
[void]$sb.Append("#pragma once`n// TU DONG SINH tu web/index.html bang tools/build_web.ps1 - KHONG sua truc tiep file nay.`n")
[void]$sb.Append("// Trang HTML nen gzip ($($raw.Length) -> $($html.Length) bytes), gui kem header Content-Encoding: gzip`n")
[void]$sb.Append("#include <pgmspace.h>`n#include <stdint.h>`n`nconst uint8_t INDEX_HTML_GZ[] PROGMEM = {`n")
Append-Bytes $sb $html
[void]$sb.Append("};`n")
[IO.File]::WriteAllText($dst, $sb.ToString(), $utf8)
Write-Host ("Da tao web_page.h  (HTML {0:N1} KB -> gzip {1:N1} KB)" -f ($raw.Length / 1KB), ($html.Length / 1KB))

# ---- Ảnh: thu nhỏ + WebP (cần Python + Pillow); lỗi thì dùng ảnh gốc ----
$imgDir = Join-Path $root 'web\img_fw'
python (Join-Path $root 'tools\fw_images.py')
if ($LASTEXITCODE -ne 0) {
  Write-Warning 'Khong chay duoc tools/fw_images.py (can: pip install pillow) - dung anh goc, firmware se qua lon cho phan vung mac dinh.'
  $imgDir = Join-Path $root 'web\img'
}
$dstImg = Join-Path $root 'duoi_soc_web\img_assets.h'
$sb = New-Object Text.StringBuilder
[void]$sb.Append("#pragma once`n// TU DONG SINH tu web/img/ bang tools/build_web.ps1 - KHONG sua truc tiep file nay.`n")
[void]$sb.Append("#include <pgmspace.h>`n#include <stddef.h>`n#include <stdint.h>`n`n")
[void]$sb.Append("struct WebAsset { const char *path; const char *mime; const uint8_t *data; size_t len; };`n`n")
$table = @(); $i = 0; $total = 0
foreach ($f in Get-ChildItem $imgDir -File | Where-Object { $_.Extension -match '^\.(jpe?g|png|svg|webp)$' } | Sort-Object Name) {
  $bytes = [IO.File]::ReadAllBytes($f.FullName); $total += $bytes.Length
  $mime = switch -Regex ($f.Extension) { 'jpe?g' {'image/jpeg'} 'png' {'image/png'} 'svg' {'image/svg+xml'} default {'image/webp'} }
  # Trang vẫn gọi /img/<tên>.jpg; bản WebP được phục vụ ở cùng đường dẫn đó
  $url = if ($f.Extension -eq '.webp') { "/img/$($f.BaseName).jpg" } else { "/img/$($f.Name)" }
  [void]$sb.Append("// $($f.Name) ($($bytes.Length) bytes)`nstatic const uint8_t ASSET_$i[] PROGMEM = {`n")
  Append-Bytes $sb $bytes
  [void]$sb.Append("};`n`n")
  $table += "  {`"$url`", `"$mime`", ASSET_$i, sizeof(ASSET_$i)},"
  $i++
}
[void]$sb.Append("static const WebAsset WEB_ASSETS[] = {`n" + ($table -join "`n") + "`n};`n")
[void]$sb.Append("static const size_t WEB_ASSET_COUNT = sizeof(WEB_ASSETS) / sizeof(WEB_ASSETS[0]);`n")
[IO.File]::WriteAllText($dstImg, $sb.ToString(), $utf8)
Write-Host ("Da tao img_assets.h ({0} anh, {1:N1} KB du lieu)" -f $i, ($total / 1KB))
