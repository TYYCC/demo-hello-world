import socket
import cv2
import numpy as np
import time
import sys
from tkinter import Tk, filedialog
import argparse
import lz4.frame

ESP32_IP = '192.168.123.159'  # 修改为你的 ESP32 IP 地址
ESP32_PORT = 6556           # ESP32 监听的端口
MAX_IMAGE_SIZE_BYTES = 90 * 1024  # 90KB single buffer
TARGET_RESOLUTION = (240, 180)

def select_video_file():
    """
    弹出文件选择对话框，选择要发送的视频文件
    返回视频文件路径
    """
    root = Tk()
    root.withdraw()  # 隐藏主窗口
    file_path = filedialog.askopenfilename(
        title="Select a video file",
        filetypes=[("Video files", "*.mp4 *.avi *.mov"), ("All files", "*.*")])
    root.destroy()
    return file_path

def resize_with_aspect_ratio(image, target_resolution):
    """
    等比缩放图像，使其适应目标分辨率
    """
    h, w = image.shape[:2]
    target_w, target_h = target_resolution
    ratio_w = target_w / w
    ratio_h = target_h / h
    ratio = min(ratio_w, ratio_h)
    new_w = int(w * ratio)
    new_h = int(h * ratio)
    return cv2.resize(image, (new_w, new_h), interpolation=cv2.INTER_AREA)

def encode_frame(frame, encoding):
    if encoding == 'jpeg':
        jpeg_quality = 70  # 合理的起始质量
        while True:
            encode_param = [int(cv2.IMWRITE_JPEG_QUALITY), jpeg_quality]
            result, encimg = cv2.imencode('.jpg', frame, encode_param)
            if not result:
                print("Error: Failed to encode frame.")
                return None, None

            if len(encimg) <= MAX_IMAGE_SIZE_BYTES:
                return encimg.tobytes(), jpeg_quality

            jpeg_quality -= 5  # 逐步降低质量
            if jpeg_quality < 40:  # 质量下限
                print("Warning: Cannot compress image under 90KB")
                return None, None
    elif encoding == 'lz4':
        # 先转换为RGB565格式，然后再压缩（按BGR通道构建并进行字节序交换以匹配LVGL配置）
        if len(frame.shape) == 3:
            # OpenCV帧是BGR顺序，直接按BGR提取，避免额外颜色转换
            b = frame[:, :, 0].astype(np.uint16)
            g = frame[:, :, 1].astype(np.uint16)
            r = frame[:, :, 2].astype(np.uint16)

            # 构建RGB565（R高位、G中位、B低位）
            r5 = (r >> 3) & 0x1F
            g6 = (g >> 2) & 0x3F
            b5 = (b >> 3) & 0x1F
            rgb565 = (r5 << 11) | (g6 << 5) | b5

            # 固件LVGL使用 LV_COLOR_DEPTH=16 且 LV_COLOR_16_SWAP=1，需进行字节序交换
            rgb565 = rgb565.byteswap()
            rgb565_bytes = rgb565.tobytes()

            compressed_data = lz4.frame.compress(rgb565_bytes)
            print(f"Frame LZ4 compressed: {len(rgb565_bytes)} -> {len(compressed_data)} bytes")
            return compressed_data, None
        else:
            raw_data = frame.tobytes()
            compressed_data = lz4.frame.compress(raw_data)
            return compressed_data, None
    elif encoding == 'raw':
        # 转换为RGB565格式发送给ESP32（按BGR通道构建并进行字节序交换以匹配LVGL配置）
        if len(frame.shape) == 3:
            # OpenCV帧是BGR顺序，直接提取
            b = frame[:, :, 0].astype(np.uint16)
            g = frame[:, :, 1].astype(np.uint16)
            r = frame[:, :, 2].astype(np.uint16)

            # 构建RGB565
            r5 = (r >> 3) & 0x1F
            g6 = (g >> 2) & 0x3F
            b5 = (b >> 3) & 0x1F
            rgb565 = (r5 << 11) | (g6 << 5) | b5

            # 匹配固件的LVGL颜色字节序
            rgb565 = rgb565.byteswap()
            rgb565_bytes = rgb565.tobytes()

            height, width = frame.shape[:2]
            print(f"Frame converted to RGB565: {width}x{height}, {len(rgb565_bytes)} bytes")
            return rgb565_bytes, None
        else:
            print(f"Unexpected frame format: shape={frame.shape}")
            return frame.tobytes(), None
    else:
        print(f"Unknown encoding: {encoding}")
        return None, None


