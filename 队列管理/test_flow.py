"""
相机模拟软件 - 用于测试队列管理逻辑

运行方式: python test_flow.py

功能:
1. 主动连接软件的9001端口
2. 等待软件发来的命令(01/11/12)
3. 根据命令返回机械臂1或机械臂2的数据
"""

import socket
import time
import sys

def run_camera_simulator(software_ip="127.0.0.1", software_port=9001):
    """运行相机模拟器"""
    print("=" * 50)
    print("相机模拟软件启动")
    print(f"软件地址: {software_ip}:{software_port}")
    print("=" * 50)
    
    while True:
        try:
            # 创建socket连接
            client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client.settimeout(5)
            client.connect((software_ip, software_port))
            print(f"[相机] 已连接软件")
            
            while True:
                # 接收软件发来的命令
                try:
                    data = client.recv(1024)
                    if not data:
                        print("[相机] 软件断开连接")
                        break
                    
                    # 解析命令 (前2字节是命令)
                    if len(data) >= 2:
                        cmd = int.from_bytes(data[:2], byteorder='big')
                        count = int.from_bytes(data[2:4], byteorder='big') if len(data) >= 4 else 0
                        print(f"[相机] 收到命令: {cmd:04X} count={count} ({data.hex()})")
                        
                        # 根据命令返回数据
                        if cmd == 0x01:
                            # 机械臂1数据
                            response = b"(1234,arm1_data)E"
                            client.send(response)
                            print(f"[相机] 发送机械臂1数据: {response}")
                            
                        elif cmd == 0x0B:
                            # 机械臂2数据
                            response = b"(2234,arm2_data)E"
                            client.send(response)
                            print(f"[相机] 发送机械臂2数据: {response}")
                            
                        elif cmd == 0x0C:
                            # 12命令，结束流程
                            print(f"[相机] 收到12命令，流程结束")
                            
                        else:
                            print(f"[相机] 未知命令: {cmd}")
                            
                except socket.timeout:
                    continue
                    
        except ConnectionRefusedError:
            print(f"[相机] 无法连接软件 {software_ip}:{software_port}，重试中...")
            time.sleep(2)
        except Exception as e:
            print(f"[相机] 异常: {e}")
            time.sleep(2)

if __name__ == "__main__":
    # 默认连接本地
    software_ip = "127.0.0.1"
    software_port = 9001
    
    if len(sys.argv) > 1:
        software_ip = sys.argv[1]
    if len(sys.argv) > 2:
        software_port = int(sys.argv[2])
    
    run_camera_simulator(software_ip, software_port)
