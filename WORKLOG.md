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

## 2026-09-11 15:11
- 修改目标：将 QHY CCD 统一入口中的 V4L2 曝光单位改为标准 `V4L2_CID_EXPOSURE_ABSOLUTE`。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - indi-qhy/indi_qhy_v4l2.h
- 修改原因与内容：
  - 原实现把 INDI 秒数乘以 1000，并使用旧的 `V4L2_CID_EXPOSURE`；现改为查询和设置 `V4L2_CID_EXPOSURE_ABSOLUTE`，按 100 微秒单位发送。
  - 例如 1 秒转换为 10000，2 秒转换为 20000；更新日志和内部默认值/范围注释以反映新单位。
  - 修改前已备份为 `indi_qhy/indi_qhy_v4l2.cpp.bak-exposure-100us-20260911` 和 `indi-qhy/indi_qhy_v4l2.h.bak-exposure-100us-20260911`。
- 影响范围：
  - 仅影响整合到 `indi_qhy_ccd` 的 V4L2 相机曝光控制；原生 QHY SDK 相机路径未修改。
- 验证结果：
  - `indi-qhy-integrated-flat` 构建目录编译 100% 成功并安装到 `/usr/bin/indi_qhy_ccd`。
  - 已通过二进制字符串检查确认新曝光日志存在；尚未通过上位机实际拍摄验证 1 秒曝光。

## 2026-09-14
- 修改目标：按设备树角色自动创建 QHY CCD 入口下的多个 V4L2 相机实例。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - indi-qhy/indi_qhy_v4l2.h
  - WORKLOG.md
- 修改原因与内容：
  - 读取每个已注册 I2C 传感器设备树节点的 `qhy,indi-role`，通过 Media Controller 的已启用链路，把传感器、对应 capture video 节点和 sensor subdev 自动配对。
  - 每个完整管线创建独立实例；默认名称由 compatible 和角色生成，例如 `QHY CCD IMX585 Main`、`QHY CCD SC2210 Guide`。可选设备树属性 `qhy,indi-name` 可覆盖默认名称。
  - 管线不完整时跳过实例，避免向上位机暴露无法连接的相机。修改前源码已备份为 `indi-qhy/indi_qhy_v4l2.cpp.backup_20260914_before_dt_multicamera` 与 `.h.backup_20260914_before_dt_multicamera`；原安装二进制备份为 `/usr/bin/indi_qhy_ccd.backup_20260914_before_dt_multicamera`。
- 影响范围：
  - 仅影响 `indi_qhy_ccd` 中整合的 V4L2 实例发现、命名及设备映射；原生 QHY SDK 相机流程未修改。
- 验证结果：
  - `cmake --build /root/Projects/build/indi-3rdparty --target indi_qhy_ccd -j4` 构建成功，仅有既有 libindi deprecated 警告。
  - 独立启动验证已创建 `QHY CCD SC2210 Guide`，映射为 `/dev/video11` 与 `/dev/v4l-subdev3`。
  - IMX585 当前尚未形成可用媒体管线，程序已按设计跳过 Main 实例；Main 的实际连接/采图待内核绑定完成后验证。

## 2026-09-15 09:45
- 修改目标：修正 IMX585 Main 相机被 INDI 默认配置为 1920x1080 的问题。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - WORKLOG.md
- 修改原因与内容：
  - IMX585 的媒体传感器格式为 3840x2160，但通用 V4L2 实例构造函数对全部相机硬编码 `1920x1080`，连接 `/dev/video0` 时通过 `VIDIOC_S_FMT` 将采集节点改为 1080p。
  - 改为名称含 ` Main` 的实例默认设置 `3840x2160`，其他实例（含 SC2210 Guide）继续使用原有 `1920x1080`。
  - 修改前源码备份为 `indi-qhy/indi_qhy_v4l2.cpp.backup_20260915_before_main_4k_default`；原安装二进制备份为 `/usr/bin/indi_qhy_ccd.backup_20260915_before_main_4k_default`。
- 影响范围：
  - 仅影响多实例 V4L2 驱动中 Main 角色的默认采集分辨率；Guide 与原生 QHY SDK 相机逻辑未修改。
