import socket
import struct
import threading
import time
import crcmod.predefined

# ----------------- 配置 -----------------
ESP32_IP = "192.168.123.159"  # 请将此IP地址更改为您ESP32的实际IP地址
ESP32_PORT = 6667

# ----------------- 协议常量 -----------------
FRAME_HEADER = 0xAA55
FRAME_TYPE_REMOTE_CONTROL = 0x01
FRAME_TYPE_TELEMETRY = 0x02
FRAME_TYPE_HEARTBEAT = 0x03

# CRC16 Modbus
crc16_func = crcmod.predefined.mkPredefinedCrcFun('modbus')

# ----------------- 模拟数据 -----------------
simulated_telemetry_data = {
    "voltage_mv": 3850,   # 3.85V
    "current_ma": 150,    # 150mA
    "roll_deg": 5,        # 0.05 deg
    "pitch_deg": -10,     # -0.10 deg
    "yaw_deg": 2500,      # 25.00 deg
    "altitude_cm": 1000,  # 10m
}

# ----------------- 协议打包/解包 -----------------

def create_telemetry_frame():
    """ 创建遥测数据帧 (发送给 ESP32) """
    # 格式化字符串 '<HHhhhi' 必须与C语言中的 telemetry_data_payload_t 结构体完全匹配
    # H: uint16_t, h: int16_t, i: int32_t
    payload = struct.pack('<HHhhhi', 
                          simulated_telemetry_data["voltage_mv"],
                          simulated_telemetry_data["current_ma"],
                          simulated_telemetry_data["roll_deg"],
                          simulated_telemetry_data["pitch_deg"],
                          simulated_telemetry_data["yaw_deg"],
                          simulated_telemetry_data["altitude_cm"])
    
    frame_type = FRAME_TYPE_TELEMETRY
    length = 1 + len(payload)

    crc_data = struct.pack('<BB', length, frame_type) + payload
    crc = crc16_func(crc_data)
    
    # 帧头使用 >H (大端序) 以匹配C代码的 AA 55 顺序
    # 其余部分（CRC除外）是小端序，但由于是单字节或由C代码处理，所以不受影响
    # CRC本身在C代码中被明确地按小端序字节处理，所以这里也用 <H
    frame = struct.pack('>HBB', FRAME_HEADER, length, frame_type) + payload + struct.pack('<H', crc)
    return frame

def parse_and_handle_frame(data):
    """ 解析并处理收到的单个数据帧 """
    # 同样，帧头使用 >H (大端序) 解析
    header, length, frame_type = struct.unpack('>HBB', data[:4])
    if header != FRAME_HEADER:
        print("无效帧头")
        return False
        
    payload_len = length - 1
    payload = data[4:4+payload_len]
    
    crc_data = data[2:4+payload_len]
    received_crc, = struct.unpack('<H', data[4+payload_len:4+payload_len+2])
    calculated_crc = crc16_func(crc_data)
    
    if received_crc != calculated_crc:
        print(f"CRC校验失败: 收到={received_crc}, 计算={calculated_crc}")
        return False
        
    if frame_type == FRAME_TYPE_REMOTE_CONTROL:
        channel_count = payload[0]
        channels = struct.unpack(f'<{channel_count}H', payload[1:])
        throttle = channels[0] if channel_count > 0 else "N/A"
        direction = channels[1] if channel_count > 1 else "N/A"
        print(f"收到遥控数据: 油门={throttle}, 方向={direction}")
    elif frame_type == FRAME_TYPE_HEARTBEAT:
        status, = struct.unpack('<B', payload)
        status_map = {0: "空闲", 1: "正常运行", 2: "错误"}
        print(f"收到心跳: 设备状态={status_map.get(status, '未知')}")
    else:
        print(f"收到未知类型的帧: {frame_type}")
        
    return True

# ----------------- TCP 客户端任务 -----------------

def sender_task(sock, stop_event):
    """ 定时发送遥测数据的线程任务 """
    while not stop_event.is_set():
        try:
            frame = create_telemetry_frame()
            sock.sendall(frame)
            print(f"--> 发送遥测数据: {frame.hex()}")
            time.sleep(1) # 每秒发送一次
        except (ConnectionResetError, BrokenPipeError, OSError) as e:
            print(f"发送遥测时连接断开: {e}")
            stop_event.set()
            break
        except Exception as e:
            print(f"发送遥测时出错: {e}")
            stop_event.set()
            break

def receiver_task(sock, stop_event):
    """ 接收并处理数据的线程任务 """
    buffer = b''
    while not stop_event.is_set():
        try:
            data = sock.recv(1024)
            if not data:
                print("服务器关闭了连接")
                stop_event.set()
                break
            
            buffer += data
            print(f"<-- 收到原始数据: {buffer.hex()}")
            
            # 循环处理缓冲区中的数据帧
            while len(buffer) >= 7: # 最小帧长度 (心跳包)
                # 寻找大端序的帧头
                header_pos = buffer.find(struct.pack('>H', FRAME_HEADER))
                if header_pos == -1:
                    buffer = b'' # 没找到，丢弃所有
                    continue
                if header_pos > 0: # 丢弃帧头前的无效数据
                    buffer = buffer[header_pos:]

                if len(buffer) < 3: # 确保至少有3字节可以读取长度
                    break

                # 解析长度
                length_field = buffer[2]
                frame_len = 2 + 1 + length_field + 2 # Header(2)+Len(1)+Payload(length_field)+CRC(2)

                if len(buffer) < frame_len:
                    print(f"数据不完整, 需要{frame_len}字节, 现有{len(buffer)}字节")
                    break # 等待更多数据
                
                frame_data = buffer[:frame_len]
                buffer = buffer[frame_len:]
                
                # 解析并处理帧
                parse_and_handle_frame(frame_data)

        except (ConnectionResetError, BrokenPipeError, OSError) as e:
            print(f"接收数据时连接断开: {e}")
            stop_event.set()
            break
        except Exception as e:
            print(f"接收数据时出错: {e}")
            stop_event.set()
            break

def main():
    print("请确保已安装 'crcmod': pip install crcmod")
    
    while True:
        try:
            print(f"正在连接到 ESP32 ({ESP32_IP}:{ESP32_PORT})...")
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((ESP32_IP, ESP32_PORT))
            print("连接成功!")
            
            stop_event = threading.Event()
            
            sender = threading.Thread(target=sender_task, args=(sock, stop_event), daemon=True)
            receiver = threading.Thread(target=receiver_task, args=(sock, stop_event), daemon=True)
            
            sender.start()
            receiver.start()
            
            # 等待任一线程结束
            while sender.is_alive() and receiver.is_alive():
                time.sleep(0.1)

        except ConnectionRefusedError:
            print("连接被拒绝。请确保ESP32正在运行并监听端口。")
        except OSError as e:
            print(f"连接出错: {e}")
        except Exception as e:
            print(f"发生未知错误: {e}")
        finally:
            if 'sock' in locals() and sock:
                sock.close()
            print("连接已关闭。将在5秒后尝试重新连接...")
            time.sleep(5)

if __name__ == '__main__':
    main()
