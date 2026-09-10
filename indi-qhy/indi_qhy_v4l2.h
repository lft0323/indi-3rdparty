/*
INDI Webcam CCD Driver

Copyright (C) 2018 Robert Lancaster (rlancaste AT gmail DOT com)

This driver was inspired by the INDI FFMpeg Driver written by
Geehalel (geehalel AT gmail DOT com), see: https://github.com/geehalel/indi-ffmpeg

This driver is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#ifndef indi_qhy_v4l2_H
#define indi_qhy_v4l2_H

#include <indiccd.h>
#include <stream/streammanager.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/version.h>

#ifdef __cplusplus
}
#endif
//#include <ctime>
#include <thread>

// V4L2 direct access support for Multiplanar devices
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

//These are required to check for AVFoundation Devices
//The reason is that we have to print and parse the output
//These can't be in indi_qhy_v4l2 class declaration because the callback method has to be passed to FFMpeg
//We need access to these variables in the callback method
std::vector<std::string> listOfSources;
bool connectedOnce = false;
bool allDevicesFound = false;
bool checkingDevices = false;

class indi_qhy_v4l2 : public INDI::CCD
{
public:
    indi_qhy_v4l2();
    ~indi_qhy_v4l2();
    void ISGetProperties(const char *dev) override;
    virtual bool ISNewNumber (const char *dev, const char *name, double values[], char *names[], int n) override;
    virtual bool ISNewSwitch (const char *dev, const char *name, ISState *states, char *names[], int n) override;
    virtual bool ISNewText (const char *dev, const char *name, char *texts[], char *names[], int n) override;
protected:
    // General device functions
    bool Connect() override;
    bool Disconnect() override;
    const char *getDefaultName() override;
    bool initProperties() override;
    bool updateProperties() override;

    void debugTriggered(bool enabled) override;



    //Related to exposures
    bool StartExposure(float duration) override;
    bool AbortExposure() override;
    void finishExposure();
    void TimerHit() override;
    float CalcTimeLeft();
    int timerID = 0;
    bool grabImage();

    bool UpdateCCDFrame(int x, int y, int w, int h) override;
    
    // 增益和偏移量控制（自定义实现，不覆盖基类）
    bool SetCCDGain(double gain);
    bool SetCCDOffset(double offset);

    //Related to streaming
    virtual bool StartStreaming() override;
    virtual bool StopStreaming() override;

    bool saveConfigItems(FILE *fp) override;

private:

    //Related to exposures
    struct timeval ExpStart { 0, 0 };
    float ExposureRequest { 0 };
    bool convertINDI_RGBtoFITS_RGB(uint8_t *originalImage, uint8_t *convertedImage);

    //These are related to how we change sources
    bool ConnectToSource(std::string device, std::string source, int framerate, std::string videosize, std::string inputpixelformat,  std::string urlSource);
    bool ChangeSource(std::string newDevice, std::string newSource, int newFramerate, std::string newInputPixelFormat, std::string newVideosize);
    bool ChangeOnlineSource(std::string newProtocol, std::string newIPAddress, std::string newPort, std::string newUserName, std::string newPassword);
    bool ChangeOnlineSource(std::string newURL);
    bool reconnectSource();

    //These are related to updating the device list
    void findAVFoundationVideoSources();
    int getNumOfInputDevices();
    bool refreshInputDevices();
    bool refreshInputSources();
    ISwitch RefreshS[1];
    ISwitchVectorProperty RefreshSP;

    //webcam stacking.
    bool webcamStacking = false;
    bool gotAnImageAlready = false;
    bool loadingSettings = false;
    bool averaging = false;
    float *stackBuffer = nullptr;
    int numberOfFramesInStack = 0;
    bool addToStack();
    void copyFinalStackToPrimaryFrameBuffer();
    void setImageDataValueFromFloat(int x, int y, float value, bool roundAnswer);
    void setRGBImageDataValueFromFloat(int x, int y, int channel, float value, bool roundAnswer);
    float getImageDataFloatValue(int x, int y);
    float getRGBImageDataFloatValue(int x, int y, int channel);

    //These are our device capture settings
    bool use16Bit = true;
    std::string videoDevice = "";
    std::string videoSource = "";
    int frameRate = 0;
    std::string videoSize = "";
    std::string inputPixelFormat = "";
    std::string outputFormat = "";
    //These are our online device capture settings
    std::string protocol = "";
    std::string IPAddress = "";
    std::string port = "";
    std::string username = "";
    std::string password = "";
    std::string customURL = "";
    std::string url = "";

    //The timeout for avformat commands like av_open_input and av_read_frame
    double ffmpegTimeout = 0;
    //The timeout for how long of a wait time constitutes a buffered frame vs a new frame
    double bufferTimeout = 0;

    //The pixel size for the camera
    double pixelSize = 0;

    //Related to Options in the Control Panel
    IText InputOptionsT[6] {};
    ITextVectorProperty InputOptionsTP;
    IText OnlineInputOptions[4] {};
    ITextVectorProperty OnlineInputOptionsP;
    IText URLPathT[1] {};
    ITextVectorProperty URLPathTP;
#ifdef __linux__
    IText V4L2SubdevPathT[1] {};
    ITextVectorProperty V4L2SubdevPathTP;
#endif

    ISwitch *OnlineProtocols = nullptr;
    ISwitchVectorProperty OnlineProtocolSelection;
    ISwitch *CaptureDevices = nullptr;
    ISwitchVectorProperty CaptureDeviceSelection;
    ISwitch *CaptureSources = nullptr;
    ISwitchVectorProperty CaptureSourceSelection;
    ISwitch *FrameRates = nullptr;
    ISwitchVectorProperty FrameRateSelection;
    ISwitch *PixelFormats = nullptr;
    ISwitchVectorProperty PixelFormatSelection;
    ISwitch *VideoSizes = nullptr;
    ISwitchVectorProperty VideoSizeSelection;
    ISwitch *RapidStacking = nullptr;
    ISwitchVectorProperty RapidStackingSelection;
    ISwitch *OutputFormats = nullptr;
    ISwitchVectorProperty OutputFormatSelection;
    // 16位字节序修正
    ISwitch *EndianFix = nullptr;
    ISwitchVectorProperty EndianFixSelection;
    bool swap16_on_send = false;
    ISwitch *PixelSizes = nullptr;
    ISwitchVectorProperty PixelSizeSelection;

    INumber TimeoutOptionsT[2] {};
    INumberVectorProperty TimeoutOptionsTP;
    INumber PixelSizeT[1] {};
    INumberVectorProperty PixelSizeTP;
    INumber VideoAdjustmentsT[3] {};
    INumberVectorProperty VideoAdjustmentsTP;
    // V4L2SubdevExposure UI属性已移除，曝光由上位机通过 CCD_EXPOSURE 控制
    
    // V4L2 增益和偏移量属性（手动定义）
    INumber GainT[1] {};
    INumberVectorProperty GainTP;
    INumber OffsetT[1] {};
    INumberVectorProperty OffsetTP;


    //Webcam setup, release, and frame capture
    bool flush_frame_buffer();
    bool setupStreaming();
    void freeMemory();
    bool getStreamFrame();

    //Related to streaming
    std::thread capture_thread;
    static void RunCaptureThread(indi_qhy_v4l2 *webcam);
    void run_capture();
    bool is_capturing = false;
    bool is_streaming = false;
    void start_capturing();
    void stop_capturing();

    //FFMpeg Variables to make captures work.
    struct SwsContext *sws_ctx;
    uint8_t *buffer;
    int numBytes = 0;
    AVPixelFormat out_pix_fmt;
    AVFormatContext *pFormatCtx;
    int              videoStream;
    AVCodecContext  *pCodecCtx;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59, 0, 100)
    AVCodec         *pCodec;
#else
    const AVCodec         *pCodec;
#endif
    AVFrame         *pFrame;
    AVFrame         *pFrameOUT;
    AVDictionary *optionsDict;

    //FFMpeg Video Adjustments
    double brightness = 0.0;
    double contrast = 1.0;
    double saturation = 1.0;
    void updateVideoAdjustments();

    // V4L2 direct access for Multiplanar devices
    bool use_v4l2_direct = false;                    // 标志：是否使用 V4L2 直接模式
    int v4l2_fd = -1;                               // V4L2 video节点设备文件描述符
    struct v4l2_format v4l2_fmt;                     // V4L2 格式结构体
    struct v4l2_buffer *v4l2_buffers = nullptr;     // V4L2 缓冲区数组
    unsigned int v4l2_buffer_count = 0;             // V4L2 缓冲区数量
    bool v4l2_streaming = false;                    // V4L2 流状态
    bool v4l2_is_mplane = false;                    // V4L2 是否为多平面模式
    void **v4l2_mmap_ptrs = nullptr;                // V4L2 内存映射指针数组
    size_t *v4l2_mmap_lens = nullptr;               // V4L2 内存映射长度数组
    bool v4l2_force_16bit = false;                  // V4L2 是否强制 16 位
    std::string v4l2_subdev_path { "/dev/v4l-subdev3" };// 子设备路径主要用来设置相机参数
    double v4l2_subdev_exposure = 1000.0;           // 子设备曝光时间
    int v4l2_subdev_fd = -1;                         // 子设备文件描述符
    double v4l2_subdev_exposure_min = 1.0;          // 子设备最小曝光时间（毫秒）
    double v4l2_subdev_exposure_max = 10000.0;      // 子设备最大曝光时间（毫秒）
    
    // V4L2 增益和偏移量控制
    int32_t v4l2_subdev_gain = 64;                  // 模拟增益值（默认64）
    int32_t v4l2_subdev_gain_min = 64;              // 最小增益
    int32_t v4l2_subdev_gain_max = 90112;           // 最大增益
    int32_t v4l2_subdev_offset = 0;                 // 偏移量值（通过寄存器设置）
    int32_t v4l2_subdev_offset_min = 0;             // 最小偏移量
    int32_t v4l2_subdev_offset_max = 4095;          // 最大偏移量（12位：0-4095）

    // Optional raw save after exposure
    bool save_raw_enable = true;
    std::string save_raw_path = "/tmp/indi_qhy_v4l2.raw";  //将indi采集到的原始数据保存到文件中
    IText SaveRawPathT[1] {};
    ITextVectorProperty SaveRawPathTP;
    ISwitch SaveRawS[1] {};
    ISwitchVectorProperty SaveRawSP;

    // Driver info property (for external clients setting DRIVER_INFO)
    IText DriverInfoT[4] {};
    ITextVectorProperty DriverInfoTP;
    
    bool ConnectToSourceV4L2(std::string source);
    bool DisconnectV4L2();
    bool getStreamFrameV4L2();
    bool flush_frame_bufferV4L2();
    bool setupV4L2Streaming();
    void freeV4L2Memory();
    
    // Enhanced V4L2 functions
    bool enumerateV4L2Formats();
    bool enumerateV4L2Sizes();
    bool enumerateV4L2FrameRates();
    bool setV4L2Format(uint32_t pixelformat);
    bool setV4L2Size(unsigned int width, unsigned int height);
    bool setV4L2FrameRate(unsigned int numerator, unsigned int denominator);
    bool setV4L2Exposure(double value);
    bool queryV4L2Control(unsigned int ctrl_id, struct v4l2_queryctrl *queryctrl);
    bool setV4L2Control(unsigned int ctrl_id, int32_t value);
    bool getV4L2Control(unsigned int ctrl_id, int32_t *value);
    bool ensureManualExposureMode();
    bool openV4L2Subdevice();
    void closeV4L2Subdevice();
    void updateV4L2SubdevExposureRange();
    void syncV4L2ExposureFromDuration(double duration);
    
    // 增益和偏移量控制函数
    bool setV4L2Gain(int32_t gain);                     // 设置模拟增益
    bool getV4L2Gain(int32_t *gain);                    // 获取当前增益
    void updateV4L2GainRange();                         // 更新增益范围
    bool setV4L2Offset(int32_t offset);                 // 设置偏移量（通过寄存器）
    bool getV4L2Offset(int32_t *offset);                // 获取当前偏移量
    bool writeV4L2Register(uint16_t reg, uint8_t value); // 写入单个寄存器
    bool readV4L2Register(uint16_t reg, uint8_t *value); // 读取单个寄存器
    
    bool setV4L2Crop(int x, int y, int w, int h);
    struct v4l2_rect getV4L2Crop();
    bool v4l2_can_crop = false;
    struct v4l2_cropcap v4l2_cropcap;
    struct v4l2_crop v4l2_crop;
    // 确保在每次 STREAMON 前缓冲区已全部入队
    bool requeueAllV4L2Buffers();

};
#endif // indi_qhy_v4l2_H