def send_video_to_esp32(video_source, encoding):
    """
    捕获视频，实时编码、压缩并发送到 ESP32
    """
    cap = cv2.VideoCapture(video_source)
    if not cap.isOpened():
        print(f"Error: Cannot open video source: {video_source}")
        return

    last_frame_time = time.time()
    frame_count = 0

    try:
        while True:
            print(f"Connecting to ESP32 {ESP32_IP}:{ESP32_PORT} ...")
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                    s.connect((ESP32_IP, ESP32_PORT))
                    s.settimeout(5.0)  # 增加超时时间到5秒
                    print("Connected. Starting video stream...")

                    while True:
                        ret, frame = cap.read()
                        if not ret:
                            # 如果是视频文件，循环播放
                            if isinstance(video_source, str):
                                print("Video ended. Restarting...")
                                cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                                continue
                            else:
                                print("Error: Can't receive frame (stream end?). Exiting ...")
                                break

                        # 1. 等比缩放
                        resized_frame = resize_with_aspect_ratio(frame, TARGET_RESOLUTION)

                        # 2. 编码
                        encoded_data, quality = encode_frame(resized_frame, encoding)
                        
                        if not encoded_data:
                            continue

                        # 3. 发送图像数据（按照TCP图传协议格式）
                        try:
                            # 构建协议数据包
                            sync_word = 0xAEBC1402  # 同步头/魔数
                            
                            # 确定数据类型
                            if encoding == 'jpeg':
                                data_type = 0x01  # JPEG编码
                            elif encoding == 'lz4':
                                data_type = 0x02  # LZ4压缩
                            elif encoding == 'raw':
                                data_type = 0x03  # 原始数据
                            else:
                                data_type = 0x01  # 默认JPEG
                            
                            data_len = len(encoded_data)  # 数据负载长度

                            print(f"Sending {encoding} data: {data_len} bytes, type: {data_type}")

                            # 构建协议头（使用小端字节序，与ESP32内存布局匹配）
                            protocol_header = sync_word.to_bytes(4, 'little') + \
                                            data_type.to_bytes(1, 'little') + \
                                            data_len.to_bytes(4, 'little')

                            total_size = len(protocol_header) + data_len
                            print(f"Total packet size: {total_size} bytes (header: {len(protocol_header)}, data: {data_len})")

                            # 发送协议头 + 图像数据
                            s.sendall(protocol_header + encoded_data)
                        except socket.error as e:
                            print(f"Socket error: {e}. Reconnecting...")
                            break # 发送失败，跳出内层循环以重新连接

                        # 4. 计算并显示帧率
                        frame_count += 1
                        current_time = time.time()
                        elapsed_time = current_time - last_frame_time
                        if elapsed_time >= 1.0:
                            fps = frame_count / elapsed_time
                            quality_str = f", Quality: {quality}" if quality is not None else ""
                            print(f"FPS: {fps:.2f}, Frame size: {len(encoded_data)/1024:.2f} KB, Encoding: {encoding}{quality_str}")
                            frame_count = 0
                            last_frame_time = current_time
            
            except ConnectionRefusedError:
                print("Connection refused. Retrying in 5 seconds...")
                time.sleep(5)
            except Exception as e:
                print(f"An error occurred: {e}")
                break

    except KeyboardInterrupt:
        print("\n用户按下 Ctrl+C，程序已退出。")
    finally:
        cap.release()
        print("Video source released.")
        sys.exit(0)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Send video stream to ESP32 with specified encoding.")
    parser.add_argument('--encoding', type=str, default='jpeg', choices=['jpeg', 'lz4', 'raw'],
                        help='Encoding type for the video stream (jpeg, lz4, raw).')
    args = parser.parse_args()

    print("Select video source:")
    print("1: Live Camera")
    print("2: Local Video File")
    choice = input("Enter your choice (1 or 2): ")

    if choice == '1':
        send_video_to_esp32(0, args.encoding)
    elif choice == '2':
        video_path = select_video_file()
        if video_path:
            send_video_to_esp32(video_path, args.encoding)
        else:
            print("No video file selected. Exiting.")
    else:
        print("Invalid choice. Exiting.")