- 验证结果：
  - `indi_qhy_ccd` 重新构建成功，仅有既有 libindi deprecated 警告。
  - QUARCS 重启、重新连接和绑定后，日志显示 `/dev/video0` 以 `3840x2160` 初始化；`v4l2-ctl -d /dev/video0 --get-fmt-video` 实测为 `3840/2160`。
  - QUARCS 绑定确认：Guider → `QHY CCD SC2210 Guide`，MainCamera → `QHY CCD IMX585 Main`，均为已连接状态。

## 2026-09-15 10:02
- 修改目标：移除旧独立 V4L2 驱动的系统注册，仅保留整合后的 QHY CCD 入口。
- 修改文件：
  - `/usr/share/indi/indi_qhy.xml`
  - `WORKLOG.md`
- 修改原因与内容：
  - 系统残留 `/usr/bin/indi_qhy_v4l2_ccd` 和 `/usr/share/indi/indi_qhy_v4l2.xml`，导致 QUARCS 驱动选择列表显示旧的 `QHY V4L2`，而整合后的 `indi_qhy_ccd` 缺少 XML 注册。
  - 将旧二进制和 XML 移至 `backups/indi_qhy_legacy_20260915/`，并安装构建目录生成的 `indi_qhy.xml`，注册 `QHY CCD` → `indi_qhy_ccd`。
- 影响范围：
  - QUARCS/INDI 驱动选择列表不再显示旧的 QHY V4L2；整合后的 QHY CCD 入口用于 V4L2 与原生 QHY SDK 相机。
- 验证结果：
  - 系统路径中已不存在旧二进制和旧 XML；新 XML 中 `QHY CCD` 条目指向 `indi_qhy_ccd`。
  - QUARCS 已重新启动并能以 `indi_qhy_ccd` 连接两个 V4L2 设备。
## 2026-09-15 10:25
- 修改目标：修正 IMX585 Main 主相机 Bayer CFA 元数据缺失。
- 修改文件：indi-qhy/indi_qhy_v4l2.cpp、WORKLOG.md
- 修改原因与内容：为 Main 实例开启 CCD_HAS_BAYER 并设置 SGBRG CFA 和 0,0 偏移；导星实例及原生 QHY SDK 路径未修改。源码备份为 indi-qhy/indi_qhy_v4l2.cpp.backup_20260915_before_imx585_cfa。
- 影响范围：修正 IMX585 FITS/预览的 Bayer 解释，不改变分辨率、12 位采集格式或导星逻辑。
- 验证结果：indi_qhy_ccd 编译安装成功；quarcs-client.service active；待 QUARCS 实际拍摄确认。

## 2026-09-15 15:38
- 修改目标：修复 IMX585 Main 主相机启用 CIF compact 模式后出现 RAW12 画面拼接。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - /usr/bin/indi_qhy_ccd
  - WORKLOG.md
- 修改原因与内容：
  - 原代码优先关闭导星节点 `rkcif-mipi-lvds1/compact_test`，成功后立即退出循环，导致主相机节点 `rkcif-mipi-lvds/compact_test` 仍保持开启。
  - 改为继续遍历所有可用的 Rockchip CIF compact 节点，并修正成功路径中的文件句柄关闭逻辑；原源码和已安装二进制均已备份。
- 影响范围：
  - 影响整合后的 QHY CCD 驱动打开 Rockchip V4L2 主/导星相机时的 compact 配置；原生 QHY SDK 相机逻辑未修改。
- 验证结果：
  - 已在 192.168.2.87 板端重新编译并替换 `/usr/bin/indi_qhy_ccd`，驱动进程已重新拉起。
  - 导星节点已保持 `0 0 0 0`；将主相机节点运行时设为 `0 0 0 0` 后，直接采集两帧成功，格式步长由 `5888` 变为 `7680`，原始数据大小为 `33177600` 字节，证明 compact 关闭后采集布局正常；QUARCS 实际 MainCamera 连接仍需用户界面侧确认。


## 2026-09-16
- 修改目标：让整合后的 V4L2 相机自动识别有效采集节点并动态获取分辨率。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - indi-qhy/indi_qhy_v4l2.cpp.backup_20260916_before_dynamic_resolution
  - WORKLOG.md
- 修改原因与内容：
  - 移除按角色写死 3840x2160/1920x1080 的初始分辨率，连接时使用 V4L2 实际协商格式。
  - 在媒体拓扑匹配后验证视频节点具备采集能力、流式 I/O 和非零 G_FMT 宽高，自动选择有效采集节点。
  - 修正多平面 V4L2 节点读取宽高的路径。
