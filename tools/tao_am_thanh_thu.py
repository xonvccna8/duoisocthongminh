# Tạo file âm thanh THỬ LOA cho firmware (MAX98357A): sound/1.wav ... sound/4.wav
# WAV 16-bit PCM, mono, 16000 Hz - đúng định dạng firmware đọc được.
# Chép cả thư mục "sound" vào thư mục gốc của thẻ nhớ cắm trên board ESP32.
import wave
from pathlib import Path
import numpy as np

RATE = 16000
OUT = Path(__file__).resolve().parent.parent / 'sound'


def tone(freq, sec):
    """Sóng sin, freq là số hoặc mảng tần số theo thời gian (Hz)."""
    n = int(RATE * sec)
    f = np.broadcast_to(np.asarray(freq, dtype=float), (n,))
    return np.sin(2 * np.pi * np.cumsum(f) / RATE)


def save(name, sig):
    sig = sig / max(1e-9, np.abs(sig).max()) * 0.9
    fade = int(RATE * 0.01)   # vào/ra 10ms cho khỏi "bụp"
    sig[:fade] *= np.linspace(0, 1, fade)
    sig[-fade:] *= np.linspace(1, 0, fade)
    with wave.open(str(OUT / name), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes((sig * 32767).astype('<i2').tobytes())


OUT.mkdir(exist_ok=True)
t = np.arange(int(RATE * 5)) / RATE
# 1: còi hú lên xuống (600-1600 Hz)
save('1.wav', tone(1100 + 500 * np.sin(2 * np.pi * 0.8 * t), 5))
# 2: bíp ngắt quãng 2 kHz
save('2.wav', tone(2000, 5) * ((t * 6) % 1 < 0.5))
# 3: quét tần số 300 -> 4000 Hz (thử loa từ trầm tới bổng)
save('3.wav', tone(np.geomspace(300, 4000, len(t)), 5))
# 4: tiếng kêu chim săn mồi (chuỗi tiếng rít trượt xuống)
chirp = np.concatenate([tone(np.linspace(3200, 1800, int(RATE * 0.35)), 0.35) * np.hanning(int(RATE * 0.35)),
                        np.zeros(int(RATE * 0.25))])
save('4.wav', np.tile(chirp, 8))
print('Da tao:', ', '.join(sorted(p.name for p in OUT.glob('*.wav'))), '->', OUT)
