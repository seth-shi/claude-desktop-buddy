# Claude Hardware Buddy · 神龙固件

[English](README.md) | **中文**

把一条会动的**神龙**摆在桌面上,实时显示你的 Claude 使用状态:

- 🐉 **会话心跳** —— Claude 桌面 App 通过 BLE 直连,告诉设备现在有没有在跑、用了多少 token、有没有等你批权限,神龙随状态换动作。
- 📊 **订阅额度** —— 本地一个 Go 小程序把你的 5 小时窗 / 7 天窗用量通过 USB 串口推给设备,画成左右两根进度条。

跑在一块国产 ESP32-S3 板子(**ES3N28P**)+ 240×320 ILI9341 屏上。会话走 BLE、额度走 USB 串口,两条通道互相独立,可同时使用。

> 本仓库是 [Claude Hardware Buddy BLE 协议](REFERENCE.md) 的一个自定义固件实现,换成了神龙主题并加上订阅额度显示。协议本身与本仓库无关,任何能广播 Nordic UART Service 的设备都能实现。

---

## 📸 效果展示

<p align="center">
  <img src="docs/device.jpg" alt="设备实拍" width="44%">
  &nbsp;&nbsp;
  <img src="docs/demo.gif" alt="运行演示" width="44%">
</p>

---

## ✨ 功能

- **神龙状态动画**:睡觉(未连接)/ 空闲 / 忙碌(多会话生成中)/ 注意(等你批权限)/ 庆祝(任务完成)
- **左右额度条**:5h + 7d 两个窗口;条**上方**是距重置倒计时(`2h`/`30m`,天级则 `3d`/`6h`),条**下方**是剩余百分比;剩余 `< 50%` 时变神龙金黄,收不到数据时整条隐藏(只留背景)
- **顶部账户名** + 中间 2×2 信息卡(token 用量、会话数等实时数据)
- **BLE 安全配对**:LE Secure Connections + 6 位配对码绑定,链路 AES-CCM 加密,重连免再配对
- **权限提示**:桌面 App 发来权限请求时,神龙进入「注意」态(设备侧回应逻辑见协议)

---

## 🧰 硬件(ES3N28P)

| 部件 | 型号 / 参数 |
| --- | --- |
| 主控 | ESP32-S3-N16R8(16MB Flash + 8MB OPI PSRAM) |
| 屏幕 | ILI9341 240×320,SPI2 @ 40MHz |
| 触摸 | FT6336G 电容触摸(在板,固件暂未启用) |
| 供电/串口 | 原生 USB-Serial-JTAG(COM3) |

**引脚**(见 [`src/display.h`](src/display.h)):`SCLK 12` · `MOSI 11` · `MISO 13` · `CS 10` · `DC 46` · `RST = EN(软复位)` · 背光 `GPIO45`(PWM,高有效)。

---

## 📁 仓库结构

```
platformio.ini          固件构建配置(env: es3n28p)
src/
  main.cpp              主循环:BLE + 串口轮询、两级重绘(防闪)
  display.h             LovyanGFX / ILI9341 引脚 & 方向配置
  theme.h               配色
  sprites.h             神龙贴图(RGB565)
  ui.h                  首页绘制(龙 / 信息卡 / 额度条 / 配对码)
  protocol.h            JSON 协议解析(心跳 + 额度 + 配对握手)
  ble_bridge.{h,cpp}    Bluedroid NUS + 安全绑定
tools/usage-bridge/     额度桥(Go,把订阅额度推到串口)
REFERENCE.md            BLE 线协议完整文档
```

---

## 🔧 固件:构建 & 烧录