- 影响范围：
  - V4L2 相机发现、节点映射和 CCD 分辨率初始化；原生 QHY SDK 路径未修改。
- 验证结果：
  - indi_qhy_ccd 编译成功并部署；独立 indiserver 日志创建 SC2210 Guide（/dev/video11）和 IMX585 Main（/dev/video0）。
- v4l2-ctl 已验证实际分辨率分别为 1920x1080 和 3840x2160。

## 2026-09-16 13:30
- 修改目标：改善 IMX585 Main V4L2 启动采集时的格式初始化和首帧同步。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - indi-qhy/indi_qhy_v4l2.h
  - indi-qhy/indi_qhy_v4l2.cpp.backup_20260916_before_initial_frame_sync
  - indi-qhy/indi_qhy_v4l2.h.backup_20260916_before_initial_frame_sync
  - /usr/bin/indi_qhy_ccd.backup_20260916_before_initial_frame_sync
  - WORKLOG.md
- 修改原因与内容：
  - 连接时始终重新提交当前完整 V4L2 格式，并检查 VIDIOC_S_FMT/VIDIOC_G_FMT 错误；开始采集后丢弃 2 帧启动帧，减少 CSI/CIF 切换阶段的无效数据影响。
  - 已保留源码和已安装驱动备份；原生 QHY SDK 路径未修改。
- 影响范围：
  - 影响整合后的 QHY CCD 驱动中 V4L2 相机的格式初始化和首帧处理；原生 QHY 相机逻辑未修改。
- 验证结果：
  - indi_qhy_ccd 已编译并安装成功。
  - 在当前板端分别使用直接 v4l2-ctl 和 INDI 抓图对比，文件大小均为 16588800 字节，抽样数据均为 0x0fff，哈希一致；当前仍受到 CSI FIFO overflow/size err 影响，尚未证明该修改已解决实际出图问题。

## 2026-09-16 17:10
- 修改目标：清理整合后的 V4L2 驱动中的固定设备参数，提升多相机通用性。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - indi-qhy/indi_qhy_v4l2.h
  - indi-qhy/indi_qhy_v4l2.cpp.backup_20260916_before_generic_cleanup
  - indi-qhy/indi_qhy_v4l2.h.backup_20260916_before_generic_cleanup
  - /usr/bin/indi_qhy_ccd.backup_20260916_before_generic_cleanup
  - WORKLOG.md
- 修改原因与内容：
  - 移除 Linux 下固定的 video11、v4l-subdev3 和旧的单一 V4L2 实例入口，继续使用设备树角色、I2C 设备和媒体拓扑自动发现采集节点。
  - 根据实际 V4L2 FOURCC 动态设置分辨率、位深和 Bayer 类型；像素尺寸改为读取设备树 qhy,pixel-size-um 属性，未提供时保持未知值。
  - 将原先针对 SC2210 的固定寄存器偏置操作改为标准 V4L2_CID_BLACK_LEVEL 控件，并动态查询范围、步进和当前值；设备不支持时不发布偏置属性。
  - 将增益控制改用 V4L2_CID_ANALOGUE_GAIN 常量；未修改原生 QHY SDK 相机路径和曝光控制逻辑。
- 影响范围：
  - 影响整合后的 QHY CCD 驱动中 V4L2 相机发现、图像元数据和增益/偏置属性；原生 QHY 相机逻辑未修改。
- 验证结果：
  - indi_qhy_ccd 编译成功并安装到 /usr/bin，构建文件与安装文件 SHA-256 一致。
  - 独立 indiserver 冒烟测试成功自动创建 SC2210 Guide 和 IMX585 Main；活动源码静态检查未发现固定 video11、固定 subdev3、0x3907/0x3908 偏置寄存器或旧增益数字常量。

## 2026-09-17 13:35
- 修改目标：继续清理整合后的 V4L2 驱动中的固定参数，提升多相机兼容性。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - indi-qhy/indi_qhy_v4l2.cpp.backup_20260917_before_dynamic_generic_update
  - /usr/bin/indi_qhy_ccd
  - WORKLOG.md
