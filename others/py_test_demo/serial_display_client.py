#!/usr/bin/env python3
"""
串口显示客户端程序
用于测试ESP32的串口显示功能
"""

import socket
import time
import threading
import argparse
import sys

class SerialDisplayClient:
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.socket = None
        self.connected = False
        
    def connect(self):
        """连接到ESP32的TCP服务器"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.host, self.port))
            self.connected = True
            print(f"已连接到 {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"连接失败: {e}")
            return False
    
    def disconnect(self):
        """断开连接"""
        if self.socket:
            self.socket.close()
            self.socket = None
        self.connected = False
        print("已断开连接")
    
    def send_text(self, text):
        """发送文本到串口屏幕"""
        if not self.connected:
            print("未连接到服务器")
            return False
        
        try:
            # 添加换行符
            if not text.endswith('\n'):
                text += '\n'
            
            data = text.encode('utf-8')
            self.socket.send(data)
            print(f"已发送: {text.strip()}")
            return True
        except Exception as e:
            print(f"发送失败: {e}")
            self.connected = False
            return False
    
    def send_data(self, data):
        """发送二进制数据到串口屏幕"""
        if not self.connected:
            print("未连接到服务器")
            return False
        
        try:
            if isinstance(data, str):
                data = data.encode('utf-8')
            self.socket.send(data)
            print(f"已发送 {len(data)} 字节数据")
            return True
        except Exception as e:
            print(f"发送失败: {e}")
            self.connected = False
            return False
    
    def interactive_mode(self):
        """交互模式 - 从键盘输入文本"""
        print("进入交互模式，输入文本将发送到串口屏幕")
        print("输入 'quit' 退出")
        print("输入 'clear' 清屏")
        print("-" * 50)
        
        while self.connected:
            try:
                text = input("> ")
                if text.lower() == 'quit':
                    break
                elif text.lower() == 'clear':
                    # 发送清屏命令 (常见串口屏幕清屏命令)
                    self.send_text('\x1B[2J')  # ANSI清屏
                    self.send_text('\x0C')     # 换页符
                else:
                    self.send_text(text)
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"输入错误: {e}")
                break
    
    def demo_mode(self):
        """演示模式 - 自动发送演示数据"""
        print("进入演示模式，将自动发送演示数据")
        print("按 Ctrl+C 停止")
        
        demo_texts = [
            "Hello World!",
            "Welcome to Serial Display",
            "ESP32 WiFi TCP -> Serial Screen",
            "这是一个中文测试",
            "Temperature: 25.6度",
            "Humidity: 65%",
            "Status: OK",
            "Counter: 1234",
            "Time: 14:30:25",
            "Date: 2024-01-15"
        ]
        
        counter = 0
        try:
            while self.connected:
                # 发送演示文本
                text = demo_texts[counter % len(demo_texts)]
                if "Counter:" in text:
                    text = f"Counter: {counter}"
                elif "Time:" in text:
                    current_time = time.strftime("%H:%M:%S")
                    text = f"Time: {current_time}"
                
                self.send_text(text)
                counter += 1
                time.sleep(0.1)  # 每0.1秒发送一次

        except KeyboardInterrupt:
            print("\n演示模式已停止")

def main():
    parser = argparse.ArgumentParser(description='串口显示客户端程序')
    parser.add_argument('host', help='ESP32的IP地址',default="192.168.233.247")
    parser.add_argument('port', type=int, help='端口号', default=8080, nargs='?')
    parser.add_argument('-i', '--interactive', action='store_true', help='交互模式')
    parser.add_argument('-d', '--demo', action='store_true', help='演示模式')
    parser.add_argument('-t', '--text', help='发送单条文本')
    parser.add_argument('-f', '--file', help='从文件读取并发送文本')
    
    args = parser.parse_args()
    
    client = SerialDisplayClient(args.host, args.port)
    
    if not client.connect():
        sys.exit(1)
    
    try:
        if args.text:
            # 发送单条文本
            client.send_text(args.text)
        elif args.file:
            # 从文件读取并发送
            try:
                with open(args.file, 'r', encoding='utf-8') as f:
                    for line in f:
                        line = line.strip()
                        if line:
                            client.send_text(line)
                            time.sleep(0.1)  # 短暂延迟
            except Exception as e:
                print(f"读取文件失败: {e}")
        elif args.demo:
            # 演示模式
            client.demo_mode()
        else:
            # 默认交互模式
            client.interactive_mode()
    
    except KeyboardInterrupt:
        print("\n程序被用户中断")
    finally:
        client.disconnect()

if __name__ == "__main__":
    main()
