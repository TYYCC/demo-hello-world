# ELRS OTA 协议详解

## 目录

1. [概述](#概述)
2. [数据包格式](#数据包格式)
3. [通道数据编码](#通道数据编码)
4. [开关编码模式](#开关编码模式)
5. [同步包结构](#同步包结构)
6. [链路统计包](#链路统计包)
7. [CRC 校验](#crc-校验)
8. [跳频机制](#跳频机制)
9. [绑定流程](#绑定流程)
10. [API 使用指南](#api-使用指南)

---

## 概述

ELRS (ExpressLRS) 是一个开源的长距离遥控链路系统，专为 FPV 无人机设计。其 OTA (Over-The-Air) 协议经过高度优化，以实现：

- **低延迟**: 最低 2ms 延迟
- **高刷新率**: 最高 1000Hz
- **长距离**: 可达 100km+
- **抗干扰**: 80 通道跳频

### 射频参数

| 参数 | 2.4GHz (SX1280/SX1281) | 900MHz (SX127x) |
|------|------------------------|-----------------|
| 频段 | 2400-2480 MHz | 868/915 MHz |
| 跳频通道数 | 80 | 取决于频段 |
| 调制方式 | LoRa / FLRC | LoRa |
| 最大发射功率 | 100mW (20dBm) | 1W (30dBm) |

### 速率配置

| 速率索引 | 名称 | 刷新率 | 调制 | 延迟 |
|---------|------|--------|------|------|
| 0 | F1000 | 1000Hz | FLRC | ~2ms |
| 1 | F500 | 500Hz | FLRC | ~4ms |
| 2 | D500 | 500Hz | DVDA | ~4ms |
| 3 | D250 | 250Hz | DVDA | ~8ms |
| 4 | 500Hz | 500Hz | LoRa | ~4ms |
| 5 | 333Hz | 333Hz | LoRa | ~6ms |
| 6 | 250Hz | 250Hz | LoRa | ~8ms |
| 7 | 150Hz | 150Hz | LoRa | ~13ms |
| 8 | 100Hz | 100Hz | LoRa | ~20ms |
| 9 | 50Hz | 50Hz | LoRa | ~40ms |

---

## 数据包格式

ELRS 使用两种数据包格式：

### 4 字节模式 (OTA_Packet4_s) - 标准模式

**总长度**: 8 字节

```
┌────────────────────────────────────────────────────────────┐
│ Byte 0   │ Byte 1-5          │ Byte 6      │ Byte 7        │
├──────────┼───────────────────┼─────────────┼───────────────┤
│ Header   │ Payload (5 bytes) │ Switches    │ CRC Low       │
│ type:2   │ 4x 10-bit CH      │ sw:7 arm:1  │               │
│ crcH:6   │                   │             │               │
└──────────┴───────────────────┴─────────────┴───────────────┘
```

**字段详解**:

| 字段 | 位数 | 描述 |
|------|------|------|
| `type` | 2 | 数据包类型 |
| `crcHigh` | 6 | CRC 高 6 位 |
| `channels` | 40 | 4 个 10-bit 通道值 (CH0-CH3) |
| `switches` | 7 | 开关值 + ACK 位 |
| `isArmed` | 1 | 解锁状态 |
| `crcLow` | 8 | CRC 低 8 位 |

### 8 字节模式 (OTA_Packet8_s) - 全分辨率模式

**总长度**: 13 字节

```
┌────────────────────────────────────────────────────────────────────────┐
│ Byte 0        │ Byte 1-5        │ Byte 6-10       │ Byte 11-12        │
├───────────────┼─────────────────┼─────────────────┼───────────────────┤
│ Header        │ CH Low          │ CH High         │ CRC16             │
│ type:2 ack:1  │ 4x 10-bit       │ 4x 10-bit       │                   │
│ pwr:3 hi:1    │ (CH0-CH3)       │ (AUX2-5/AUX6-9) │                   │
│ arm:1         │                 │                 │                   │
└───────────────┴─────────────────┴─────────────────┴───────────────────┘
```

**字段详解**:

| 字段 | 位数 | 描述 |
|------|------|------|
| `packetType` | 2 | 数据包类型 (0b00 = RC数据) |
| `stubbornAck` | 1 | 遥测确认位 |
| `uplinkPower` | 3 | 发射功率等级 (0-7 → 1-8) |
| `isHighAux` | 1 | 0=AUX2-5, 1=AUX6-9 |
| `isArmed` | 1 | 解锁状态 |
| `chLow` | 40 | 4 个 10-bit 通道 (CH0-CH3) |
| `chHigh` | 40 | 4 个 10-bit 辅助通道 |
| `crc` | 16 | CRC16 校验 |

### 数据包类型定义

```c
#define PACKET_TYPE_RCDATA      0b00  // 遥控数据 (上行)
#define PACKET_TYPE_DATA        0b01  // 通用数据传输
#define PACKET_TYPE_SYNC        0b10  // 同步包 (上行)
#define PACKET_TYPE_LINKSTATS   0b00  // 链路统计 (下行)
```

---

## 通道数据编码

### CRSF 通道值范围

```c
#define CRSF_CHANNEL_VALUE_EXT_MIN  0     // 880us (-121.1%)
#define CRSF_CHANNEL_VALUE_MIN      172   // 987us (标准最小)
#define CRSF_CHANNEL_VALUE_1000     191   // 1000us
#define CRSF_CHANNEL_VALUE_MID      992   // 1500us (中间值)
#define CRSF_CHANNEL_VALUE_2000     1792  // 2000us
#define CRSF_CHANNEL_VALUE_MAX      1811  // 2012us (标准最大)
#define CRSF_CHANNEL_VALUE_EXT_MAX  1984  // 2120us (+121.1%)
```

### 通道值映射

| 描述 | CRSF 值 | PWM 微秒 | 百分比 |
|------|---------|----------|--------|
| 扩展最小 | 0 | 880us | -121.1% |
| 标准最小 | 172 | 987us | -100% |
| 1000us | 191 | 1000us | -100% |
| **中间值** | **992** | **1500us** | **0%** |
| 2000us | 1792 | 2000us | +100% |
| 标准最大 | 1811 | 2012us | +100% |
| 扩展最大 | 1984 | 2120us | +121.1% |

### 11-bit 到 10-bit 压缩

OTA 传输时，原始 11-bit CRSF 值被压缩为 10-bit：

#### 方法 1: 限制模式 (Limit)
```c
// 限制到标准范围，然后转换
static uint32_t Decimate11to10_Limit(uint32_t ch11bit) {
    return CRSF_to_UINT10(constrain(ch11bit, 
        CRSF_CHANNEL_VALUE_MIN,   // 172
        CRSF_CHANNEL_VALUE_MAX)); // 1811
}
```

#### 方法 2: 简单除 2 (Div2)
```c
// 直接右移 1 位 (用于 Full Resolution 模式)
static uint32_t Decimate11to10_Div2(uint32_t ch11bit) {
    return ch11bit >> 1;
}
```

### 4 通道打包到 5 字节

```
输入: 4 个 10-bit 值 (40 bits 总计)
输出: 5 个字节

通道 0: AAAAAAAAAA (10 bits)
通道 1: BBBBBBBBBB (10 bits)
通道 2: CCCCCCCCCC (10 bits)
通道 3: DDDDDDDDDD (10 bits)

打包后:
Byte 0: AAAAAAAA    (通道 0 低 8 位)
Byte 1: BBBBBBAA    (通道 1 低 6 位 + 通道 0 高 2 位)
Byte 2: CCCCBBBB    (通道 2 低 4 位 + 通道 1 高 4 位)
Byte 3: DDCCCCCC    (通道 3 低 2 位 + 通道 2 高 6 位)
Byte 4: DDDDDDDD    (通道 3 高 8 位)
```

**编码函数**:

```c
static void PackUInt11ToChannels4x10(
    uint32_t const * const src,           // 输入: 4 个通道值
    OTA_Channels_4x10 * const dest,       // 输出: 5 字节
    Decimate11to10_fn decimate)           // 压缩函数
{
    const unsigned DEST_PRECISION = 10;
    uint8_t *destBytes = (uint8_t *)dest;
    *destBytes = 0;
    unsigned destShift = 0;
    
    for (unsigned ch = 0; ch < 4; ++ch) {
        // 转换为 10-bit 值
        unsigned chVal = decimate(src[ch]);
        
        // 低位放入当前字节剩余空间
        *destBytes++ |= chVal << destShift;
        
        // 高位放入下一个字节
        unsigned srcBitsLeft = DEST_PRECISION - 8 + destShift;
        *destBytes = chVal >> (DEST_PRECISION - srcBitsLeft);
        
        destShift = srcBitsLeft;
    }
}
```

---

## 开关编码模式

ELRS 支持多种开关编码模式，用于传输 AUX 通道：

### 模式枚举

```c
enum OtaSwitchMode_e {
    smWideOr8ch = 0,      // HybridWide 或 8 通道
    smHybridOr16ch = 1,   // Hybrid8 或 16 通道
    sm12ch = 2            // 12 通道
};
```

### Hybrid8 模式

**特点**:
- AUX1 (CH5) 每包发送
- AUX2-AUX8 轮询发送
- AUX8 使用 4-bit (16 档位)
- 其他 AUX 使用 3-bit (8 档位)

**编码结构**:
```
switches 字节 (7 bits):
┌─────────────────────────────────┐
│ stubbornAck:1 │ index:3 │ val:3/4 │
└─────────────────────────────────┘
```

**代码**:
```c
void GenerateChannelDataHybrid8(OTA_Packet_s * const otaPktPtr, 
                                const uint32_t *channelData,
                                bool stubbornAck)
{
    OTA_Packet4_s * const ota4 = &otaPktPtr->std;
    PackChannelDataHybridCommon(ota4, channelData);
    
    uint8_t switchIndex = Hybrid8NextSwitchIndex;
    uint8_t value;
    
    // AUX8 是高分辨率 16 档位 (4-bit)
    if (switchIndex == 6)
        value = CRSF_to_N(channelData[6 + 1 + 4], 16);
    else
        value = CRSF_to_SWITCH3b(channelData[switchIndex + 1 + 4]);
    
    ota4->rc.switches = 
        stubbornAck << 6 |
        switchIndex << 3 |
        value;
    
    // 轮询下一个开关
    Hybrid8NextSwitchIndex = (switchIndex + 1) % 7;
}
```

### HybridWide 模式

**特点**:
- 6-bit 开关值 (64 档位)
- 基于 Nonce 确定发送哪个开关
- 第 8 个槽位用于发送 TX 功率

**Nonce 到开关索引映射**:
```c
// 返回 0-7 的序列，确保每 16 个包都发送完整的开关状态
static uint8_t HybridWideNonceToSwitchIndex(uint8_t nonce) {
    return ((nonce & 0b111) + ((nonce >> 3) & 0b1)) % 8;
}
```

**编码**:
```c
void GenerateChannelDataHybridWide(OTA_Packet_s * const otaPktPtr,
                                   const uint32_t *channelData,
                                   bool stubbornAck)
{
    OTA_Packet4_s * const ota4 = &otaPktPtr->std;
    PackChannelDataHybridCommon(ota4, channelData);
    
    uint8_t nextSwitchIndex = HybridWideNonceToSwitchIndex(OtaNonce);
    uint8_t value = stubbornAck << 6;
    
    if (nextSwitchIndex == 7) {
        // 第 8 个槽位发送 TX 功率
        value |= linkStats.uplink_TX_Power;
    } else {
        // 6-bit 开关值
        value |= HybridWideSwitchToOta(channelData, nextSwitchIndex + 1);
    }
    
    ota4->rc.switches = value;
}
```

### 8/12/16 通道模式 (Full Resolution)

使用 `OTA_Packet8_s` 格式：

| 模式 | isHighAux=0 | isHighAux=1 |
|------|-------------|-------------|
| 8ch | CH0-3 + AUX2-5 | - |
| 12ch | CH0-3 + AUX2-5 | CH0-3 + AUX6-9 |
| 16ch | CH0-3 + AUX2-5 | AUX6-9 + AUX10-13 |

---

## 同步包结构

同步包用于建立和维持 TX/RX 连接：

```c
typedef struct {
    uint8_t fhssIndex;      // 当前跳频索引
    uint8_t nonce;          // 同步计数器
    uint8_t rfRateEnum;     // 射频速率
    uint8_t switchEncMode:1,// 开关编码模式
            newTlmRatio:3,  // 遥测比率
            geminiMode:1,   // 双天线模式
            otaProtocol:2,  // OTA 协议版本
            free:1;         // 保留
    uint8_t UID4;           // UID 字节 4
    uint8_t UID5;           // UID 字节 5
} PACKED OTA_Sync_s;
```

### 同步包字段说明

| 字段 | 描述 |
|------|------|
| `fhssIndex` | 跳频序列当前位置 (0-79) |
| `nonce` | 8-bit 计数器，每包递增 |
| `rfRateEnum` | 当前使用的速率配置 |
| `switchEncMode` | 0=Wide/8ch, 1=Hybrid/16ch |
| `newTlmRatio` | 遥测数据发送比率 |
| `geminiMode` | 双接收天线模式 |
| `otaProtocol` | 协议版本 (4字节/8字节) |
| `UID4/UID5` | 绑定 UID 的后 2 字节 |

### ModelMatch 机制

```c
#define MODELMATCH_MASK 0x3f  // 低 6 位

// TX 端: 将 ModelId XOR 到 sync 包
sync.UID5 ^= (ModelId & MODELMATCH_MASK);

// RX 端: 验证 ModelId 匹配
bool modelMatch = ((receivedUID5 ^ storedUID5) & MODELMATCH_MASK) == 
                  (config.GetModelId() & MODELMATCH_MASK);
```

---

## 链路统计包

RX 发送给 TX 的链路质量信息：

```c
typedef struct {
    uint8_t uplink_RSSI_1:7,        // 天线 1 RSSI (dBm 偏移)
            antenna:1;               // 当前使用的天线
    uint8_t uplink_RSSI_2:7,        // 天线 2 RSSI
            modelMatch:1;            // ModelId 匹配标志
    uint8_t lq:7,                   // 链路质量 (0-100)
            trueDiversityAvailable:1;// 双天线可用
    int8_t SNR;                     // 信噪比 (dB)
} PACKED OTA_LinkStats_s;
```

### RSSI 解码

```c
// 存储格式: 实际值 + 120 (使其为正数)
// 例如: -80 dBm → 存储为 40

int8_t actual_rssi = stored_rssi - 120;
```

---

## CRC 校验

### CRC 初始化

```c
void OtaUpdateCrcInitFromUid() {
    // 使用 UID 最后 2 字节初始化 CRC
    OtaCrcInitializer = (UID[4] << 8) | UID[5];
    
    // XOR OTA 版本号到高字节
    OtaCrcInitializer ^= (uint16_t)OTA_VERSION_ID << 8;
}
```

### CRC 多项式

```c
#define ELRS_CRC_POLY     0x07    // 8-bit CRC (0x83 Koopman)
#define ELRS_CRC14_POLY   0x2E57  // 14-bit CRC
#define ELRS_CRC16_POLY   0x3D65  // 16-bit CRC (0x9EB2 Koopman)
```

### 4 字节模式 CRC 计算

```c
void GenerateCrc14(OTA_Packet_s * const otaPktPtr) {
    OTA_Packet4_s * const ota4 = &otaPktPtr->std;
    
    // 初始化包含 Nonce
    uint16_t crc = ota_crc.calc(
        (uint8_t*)ota4 + 1,                    // 从 byte 1 开始
        OTA4_CRC_CALC_LEN - 1,                 // 计算长度
        OtaCrcInitializer ^ (OtaNonce << 8));  // 初始值 XOR Nonce
    
    // CRC 分成高 6 位和低 8 位
    ota4->crcHigh = crc >> 8;
    ota4->crcLow = crc & 0xFF;
}
```

### 8 字节模式 CRC 计算

```c
void GenerateCrc16(OTA_Packet_s * const otaPktPtr) {
    OTA_Packet8_s * const ota8 = (OTA_Packet8_s *)otaPktPtr;
    
    uint16_t crc = ota_crc.calc(
        (uint8_t*)ota8,
        OTA8_CRC_CALC_LEN,
        OtaCrcInitializer ^ (OtaNonce << 8));
    
    ota8->crc = crc;  // 直接存储 16-bit CRC
}
```

---

## 跳频机制

### 跳频参数

| 参数 | 2.4GHz | 说明 |
|------|--------|------|
| 通道数 | 80 | 跳频序列长度 |
| 同步通道 | 40 | 同步包发送位置 |
| 频率间隔 | 1 MHz | 相邻通道间隔 |

### 跳频序列生成

```c
void FHSSrandomiseFHSSsequence(uint32_t seed) {
    // 使用 UID 作为种子，生成伪随机跳频序列
    rngSeed(seed);
    
    // Fisher-Yates 洗牌算法
    for (int i = FHSS_SEQUENCE_LEN - 1; i > 0; i--) {
        int j = rng() % (i + 1);
        swap(FHSSsequence[i], FHSSsequence[j]);
    }
}
```

### 跳频同步

```c
// TX 端: 在同步位置发送同步包
if (FHSSptr == syncChannel) {
    SendSyncPacket();
}

// RX 端: 根据同步包重置跳频指针
void ProcessSyncPacket(OTA_Sync_s *sync) {
    FHSSptr = sync->fhssIndex;
    OtaNonce = sync->nonce;
}
```

---

## 绑定流程

### UID 结构

```c
uint8_t UID[6];  // 6 字节唯一标识符

// 绑定 UID (工厂默认)
const uint8_t BindingUID[6] = {0, 1, 2, 3, 4, 5};
```

### 绑定步骤

```
1. TX 进入绑定模式
   └─ 使用 BindingUID 广播
   └─ 发送绑定速率的同步包

2. RX 进入绑定模式
   └─ 接收绑定包
   └─ 提取 TX 的 UID

3. 保存绑定
   └─ RX 存储 TX 的 UID 到 EEPROM
   └─ 双方退出绑定模式

4. 正常连接
   └─ 使用存储的 UID 通信
```

### 绑定包特征

```c
// 绑定模式使用固定速率
#define RATE_BINDING RATE_LORA_2G4_50HZ  // 50Hz

// 绑定包发送次数
#define BindingSpamAmount 600
```

---

## API 使用指南

### TX 端 API

```c
// 声明外部函数
extern "C" {
    void elrs_setup(void);
    void elrs_loop(void);
    
    void elrs_set_channel(uint8_t channel, uint16_t value);
    void elrs_set_channels(const uint16_t *values, uint8_t count);
    uint16_t elrs_get_channel(uint8_t channel);
    void elrs_reset_channels(void);
    uint16_t elrs_us_to_crsf(uint16_t us);
    int elrs_is_connected(void);
}
```

### TX 使用示例

```c
#include "esp_log.h"

extern "C" void app_main(void) {
    initArduino();
    
    // 初始化 ELRS TX
    elrs_setup();
    
    // 主循环
    while (1) {
        // 更新通道数据
        elrs_set_channel(0, elrs_us_to_crsf(1500));  // Roll
        elrs_set_channel(1, elrs_us_to_crsf(1500));  // Pitch
        elrs_set_channel(2, elrs_us_to_crsf(1000));  // Throttle
        elrs_set_channel(3, elrs_us_to_crsf(1500));  // Yaw
        
        // AUX 通道 (开关)
        elrs_set_channel(4, elrs_us_to_crsf(1000));  // AUX1 - Arm
        elrs_set_channel(5, elrs_us_to_crsf(1500));  // AUX2 - Mode
        
        // 运行 ELRS 循环 (自动发送)
        elrs_loop();
        
        vTaskDelay(pdMS_TO_TICKS(1));  // 1ms
    }
}
```

### 通道映射

| 通道索引 | 名称 | 功能 | 典型用途 |
|---------|------|------|---------|
| 0 | CH1 | Roll | 横滚/副翼 |
| 1 | CH2 | Pitch | 俯仰/升降 |
| 2 | CH3 | Throttle | 油门 |
| 3 | CH4 | Yaw | 偏航/方向 |
| 4 | CH5/AUX1 | Arm | 解锁开关 |
| 5 | CH6/AUX2 | Mode | 飞行模式 |
| 6 | CH7/AUX3 | - | 自定义 |
| 7 | CH8/AUX4 | - | 自定义 |
| 8 | CH9/AUX5 | - | 自定义 |
| 9 | CH10/AUX6 | - | 自定义 |
| 10 | CH11/AUX7 | - | 自定义 |
| 11 | CH12/AUX8 | - | 自定义 |
| 12-15 | CH13-16 | - | 扩展通道 |

### 通道值转换

```c
// PWM 微秒 → CRSF
uint16_t crsf = elrs_us_to_crsf(1500);  // 返回 992

// 百分比 → CRSF
uint16_t percent_to_crsf(int percent) {
    // -100% ~ +100% → 172 ~ 1811
    return (uint16_t)(992 + (percent * 820 / 100));
}

// 模拟值 (0-1023) → CRSF
uint16_t adc_to_crsf(uint16_t adc) {
    return (uint16_t)(172 + (adc * (1811 - 172) / 1023));
}
```

---

## 附录

### 源码文件位置

| 文件 | 描述 |
|------|------|
| `lib/OTA/OTA.cpp` | OTA 协议核心实现 |
| `lib/OTA/OTA.h` | 数据包结构定义 |
| `lib/CrsfProtocol/crsf_protocol.h` | CRSF 协议常量 |
| `lib/FHSS/FHSS.cpp` | 跳频实现 |
| `src/tx_main.cpp` | TX 主程序 |
| `src/rx_main.cpp` | RX 主程序 |

### 调试技巧

```c
// 启用调试日志
#define DEBUG_LOG

// 查看发送的通道值
for (int i = 0; i < 16; i++) {
    printf("CH%d: %u\n", i, ChannelData[i]);
}

// 监控链路质量
printf("LQ: %d%%, RSSI: %d dBm\n", 
    LQCalc.getLQ(), 
    Radio.LastPacketRSSI);
```

### 常见问题

**Q: 为什么连接不上？**
- 检查 UID 是否匹配
- 确认射频速率相同
- 验证跳频序列同步

**Q: 延迟过高？**
- 选择更高的刷新率
- 检查遥测比率设置
- 减少无线干扰

**Q: 通道值异常？**
- 确认使用 CRSF 格式 (172-1811)
- 检查通道映射
- 验证开关编码模式

---

*文档版本: 1.0*
*最后更新: 2025-12-08*
*基于 ExpressLRS 3.x*