- 修改原因与内容：
  - 连接子设备后根据 V4L2_EXPOSURE_ABSOLUTE 的实际最小值、最大值和步进更新 INDI 的 CCD_EXPOSURE 范围，曝光时间仍由 INDI 秒单位转换为 V4L2 的 100 微秒单位。
  - 扩展 RAW 元数据和采集路径，识别 Y10/Y12/Y14/Y16、RG/BA/GB/BG 10/12/14/16 位格式；对 16 位容器按行 stride 拷贝，拒绝无法安全解包的 packed RAW，避免图像错位。
  - 相机发现不再强制要求 qhy,indi-role；同一传感器存在多个 video 节点时优先选择有效 RAW 且分辨率最大的节点，角色仍优先用于命名。
  - Bayer 偏移量改为读取 video 节点的实际 crop 起点并取相位奇偶值，驱动不支持 selection 查询时回退为 0。
- 影响范围：
  - 影响整合后的 QHY CCD V4L2 相机发现、曝光范围、RAW 输出和多节点选择；原生 QHY SDK 路径未修改。
- 验证结果：
  - indi_qhy_ccd 已重新编译并安装到 87 板端 /usr/bin。
  - 独立 indiserver 冒烟测试仍自动创建 `QHY CCD SC2210 Guide` 和 `QHY CCD IMX585 Main`。
  - 现场未接入未标注角色的新相机，因此该回退路径尚未用实物验证。

## 2026-09-18
- 修改目标：将192.168.2.87板端的indi-3rdparty同步到192.168.2.240。
- 修改文件：
  - /root/Projects/indi-3rdparty（已切换为87版本）
  - /root/Projects/indi-3rdparty.backup-20260918020330（240原版本备份）
  - /root/Projects/indi-3rdparty/WORKLOG.md
- 修改原因与内容：
  - 先完成临时目录断点传输、残留清理和关键文件校验，再备份240原目录并切换为87版本。
- 影响范围：
  - 240端第三方INDI源码、构建配置及相关资源；原版本可从备份目录恢复。
- 验证结果：
  - 新目录文件数、关键CMake和V4L2文件哈希与87端一致，目录权限保持为root:root；本次未执行编译或重启服务。

## 2026-09-18
- 修改目标：重新编译并安装240板端的indi-3rdparty。
- 修改文件：
  - /root/Projects/build/indi-3rdparty（重新配置并生成构建产物）
  - /usr/bin/indi_qhy_ccd及相关第三方驱动安装文件
  - /root/Projects/indi-3rdparty/WORKLOG.md
- 修改原因与内容：
  - 使用Debug配置、CMAKE_INSTALL_PREFIX=/usr、WITH_WEBCAM=ON重新构建全部第三方驱动，并安装到系统。
- 影响范围：
  - 240板端的INDI第三方驱动可执行文件和驱动描述文件；未修改源码。
- 验证结果：
  - make -j4和make install均完成，返回码为0；indi-qhy相关目标已编译安装；NFS环境下indi_pentax的capability设置提示不影响本次安装。

## 2026-09-18（独立设备树名称与角色）
- 修改目标：使V4L2相机实例名称与内核compatible解耦。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - indi-qhy/indi_qhy_v4l2.cpp.backup_20260918_before_independent_name_role
  - /usr/bin/indi_qhy_ccd
  - WORKLOG.md
- 修改原因与内容：
  - 移除从compatible提取并转换传感器名称的逻辑。
  - 新增统一命名规则：QHY CCD + qhy,indi-name + qhy,indi-role；main/guide分别显示Main/Guide。
  - qhy,indi-name缺失时仅回退到I2C节点名，不再依赖compatible。
- 影响范围：
  - 仅影响整合到indi_qhy_ccd中的V4L2实例命名；compatible仍只负责内核驱动匹配，原生QHY SDK路径未修改。
- 验证结果：
  - 240端indi_qhy_ccd目标编译和安装成功，仅有既有libindi弃用警告；独立端口冒烟启动成功。
  - 240当前运行时设备树未发现qhy,indi-name节点，因此最终显示名称待带新设备树的板端验证；87当前离线，尚待同步。

## 2026-09-21 11:00
- 修改目标：移除整合版QHY V4L2直通路径对FFmpeg的编译、链接和运行时依赖。
- 修改文件：
  - indi-qhy/CMakeLists.txt
  - indi-qhy/indi_qhy_v4l2.cpp
  - indi-qhy/indi_qhy_v4l2.h
  - /usr/bin/indi_qhy_ccd
  - WORKLOG.md
  - backups/20260921_before_ffmpeg_removal/（原始源码、已安装驱动和工作日志备份）
