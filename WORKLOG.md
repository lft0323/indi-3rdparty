# indi-3rdparty WORKLOG\n\n## 2026-09-08\n- 修改目标：将旧版自研 indi-qhy-v4l2 驱动接入新版 indi-3rdparty。\n- 修改文件：indi-qhy-v4l2/*、CMakeLists.txt、WORKLOG.md\n- 修改原因与内容：复制旧版驱动源码；新增 WITH_QHY_V4L2 选项和构建入口，保持旧版源码目录不变。\n- 影响范围：仅新版第三方库构建入口；尚未安装或替换板端现有 INDI。\n- 验证结果：待新版 core、第三方库配置和编译验证。\n
## 2026-09-08 15:55
- 修改目标：将旧版 indi-qhy-v4l2 迁移到 INDI 2.2.5 第三方驱动工程并完成构建安装。
- 修改文件：
  - indi-qhy-v4l2/
  - CMakeLists.txt
  - WORKLOG.md
- 修改原因与内容：
  - 将旧版本自定义 V4L2 相机驱动接入新版工程，新增 WITH_QHY_V4L2 构建选项，并完成核心库、第三方库及驱动安装。
- 影响范围：
  - 新版 INDI 环境新增 indi_qhy_v4l2_ccd；其他驱动按新版工程完成安装。
- 验证结果：
  - indi-core 2.2.5 构建安装成功；第三方库构建安装成功；全部第三方驱动构建到 100%；indi_qhy_v4l2_ccd 及 XML 已安装；ldd 未发现缺失依赖。

## 2026-09-09
- 修改目标：在 indi_qhy_ccd 中整合 V4L2 相机支持，同时保持原有 QHY SDK 代码和热插拔逻辑不变。
- 修改文件：
  - indi-qhy/CMakeLists.txt
- 新增并复用：
  - indi-qhy-v4l2/indi_qhy_v4l2.cpp
  - indi-qhy-v4l2/indi_qhy_v4l2.h
- 修改原因与内容：
  - 将现有 V4L2 实现作为附加设备对象编译进 indi_qhy_ccd，并补充 FFmpeg 依赖和 V4L2 版本宏。
- 影响范围：
  - indi_qhy_ccd 可同时包含原有 QHY SDK 驱动和 V4L2 驱动；原有 QHY SDK 源码未修改。
- 验证结果：
  - indi_qhy_ccd 构建 100% 成功并安装；可执行文件字符串检查包含 V4L2 实现；独立进程初始化检查完成。indiserver 运行测试因系统已有实例占用本地服务资源未执行完整连接测试。

## 2026-09-09（目录合并调整）
- 修改目标：将 V4L2 实现完全收进 indi-qhy 目录，不再保留顶层 indi-qhy-v4l2。
- 修改文件：
  - CMakeLists.txt
  - indi-qhy/CMakeLists.txt
  - indi-qhy/indi_qhy_v4l2.cpp
  - indi-qhy/indi_qhy_v4l2.h
- 修改原因与内容：
  - 移除顶层 WITH_QHY_V4L2 选项和独立 add_subdirectory；将 V4L2 源码平铺到 indi-qhy，并由 indi_qhy_ccd 直接编译。旧独立驱动的 CMake、spec 和 XML 模板不再保留。
- 影响范围：
  - 源码结构只保留 indi-qhy 一个模块；indi_qhy_ccd 同时包含 QHY SDK 和 V4L2 两种实现。原有 QHY 源码逻辑未修改。
- 验证结果：
  - indi-qhy 独立干净构建 100% 成功并安装；indi-3rdparty 顶层重新配置并构建 indi_qhy_ccd 成功。仅有 INDI 接口弃用警告。

## 2026-09-09（统一 QHY CCD 入口）
- 修改目标：上位机只显示 QHY CCD 驱动入口，由同一 indi_qhy_ccd 同时识别和控制原生 QHY SDK 相机与 V4L2 相机。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - WORKLOG.md
- 修改原因与内容：
  - 将 V4L2 设备默认名统一为 QHY CCD，将 DRIVER_EXEC 改为 indi_qhy_ccd；移除系统中旧的 indi_qhy_v4l2_ccd 和 indi_qhy_v4l2.xml，并停止旧独立驱动进程。
- 影响范围：
  - 上位机不再暴露 QHY V4L2 独立驱动；QHY CCD 下同时包含 V4L2 通用设备和原生 QHY 型号设备。原生 QHY SDK 控制逻辑未修改。
