# 遥测服务模块使用指南

## 概述

遥测服务模块提供了一个完整的TCP命令服务器和协议处理系统，支持命令传输、解析和ACK应答机制。该模块基于自定义的遥测协议，可以同时处理多个客户端连接，并为每个命令提供可靠的应答机制。

## 主要功能

1. **TCP命令服务器**: 支持多客户端连接，自动命令解析和ACK应答
2. **协议处理**: 完整的遥测协议实现，支持多种帧类型
3. **心跳机制**: 集成心跳包处理，确保连接可靠性
4. **数据广播**: 支持向所有客户端广播遥测数据
5. **错误处理**: 完善的错误检测和处理机制
6. **统计监控**: 提供详细的连接和命令处理统计信息

## 文件结构

```
app/Telemetry/
├── inc/
│   ├── telemetry_protocol.h     # 协议定义和函数声明
│   ├── tcp_command_server.h     # TCP命令服务器接口
│   ├── telemetry_main.h         # 主服务头文件 (兼容旧版)
│   └── telemetry_tcp.h          # TCP通信头文件 (兼容旧版)
└── src/
    ├── telemetry_protocol.c     # 协议实现
    ├── tcp_command_server.c     # TCP命令服务器实现
    ├── tcp_command_example.c    # 使用示例
    ├── telemetry_main.c         # 主服务实现 (兼容旧版)
    ├── telemetry_tcp.c          # TCP通信实现 (兼容旧版)
    └── telemetry_test.c         # 测试示例 (兼容旧版)
```

## 支持的协议帧类型

| 帧类型 | 值 | 描述 | 方向 |
|--------|----|----- |------|
| FRAME_TYPE_RC | 0x01 | 遥控命令 | 地面站 → ESP32 |
| FRAME_TYPE_TELEMETRY | 0x02 | 遥测数据 | ESP32 → 地面站 |
| FRAME_TYPE_HEARTBEAT | 0x03 | 心跳包 | 双向 |
| FRAME_TYPE_EXT_CMD | 0x04 | 扩展命令 | 地面站 → ESP32 |
| FRAME_TYPE_ACK | 0x05 | ACK应答 | ESP32 → 地面站 |

## ACK状态码

| 状态码 | 值 | 描述 |
|--------|----|----- |
| ACK_STATUS_SUCCESS | 0x00 | 命令执行成功 |
| ACK_STATUS_ERROR | 0x01 | 命令执行错误 |
| ACK_STATUS_INVALID_PARAM | 0x02 | 参数无效 |
| ACK_STATUS_UNSUPPORTED | 0x03 | 不支持的命令 |
| ACK_STATUS_TIMEOUT | 0x04 | 执行超时 |
| ACK_STATUS_BUSY | 0x05 | 系统忙 |

## 使用方法

### 1. 新版TCP命令服务器使用

```c
#include "tcp_command_server.h"
#include "telemetry_protocol.h"

// 命令处理回调函数
static command_result_t my_command_handler(uint8_t frame_type, const uint8_t* payload, 
                                          size_t payload_len, uint32_t client_index) {
    command_result_t result = {0};
    
    switch (frame_type) {
        case FRAME_TYPE_RC:
            // 处理遥控命令
            result.status = ACK_STATUS_SUCCESS;
            break;
            
        case FRAME_TYPE_EXT_CMD:
            // 处理扩展命令
            result.status = ACK_STATUS_SUCCESS;
            break;
            
        default:
            result.status = ACK_STATUS_UNSUPPORTED;
            break;
    }
    
    return result;
}

// 连接状态回调函数
static void my_connection_callback(uint32_t client_index, bool connected) {
    if (connected) {
        ESP_LOGI("APP", "客户端 %d 已连接", client_index);
    } else {
        ESP_LOGI("APP", "客户端 %d 已断开", client_index);
    }
}

void app_main(void) {
    // 配置服务器
    tcp_cmd_server_config_t config = {
        .server_port = 8080,
        .max_clients = 5,
        .ack_timeout_ms = 5000,
        .recv_timeout_ms = 10000
    };
    
    // 初始化并启动服务器
    tcp_command_server_init(&config);
    tcp_command_server_start("tcp_cmd_srv", 8192, 5, 
                            my_command_handler, 
                            my_connection_callback);
}
```

### 2. 发送遥测数据

