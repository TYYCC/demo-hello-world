#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TCP测试服务器 - 用于测试ESP32心跳和遥测数据
支持双端口监听：7878(心跳) 和 6667(遥测)
"""

import socket
import threading
import struct
import time
from datetime import datetime

# 协议常量定义 - 兼容Telemetry协议格式
FRAME_HEADER_1 = 0xAA
FRAME_HEADER_2 = 0x55
FRAME_TYPE_COMMAND = 0x01
FRAME_TYPE_TELEMETRY = 0x02
FRAME_TYPE_HEARTBEAT = 0x03
FRAME_TYPE_EXTENDED = 0x04

# 端口配置
HEARTBEAT_PORT = 7878
TELEMETRY_PORT = 6667

class TCPTestServer:
    def __init__(self):
        self.running = False
        self.heartbeat_stats = {'total': 0, 'valid': 0, 'invalid': 0}
        self.telemetry_stats = {'total': 0, 'valid': 0, 'invalid': 0}
        self.telemetry_clients = []
        self.lock = threading.Lock()

    def crc16_modbus(self, data):
        """计算CRC16 Modbus校验"""
        crc = 0xFFFF
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 0x0001:
                    crc = (crc >> 1) ^ 0xA001
                else:
                    crc >>= 1
        return crc
    
    def parse_protocol_frame(self, data):
        """解析协议帧 - 兼容Telemetry协议格式"""
        if len(data) < 7:  # 最小帧长度: 4字节头部 + 1字节最小载荷 + 2字节CRC
            return None, "数据长度不足"
            
        try:
            # 解析帧头 - 使用Telemetry协议的4字节头部格式
            # header1(1B) + header2(1B) + length(1B) + frame_type(1B)
            header1, header2, length, frame_type = struct.unpack('<BBBB', data[:4])
            
            if header1 != FRAME_HEADER_1 or header2 != FRAME_HEADER_2:
                return None, f"帧头错误: 0x{header1:02X}{header2:02X}"
                
            # length字段表示类型字段+载荷长度（ESP32协议格式）
            # length = 1(类型) + N(载荷)
            payload_len = length - 1  # 减去类型字段的1字节
            total_frame_len = 4 + payload_len + 2  # 头部 + 载荷 + CRC
            
            if len(data) < total_frame_len:
                return None, "帧长度不完整"
                
            # 提取载荷和CRC
            payload = data[4:4+payload_len]
            crc_received = struct.unpack('<H', data[4+payload_len:4+payload_len+2])[0]
            
            # 验证CRC - CRC计算从length字段开始，包括length + type + payload
            # CRC范围：length(1B) + type(1B) + payload(payload_len B)
            crc_data = data[2:4+payload_len]  # 从data[2]开始（length字段）到载荷结束
            crc_calculated = self.crc16_modbus(crc_data)
            
            if crc_received != crc_calculated:
                return None, f"CRC校验失败: 收到0x{crc_received:04X}, 计算0x{crc_calculated:04X}"
                
            return {
                'header1': header1,
                'header2': header2,
                'frame_type': frame_type,
                'payload_len': payload_len,
                'payload': payload,
                'crc': crc_received
            }, None
            
        except struct.error as e:
            return None, f"解析错误: {e}"
    
    def parse_heartbeat_payload(self, payload):
        """解析心跳载荷"""
        if len(payload) < 5:  # ESP32心跳数据结构：1+4=5字节
            return None, "心跳载荷长度不足"
            
        try:
            # ESP32数据结构：device_status(uint8), timestamp(uint32)
            device_status, timestamp = struct.unpack('<BI', payload[:5])
            return {
                'device_status': device_status,
                'timestamp': timestamp,
                'time_str': datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M:%S')
            }, None
        except struct.error as e:
            return None, f"心跳解析错误: {e}"
    
    def parse_telemetry_payload(self, payload):
        """解析遥测载荷"""
        if len(payload) < 14:  # ESP32遥测数据结构：2+2+2+2+2+4=14字节
            return None, "遥测载荷长度不足"
            
        try:
            # ESP32数据结构：voltage_mv(uint16), current_ma(uint16), roll_deg(int16), pitch_deg(int16), yaw_deg(int16), altitude_cm(int32)
            voltage_mv, current_ma, roll_deg, pitch_deg, yaw_deg, altitude_cm = struct.unpack('<HHhhhI', payload[:14])
            return {
                'voltage_mv': voltage_mv,
                'current_ma': current_ma, 
                'roll_deg': roll_deg / 100.0,  # 转换为度数
                'pitch_deg': pitch_deg / 100.0,  # 转换为度数
                'yaw_deg': yaw_deg / 100.0,  # 转换为度数
                'altitude_cm': altitude_cm
            }, None
        except struct.error as e:
            return None, f"遥测解析错误: {e}"

    def create_control_command(self, channel_values):
        """创建遥控命令帧"""
        channel_count = len(channel_values)
        if channel_count > 8:
            raise ValueError("通道数量不能超过8")

        # 负载: 通道数量 (1B) + 通道值 (N*2B)
        payload = struct.pack('<B', channel_count)
        for value in channel_values:
            payload += struct.pack('>H', value) # 通道值为大端序

        # 帧长度: 类型(1B) + 负载(len(payload)) (不包含CRC)
        frame_len = 1 + len(payload)

        # 待计算CRC的数据: 长度(1B) + 类型(1B) + 负载
        crc_data = struct.pack('<B', frame_len) + struct.pack('<B', FRAME_TYPE_COMMAND) + payload
        crc = self.crc16_modbus(crc_data)

        # 完整帧: 帧头(2B) + 长度(1B) + 类型(1B) + 负载 + CRC(2B)
        frame = struct.pack('<BB', FRAME_HEADER_1, FRAME_HEADER_2) + crc_data + struct.pack('<H', crc)
        return frame

    def send_control_commands(self, client_socket):
        """循环发送遥控命令"""
        try:
            throttle = 500
            direction = 500
            while self.running and not client_socket._closed:
                # 模拟油门和方向变化
                throttle = (throttle + 10) % 1001
                channel_values = [throttle, direction, 500, 500] # 4通道

                command_frame = self.create_control_command(channel_values)
                print(f"[{datetime.now().strftime('%H:%M:%S')}] 发送遥控命令: {command_frame.hex()}")
                client_socket.sendall(command_frame)
                time.sleep(1)
        except (socket.error, BrokenPipeError) as e:
            print(f"[{datetime.now().strftime('%H:%M:%S')}] 发送遥控命令时连接断开: {e}")
        finally:
            with self.lock:
                if client_socket in self.telemetry_clients:
                    self.telemetry_clients.remove(client_socket)

    def handle_client(self, client_socket, client_addr, port_type):
        """处理客户端连接"""
        print(f"[{datetime.now().strftime('%H:%M:%S')}] {port_type}客户端连接: {client_addr}")
        
        if port_type == "遥测":
            with self.lock:
                self.telemetry_clients.append(client_socket)
            # 为遥测客户端启动一个发送遥控命令的线程
            command_thread = threading.Thread(target=self.send_control_commands, args=(client_socket,))
            command_thread.daemon = True
            command_thread.start()

        try:
            while self.running:
                data = client_socket.recv(1024)
                if not data:
                    break
                    
                # 打印原始数据用于调试
                # print(f"[{datetime.now().strftime('%H:%M:%S')}] {port_type}收到原始数据 ({len(data)}字节): {data.hex()}")
                
                # 解析协议帧
                frame, error = self.parse_protocol_frame(data)
                
                if port_type == "心跳":
                    self.heartbeat_stats['total'] += 1
                else:
                    self.telemetry_stats['total'] += 1
                
                if error:
                    print(f"[{datetime.now().strftime('%H:%M:%S')}] {port_type}解析错误: {error}")
                    if port_type == "心跳":
                        self.heartbeat_stats['invalid'] += 1
                    else:
                        self.telemetry_stats['invalid'] += 1
                    continue
                
                # 根据帧类型处理
                if frame['frame_type'] == FRAME_TYPE_HEARTBEAT:
                    hb_data, hb_error = self.parse_heartbeat_payload(frame['payload'])
                    if hb_error:
                        print(f"[{datetime.now().strftime('%H:%M:%S')}] 心跳数据错误: {hb_error}")
                        self.heartbeat_stats['invalid'] += 1
                    else:
                        self.heartbeat_stats['valid'] += 1
                        print(f"[{datetime.now().strftime('%H:%M:%S')}] 心跳数据: 状态={hb_data['device_status']}, 时间={hb_data['time_str']}")
                        
                elif frame['frame_type'] == FRAME_TYPE_TELEMETRY:
                    tel_data, tel_error = self.parse_telemetry_payload(frame['payload'])
                    if tel_error:
                        print(f"[{datetime.now().strftime('%H:%M:%S')}] 遥测数据错误: {tel_error}")
                        self.telemetry_stats['invalid'] += 1
                    else:
                        self.telemetry_stats['valid'] += 1
                        print(f"[{datetime.now().strftime('%H:%M:%S')}] 遥测数据: 电压={tel_data['voltage_mv']/1000.0:.2f}V, 电流={tel_data['current_ma']/1000.0:.2f}A, 姿态=({tel_data['roll_deg']:.1f},{tel_data['pitch_deg']:.1f},{tel_data['yaw_deg']:.1f}), 高度={tel_data['altitude_cm']/100.0:.1f}m")
                        
                else:
                    print(f"[{datetime.now().strftime('%H:%M:%S')}] 未知帧类型: {frame['frame_type']}")
                    
        except Exception as e:
            print(f"[{datetime.now().strftime('%H:%M:%S')}] {port_type}客户端处理错误: {e}")
        finally:
            client_socket.close()
            print(f"[{datetime.now().strftime('%H:%M:%S')}] {port_type}客户端断开: {client_addr}")
            if port_type == "遥测":
                with self.lock:
                    if client_socket in self.telemetry_clients:
                        self.telemetry_clients.remove(client_socket)

    def start_server(self, port, port_type):
        """启动服务器"""
        server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        try:
            server_socket.bind(('0.0.0.0', port))
            server_socket.listen(5)
            server_socket.settimeout(1.0)  # 设置超时以便检查running状态
            
            print(f"[{datetime.now().strftime('%H:%M:%S')}] {port_type}服务器启动，监听端口 {port}")
            
            while self.running:
                try:
                    client_socket, client_addr = server_socket.accept()
                    client_thread = threading.Thread(
                        target=self.handle_client,
                        args=(client_socket, client_addr, port_type)
                    )
                    client_thread.daemon = True
                    client_thread.start()
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running:
                        print(f"[{datetime.now().strftime('%H:%M:%S')}] {port_type}服务器错误: {e}")
                        
        except Exception as e:
            print(f"[{datetime.now().strftime('%H:%M:%S')}] {port_type}服务器启动失败: {e}")
        finally:
            server_socket.close()
            print(f"[{datetime.now().strftime('%H:%M:%S')}] {port_type}服务器已关闭")
    
    def print_stats(self):
        """打印统计信息"""
        while self.running:
            time.sleep(30)  # 每10秒打印一次统计
            print(f"\n=== 统计信息 [{datetime.now().strftime('%H:%M:%S')}] ===")
            print(f"心跳: 总计={self.heartbeat_stats['total']}, 有效={self.heartbeat_stats['valid']}, 无效={self.heartbeat_stats['invalid']}")
            print(f"遥测: 总计={self.telemetry_stats['total']}, 有效={self.telemetry_stats['valid']}, 无效={self.telemetry_stats['invalid']}")
            print("=" * 50)
    
    def run(self):
        """运行服务器"""
        self.running = True
        
        print("TCP测试服务器启动中...")
        print(f"心跳端口: {HEARTBEAT_PORT}")
        print(f"遥测端口: {TELEMETRY_PORT}")
        print("按 Ctrl+C 停止服务器\n")
        
        # 启动心跳服务器线程
        heartbeat_thread = threading.Thread(
            target=self.start_server,
            args=(HEARTBEAT_PORT, "心跳")
        )
        heartbeat_thread.daemon = True
        heartbeat_thread.start()
        
        # 启动遥测服务器线程
        telemetry_thread = threading.Thread(
            target=self.start_server,
            args=(TELEMETRY_PORT, "遥测")
        )
        telemetry_thread.daemon = True
        telemetry_thread.start()
        
        # 启动统计线程
        stats_thread = threading.Thread(target=self.print_stats)
        stats_thread.daemon = True
        stats_thread.start()
        
        try:
            # 主线程等待
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\n正在停止服务器...")
            self.running = False
            
            # 等待线程结束
            heartbeat_thread.join(timeout=2)
            telemetry_thread.join(timeout=2)
            
            print("服务器已停止")

if __name__ == "__main__":
    server = TCPTestServer()
    server.run()