- 验证结果：
  - indiserver 7624 已加载新版 indi_qhy_ccd；客户端实际枚举到 QHY CCD 与 QHY CCD QHY26800A，无 QHY V4L2；V4L2 CONNECTION 返回 Ok，CCD_GAIN 写入返回 Ok。
## 2026-09-09 12:55
- 修改目标：修复整合后的 QHY CCD V4L2 相机无法正常控制和拍摄的问题。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - indi-qhy/indi_qhy_v4l2.h
- 修改原因与内容：
  - 修正曝光单位错误：V4L2 传感器曝光控制使用行数，不能把 INDI 传入的秒数直接按毫秒写入。
  - 根据子设备的 pixel_rate、horizontal_blanking 和图像宽度动态计算单行时间，将 INDI 秒数换算为曝光行数并限制在硬件范围内。
  - 连接时读取传感器当前曝光值，不再强制写入固定的 1000 行；同时将硬件曝光范围换算为秒后提供给 INDI 客户端。
  - 清理重复及遗留的 indi_qhy_ccd 进程，确保 indiserver 只管理一个驱动实例。
- 影响范围：
  - 影响 QHY CCD 条目下的 V4L2 相机曝光控制和可用曝光范围。
  - 原有 QHY SDK 相机实现及 QHY CCD QHY26800A 设备路径未修改。
- 验证结果：
  - indi_qhy_ccd 编译、链接和安装成功，仅有原有 INDI API 弃用警告。
  - 单实例运行验证通过；QHY CCD 连接状态为 Ok。
  - 曝光 0.0001 秒正确换算为 5 行，曝光及增益状态均为 Ok。
  - 成功生成 4150080 字节有效 FITS；原始 1920x1080 12 位数据不再全为 4095，像素范围 739-1151。

## 2026-09-09 14:56
- 修改目标：修复 QUARCS 重建 indiserver 后 QHY V4L2 相机被残留进程占用、再次扫描和连接失败的问题。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - WORKLOG.md
- 修改原因与内容：
  - 为合并后的 indi_qhy_ccd 增加 Linux 父进程退出保护；indiserver 终止时驱动自动接收 SIGTERM 退出，避免成为 PPID 1 的孤儿并长期占用 `/dev/video11`。
- 影响范围：
  - 仅影响 indi_qhy_ccd 与 indiserver 的进程生命周期；不改变原生 QHY SDK 和 V4L2 采集接口。
- 验证结果：
  - 编译、链接及安装成功。
  - 连续三轮 V4L2 连接、曝光、接收 4150080 字节 FITS、断开均成功，断开后设备句柄正常释放。
  - 独立 indiserver 终止测试通过：子驱动自动退出，无孤儿进程；QUARCS 服务重启后旧驱动也已正常消失。
## 2026-09-09 15:52
- 修改目标：按要求将 INDI 中 V4L2 相机的曝光时间设置恢复为最初迁移版本。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - indi-qhy/indi_qhy_v4l2.h
  - WORKLOG.md
- 修改原因与内容：
  - 移除基于像素时钟、行消隐和图像宽度计算单行曝光时间的逻辑。
  - 移除根据曝光行时间动态改写 `CCD_EXPOSURE` 秒范围的逻辑。
  - 恢复将 INDI 曝光秒数乘以 1000 后写入 V4L2 曝光控制、默认值 1000，以及原有毫秒日志。
- 影响范围：
  - 仅影响合并到 `indi_qhy_ccd` 中的 V4L2 相机曝光设置；原生 QHY 相机流程未修改。
  - 之前增加的父进程退出保护、设备识别及图像处理修改保持不变。
- 验证结果：
  - `indi_qhy_ccd` 已重新编译成功，仅有既有的 libindi deprecated 警告。
  - 新程序已安装到 `/usr/bin/indi_qhy_ccd`，`quarcs-client.service` 重启后状态为 active。
## 2026-09-10
- 修改目标：将整合后的 V4L2 相机显示名称调整为 `QHY CCD QHY26800A_guide`。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - WORKLOG.md
- 修改原因与内容：
  - 仅修改 `indi_qhy_v4l2::getDefaultName()` 返回值，便于在上位机中区分导星用 V4L2 相机。
  - 未修改原生 QHY SDK 相机的名称、控制流程或曝光逻辑。
- 影响范围：
  - 仅影响整合驱动中 V4L2 设备的显示名称。
- 验证结果：
  - `indi_qhy_ccd` 重新编译成功，仅有既有的 libindi deprecated 警告。
  - 程序已安装到 `/usr/bin/indi_qhy_ccd`，`quarcs-client.service` 重启后状态为 active。
