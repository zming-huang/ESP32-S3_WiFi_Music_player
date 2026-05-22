# ESP32-S3 WiFi DLNA 音乐播放器

基于 ESP32-S3 开发的 WiFi 投放（流媒体）播放器，支持标准 DLNA / UPnP 协议。可以将手机（如 QQ 音乐、网易云音乐、网易云、BubbleUPnP 等）上的音乐通过 WiFi 无线投放到 ESP32-S3 设备上，并通过 I2S 功放进行高保真音频解码输出。

项目采用 Arduino 框架编写，采用单文件 `.ino` 结构，方便修改与烧录。

---

## 🛠️ 首次使用：WiFi 初始配置指南

设备具备 **智能自适应配网** 功能。首次烧录或在新的网络环境中使用时，请按照以下步骤连接 WiFi：

### 1. 连接初始热点 (AP 模式)
- 当设备无法连接到预设的 WiFi 时，屏幕底部会显示 `AP Mode: ESP32-S3-Music-AP`。
- 打开手机或电脑的 WiFi 列表，搜索并连接以下热点：
  - **WiFi 名称 (SSID)**: `ESP32-S3-Music-AP`
  - **初始密码**: `12345678`

### 2. 进入配网后台
- 手机成功连接热点后，打开浏览器，在地址栏输入以下 IP 地址并访问：
  ```text
  192.168.4.1
基于 ESP32-S3 的 WiFi 投放播放器项目，支持 DLNA/UPnP 协议，可以将手机上的音乐通过 WiFi 投放到 ESP32 播放。
通过Arduino IDE 编写程序，单一文件格式。输出文件即可，机主会根据程序调试和输出报错进行修改。

# 🔊 音量映射与硬件增益说明

### 1. 代码音量线性对齐
针对手机端（0-100%）与 `ESP32-audioI2S` 库（0-21级）进行了正向等比线性映射：
$$\text{驱动级音量(0-21)} = \frac{\text{手机端音量(0-100)} \times 21}{100}$$
解决了音量调节不随手机按键同步或反向突变的问题。

### 2. 硬件物理增益（可选微调）
MAX98357A 功放板的物理放大倍数由其 **GAIN** 引脚的接线决定。如果觉得整体声音偏轻，可通过硬件接线调节物理增益：
- **GAIN 悬空**：默认增益为 **9dB**
- **GAIN 接 GND**：增益拉高至 **12dB**（声音明显变大）
- **GAIN 接 Vin (3.3V/5V)**：增益拉满至 **15dB**（声音非常响亮）

---

## 🛠️ 开发环境与依赖库

项目基于 **Arduino IDE** 环境开发（建议 ESP32 核心包版本 3.x 及以上），所需第三方依赖库列表：

1. **Audio** (由 *schreibfaul1* 开发的 `ESP32-audioI2S`) —— 核心音频解码库
2. **ESPAsyncWebServer** —— 异步 Web 服务器
3. **AsyncTCP** —— 异步网络底层支持
4. **Arduino_GFX_Library** —— 屏幕驱动与图形库
5. **ArduinoJson** —— JSON 格式解析支持

---

## 📝 核心主循环防卡顿机制

为了确保音乐播放平滑丝顺、无断音，程序将任务进行了合理切分：
- `loop()` 函数中以最高优先级、无阻塞地轮询 `audio.loop()` 解码器。
- 屏幕刷新、网络状态检查等低频事件通过 `millis()` 软件定时器限定为 **500ms** 触发一次。
- 局域网搜寻握手服务（SSDP 组播）被独立分配在 FreeRTOS 的后台核心任务中运行，完美避免了网络握手和音频播放之间的相互抢占。


## 硬件配置

### 1. 主控板
- **MCU**: ESP32-S3 N16R8
- **Flash**: 8MB+
- **PSRAM**: 8MB (推荐，用于音频缓冲)

### 2. 显示屏 (ST7789 1.54" TFT)
| 丝印 | 功能 | ESP32-S3 GPIO | 备注 |
|------|------|---------------|------|
| SCL | 串行时钟 (SCK) | GPIO 12 | 硬件 SPI 时钟 |
| SDA | 数据输入 (MOSI) | GPIO 11 | 硬件 SPI 数据线 |
| RES | 复位 (Reset) | GPIO 10 | 控制屏幕初始化 |
| DC | 数据/命令选择 | GPIO 9 | 区分指令和像素数据 |
| BLK | 背光控制 (Backlight) | GPIO 21 | 高电平点亮 |
| CS | 片选 (Chip Select) | GPIO 1 | 设为虚拟引脚 |
| VCC | 电源正极 | 3.3V/5V | 根据模块选择 |
| GND | 电源地 | GND | 共地 |

### 3. 音频功放 (MAX98357A)
| 丝印 | 功能 | ESP32-S3 GPIO | 备注 |
|------|------|---------------|------|
| BCLK | 位时钟 (Bit Clock) | GPIO 15 | I2S BCK |
| LRC | 左右声道选择 (WS) | GPIO 16 | I2S LRCK |
| DIN | 数字数据输入 | GPIO 7 | I2S DATA |
| GAIN | 增益控制 | 悬空/接地 | 默认即可 |
| VCC | 电源正极 | 3.3V | |
| GND | 电源地 | GND | 共地 |

## 软件依赖

### PlatformIO 库依赖
```ini
moononournation/GFX Library for Arduino  # ST7789 驱动
earlephilips/ESP32-audioI2S            # I2S 音频播放
me-no-dev/ESPAsyncWebServer            # HTTP 服务器
me-no-dev/AsyncTCP                     # 异步 TCP
bblanchon/ArduinoJson                  # JSON 处理
```

### 开发环境
- **Arduino IDE  通过arduino IDE 烧录 **
- **框架**: Arduino Framework for ESP32


## 功能特性

### ✅ 已实现功能
1. **WiFi 连接管理**
   - 自动连接已保存的网络
   - AP 配网模式 (未配置时自动启动)
   - 网络断开自动重连

2. **DLNA 媒体渲染器**
   - UPnP AVTransport 服务
   - 播放/暂停/停止/跳转控制
   - 音量控制 (0-100%)
   - 支持 HTTP 音频流 (MP3, AAC, FLAC 等)

3. **音频播放**
   - 网络流媒体播放
   - 音量硬件控制 (MAX98357A)
   - 元数据解析 (标题、艺术家)

4. **显示界面**
   - 240x240 TFT 显示
   - 当前播放信息显示
   - 进度条和时间显示
   - 音量显示
   - WiFi 状态指示

### 🔧 配置参数

#### WiFi 配置 (config.h)
```cpp
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define DEVICE_NAME   "ESP32-S3 Music Player"
```

#### 音量映射
手机端音量 (0-100) -> 代码端 (0-21)
- MAX98357A 增益公式: `Gain(dB) = 12dB - (volume * 3dB)`
- 0 = 最大音量 (+12dB)
- 21 = 静音 (-51dB)



## 使用说明

### 首次配置
1. 首次启动时，如果没有保存 WiFi 凭证，设备会启动 AP 模式
2. 连接热点 `ESP32-Music-Player`，密码: `12345678`
3. 配置 WiFi 网络 (可通过内置的配网页面或修改代码)

### 手机投屏
1. 确保手机和 ESP32 在同一 WiFi 网络
2. 使用支持 DLNA 的播放器 (如: BubbleUPnP, Hi-Fi Cast, 网易云音乐等)
3. 在播放器中选择 "投放" 或 "设备"
4. 选择设备 `ESP32-S3 Music Player`
5. 开始播放音乐

### 支持的音频格式
- MP3
- AAC
- FLAC
- WAV
- OGG
- OPUS (部分支持)

## 项目结构

```
esp32-wifi-player/
通过编写单一文件实现功能
```

## 故障排除

### 屏幕无显示
1. 检查接线是否正确
2. 确认屏幕背光引脚 (GPIO 21) 已拉高
3. 检查屏幕供电电压 (3.3V/5V)

### 无声音输出
1. 检查 MAX98357A 接线
2. 确认 I2S 引脚配置正确
3. 检查音频文件格式是否支持
4. 检查音量设置

### WiFi 连接失败
1. 确认 WiFi 凭证正确
2. 检查信号强度
3. 尝试清除凭证后重新配置
4. 检查路由器是否支持设备连接

### DLNA 设备无法发现
1. 确保手机和 ESP32 在同一网络
2. 检查防火墙设置
3. 等待 30 秒后重试 (UPnP 广播间隔)
4. 重启 ESP32

## 串口日志

连接串口监控器查看运行日志：
机主会调试文件，同时输出报错内容。