- 修改原因与内容：
  - V4L2 Direct只保留V4L2 ioctl、mmap和设备树/媒体拓扑发现路径，移除indi_qhy_ccd目标的FFmpeg头文件、查找和链接项。
  - 连接、采集、清空帧缓存、流式采集和资源释放统一走V4L2路径；保留原生QHY SDK代码不变。
  - 将原FFmpeg超时配置改为V4L2缓冲轮询超时，并停止读取旧的FFmpeg/设备选择配置项，避免升级后的无效配置告警。
- 影响范围：
  - indi_qhy_ccd不再支持FFmpeg设备、IP视频源和FFmpeg图像转换；Linux板端V4L2 Direct功能保留。
  - 独立的indi-webcam工程仍是可选组件；本次没有修改该组件。
  - 原生QHY相机、设备树角色命名、多相机实例发现逻辑未主动修改。
- 验证结果：
  - indi_qhy_ccd目标使用cmake --build编译成功，仅有原有libindi API弃用警告。
  - 构建产物和/usr/bin/indi_qhy_ccd的ldd均未发现libavcodec、libavdevice、libavformat、libavutil或libswscale。
  - 独立端口indiserver冒烟启动成功，并发现QHY CCD QHY26800A Guide（/dev/video11、/dev/v4l-subdev3）。
  - v4l2-ctl对/dev/video11单帧采集成功，生成4147200字节原始帧。

## 2026-09-21 11:37
- 修改目标：彻底清理整合版 indi_qhy_v4l2 中残留的 FFmpeg 旧实现。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - indi-qhy/indi_qhy_v4l2.h
  - /usr/bin/indi_qhy_ccd
  - WORKLOG.md
  - backups/20260921_before_ffmpeg_deadcode_cleanup/（修改前源码、CMake、工作日志和已安装程序备份）
- 修改原因与内容：
  - 删除 7 个由 #if 0 禁用的 FFmpeg/AVFoundation/LibAV/SWS 旧代码块，共清理 cpp 约 613 行。
  - 删除仅供旧 AVFoundation 回调使用的全局变量及过期注释；保留文件头中的来源说明。
  - V4L2 ioctl、mmap、媒体拓扑发现、曝光、增益、偏置和原生 QHY SDK 路径未改动。
- 影响范围：
  - indi_qhy_ccd 仅保留原生 V4L2 Direct 采集后端；不再含可重新启用的 FFmpeg 后端死代码。
  - 独立 indi-webcam 工程未修改。
- 验证结果：
  - indi_qhy_ccd clean-first 重新编译和链接成功，仅有既有 libindi 弃用警告。
  - 源码未检出 LibAV、SWS、AV 对象、dlopen/dlsym 或 #if 0 残留；文件头仅保留来源说明。
  - ldd、readelf、nm 和 strings 均未检出 FFmpeg 库、符号或二进制字符串。
  - 构建产物与 /usr/bin/indi_qhy_ccd SHA-256 一致，quarcs-client.service 为 active。
  - 独立 indiserver 冒烟测试因板端已有 indiserver 实例占用本地服务资源未完成，不影响编译和依赖验证。

## 2026-09-21 12:29
- 修改目标：统一 indi_qhy_v4l2 有效源码中的注释语言。
- 修改文件：
  - indi-qhy/indi_qhy_v4l2.cpp
  - indi-qhy/indi_qhy_v4l2.h
  - /usr/bin/indi_qhy_ccd
  - WORKLOG.md
  - backups/20260921_before_english_comment_cleanup/（修改前源码、工作日志和已安装程序备份）
- 修改原因与内容：
  - 将曝光同步、缓冲入队、RAW stride、动态设备范围、媒体拓扑和 V4L2 控件等必要中文注释翻译为英文。
  - 删除与函数名或代码行为重复的中文注释；未修改任何功能语句、参数或控制流程。
- 影响范围：
  - 仅影响 indi_qhy_v4l2.cpp 和 indi_qhy_v4l2.h 的注释及生成程序中的调试行号；采集和控制逻辑不变。
- 验证结果：
  - 两个有效源码文件的中文字符扫描结果为空。
  - indi_qhy_ccd clean-first 编译和链接成功，仅有既有 libindi 弃用警告。
  - 新程序已安装，构建产物与 /usr/bin/indi_qhy_ccd SHA-256 一致，quarcs-client.service 为 active。
  - FFmpeg 依赖回归检查仍为空。
