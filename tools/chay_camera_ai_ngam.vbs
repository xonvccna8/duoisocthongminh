' Chạy ngầm (không hiện cửa sổ) chương trình xem camera + AI YOLO trên GPU: tools\xem_camera_ai.py
' Bản sao trong thư mục Startup của Windows để tự chạy khi đăng nhập. Nhật ký: ai_soc\logs\xem_camera_ai.log
' Đã có bản đang chạy thì bản mới tự thoát (không chạy 2 bản).
Set fso = CreateObject("Scripting.FileSystemObject")
root = "D:\DU AN KHKT 2026-2027\DUANTHANHLAMDONG"
WScript.Sleep 20000   ' chờ WiFi và Tailscale sẵn sàng sau khi bật máy
CreateObject("WScript.Shell").Run "cmd /c cd /d """ & root & """ && set PYTHONIOENCODING=utf-8 && python -u tools\xem_camera_ai.py > ai_soc\logs\xem_camera_ai.log 2>&1", 0, False
