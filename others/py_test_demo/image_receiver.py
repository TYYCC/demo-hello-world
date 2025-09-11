import socket
import cv2
import numpy as np
import time
import sys
import argparse
import lz4.frame
from datetime import datetime
import threading

# python ./others\py_test_demo\image_receiver.py

# 接收配置
HOST = '0.0.0.0'  # 监听所有接口
PORT = 6556        # 与image_server.py中相同的端口
BUFFER_SIZE = 1024  # 每次接收的缓冲区大小
TARGET_RESOLUTION = (240, 200)  # 目标分辨率

# 统计数据
class Stats:
    def __init__(self):
        self.frame_count = 0
        self.last_fps_time = time.time()
        self.fps = 0
        self.total_bytes = 0
        self.start_time = time.time()
        self.last_frame_time = None
        self.current_latency = 0
        self.lock = threading.Lock()

    def update_frame_received(self, bytes_received):
        with self.lock:
            current_time = time.time()
            self.frame_count += 1
            self.total_bytes += bytes_received
            
            # 计算FPS
            elapsed = current_time - self.last_fps_time
            if elapsed >= 1.0:
                self.fps = self.frame_count / elapsed
                self.frame_count = 0
                self.last_fps_time = current_time
            
            # 计算延迟
            if self.last_frame_time is not None:
                self.current_latency = (current_time - self.last_frame_time) * 1000  # 毫秒
            self.last_frame_time = current_time

    def get_stats(self):
        with self.lock:
            total_time = time.time() - self.start_time
            avg_bitrate = (self.total_bytes * 8) / (total_time * 1024)  # kbps
            return {
                "fps": self.fps,
                "latency": self.current_latency,
                "bitrate": avg_bitrate,
                "total_received": self.total_bytes / 1024  # KB
            }

# 解码协议数据
def decode_frame(data_type, frame_data, frame_width, frame_height):
    try:
        if data_type == 0x01:  # JPEG
            # 解码JPEG数据
            img = cv2.imdecode(np.frombuffer(frame_data, np.uint8), cv2.IMREAD_COLOR)
            if img is None:
                print("Error: Failed to decode JPEG data")
                return None
            return img
            
        elif data_type == 0x02:  # LZ4压缩
            # 解压LZ4数据
            decompressed_data = lz4.frame.decompress(frame_data)
            
            # 计算解压后数据可以表示的像素数量（每个像素2字节）
            pixel_count = len(decompressed_data) // 2
            
            # 计算实际可以填充的高度（保持宽度不变）
            actual_height = pixel_count // frame_width
            
            print(f"Decompressed data size: {len(decompressed_data)} bytes, can fill {actual_height}x{frame_width} pixels")
            
            if actual_height < frame_height:
                print(f"Warning: Image data only fills {actual_height} of {frame_height} rows")
            
            # 将解压后的数据转换为RGB565格式的图像，使用实际可填充的尺寸
            rgb565_array = np.frombuffer(decompressed_data, dtype=np.uint16)
            rgb565_array = rgb565_array.reshape((actual_height, frame_width))
            
            # 将RGB565转换回RGB888
            r = ((rgb565_array >> 11) & 0x1F) << 3
            g = ((rgb565_array >> 5) & 0x3F) << 2
            b = (rgb565_array & 0x1F) << 3
            
            # 创建RGB图像
            rgb_image = np.zeros((actual_height, frame_width, 3), dtype=np.uint8)
            rgb_image[:, :, 0] = r.astype(np.uint8)
            rgb_image[:, :, 1] = g.astype(np.uint8)
            rgb_image[:, :, 2] = b.astype(np.uint8)
            
            # 如果实际高度小于预期高度，创建一个全黑背景并将图像放在上面
            if actual_height < frame_height:
                full_image = np.zeros((frame_height, frame_width, 3), dtype=np.uint8)
                full_image[:actual_height, :, :] = rgb_image
                rgb_image = full_image
            
            # 转换为BGR (OpenCV格式)
            return cv2.cvtColor(rgb_image, cv2.COLOR_RGB2BGR)
            
        elif data_type == 0x03:  # 原始RGB565
            # 计算数据可以表示的像素数量（每个像素2字节）
            pixel_count = len(frame_data) // 2
            
            # 计算实际可以填充的高度（保持宽度不变）
            actual_height = pixel_count // frame_width
            
            print(f"Raw RGB565 data size: {len(frame_data)} bytes, can fill {actual_height}x{frame_width} pixels")
            
            if actual_height < frame_height:
                print(f"Warning: Image data only fills {actual_height} of {frame_height} rows")
            
            # 将原始数据转换为RGB565格式的图像，使用实际可填充的尺寸
            rgb565_array = np.frombuffer(frame_data, dtype=np.uint16)
            rgb565_array = rgb565_array.reshape((actual_height, frame_width))
            
            # 将RGB565转换回RGB888
            r = ((rgb565_array >> 11) & 0x1F) << 3
            g = ((rgb565_array >> 5) & 0x3F) << 2
            b = (rgb565_array & 0x1F) << 3
            
            # 创建RGB图像
            rgb_image = np.zeros((actual_height, frame_width, 3), dtype=np.uint8)
            rgb_image[:, :, 0] = r.astype(np.uint8)
            rgb_image[:, :, 1] = g.astype(np.uint8)
            rgb_image[:, :, 2] = b.astype(np.uint8)
            
            # 如果实际高度小于预期高度，创建一个全黑背景并将图像放在上面
            if actual_height < frame_height:
                full_image = np.zeros((frame_height, frame_width, 3), dtype=np.uint8)
                full_image[:actual_height, :, :] = rgb_image
                rgb_image = full_image
            
            # 转换为BGR (OpenCV格式)
            return cv2.cvtColor(rgb_image, cv2.COLOR_RGB2BGR)
        else:
            print(f"Unknown data type: {data_type}")
            return None
    except Exception as e:
        print(f"Error decoding frame: {e}")
        return None

