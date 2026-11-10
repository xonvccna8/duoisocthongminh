@echo off
rem =====================================================================
rem  NAP FIRMWARE DUOI SOC - khong can chon menu trong Arduino IDE
rem  Cau hinh dung da ghi san: ESP32S3 Dev Module, Flash 8MB,
rem  Partition "8M with spiffs (3MB APP)", PSRAM OPI, Serial ra cong USB-UART (CH343).
rem
rem    NAP_FIRMWARE.bat              bien dich + nap duoi_soc_web (tu tim cong)
rem    NAP_FIRMWARE.bat COM8         nap vao cong chi dinh
rem    NAP_FIRMWARE.bat kiemtra      chi bien dich, khong nap
rem    NAP_FIRMWARE.bat test [COMx]  nap sketch hardware_test
rem =====================================================================
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "FQBN=esp32:esp32:esp32s3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB"
set "SKETCH=duoi_soc_web"
set "MODE=nap"
set "PORT="

:args
if "%~1"=="" goto args_done
if /i "%~1"=="kiemtra" (set "MODE=kiemtra") else if /i "%~1"=="test" (set "SKETCH=hardware_test") else set "PORT=%~1"
shift
goto args
:args_done

rem --- Tim arduino-cli: ban cai rieng, hoac ban di kem Arduino IDE ---
set "CLI=C:\Program Files\Arduino CLI\arduino-cli.exe"
if not exist "%CLI%" set "CLI=%LOCALAPPDATA%\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe"
if not exist "%CLI%" (
  echo [LOI] Khong tim thay arduino-cli. Hay cai Arduino IDE 2.
  goto end
)

echo.
echo ==== BIEN DICH %SKETCH% ====
echo Lan dau mat khoang 5-10 phut, cac lan sau nhanh hon.
"%CLI%" compile --fqbn "%FQBN%" --build-path "%~dp0.build\%SKETCH%" "%SKETCH%"
if errorlevel 1 (
  echo.
  echo [LOI] Bien dich that bai. Thieu thu vien thi cai bang lenh:
  echo   "%CLI%" lib install DFRobotDFPlayerMini RTClib BH1750 "Adafruit INA219" "Adafruit SSD1306"
  goto end
)
if /i "%MODE%"=="kiemtra" (
  echo.
  echo [OK] Bien dich thanh cong. Chua nap vao board.
  goto end
)

rem --- Tu tim cong ESP32 neu chua chi dinh ---
if "%PORT%"=="" (
  "%CLI%" board list > "%TEMP%\duoisoc_ports.txt" 2>nul
  for /f "tokens=1" %%p in ('findstr /i /r /c:"^COM[0-9]*.*ESP32" /c:"^COM[0-9]*.*CH34" /c:"^COM[0-9]*.*USB" "%TEMP%\duoisoc_ports.txt"') do if "!PORT!"=="" set "PORT=%%p"
)
if "%PORT%"=="" (
  echo [LOI] Khong thay board ESP32. Cam cap USB roi chay lai, hoac ghi cong: NAP_FIRMWARE.bat COM8
  goto end
)

rem --- Cong dang bi chuong trinh khac giu (Serial Monitor, he thong canh bao...) ---
mode %PORT% >nul 2>&1
if errorlevel 1 (
  echo.
  echo [LOI] Cong %PORT% dang bi chuong trinh khac su dung.
  echo   - Dong Serial Monitor trong Arduino IDE.
  echo   - Tam tat he thong canh bao du an DUANHUYNHTHAINGUYEN ^(TU_CHAY.bat go^), nap xong chay lai TU_CHAY.bat
  goto end
)

echo.
echo ==== NAP VAO %PORT% ====
"%CLI%" upload -p %PORT% --fqbn "%FQBN%" --input-dir "%~dp0.build\%SKETCH%" "%SKETCH%"
if errorlevel 1 (
  echo.
  echo [LOI] Nap that bai.
  echo   - Bao "port is busy / Access is denied": cong bi chuong trinh khac chiem ^(Serial Monitor, he thong canh bao^)
  echo   - Bao loi khac: giu nut BOOT, bam RST, tha BOOT roi chay lai.
  goto end
)
echo.
echo [OK] Da nap xong %SKETCH% vao %PORT%. Board dang khoi dong lai.

:end
echo.
pause
