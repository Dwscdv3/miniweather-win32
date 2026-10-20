# 微天气 · MiniWeather

一个极简的 Windows 桌面天气应用：**纯 Win32 API + GDI 绘制，C 语言，零第三方库**。
整个程序就是一个 exe，没有安装包、没有运行时、没有 Electron。

![预览](docs/preview.png)

## 为什么这样写

目标很明确：内存占用尽可能低。所以没有选 Web 技术栈，也没有用 .NET —— 直接用 Win32 的子窗口/消息循环，
界面全部用 GDI 画在窗口客户区里（标签、按钮、图标都是自绘，没有用任何控件）。

| 指标 | 实测 |
| --- | --- |
| 可执行文件 | 237 KB（静态链接 CRT） |
| 私有内存 | ~7.9 MB |
| 工作集合 | ~23 MB（其中大部分是 winhttp / crypt32 等系统 DLL 的共享页） |
| 句柄 | ~326 |
| 线程 | 1 个自建后台线程 + WinHTTP 内部线程 |
| 第三方依赖 | 无（只用系统自带的 winhttp / gdi32 / user32 / advapi32 / shell32） |
| 空闲 CPU | 0%（定时器 10 分钟一次刷新） |

## 功能

- **数据源可切换**：配好和风天气（QWeather）就用和风（中国区 3–5 km、支持区县），没配置则自动用 Open-Meteo
- 顶部常用城市标签栏，最多 5 个城市，点击切换、**拖拽排序**、悬停出 `×` 删除，`+` 号搜索新城市
- 右上角刷新按钮，配合自动刷新（每 10 分钟）
- 当前温度、天气现象、体感温度、当日最高/最低
- 相对湿度、风速与风向、**空气质量（AQI，按等级着色）**
- 未来 24 小时温度曲线（叠加降水概率柱），右上角小字显示气压
- 7 天预报，温度区间条按温度着色，小标题带日出日落
- 底栏状态行显示分钟级降水摘要（和风中国区独有）
- 城市搜索：内置地理编码，支持中文城市名（和风可到区县）
- 深色界面，跟随系统 DPI 缩放，双缓冲绘制无闪烁

## 快捷键

| 操作 | 说明 |
| --- | --- |
| `F5` | 立即刷新 |
| `C` | 打开城市输入框（回车确认，Esc 取消） |

## 构建

需要 Visual Studio 2022（含 C++ 生成工具）与 Windows 10 SDK。

```bat
build.bat
build\weather.exe
```

`build.bat` 会自己定位 cl.exe 与 SDK 的 include/lib 目录，**不需要**先打开 VS 开发者命令行，
也不依赖 `vcvars64.bat`。若要换路径，改脚本顶部的 `VSDIR` / `KITS` 两个变量即可。

## 测试

```bat
build.bat test
```

编译 `tests/test_json.c`，用 `tests/sample.json`（真实 API 响应留档）跑一遍解析自检，
覆盖：字段提取、数组对齐、缺失键、越界索引、残缺/非法输入、`\uXXXX` 与代理对。

再跑 `tests/test_gunzip.c`，用 `tests/qweather_24h.gz`（和风真实 gzip 响应）验证自写的
inflate：解压正确性、解压后可解析、截断数据被拒绝。

`tools/` 下是开发辅助程序（不参与打包）：

- `shot.c` —— 把运行中的窗口截成原始位图，配合 `tools/raw2png.py` 生成 PNG（文档截图用）
- `uitest.c` —— 交互冒烟测试：加城市、切标签、点删除、悬停、拖拽排序、刷新

```bat
build.bat shot
build\uitest.exe add 杭州                       :: 打开输入框 → 输入 → 回车
build\uitest.exe click 116 29                   :: 客户端坐标点击（切标签 / 点 × / 点刷新）
build\uitest.exe drag 46 29 150 29              :: 按住第一个标签拖到右侧
build\uitest.exe hoverreal 46 29
```

## 目录结构

```
src/app.h     共享类型、主题配色、全局状态、接口声明
src/main.c    窗口、消息循环、常用城市管理、后台任务调度、命令行参数
src/ui.c      全部界面绘制与标签栏布局（GDI，双缓冲）
src/api.c     数据源分发、Open-Meteo 请求与解析、WMO 天气码中文文案
src/qweather.c 和风天气接入（GeoAPI / now / 24h / 7d）
src/net.c     WinHTTP 封装（含 gzip 响应自动解码）
src/gunzip.c  自写的 gzip/deflate 解压（无第三方依赖）
src/json.c    极简 JSON 解析器（直接解码为 UTF-16）
src/store.c   常用城市与 API 配置的注册表读写、旧配置迁移
src/util.c    UTF-8 转换、日期与字符串工具
tests/        JSON / gzip 自检用例 + 真实响应样本
tools/        截图与交互测试小工具
```

## 数据与隐私

默认走 [Open-Meteo](https://open-meteo.com/)：免注册、免 Key、非商用免费，但地理编码只到地级市。
配上和风天气后自动切换，中国区精度与区县覆盖都更好（详见
[docs/weather-api-survey.md](docs/weather-api-survey.md)）。

### 启用和风天气

在和风控制台拿到 **API Host**（形如 `xxxxx.re.qweatherapi.com`）和 **API Key**，然后：

```bat
build\weather.exe --host xxxxx.re.qweatherapi.com --key 你的Key
```

参数会写入注册表后立即生效，之后直接双击 `weather.exe` 即可。清除配置回到 Open-Meteo：

```bat
build\weather.exe --reset-api
```

配置保存在注册表（与城市列表同处）：

```
HKEY_CURRENT_USER\Software\MiniWeather
  Cities = REG_MULTI_SZ  '北京|39.9042|116.4074' ...
  Active = REG_SZ        '北京'
  QwHost = REG_SZ        'xxxxx.re.qweatherapi.com'
  QwKey  = REG_SZ        '...'
```

首次运行若检测到旧版的 `%APPDATA%\MiniWeather\config.txt`，会自动迁移成一条城市记录。
清空这个注册表键即恢复默认（北京 + Open-Meteo）。

> Key 以明文存在 HKCU 下（仅当前用户可读）。若这台机器多人共用，建议先用
> `--reset-api` 清掉，或改用系统凭据管理器保管。注意：Key 一旦出现在截图/聊天记录里，
> 最稳妥的做法是去控制台轮换一次。

客户端只发送经纬度（或你输入的城市名），不发送任何设备标识。

> 和风比 Open-Meteo 多出来的数据（中国标准 AQI、分钟级降水）以及哪些数据包不在免费订阅内，
> 见 [docs/weather-api-survey.md](docs/weather-api-survey.md) 第六节。
> 接和风后每轮刷新是 5 个请求，所以自动刷新间隔从 10 分钟放宽到 15 分钟（≈480 次/天，额度 1000/天）。

## 已知限制

- 仅支持 Windows 10 及以上（用到 `GetDpiForSystem`、`SetProcessDpiAwarenessContext`，缺失时自动降级）
- 窗口固定尺寸，不支持拖拽缩放
- 城市名较长且数量到上限时，标签文字会自动省略
- 网络走系统代理设置，需要能访问 `api.open-meteo.com`
