# NullLineUart —— CH572 USB↔2.4G 无线串口 + IAP 升级

Dongle 插电脑 USB 被识别为 CH340 的 COMx，经 RF 透传给 Dev，Dev 再经物理串口(PA0/PA1) 接设备，数据双向透传。

## 固件布局（Flash）

| 区域 | 地址 | 说明 |
|---|---|---|
| IAP boot（dongle/dev 共用同一镜像） | 0x0000 ~ 0x8000 | 读共享信息页判断进 IAP 还是跳 app |
| App | 0x8000 ~ 0x3B000 | Dongle/Device 应用 |
| 共享信息页 | 0x3B000 | 配对信息 + IAP 标志 + 升级通道 |

上电 boot 先跑：`ImageFlag != IAP` → 立即跳 app；为 IAP 则进升级流程。

## 构建

```bat
cmake --build build/riscv-release
```

产物（`build/riscv-release/`）：

| 文件 | 用途 |
|---|---|
| `NullLineUart_Dongle.hex` / `NullLineUart_Device.hex` | **boot+app 合并镜像，直接烧录用** |
| `NullLineUart_Dongle_app.hex` / `NullLineUart_Device_app.hex` | 仅 App（起始 0x8000），供官方工具升级 |
| `NullLineUart_Boot.hex` | 独立 boot（调试用） |

烧录新固件用**合并镜像**（烧完上电即进 app，行为正常）。

## IAP 升级（无按键，COM 口发魔数进 boot）

需 PC 装 WCH USB 驱动（CH375 体系），官方工具 `WCHMcuIAP_WinAPP.exe`。

### 进 boot 的魔数（COM 口发送）

| 目标 | 魔数（十六进制） | 说明 |
|---|---|---|
| Dongle | `1B 49 41 50 44 4C 47 1B` | ESC `IAPDLG` ESC → 进 **USB** 升级 |
| Device | `1B 49 41 50 44 45 56 1B` | ESC `IAPDEV` ESC → 经 dongle 透传，进 **RF+串口** 升级 |

### Dongle 升级（USB 接口）

1. 串口工具打开 dongle COM，发魔数 `1B494150444C471B`
2. dongle 掉线重枚举为 PID `55E0`
3. `WCHMcuIAP` 选 **USB 接口** → 扫描到设备 → 加载 `NullLineUart_Dongle_app.hex` → 下载
4. 成功后自动跳回 app，COM 恢复

### Device 升级（串口接口，经 dongle 透传）

1. 确保 dongle 与 dev 已配对
2. 串口工具打开 dongle COM，发魔数 `1B4941504445561B`
3. dev 进 boot，~1~2s 重连 dongle
4. `WCHMcuIAP` 选 **串口接口** → 同 COM、115200 → 加载 `NullLineUart_Device_app.hex` → 下载
5. 成功后 dev 自动跳回 app 并重新配对

## 已知限制

- 魔数为 8 字节特定序列；若透传的设备数据中恰含该序列，可能误触发进 boot（概率极低）。
