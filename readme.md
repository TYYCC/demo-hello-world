# ESP32-S3 Demo Hello World

## 项目简介

本项目基于 ESP32-S3 R8N16 开发板，演示了多种功能，包括图像传输、串口打印、陀螺仪体感、小型游戏及远程通话（蓝牙 Mesh 实现）。适用于物联网、智能硬件等场景的快速开发和学习。

## 硬件

- ESP32-S3 R8N16 开发板

## 支持功能

1. 图像传输
2. 串口打印
3. 陀螺仪体感（开发中）
4. 小游戏
   - 俄罗斯方块
   - 贪吃蛇（开发中）
5. 远程通话（开发中）

## 目录结构

- `main/`：主程序代码，包括入口文件和各功能模块
- `components/`：自定义组件及第三方库
- `build/`：编译生成的中间文件和固件
- `doc/`：相关文档说明
- `managed_components/`：通过组件管理器安装的依赖
- `others/`：其他辅助文件或测试代码

## 开发环境

ESP32-IDF v5.5 和 ESP-ADF库

ESP-IDFv5.5[下载目录](https://github.com/espressif/idf-installer/releases/download/offline-5.5/esp-idf-tools-setup-offline-5.5.exe),国内[镜像](https://dl.espressif.com/dl/idf-installer/esp-idf-tools-setup-offline-5.5.exe)


## 安装中文字库

1. 确保工程目录下存在font文件夹，里面包含现有的字库文件
2. 查看分区是否正确，字库分区为1536K大小，在[分区表](./partitions.csv)中查看
3. 打开VS-Code的ESP-IDF工具，在设备分区资源管理器中选中font，下载二进制文件到该分区中，二进制文件为.bin字体文件，如果不喜欢该字体，可以更换为自己喜欢的字体。使用LVGL的[字体转换网站](https://lvgl.io/tools/fontconverter)制作即可。

### 字体制作指南

|选项|提示|备注|
| :--- | :---|:--- |
|Size|16|显示标题可以改为20|
|Bpp|2|如果选择2无法显示某些字，改成1即可|
|Range|0x20-0x7F,0x4E00-0x9FA5|-|

|图标|提示|
| :--- | :---|
|Size|18|
|Bpp|2|
|Range|0xe66b,0xe6ab|




其他选择自己喜欢的中文字体即可，上述的范围包括常规的汉字和数字。通过网页转换如果字体文件较大的话会卡顿，耐心等待。

## 编译与烧录

1. 安装 ESP-IDF 开发环境（建议使用官方文档步骤）
2. 修改[Cmakelist.txt](./CMakeLists.txt)文件可以选择编译发射端和服务端
3. 在项目根目录下执行：
   ```powershell
   idf.py build
   idf.py -p <COM端口号> flash
   ```
4. 连接开发板，打开串口工具查看输出

### 更新计划

1. 后续支持ELRS协议，支持串行多通道PWM传输
2. 支持接收端8通道PWM直出
3. 接收端将采用PCIE接口设计，目前想法为pcie x1 接口

## 参考文档

- [ESP-IDF 官方文档](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/index.html)
- 项目内 `doc/` 文件夹

TCP服务端口分布情况：
- 端口8080：用于串口文本传输
- 端口6556：用于图像传输
- 端口7878：用于心跳包和常规命令控制
- 端口6667：用于远程控制
- 端口7557：用于音频传输
- 端口1100：用于特殊命令发送
