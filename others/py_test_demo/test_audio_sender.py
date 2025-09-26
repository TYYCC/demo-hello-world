# test_audio_sender.py
# 用于测试发送MP3解码后的PCM数据到ESP32
# 依赖: pip install pydub
# 还需要安装ffmpeg: https://ffmpeg.org/download.html

import socket
import sys
from pydub import AudioSegment
import os
import tkinter as tk
from tkinter import filedialog, simpledialog

ESP32_IP = "192.168.233.247"

# 采样率（与ESP32一致）
SAMPLE_RATE = 44100

def send_audio():
    root = tk.Tk()
    root.withdraw()  # 隐藏主窗口

    # 选择MP3文件
    MP3_FILE = filedialog.askopenfilename(title="选择MP3文件", filetypes=[("MP3 files", "*.mp3")])
    if not MP3_FILE:
        print("No file selected")
        sys.exit(1)

    if not os.path.exists(MP3_FILE):
        print(f"File not found: {MP3_FILE}")
        sys.exit(1)

    # 输入ESP32 IP
    ESP32_IP = simpledialog.askstring("输入ESP32 IP", "请输入ESP32的IP地址:", initialvalue="192.168.1.100")
    if not ESP32_IP:
        print("No IP provided")
        sys.exit(1)

    ESP32_PORT = 7557

    # 加载MP3文件
    audio = AudioSegment.from_mp3(MP3_FILE)

    # 转换为PCM（raw）格式，单声道，16位，采样率44100
    audio = audio.set_channels(1).set_frame_rate(SAMPLE_RATE).set_sample_width(2)
    pcm_data = audio.raw_data

    print(f"Audio loaded: {len(pcm_data)} bytes, sample rate: {SAMPLE_RATE}")

    # 创建TCP socket
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        try:
            sock.connect((ESP32_IP, ESP32_PORT))
            print(f"Connected to {ESP32_IP}:{ESP32_PORT}")

            # 分块发送数据
            chunk_size = 4096
            for i in range(0, len(pcm_data), chunk_size):
                chunk = pcm_data[i:i + chunk_size]
                sock.sendall(chunk)
                print(f"Sent {len(chunk)} bytes")

            print("All data sent")
        except Exception as e:
            print(f"Error: {e}")

if __name__ == '__main__':
    send_audio()