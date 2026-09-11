<img width="1282" height="832" alt="image" src="https://github.com/user-attachments/assets/046d94b1-71bc-4031-87b9-343239768d0f" />
## 🎯 软件概览
QStereoView 是一套基于 Qt 5 + OpenGL 2.1 的跨平台立体影像显示系统（`main.cpp:8-27` ）。它能够以多种立体观察方式展示左右影像对，并提供丰富的交互工具用于立体测量与影像对齐。

## 📦 核心功能模块
### 1. 立体影像显示（StereoCanvas + GLSL Shader）
支持 12 种显示模式 （`core/DisplayMode.h:6-20` ），覆盖几乎所有主流立体观察方式：

模式 说明 闪屏 Shutter 优先走硬件四缓冲（`GL_BACK_LEFT` /`GL_BACK_RIGHT` ）输出 真彩 RGB ，无硬件时软件高速翻页 红青 Dubois Dubois 算法消串扰的红青立体，适合红青眼镜 红青 Simple 简易红青合成 左右分屏 SBS 左|右并排 右左分屏 SBS-RL 右|左并排 上下分屏 TopBottom 上左下右 行交错 RowInterlace 隔行扫描（3D 电视/显示器） 列交错 ColumnInterlace 隔列扫描 棋盘 Checkerboard 棋盘格交错 仅左片 / 仅右片 单眼查看 差值 Difference 左右片相减，用于检查相对定向与对齐

### 2. 立体相机系统（core/StereoCamera.h:10-69）
一个 纯数学、无 UI 依赖 的 3x3 仿射变换引擎，支持：

- 🔍 缩放 （zoom）— 支持光标处缩放，范围从 0.01× 到 256×
- 🔄 旋转 （rotation）— 整幅影像旋转
- ⬅️➡️ 视差调节 （parallax）— 右片水平偏移，对应立体量测中的"高程/视差"概念
- 👁️ 独立锚点 — 左右眼各自有独立的影像中心锚点，可独立平移对齐
- 🔀 反立体 （inverse）— 交换左右眼
### 3. 超大影像瓦片金字塔（gl/GpuImage.h:62-149）
专为处理 超大尺寸影像 （如航空/卫星影像）设计：

- 多级金字塔 — 根据缩放级别选择合适分辨率的瓦片
- 按需加载 — 只加载当前视口覆盖区域 + 256px 外扩
- LRU 缓存淘汰 — 限制 GPU 驻留瓦片数量，自动回收旧瓦片
- 异步解码 —`ImageDecodeJob` 运行在独立`QThread` ，不阻塞 UI
### 4. 影像加载方式
- 文件打开 — 支持 TIFF / JPEG / PNG / BMP / WebP / JPEG2000 等格式（`MainWindow.cpp:337` ）
- 拖拽导入 — 拖入 1~2 张图片自动识别左右
- 内存直接设置 —`setLeftImage()` /`setRightImage()` 接受`QImage` ，方便嵌入其他程序
- 内置示例 —`loadSamplePair()` 生成合成立体对，首次运行即可体验
### 5. 丰富的交互操作 鼠标操作（`StereoCanvas.cpp:969-1077` ）：
- 左键拖动 → 平移
- 右键 / Alt+左键拖动 → 仅移动右片对齐
- 滚轮 → 调节视差（高程）
- Ctrl+滚轮 → 光标处缩放
- Shift+滚轮 / 横向滚轮 → 水平微调右片
- Alt+滚轮 → 垂直微调右片
- 按住 Shift/Alt → 锁定显示右片，方便观察调整效果 键盘操作（`StereoCanvas.cpp:1079-1150` ）：
- `↑↓` /`[ ]` → 视差微调
- `Shift+方向键` → 右片对齐微调
- `Space` → 循环切换显示模式
- `I` → 反立体开关
- `F` → 适应窗口
- `Home` → 重置视图
- `Ctrl+O` → 打开左片
- `Ctrl+Shift+O` → 打开右片
- `Ctrl+S` → 导出当前视图
### 6. 每眼独立色彩增强（gl/GpuImage.h:16-28）
左片和右片可以独立调节：

- 亮度 （brightness）
- 对比度 （contrast）
- 伽马 （gamma）
这对于处理左右相机曝光不一致的立体对非常有用。

### 7. 立体测量辅助（光标十字丝）
- 黄色十字丝 + 青色圆环 — 精确标记鼠标位置
- 右眼标记 — 在右片上显示左片光标对应位置， 青色连线 直观展示视差偏移（`StereoCanvas.cpp:783-828` ）
- 状态栏实时显示：当前影像坐标、右片相对左片的 ΔX/ΔY 偏移量、视差值、缩放比等
### 8. 硬件立体优先
- 启动时请求`QSurfaceFormat::setStereo(true)` 四缓冲（`main.cpp:17` ）
- 运行时检测`m_hasHwStereo` ，有硬件时直接向`GL_BACK_LEFT` /`GL_BACK_RIGHT` 输出真彩
- 无硬件时自动降级为软件翻页模式
- 使用`wglSwapIntervalEXT` /`glXSwapIntervalSGI` 控制垂直同步
### 9. 其他特性
- HiDPI 支持 — Qt 5.6+ 自动启用高 DPI 缩放（`main.cpp:10` ）
- OpenGL 2.1 兼容 — 使用 Compatibility Profile，适配老旧显卡
- FBO 离屏渲染 — 非闪屏模式下先用 FBO 分别渲染左右眼，再由 composite shader 合成
- 窗口状态持久化 — 用`QSettings` 保存窗口 geometry 和 dock 布局
- 零依赖 GL 库 — 通过`QLibrary` 动态加载`opengl32.dll` 解析`glDrawBuffer`
## 🏗️ 技术架构图

`main.cpp
 └─ MainWindow (QMainWindow)       ← 菜单、工具栏、参数面板
     └─ StereoCanvas (QOpenGLWindow)  ← 核心立体渲染 + 交互
         ├─ StereoCamera            ← 数学引擎（缩放/旋转/视差/对齐）
         ├─ GpuImage (左/右眼各一)  ← 瓦片金字塔 + LRU 缓存
         ├─ ImageDecodeJob (QThread)← 异步超大影像分块解码
         └─ GLSL Shaders:
             ├─ image.vert/frag      ← 纹理采样 + 亮度/对比度/伽马
             ├─ composite.vert/frag  ← 12 种立体合成模式
             └─ color.vert/frag      ← 光标/叠加层绘制`

## 💡 典型应用场景
1. 摄影测量 — 航空/卫星影像立体观测、视差/高程量测
2. 3D 摄影鉴赏 — 左右分屏、红青、交错等多种方式欣赏立体照片
3. 相对定向检查 — 用"差值模式"快速检验左右片是否对齐
4. 立体显示器测试 — 硬件四缓冲闪屏模式下测试专业立体显示设备
这是一个设计精良、工程化程度较高的专业立体影像工具。