```c
void send_telemetry_data(void) {
    telemetry_data_payload_t telemetry = {
        .voltage_mv = 12000,
        .current_ma = 1500,
        .roll_deg = 100,
        .pitch_deg = -50,
        .yaw_deg = 18000,
        .altitude_cm = 15000
    };
    
    // 广播遥测数据到所有客户端
    uint32_t sent_count = tcp_command_server_broadcast(FRAME_TYPE_TELEMETRY, 
                                                      (const uint8_t*)&telemetry, 
                                                      sizeof(telemetry));
    
    ESP_LOGI("APP", "遥测数据已发送给 %d 个客户端", sent_count);
}
```

### 3. 在UI中使用 (兼容旧版)

UI界面已经集成了遥测服务控制：

```c
#include "ui.h"

// 创建遥测界面
void create_telemetry_page() {
    lv_obj_t *screen = lv_scr_act();
    ui_telemetry_create(screen);  // 自动初始化服务
}

// 清理遥测界面  
void cleanup_telemetry_page() {
    ui_telemetry_cleanup();  // 自动停止服务并清理资源
}
```

### 4. 在代码中直接使用 (兼容旧版)

```c
#include "telemetry_main.h"

// 数据回调函数
void my_data_callback(const telemetry_data_t *data) {
    printf("Voltage: %.2f V, Current: %.2f A\\n", data->voltage, data->current);
}

void app_main() {
    // 1. 初始化服务
    telemetry_service_init();
    
    // 2. 启动服务
    telemetry_service_start(my_data_callback);
    
    // 3. 发送控制命令
    telemetry_service_send_control(500, 600);  // 油门500, 方向600
    
    // 4. 停止服务（退出时）
    telemetry_service_stop();
    telemetry_service_deinit();
}
```

## TCP通信协议

### 连接方式
- **端口**: 6666
- **协议**: TCP
- **编码**: ASCII文本

### 命令格式

发送控制命令：
```
CTRL:throttle,direction\\n
```

示例：
```
CTRL:500,600\\n    # 油门500，方向600
```

### 响应格式

成功响应：
```
OK\\n
```

错误响应：
```
ERROR\\n
```

## 测试方法

### 1. 使用telnet测试

```bash
# Windows
telnet <ESP32_IP> 6666

# 发送命令
CTRL:500,600
```

### 2. 使用netcat测试

```bash
# Linux/Mac
nc <ESP32_IP> 6666

# 发送命令  
CTRL:500,600
```

### 3. 使用测试代码

```c
#include "telemetry_test.c"

void app_main() {
    start_telemetry_test();  // 启动自动化测试
}
```

## UI界面功能

### 控制元素
- **油门滑块**: 控制范围 0-1000
- **方向滑块**: 控制范围 0-1000  
- **启动/停止按钮**: 控制服务启停
- **状态显示**: 显示服务运行状态

### 数据显示
- **电压显示**: 实时电压值
- **电流显示**: 实时电流值
- **服务状态**: 显示服务运行状态

## 数据结构

```c
typedef struct {
    int32_t throttle;   // 油门值 (0-1000)
    int32_t direction;  // 方向值 (0-1000)
    float voltage;      // 电压 (V)
    float current;      // 电流 (A)
    float roll;         // 横滚角 (度)
    float pitch;        // 俯仰角 (度)  
    float yaw;          // 偏航角 (度)
    float altitude;     // 高度 (米)
} telemetry_data_t;
```

## 服务状态

```c
typedef enum {
    TELEMETRY_STATUS_STOPPED = 0,   // 已停止
    TELEMETRY_STATUS_STARTING,      // 启动中
    TELEMETRY_STATUS_RUNNING,       // 运行中
    TELEMETRY_STATUS_STOPPING,      // 停止中
    TELEMETRY_STATUS_ERROR          // 错误状态
} telemetry_status_t;
```

## 注意事项

1. **资源管理**: 服务会自动管理TCP端口和FreeRTOS任务，无需手动清理
2. **线程安全**: 所有API都是线程安全的，可以在多个任务中调用
3. **内存使用**: 服务使用少量堆内存，主要用于队列和信号量
4. **网络要求**: 需要确保ESP32已连接到WiFi网络

## 故障排除

### 服务启动失败
- 检查WiFi连接状态
- 确保端口6666未被占用
- 查看内存使用情况

### TCP连接失败  
- 检查防火墙设置
- 确认ESP32的IP地址
- 验证网络连通性

### UI无响应
- 检查遥测服务是否已启动
- 查看任务栈使用情况
- 确认回调函数正常执行
