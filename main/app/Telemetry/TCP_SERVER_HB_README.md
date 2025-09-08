# TCP心跳服务器 (TCP Heartbeat Server)

## 概述

TCP心跳服务器是一个独立的模块，用于接收来自客户端的心跳包。它使用与现有遥测系统兼容的协议，能够自动处理多个客户端连接，并提供丰富的统计信息。

## 特性

- **多客户端支持**: 同时支持最多5个客户端连接
- **协议兼容**: 使用telemetry_protocol.h定义的协议格式
- **自动超时检测**: 自动检测并断开超时的客户端连接
- **统计信息**: 提供详细的连接和心跳统计信息
- **回调机制**: 支持心跳包接收和连接状态变更回调
- **线程安全**: 内部使用FreeRTOS的任务机制

## 端口配置

默认监听端口: **7878**

可以通过 `tcp_server_hb_config_t` 结构体自定义端口：

```c
tcp_server_hb_config_t config = {
    .server_port = 7878,           // 自定义端口
    .max_clients = 5,              // 最大客户端数
    .heartbeat_timeout_ms = 90000  // 心跳超时时间(90秒)
};
```

## 基本使用流程

### 1. 初始化服务器

```c
// 使用默认配置
tcp_server_hb_init(NULL);

// 或使用自定义配置
tcp_server_hb_config_t config = {
    .server_port = 8080,
    .max_clients = 10,
    .heartbeat_timeout_ms = 60000
};
tcp_server_hb_init(&config);
```

### 2. 启动服务器

```c
// 定义回调函数
void heartbeat_callback(uint32_t client_index, const tcp_server_hb_payload_t* payload) {
    ESP_LOGI(TAG, "收到客户端 %d 的心跳包", client_index);
    ESP_LOGI(TAG, "设备状态: %d, 时间戳: %u", payload->device_status, payload->timestamp);
}

void connection_callback(uint32_t client_index, bool connected) {
    if (connected) {
        ESP_LOGI(TAG, "客户端 %d 已连接", client_index);
    } else {
        ESP_LOGI(TAG, "客户端 %d 已断开", client_index);
    }
}

// 启动服务器
tcp_server_hb_start("tcp_hb_server", 4096, 5,
                    heartbeat_callback, connection_callback);
```

### 3. 监控服务器状态

```c
// 获取服务器状态
tcp_server_hb_state_t state = tcp_server_hb_get_state();

// 获取统计信息
const tcp_server_hb_stats_t* stats = tcp_server_hb_get_stats();
ESP_LOGI(TAG, "活跃客户端: %d", stats->active_clients);
ESP_LOGI(TAG, "心跳包接收: %d", stats->heartbeat_received_count);

// 获取客户端信息
const tcp_server_hb_client_info_t* client = tcp_server_hb_get_client_info(0);
if (client && client->state == TCP_SERVER_HB_CLIENT_CONNECTED) {
    ESP_LOGI(TAG, "客户端0连接中");
}
```

### 4. 停止服务器

```c
// 停止服务器
tcp_server_hb_stop();

// 销毁服务器资源
tcp_server_hb_destroy();
```

## 心跳包格式

心跳包使用telemetry_protocol.h定义的帧格式：

```
帧头: 0xAA 0x55
长度: 1字节 (类型字段长度 + 负载长度)
类型: 0x03 (FRAME_TYPE_HEARTBEAT)
负载: [设备状态(1字节)][时间戳(4字节)]
CRC: 2字节
```

### 负载结构

```c
typedef struct __attribute__((packed)) {
    uint8_t device_status;    // 设备状态
    uint32_t timestamp;       // 时间戳(秒)
} tcp_server_hb_payload_t;
```

## 超时机制

服务器会自动检测客户端心跳超时：

- **默认超时时间**: 90秒
- **检测频率**: 每秒检查一次
- **超时处理**: 自动断开连接并调用连接回调

## 统计信息

服务器提供以下统计信息：

```c
typedef struct {
    uint32_t total_connections;        // 总连接数
    uint32_t active_clients;           // 当前活跃客户端数
    uint32_t heartbeat_received_count; // 已接收心跳包数量
    uint32_t heartbeat_failed_count;   // 心跳包解析失败数量
    uint32_t last_heartbeat_time;      // 最后心跳时间
} tcp_server_hb_stats_t;
```

## 错误处理

服务器内部会自动处理以下错误情况：

- **连接断开**: 自动清理资源并通知上层
- **数据解析失败**: 记录失败统计，不影响其他客户端
- **套接字错误**: 自动断开故障连接
- **内存不足**: 返回错误状态

## 与现有系统的集成

### 从遥测系统中分离

新的TCP心跳服务器与现有的遥测系统完全分离：

1. **移除了** `telemetry_sender.c` 中的心跳包发送代码
2. **移除了** `telemetry_protocol.c` 中的心跳包创建函数
3. **保持了** 心跳包的解析功能（用于接收）

### 架构优势

- **解耦合**: 心跳功能独立，不依赖遥测系统
- **可重用**: 可以单独使用或与其他系统集成
- **协议兼容**: 与现有客户端完全兼容
- **资源隔离**: 独立的任务和内存管理

## 调试和监控

### 日志输出

服务器提供详细的日志输出：

```
I TCP_SERVER_HB: 服务器状态变更: 0 -> 2
I TCP_SERVER_HB: 服务器开始监听端口 7878
I TCP_SERVER_HB: 新客户端连接: 槽位=0, 套接字=54
I TCP_SERVER_HB: 接收到客户端 0 的心跳包: 状态=1, 时间戳=1234567890
```

### 状态监控函数

```c
// 打印服务器状态
tcp_server_hb_print_status();

// 打印统计信息
tcp_server_hb_reset_stats(); // 重置统计信息
```

## 性能考虑

- **内存占用**: 约4KB任务栈 + 动态客户端信息
- **CPU占用**: 主要在select()系统调用上，负载很低
- **并发处理**: 同时处理多个客户端，无阻塞设计
- **网络效率**: 使用TCP Keep-Alive保持连接

## 故障排除

### 常见问题

1. **无法绑定端口**: 检查端口是否被其他程序占用
2. **客户端无法连接**: 检查防火墙和网络配置
3. **心跳包解析失败**: 确认客户端使用正确的协议格式
4. **连接频繁断开**: 检查网络稳定性或增加超时时间

### 调试技巧

1. 启用详细日志: `esp_log_level_set("TCP_SERVER_HB", ESP_LOG_VERBOSE)`
2. 定期调用状态监控函数检查服务器状态
3. 使用 `tcp_server_hb_print_status()` 查看详细状态信息

## 示例代码

完整的示例代码请参考 `tcp_server_hb_example.c` 文件。

该示例展示了：
- 如何初始化和启动服务器
- 如何处理心跳包和连接事件
- 如何监控服务器状态
- 如何正确停止服务器