前置:VS Code + [PlatformIO](https://platformio.org/) 扩展(或 `pio` CLI)。国内首次编译要下 espressif32 工具链,记得挂代理:

```powershell
$env:HTTPS_PROXY = "http://127.0.0.1:7897"   # 换成你的代理端口
```

板子接 USB(认到 COM3),编译 + 烧录:

```bash
pio run -e es3n28p -t upload
```

看串口日志:

```bash
pio device monitor -e es3n28p      # 115200,原生 USB-Serial-JTAG
```

---

## 📡 额度桥 `tools/usage-bridge`

一个自包含的小程序:读 CLI 存好的 OAuth token → 查你的订阅额度 → 每 5 分钟写一行 JSON 到设备串口,点亮左右两根额度条。它和 BLE 是**两条独立通道**,可以和桌面 App 同时跑。

### 前置

1. 用订阅账号登录 CLI:`claude auth login`(必须全权限 token;`setup-token` 会 401)
2. 能访问 Anthropic API —— **国内必须带代理**(Anthropic 对中国 IP 直接返回 `403 Request not allowed`)

### 构建

```bash
cd tools/usage-bridge
# Windows
GOTOOLCHAIN=local go build -o claude-buddy-usage.exe .
# macOS / Linux
GOTOOLCHAIN=local go build -o claude-buddy-usage .
```

> 依赖锁定 `go.bug.st/serial v1.6.2` + `go 1.24`,用本地工具链编(`GOTOOLCHAIN=local`),别让它自动升到 1.25(会编译崩)。

### 命令

> ⚠️ Go 的 flag 遇到第一个位置参数就停,`--port`/`--proxy` 等 **必须放在子命令前面**:`claude-buddy-usage --port COM3 enable`。

| 命令 | 作用 |
| --- | --- |
| `claude-buddy-usage`（无子命令） | **正式运行**:取额度 → 每 300s 推串口(默认循环) |
| `... test` | 只查额度并打印,不碰串口。验证取数/解析 |
| `... ports` | 列出可用串口 |
| `... serialtest` | 免 token,保持连接每 5s 推一条假额度。验证 主机→串口→设备 |
| `... refresh` | 手动触发一次 CLI 刷新 token |
| `... enable` | **装开机自启**(见下),并立刻后台启动 |
| `... disable` | 卸载自启 + 停止 |
| `... status` | 查自启是否安装、进程是否在跑 |

**参数**:`--port <COM3 / /dev/tty…>` · `--proxy <url>` · `--interval <秒,默认 300>` · `--once`(推一次退出) · `--token <t>`。

### 正式运行

```bash
# 国内带代理;--proxy 会同时应用到本程序和它拉起的 claude 子进程
./claude-buddy-usage --port COM3 --proxy http://127.0.0.1:7897
```

### 开机自启(推荐)

一条命令装成登录自启,后台无窗口运行、崩溃自愈:

```bash
./claude-buddy-usage --port COM3 --proxy http://127.0.0.1:7897 enable
```

| 平台 | 机制 |
| --- | --- |
| **macOS** | launchd LaunchAgent(`~/Library/LaunchAgents/com.claudebuddy.usage.plist`,`RunAtLoad` + `KeepAlive` 自动重启) |
| **Windows** | 登录注册表启动项(`HKCU\…\Run`,免管理员;后台无窗口) |
| **Linux** | 提示手动建 systemd user service |

日志统一写到 `~/.claude-buddy-usage.log`。管理:

```bash
claude-buddy-usage status      # 装没装 + 进程在不在跑
claude-buddy-usage disable     # 卸载
```

- **没插设备也无害**:`run` 循环里开串口失败就每 ~10s 重试一次,插上设备下一轮自动接管,不会崩。
- **停止/卸载会让设备复位一次**(关串口 → DTR 掉 → ESP32-S3 自动复位),属正常现象,重连后下次推送即恢复。

### 工作原理

1. 读 CLI 存的 OAuth token(`~/.claude/.credentials.json`)→ `GET /api/oauth/usage` → 解析 5h/7d 的剩余% + 重置时间 → 写 `{"rem5":..,"sec5":..,"rem7":..,"sec7":..}` 到串口。
2. **token 刷新交给 CLI**:直连刷新端点会被 Anthropic 区域墙挡死,所以 token 过期(401)时程序调一次便宜的 `claude -p`(Haiku)让 CLI 自己刷新 token,再重读重试。
3. **串口须拉高 DTR/RTS** 设备才能收数(ESP32-S3 USB-Serial-JTAG 用 DTR 判「主机在线」),程序开口时已处理。

---

## 🔗 与桌面 App 配对

BLE 桥默认关闭,在 Claude for macOS / Windows 里开启:

1. **Help → Troubleshooting → Enable Developer Mode**
2. **Developer → Open Hardware Buddy…**
3. **Connect**,从扫描列表选你的 `Claude-Buddy-XXXX`,按屏幕上显示的 6 位配对码完成配对

配对后后台自动重连,只在初次配对 / 看统计面板时需要开着窗口。完整步骤与线协议见 **[REFERENCE.md](REFERENCE.md)**。

---

## 📝 协议速览

设备解析换行分隔的 UTF-8 JSON:

- **心跳**(BLE,桌面 App):`total` / `running` / `waiting` / `tokens` / `tokens_today` / `msg` / `prompt` …
- **额度**(USB 串口,本桥):`rem5` / `sec5` / `rem7` / `sec7`
- **握手**:`{"cmd":"status|owner|unpair"}` → ack;`{"time":[epoch,tz]}` 时间同步;`{"cmd":"permission",…}` 权限决策

字段全表、权限决策、文件推送、安全绑定细节见 **[REFERENCE.md](REFERENCE.md)**。

---

## 致谢

- BLE 协议 & 示例:Claude Hardware Buddy(Anthropic,面向 maker,非官方支持特性)
- 显示:[LovyanGFX](https://github.com/lovyan03/LovyanGFX) · JSON:[ArduinoJson](https://arduinojson.org/) · 串口:[go.bug.st/serial](https://github.com/bugst/go-serial)
