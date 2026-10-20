# 微天气 · MiniWeather

一个极简的 Windows 桌面天气应用：**纯 Win32 API + GDI 绘制，C 语言，零第三方库**。
整个程序就是一个 exe，没有安装包、没有运行时、没有 Electron。

![预览](docs/preview.png)

## 为什么这样写

目标很明确：内存占用尽可能低。所以没有选 Web 技术栈，也没有用 .NET —— 直接用 Win32 的子窗口/消息循环，
界面全部用 GDI 画在窗口客户区里（标签、按钮、图标都是自绘，没有用任何控件）。

| 指标 | 实测 |
| --- | --- |
| 可执行文件 | 223 KB（静态链接 CRT） |
| 私有内存 | ~7.6 MB |
| 工作集合 | ~23 MB（其中大部分是 winhttp / crypt32 等系统 DLL 的共享页） |
| 句柄 | ~320 |
| 线程 | 1 个自建后台线程 + WinHTTP 内部线程 |
| 第三方依赖 | 无（只用系统自带的 winhttp / gdi32 / user32 / advapi32） |
| 空闲 CPU | 0%（定时器 10 分钟一次刷新） |

## 功能

- 顶部常用城市标签栏，最多 5 个城市，点击切换、悬停出 `×` 删除，`+` 号搜索新城市
- 右上角刷新按钮，配合自动刷新（每 10 分钟）
- 当前温度、天气现象、体感温度、当日最高/最低
- 相对湿度、风速与风向、海平面气压
- 未来 24 小时温度曲线（叠加降水概率柱）
- 7 天预报，温度区间条按温度着色
- 城市搜索：内置 Open-Meteo Geocoding，支持中文城市名
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

`tools/` 下是开发辅助程序（不参与打包）：

- `shot.c` —— 把运行中的窗口截成原始位图，配合 `tools/raw2png.py` 生成 PNG（文档截图用）
- `uitest.c` —— 交互冒烟测试：模拟加城市、切标签、点删除、悬停、刷新

```bat
build.bat shot
build\uitest.exe add 杭州      :: 打开输入框 → 输入 → 回车
build\uitest.exe click 116 29  :: 客户端坐标点击（切标签 / 点 × / 点刷新）
build\uitest.exe hoverreal 46 29
```

## 目录结构

```
src/app.h     共享类型、主题配色、全局状态、注册表接口
src/main.c    窗口、消息循环、常用城市管理、后台任务调度
src/ui.c      全部界面绘制与标签栏布局（GDI，双缓冲）
src/api.c     Open-Meteo 请求构造与解析、WMO 天气码中文文案
src/net.c     WinHTTP 同步 GET 封装
src/json.c    极简 JSON 解析器（直接解码为 UTF-16）
src/store.c   常用城市持久化（注册表）与旧配置迁移
src/util.c    UTF-8 转换、字符串工具
tests/        JSON 自检用例 + 真实响应样本
tools/        截图与交互测试小工具
```

## 数据与隐私

天气与地理编码均来自 [Open-Meteo](https://open-meteo.com/)，免费且无需 API Key。
客户端只发送经纬度坐标（或你输入的城市名），不发送任何设备标识。

常用城市列表与当前选中项保存在注册表：

```
HKEY_CURRENT_USER\Software\MiniWeather
  Cities = REG_MULTI_SZ  '北京|39.9042|116.4074' '杭州|30.2936|120.1614' ...
  Active = REG_SZ        '北京'
```

首次运行若检测到旧版的 `%APPDATA%\MiniWeather\config.txt`，会自动迁移成一条城市记录。
删掉这个注册表键即恢复默认（北京）。

## 已知限制

- 仅支持 Windows 10 及以上（用到 `GetDpiForSystem`、`SetProcessDpiAwarenessContext`，缺失时自动降级）
- 窗口固定尺寸，不支持拖拽缩放
- 城市名较长且数量到上限时，标签文字会自动省略
- 网络走系统代理设置，需要能访问 `api.open-meteo.com`