def start_receiver(frame_width=240, frame_height=200):
    """启动接收服务器并显示接收到的图像"""
    stats = Stats()
    
    # 创建显示窗口
    cv2.namedWindow('Received Image', cv2.WINDOW_NORMAL)
    cv2.resizeWindow('Received Image', frame_width*2, frame_height*2)  # 放大显示
    
    # 创建套接字
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind((HOST, PORT))
    server_socket.listen(1)
    
    print(f"Waiting for connection on {HOST}:{PORT}...")
    
    try:
        while True:
            client_socket, addr = server_socket.accept()
            print(f"Connected by {addr}")
            client_socket.settimeout(5.0)  # 设置超时
            
            # 用于存储部分接收的数据
            buffer = b""
            waiting_for_header = True
            expected_data_length = 0
            data_type = 0
            
            try:
                while True:
                    # 接收数据
                    data = client_socket.recv(BUFFER_SIZE)
                    if not data:
                        print("Connection closed by the sender")
                        break
                    
                    buffer += data
                    
                    # 处理收到的数据
                    while True:
                        if waiting_for_header:
                            # 需要至少9字节的协议头
                            if len(buffer) < 9:
                                break
                            
                            # 解析协议头
                            sync_word = int.from_bytes(buffer[0:4], 'little')
                            if sync_word != 0xAEBC1402:
                                print(f"Invalid sync word: {sync_word:X}, searching for valid header...")
                                # 尝试找到下一个有效的同步头
                                sync_index = buffer[1:].find(bytes([0x02, 0x14, 0xBC, 0xAE]))
                                if sync_index != -1:
                                    buffer = buffer[sync_index+1:]
                                    continue
                                else:
                                    buffer = buffer[1:]  # 丢弃一个字节并继续
                                    break
                            
                            data_type = buffer[4]
                            expected_data_length = int.from_bytes(buffer[5:9], 'little')
                            
                            # 移除协议头
                            buffer = buffer[9:]
                            waiting_for_header = False
                            
                            print(f"Found header - Type: {data_type}, Length: {expected_data_length}")
                        else:
                            # 检查是否收到足够的数据
                            if len(buffer) < expected_data_length:
                                break
                            
                            # 提取完整的帧数据
                            frame_data = buffer[:expected_data_length]
                            buffer = buffer[expected_data_length:]
                            
                            # 更新统计信息
                            stats.update_frame_received(expected_data_length)
                            
                            # 解码并显示图像
                            img = decode_frame(data_type, frame_data, frame_width, frame_height)
                            if img is not None:
                                # 获取当前统计数据
                                current_stats = stats.get_stats()
                                
                                # 在图像上添加统计信息
                                info_text = f"FPS: {current_stats['fps']:.1f} | " \
                                           f"Latency: {current_stats['latency']:.1f}ms | " \
                                           f"Bitrate: {current_stats['bitrate']:.1f}kbps"
                                cv2.putText(img, info_text, (10, 20), 
                                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)
                                
                                # 显示时间戳
                                timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                                cv2.putText(img, timestamp, (10, frame_height - 10),
                                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)
                                
                                # 显示图像
                                cv2.imshow('Received Image', img)
                                
                                if cv2.waitKey(1) & 0xFF == ord('q'):
                                    print("User pressed 'q', exiting...")
                                    return
                            
                            # 准备接收下一帧
                            waiting_for_header = True
                    
            except socket.timeout:
                print("Connection timed out")
            except Exception as e:
                print(f"Error processing data: {e}")
            finally:
                client_socket.close()
                print("Client disconnected, waiting for new connection...")
    
    except KeyboardInterrupt:
        print("\n用户按下 Ctrl+C，程序已退出。")
    finally:
        server_socket.close()
        cv2.destroyAllWindows()
        print("Server closed.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Receive and display video stream from image_server.py")
    parser.add_argument('--width', type=int, default=240, help='Expected frame width')
    parser.add_argument('--height', type=int, default=200, help='Expected frame height')
    args = parser.parse_args()
    
    print(f"Starting image receiver with resolution {args.width}x{args.height}")
    print("Press 'q' in the image window to exit")
    
    start_receiver(args.width, args.height)
