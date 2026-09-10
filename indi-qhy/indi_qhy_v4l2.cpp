/*
INDI QHY CCD CCD Driver

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

#include <zlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <eventloop.h>
#include <algorithm>
#include <cmath>
#ifdef __linux__
#include <dirent.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/select.h>
#endif

#include "indi_qhy_v4l2.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "libavutil/dict.h"
#ifdef __cplusplus
}
#endif

#include "config.h"

#ifdef __linux__
#ifndef QHY_V4L2_DIRECT_DEVICE
#define QHY_V4L2_DIRECT_DEVICE "V4L2 Direct"
#endif
#endif

#ifdef __linux__
namespace
{
struct ParentDeathGuard
{
    ParentDeathGuard()
    {
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) == 0 && getppid() == 1)
            _exit(1);
    }
};

ParentDeathGuard parentDeathGuard;
}
#endif

static std::unique_ptr<indi_qhy_v4l2> qhy_v4l2(new indi_qhy_v4l2());

//Note this is how we get information about AVFoundation Devices
//FFMpeg does not provide a way to programmatically get them, but there is a way to log them.
//So we capture the logging and parse it to get the list of devices.
//This is the function FFMpeg calls.
void logDevices(void *ptr, int level, const char *fmt, va_list vargs)
{
    int printPrefix = 1;
    int lineSize = 1024;
    char *lineBuffer = (char *)malloc(1024);
    av_log_format_line(ptr, level, fmt, vargs, lineBuffer, lineSize, &printPrefix);
    if(checkingDevices)
    {
        if(allDevicesFound)
        {
            if(lineBuffer)
                free(lineBuffer);
            return;
        }
        if(strstr(lineBuffer, "AVFoundation video devices:") != nullptr)
        {
            if(lineBuffer)
                free(lineBuffer);
            return;
        }
        if(strstr(lineBuffer, "AVFoundation audio devices:") != nullptr)
        {
            allDevicesFound = true;
            if(lineBuffer)
                free(lineBuffer);
            return;
        }
        std::string line = lineBuffer;

        // This will remove the avfoundation label and hashcode and just leave the number in brackets and device name
        std::string device = line.substr(line.find_first_of("]") + 2);
        listOfSources.push_back(device);
    }
    else
    {
        if(av_log_get_level() >= level)
            fprintf(stderr, "%s", lineBuffer);
    }
    if(lineBuffer)
        free(lineBuffer);
}

//This method finds AVFoundation Devices.  Please see description above.
void indi_qhy_v4l2::findAVFoundationVideoSources()
{
    //Need to disconnect streaming if it is running
    bool was_streaming = false;
    if(is_streaming)
    {
        was_streaming = true;
        StopStreaming();
    }

    //Need to disconnect the source to probe the streams
    if(isConnected())
    {
        avcodec_close(pCodecCtx);
        avformat_close_input(&pFormatCtx);
    }
    else
    {
        //This appears to be needed for the list to get updated if a device was not connected
        //But it is fine to do without this the first time, so it isn't done in that case.
        if(!connectedOnce)
            connectedOnce = true;
        else
        {
            DEBUG(INDI::Logger::DBG_SESSION, "Briefly connecting to avfoundation to update the source list");
            if(ConnectToSource("avfoundation", "default", frameRate, videoSize, inputPixelFormat, "Not using IP Camera"))
                DEBUG(INDI::Logger::DBG_SESSION, "Source List Updated");
            avcodec_close(pCodecCtx);
            avformat_close_input(&pFormatCtx);
        }
    }

    listOfSources.clear();
    allDevicesFound = false;
    checkingDevices = true;

    //This is how we tell FFMpeg to call our method to log the sources.
    av_log_set_callback(logDevices);

    //This set of commands should open the avfoundation device to list its sources
    AVDictionary* options = nullptr;
    av_dict_set(&options, "list_devices", "true", 0);
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59, 0, 100)
    AVInputFormat *iformat = av_find_input_format("avfoundation");
#else
    const AVInputFormat *iformat = av_find_input_format("avfoundation");
#endif
    avformat_open_input(&pFormatCtx, "", iformat, &options);
    avformat_close_input(&pFormatCtx);
    checkingDevices = false;

    //Need to hook back up the source if it should be connected
    if(isConnected())
        ConnectToSource(videoDevice, videoSource, frameRate, videoSize, inputPixelFormat, url);

    //Hook back up streaming if it should be running
    if(was_streaming)
        StartStreaming();
}

indi_qhy_v4l2::indi_qhy_v4l2()
{
    setVersion(QHY_V4L2_VERSION_MAJOR, QHY_V4L2_VERSION_MINOR);

    pFormatCtx = nullptr;
    pCodecCtx = nullptr;
    pCodec = nullptr;
    optionsDict = nullptr;
    pFrame = nullptr;
    pFrameOUT = nullptr;
    sws_ctx = nullptr;
    buffer = nullptr;

    // These calls are depreciated, but are required for some older FFMPEG distributions on Linux
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    av_register_all();
#endif
    //This registers all devices
    avdevice_register_all();
    //This registers all codecs
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    avcodec_register_all();
#endif

    //This call is required for the IP Camera functionality to work
    avformat_network_init();

    //setting default values

#ifdef __linux__
    videoDevice = QHY_V4L2_DIRECT_DEVICE;
    videoSource = "/dev/video11";
    inputPixelFormat = "yuv420p";
#elif __APPLE__
    videoDevice = "avfoundation";
    videoSource = "0";
    inputPixelFormat = "uyvy422";
#else
    videoDevice = ""
    videoSource = "";
#endif

    frameRate = 30;
    videoSize = "1920x1080";
    webcamStacking = false;
    averaging = false;
    outputFormat = "8 bit RGB";

    protocol = "HTTP";
    IPAddress = "xxx.xxx.x.xxx";
    port = "xxxx";
    username = "iphone";
    password = "password";

    ffmpegTimeout = 1000000;
    bufferTimeout = 10000;
    pixelSize = 5.0;

    //Creating the format context.
    pFormatCtx = nullptr;
    pFormatCtx = avformat_alloc_context();
}

indi_qhy_v4l2::~indi_qhy_v4l2()
{
    if(pFormatCtx)
        free(pFormatCtx);
}


/**************************************************************************************
** Client is asking us to establish connection to the device
***************************************************************************************/
bool indi_qhy_v4l2::Connect()
{
    bool rc = false;

    ISwitchVectorProperty *connect = getSwitch("CONNECTION");
    if (connect)
        connect->s = IPS_BUSY;

    if(videoDevice == "IP Camera")
        DEBUGF(INDI::Logger::DBG_SESSION, "Trying to connect to IP Camera at: %s", url.c_str());
    else
        DEBUGF(INDI::Logger::DBG_SESSION, "Trying to connect to: %s, on device: %s with %s at %u frames per second",
               videoSource.c_str(), videoDevice.c_str(), videoSize.c_str(), frameRate);

    rc = ConnectToSource(videoDevice, videoSource, frameRate, videoSize, inputPixelFormat, url.c_str());
    return rc;
}

//This is the code that we use for FFMpeg to set up an input, connect to it, and set up the correct codecs.
bool indi_qhy_v4l2::ConnectToSource(std::string device, std::string source, int framerate, std::string videosize, std::string inputpixelformat,
                                  std::string urlSource)
{
    // Direct V4L2 path without FFMPEG
#ifdef __linux__
    if (device == QHY_V4L2_DIRECT_DEVICE || source.rfind("/dev/video", 0) == 0)
    {
        if (isConnected())
            DisconnectV4L2();
        if (!ConnectToSourceV4L2(source))
            return false;

        int w = v4l2_fmt.fmt.pix.width;
        int h = v4l2_fmt.fmt.pix.height;
        SetCCDParams(w, h, 12, pixelSize, pixelSize);
        DEBUG(INDI::Logger::DBG_SESSION, "Connection Successful (V4L2 Direct).");
        return true;
    }
#endif
    char stringFrameRate[16];
    snprintf(stringFrameRate, 16, "%u", framerate);
    char stringffmpegTimeout[16];
    snprintf(stringffmpegTimeout, 16, "%.0f", ffmpegTimeout);
    if(isConnected())
    {
        avcodec_close(pCodecCtx);
        avformat_close_input(&pFormatCtx);
    }

    AVDictionary* options = nullptr;
    av_dict_set(&options, "timeout", stringffmpegTimeout, 0); //Timeout for open_input and for read_frame.  VERY important.

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59, 0, 100)
    AVInputFormat *iformat = nullptr;
#else
    const AVInputFormat *iformat = nullptr;
#endif

    if(device != "IP Camera")
    {
        //These items are not used by an IP Camera
        av_dict_set(&options, "framerate", stringFrameRate, 0);
        av_dict_set(&options, "video_size", videosize.c_str(), 0);
        av_dict_set(&options, "pixel_format", inputpixelformat.c_str(), 0);
        iformat = av_find_input_format(device.c_str());
    }
    DEBUG(INDI::Logger::DBG_SESSION, "Attempting to connect");

    //This opens the input to get it ready for streaming.
    //Warning:  It is possible for the avformat_open_input command to hang if the camera is there and does not respond.
    //I have not yet solved this problem.  It does not happen often.
    int connect = -1;
    if(device == "IP Camera")
        connect = avformat_open_input(&pFormatCtx, urlSource.c_str(), nullptr, &options );
    else
        connect = avformat_open_input(&pFormatCtx, source.c_str(), iformat, &options );
    if (connect != 0)
    {
        char errbuff[200];
        av_make_error_string(errbuff, 200, connect);
        DEBUGF(INDI::Logger::DBG_SESSION, "Failed to open source. Check your settings: %s", errbuff);
        return false;
    }

    //pFormatCtx->max_analyze_duration = 10000000;

    if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
    {
        return false;
    }

    //This will attempt to find a video stream in the input.
    videoStream = -1;
    for(unsigned int i = 0; i < pFormatCtx->nb_streams; i++)
        if(pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            videoStream = i;
    if(videoStream == -1)
    {
        DEBUG(INDI::Logger::DBG_SESSION, "Failed to get a video stream.");
        return false;
    }

    //Find an appropriate decoder and then
    // Allocate a pointer to the codec context for the video stream
    pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id);
    pCodecCtx = avcodec_alloc_context3(pCodec);
    avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);

    //If an appropriate codec was not found, abort the connection
    if(pCodec == nullptr)
    {
        DEBUG(INDI::Logger::DBG_SESSION, "Unsupported codec.");
        return false;
    }

    //Attempt to open the codec.  If that fails, abort the connection.
    if(avcodec_open2(pCodecCtx, pCodec, &optionsDict) < 0)
    {
        DEBUG(INDI::Logger::DBG_SESSION, "Failed to open codec.");
        return false;
    }

    //Set the initial parameters for the CCD.
    SetCCDParams(pCodecCtx->width, pCodecCtx->height, 8, pixelSize, pixelSize);

    DEBUG(INDI::Logger::DBG_SESSION, "Connection Successful.");
    return true;

}

//This should be run if the source was somehow disconnected to try to reconnect it.
//It will make 10 attempts.
//It returns true if it was successful.
bool indi_qhy_v4l2::reconnectSource()
{
    int attempt = 0;
    while(attempt < 10)
    {
        if(ConnectToSource(videoDevice, videoSource, frameRate, videoSize, inputPixelFormat, url))
            return true;
    }
    //All 10 attempts resulted in failure.
    return false;
}

//This is the method that should be called to change the streaming device, source, framerate, or video size
//If it was already connected, it will attempt a connection with the new settings and if it is not successful, it will revert to the old ones.
//It should be safe to use while streaming or between image captures because it will pause them and return them to normal afterwards.
bool indi_qhy_v4l2::ChangeSource(std::string newDevice, std::string newSource, int newFramerate, std::string newInputPixelFormat, std::string newVideosize)
{
    //This will pause the streaming while it attempts the new connection settings.
    bool was_streaming = false;
    if(is_streaming)
    {
        was_streaming = true;
        StopStreaming();
    }

    DEBUGF(INDI::Logger::DBG_SESSION, "New Connection Settings: %s, on device: %s with %s at %u frames per second",
           newSource.c_str(), newDevice.c_str(), newVideosize.c_str(), newFramerate);

    //This is the case if the source is not currently connected yet.
    if(isConnected() == false)
        DEBUG(INDI::Logger::DBG_SESSION, "Not connected now, accepting settings.  It will be tested on connection");
    if(isConnected() == false || loadingSettings)
    {
        videoDevice = newDevice;
        videoSource = newSource;
        frameRate = newFramerate;
        videoSize = newVideosize;
        return true;
    }

    //This is an attempt to connect, if it is already connected.  If it is not successful, it goes back to the old settings.
    if(ConnectToSource(newDevice, newSource, newFramerate, newVideosize, newInputPixelFormat, url) == false)
    {
        DEBUG(INDI::Logger::DBG_SESSION, "Connection was NOT successful");
        DEBUGF(INDI::Logger::DBG_SESSION, "Changing back to: %s, on device: %s with %s at %u frames per second",
               videoSource.c_str(), videoDevice.c_str(), videoSize.c_str(), frameRate);
        ConnectToSource(videoDevice, videoSource, frameRate, videoSize, inputPixelFormat, url);
        if(was_streaming)
            StartStreaming();
        return false;
    }

    //This is what happens if the connection was successful, it saves the settings and continues.
    DEBUG(INDI::Logger::DBG_SESSION, "Due to success, Saving settings.");
    videoDevice = newDevice;
    videoSource = newSource;
    frameRate = newFramerate;
    inputPixelFormat = newInputPixelFormat;
    videoSize = newVideosize;

    //If it was streaming, we need to reinitialize that.
    if(was_streaming)
        StartStreaming();
    return true;
}

//This is the method that should be called to change the streaming device, source, framerate, or video size
//If it was already connected, it will attempt a connection with the new settings and if it is not successful, it will revert to the old ones.
//It should be safe to use while streaming or between image captures because it will pause them and return them to normal afterwards.
bool indi_qhy_v4l2::ChangeOnlineSource(std::string newProtocol, std::string newIPAddress, std::string newPort, std::string newUserName,
                                   std::string newPassword)
{
    std::string newURL;

    if(!strcmp(newProtocol.c_str(), "CUSTOM"))
        newURL = customURL;
    else if(!strcmp(newProtocol.c_str(), "HTTP"))
        newURL = "http://" + newUserName + ":" + newPassword + "@" + newIPAddress + ":" + newPort;
    //else if(!strcmp(newProtocol.c_str(), "RTSP"))
    //    newURL = "rstp://" + newIPAddress + ":" + newPort + "//user=" + newUserName + "_password=" + newPassword + "_channel=1_stream=0.sdp?real_stream";

    if(ChangeOnlineSource(newURL))
    {
        protocol = newProtocol;
        IPAddress = newIPAddress;
        port = newPort;
        username = newUserName;
        password = newPassword;
        return true;
    }
    return false;
}

bool indi_qhy_v4l2::ChangeOnlineSource(std::string newURL)
{

    //This will pause the streaming while it attempts the new connection settings.
    bool was_streaming = false;
    if(is_streaming)
    {
        was_streaming = true;
        StopStreaming();
    }

    //This is the case if the source is not currently connected yet.
    if(isConnected() == false)
        DEBUG(INDI::Logger::DBG_SESSION, "Not connected now, accepting settings.  It will be tested on connection");
    if(isConnected() == false || loadingSettings)
    {
        url = newURL;
        IText *URLText = &URLPathT[0];
        IUSaveText(URLText, newURL.c_str());
        IDSetText(&URLPathTP, nullptr);
        return true;
    }

    DEBUGF(INDI::Logger::DBG_SESSION, "Attempting to Connect: IP Camera at: %s", newURL.c_str());

    //This is an attempt to connect, if it is already connected.  If it is not successful, it goes back to the old settings.
    if(ConnectToSource(videoDevice, videoSource, frameRate, videoSize, inputPixelFormat, newURL) == false)
    {
        DEBUG(INDI::Logger::DBG_SESSION, "Connection was NOT successful");
        DEBUGF(INDI::Logger::DBG_SESSION, "Changing back to IP Camera at: %s", url.c_str());
        ConnectToSource(videoDevice, videoSource, frameRate, videoSize, inputPixelFormat, url);
        if(was_streaming)
            StartStreaming();
        return false;
    }

    //This is what happens if the connection was successful, it saves the settings and continues.
    DEBUG(INDI::Logger::DBG_SESSION, "Due to success, saving settings.");
    url = newURL;
    IText *URLText = &URLPathT[0];
    IUSaveText(URLText, newURL.c_str());
    IDSetText(&URLPathTP, nullptr);

    //If it was streaming, we need to reinitialize that.
    if(was_streaming)
        StartStreaming();
    return true;
}


/**************************************************************************************
** Client is asking us to terminate connection to the device
***************************************************************************************/
bool indi_qhy_v4l2::Disconnect()
{
    if (isConnected())
    {
        if (use_v4l2_direct)
        {
            DisconnectV4L2();
        }
        else
    {
        // Close the codecs
        avcodec_close(pCodecCtx);
        // Close the video file
        avformat_close_input(&pFormatCtx);
        }

        DEBUG(INDI::Logger::DBG_SESSION, "QHY CCD disconnected successfully!");
    }
    return true;
}
/**************************************************************************************
** INDI is asking us for our default device name
***************************************************************************************/
const char * indi_qhy_v4l2::getDefaultName()
{
    return "QHY CCD QHY26800A_guide";
}
/**************************************************************************************
** INDI is asking us to init our properties.
***************************************************************************************/
bool indi_qhy_v4l2::initProperties()
{
    loadingSettings = true;
    // Must init parent properties first!
    INDI::CCD::initProperties();

    setDefaultPollingPeriod(10);

    DEBUG(INDI::Logger::DBG_SESSION, "QHY CCD Driver initialized");
    
#ifdef __linux__
    // 定义增益和偏移量属性（仅 V4L2 模式）
    IUFillNumber(&GainT[0], "GAIN", "Gain", "%.0f", 
                 static_cast<double>(v4l2_subdev_gain_min), 
                 static_cast<double>(v4l2_subdev_gain_max), 
                 1.0, static_cast<double>(v4l2_subdev_gain));
    IUFillNumberVector(&GainTP, GainT, 1, getDeviceName(), "CCD_GAIN",
                       "Gain", IMAGE_SETTINGS_TAB, IP_RW, 60, IPS_IDLE);
    
    IUFillNumber(&OffsetT[0], "OFFSET", "Offset", "%.0f", 
                 static_cast<double>(v4l2_subdev_offset_min), 
                 static_cast<double>(v4l2_subdev_offset_max), 
                 1.0, static_cast<double>(v4l2_subdev_offset));
    IUFillNumberVector(&OffsetTP, OffsetT, 1, getDeviceName(), "CCD_OFFSET",
                       "Offset", IMAGE_SETTINGS_TAB, IP_RW, 60, IPS_IDLE);
#endif

    CaptureFormat rgb = {"INDI_RGB", "RGB", 8, true};
    addCaptureFormat(rgb);

    RapidStacking = new ISwitch[3];
    IUFillSwitch(&RapidStacking[0], "Integration", "Integration", ISS_OFF);
    IUFillSwitch(&RapidStacking[1], "Average", "Average", ISS_OFF);
    IUFillSwitch(&RapidStacking[2], "Off", "Off", ISS_ON);

    IUFillSwitchVector(&RapidStackingSelection, RapidStacking, 3, getDeviceName(), "RAPID_STACKING_OPTION", "Rapid Stacking",
                       MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
    defineProperty(&RapidStackingSelection);

    OutputFormats = new ISwitch[3];
    IUFillSwitch(&OutputFormats[0], "16 bit Grayscale", "16 bit Grayscale", ISS_OFF);
    IUFillSwitch(&OutputFormats[1], "16 bit RGB", "16 bit RGB", ISS_OFF);
    IUFillSwitch(&OutputFormats[2], "8 bit RGB", "8 bit RGB", ISS_ON);

    IUFillSwitchVector(&OutputFormatSelection, OutputFormats, 3, getDeviceName(), "OUTPUT_FORMAT_OPTION", "Output Format",
                       MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
    defineProperty(&OutputFormatSelection);

    IUFillNumber(&TimeoutOptionsT[0], "FFMPEG_TIMEOUT", "FFMPEG", "%.0f", 0 , 100000000, 1, ffmpegTimeout);
    IUFillNumber(&TimeoutOptionsT[1], "BUFFER_TIMEOUT", "Buffer", "%.0f", 0 , 10000000, 1, bufferTimeout);
    IUFillNumberVector(&TimeoutOptionsTP, TimeoutOptionsT, NARRAY(TimeoutOptionsT), getDeviceName(), "TIMEOUT_OPTIONS",
                     "Timeouts (us)", OPTIONS_TAB, IP_RW, 0, IPS_IDLE);

    defineProperty(&TimeoutOptionsTP);

    IUFillNumber(&PixelSizeT[0], "PIXEL_SIZE_um", "Pixel Size (µm)", "%.3f", 0 , 50, 0.1, pixelSize);
    IUFillNumberVector(&PixelSizeTP, PixelSizeT, NARRAY(PixelSizeT), getDeviceName(), "PIXEL_SIZE",
                     "Pixel Size", OPTIONS_TAB, IP_RW, 0, IPS_IDLE);

    defineProperty(&PixelSizeTP);
    // Optional raw save controls
    IUFillSwitch(&SaveRawS[0], "ENABLE", "Enable Save", ISS_OFF);
    IUFillSwitchVector(&SaveRawSP, SaveRawS, 1, getDeviceName(), "AUTO_SAVE_RAW", "Auto Save Raw", OPTIONS_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
    defineProperty(&SaveRawSP);

    IUFillText(&SaveRawPathT[0], "SAVE_RAW_PATH", "Raw Path", save_raw_path.c_str());
    IUFillTextVector(&SaveRawPathTP, SaveRawPathT, 1, getDeviceName(), "SAVE_RAW_PATH", "Raw Save Path", OPTIONS_TAB, IP_RW, 0, IPS_IDLE);
    defineProperty(&SaveRawPathTP);


    PixelSizes = new ISwitch[15];
    IUFillSwitch(&PixelSizes[0], "2.20", "NexImage 5 - 2.2", ISS_OFF);
    IUFillSwitch(&PixelSizes[1], "3.30", "Logitech Webcam Pro 9000 - 3.3", ISS_OFF);
    IUFillSwitch(&PixelSizes[2], "3.00", "SVBONY SV105 - 3.0", ISS_OFF);
    IUFillSwitch(&PixelSizes[3], "4.00", "SVBONY SV205 - 4.0", ISS_OFF);
    IUFillSwitch(&PixelSizes[4], "1.67", "NexImage 10 - 1.67", ISS_OFF);
    IUFillSwitch(&PixelSizes[5], "3.75", "NexImage Burst - 3.75", ISS_OFF);
    IUFillSwitch(&PixelSizes[6], "3.75", "Skyris 132 - 3.75", ISS_OFF);
    IUFillSwitch(&PixelSizes[7], "2.80", "Skyris 236 - 2.8", ISS_OFF);
    IUFillSwitch(&PixelSizes[8], "3.75", "iOptron iGuider or iPolar - 3.75", ISS_OFF);
    IUFillSwitch(&PixelSizes[9], "1.55", "Raspberry Pi HQ Camera - 1.55", ISS_OFF);
    IUFillSwitch(&PixelSizes[10], "2.8", "Logitech HD C270 - 2.8", ISS_OFF);
    IUFillSwitch(&PixelSizes[11], "2.9", "IMX290 USB 2.0 Camera Board - 2.9", ISS_OFF);
    IUFillSwitch(&PixelSizes[12], "2.9", "Spinel 2MP IMX290 H264 Camera - 2.9", ISS_OFF);
    IUFillSwitch(&PixelSizes[13], "3.0", "Microsoft LifeCam Cinema TM - 3.0", ISS_OFF);
    IUFillSwitch(&PixelSizes[14], "2.9", "OpenAstroGuider - 2.9", ISS_OFF);

    IUFillSwitchVector(&PixelSizeSelection, PixelSizes, 15, getDeviceName(), "PIXEL_SIZE_SELECTION", "Camera Pixel Sizes (µm)",
                       OPTIONS_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
     defineProperty(&PixelSizeSelection);

    IUFillSwitch(&RefreshS[0], "Scan Ports", "Scan Sources", ISS_OFF);
    IUFillSwitchVector(&RefreshSP, RefreshS, 1, getDeviceName(), "INPUT_SCAN", "Refresh", CONNECTION_TAB, IP_RW, ISR_ATMOST1,
                       60, IPS_IDLE);

    defineProperty(&RefreshSP);

    IUFillText(&InputOptionsT[0], "CAPTURE_DEVICE_TEXT", "Capture Device", videoDevice.c_str());
    IUFillText(&InputOptionsT[1], "CAPTURE_SOURCE_TEXT", "Capture Source", videoSource.c_str());
    IUFillText(&InputOptionsT[2], "CAPTURE_FRAME_RATE", "Frame Rate", "30");
    IUFillText(&InputOptionsT[3], "INPUT_PIXEL_FORMAT", "Input Pixel Format", inputPixelFormat.c_str());
    IUFillText(&InputOptionsT[4], "CAPTURE_VIDEO_SIZE", "Video Size", videoSize.c_str());
    IUFillTextVector(&InputOptionsTP, InputOptionsT, NARRAY(InputOptionsT), getDeviceName(), "INPUT_OPTIONS", "Input Options",
                     CONNECTION_TAB, IP_RW, 0, IPS_IDLE);
    defineProperty(&InputOptionsTP);

    IUFillText(&OnlineInputOptions[0], "CAPTURE_IP_ADDRESS", "IP Address", IPAddress.c_str());
    IUFillText(&OnlineInputOptions[1], "CAPTURE_PORT_NUMBER", "Port", port.c_str());
    IUFillText(&OnlineInputOptions[2], "CAPTURE_USERNAME", "User Name", username.c_str());
    IUFillText(&OnlineInputOptions[3], "CAPTURE_PASSWORD", "Password", password.c_str());
    IUFillTextVector(&OnlineInputOptionsP, OnlineInputOptions, 4, getDeviceName(), "ONLINE_INPUT_OPTIONS", "IP Camera",
                     CONNECTION_TAB, IP_RW, 0, IPS_IDLE);
    defineProperty(&OnlineInputOptionsP);

    OnlineProtocols = new ISwitch[3];
    IUFillSwitch(&OnlineProtocols[0], "CUSTOM", "CUSTOM", ISS_OFF);
    IUFillSwitch(&OnlineProtocols[1], "HTTP", "HTTP", ISS_ON);
    //IUFillSwitch(&OnlineProtocols[2], "RTSP", "RTSP", ISS_OFF);

    IUFillSwitchVector(&OnlineProtocolSelection, OnlineProtocols, 2, getDeviceName(), "ONLINE_PROTOCOL", "Online Protocol",
                       CONNECTION_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
    defineProperty(&OnlineProtocolSelection);

    IUFillText(&URLPathT[0], "URL_PATH", "URL", url.c_str());
    IUFillTextVector(&URLPathTP, URLPathT, NARRAY(URLPathT), getDeviceName(), "ONLINE_PATH",
                     "Online Path", CONNECTION_TAB, IP_RW, 0, IPS_IDLE);
    defineProperty(&URLPathTP);

#ifdef __linux__
    IUFillText(&V4L2SubdevPathT[0], "SUBDEV_PATH", "Sub-device", v4l2_subdev_path.c_str());
    IUFillTextVector(&V4L2SubdevPathTP, V4L2SubdevPathT, 1, getDeviceName(), "V4L2_SUBDEVICE_PATH",
                     "Sensor Control", OPTIONS_TAB, IP_RW, 0, IPS_IDLE);
    defineProperty(&V4L2SubdevPathTP);

    // V4L2_SUBDEV_EXPOSURE UI控件已移除，曝光时间由上位机通过 CCD_EXPOSURE 控制
    // 内部变量 v4l2_subdev_exposure 仍然保留，由 syncV4L2ExposureFromDuration() 自动设置
#endif

    FrameRates = new ISwitch[7];
    IUFillSwitch(&FrameRates[0], "30", "30 fps", ISS_ON);
    IUFillSwitch(&FrameRates[1], "25", "25 fps", ISS_OFF);
    IUFillSwitch(&FrameRates[2], "20", "20 fps", ISS_OFF);
    IUFillSwitch(&FrameRates[3], "15", "15 fps", ISS_OFF);
    IUFillSwitch(&FrameRates[4], "10", "10 fps", ISS_OFF);
    IUFillSwitch(&FrameRates[5], "5", "5 fps", ISS_OFF);
    IUFillSwitch(&FrameRates[6], "1", "1 fps", ISS_OFF);

    IUFillSwitchVector(&FrameRateSelection, FrameRates, 7, getDeviceName(), "CAPTURE_FRAME_RATE", "Frame Rate",
                       CONNECTION_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    PixelFormats = new ISwitch[6];
    IUFillSwitch(&PixelFormats[0], "uyvy422", "uyvy422", ISS_ON);
    IUFillSwitch(&PixelFormats[1], "yuyv422", "yuyv422", ISS_OFF);
    IUFillSwitch(&PixelFormats[2], "yuv420p", "yuv420p", ISS_OFF);
    IUFillSwitch(&PixelFormats[3], "nv12", "nv12", ISS_OFF);
    IUFillSwitch(&PixelFormats[4], "0rgb", "0rgb", ISS_OFF);
    IUFillSwitch(&PixelFormats[5], "bgr0", "bgr0", ISS_OFF);

    IUFillSwitchVector(&PixelFormatSelection, PixelFormats, 6, getDeviceName(), "INPUT_PIXEL_FORMAT", "PixelFormat",
                       CONNECTION_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);

    VideoSizes = new ISwitch[7];
    IUFillSwitch(&VideoSizes[0], "320x240", "320x240", ISS_OFF);
    IUFillSwitch(&VideoSizes[1], "640x480", "640x480", ISS_ON);
    IUFillSwitch(&VideoSizes[2], "800x600", "800x600", ISS_OFF);
    IUFillSwitch(&VideoSizes[3], "1024x768", "1024x768", ISS_OFF);
    IUFillSwitch(&VideoSizes[4], "1280x720", "1280x720", ISS_OFF);
    IUFillSwitch(&VideoSizes[5], "1280x1024", "1280x1024", ISS_OFF);
    IUFillSwitch(&VideoSizes[6], "1600x1200", "1600x1200", ISS_OFF);

    IUFillSwitchVector(&VideoSizeSelection, VideoSizes, 7, getDeviceName(), "CAPTURE_VIDEO_SIZE", "Video Size",
                       CONNECTION_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);


    IUFillNumber(&VideoAdjustmentsT[0], "BRIGHTNESS", "Brightness", "%.3f", -2.00, 2.00, 0.1, 0.00);
    IUFillNumber(&VideoAdjustmentsT[1], "CONTRAST", "Contrast", "%.3f", 0.00, 2.00, 0.1, 1.00);
    IUFillNumber(&VideoAdjustmentsT[2], "SATURATION", "Saturation", "%.3f", 0.00, 8.00, 0.1, 1.00);
    IUFillNumberVector(&VideoAdjustmentsTP, VideoAdjustmentsT, NARRAY(VideoAdjustmentsT), getDeviceName(), "VIDEO_ADJUSTMENTS",
                       "Video Adjustment Options", IMAGE_SETTINGS_TAB, IP_RW, 0, IPS_IDLE);
    defineProperty(&VideoAdjustmentsTP);

    //Setting the log level
    av_log_set_level(AV_LOG_INFO);

    // Set minimum exposure speed to 0.001 seconds
    PrimaryCCD.setMinMaxStep("CCD_EXPOSURE", "CCD_EXPOSURE_VALUE", 0.001, 3600, 1, false);

    /* Add debug controls so we may debug driver if necessary */
    addDebugControl();

    uint32_t cap = 0;
    cap |= CCD_HAS_STREAMING;
    cap |= CCD_CAN_SUBFRAME;
    cap |= CCD_CAN_ABORT;
    SetCCDCapability(cap);

    // DRIVER_INFO property (for clients that set it)
    IUFillText(&DriverInfoT[0], "DRIVER_NAME", "Name", getDefaultName());
    IUFillText(&DriverInfoT[1], "DRIVER_EXEC", "Exec", "indi_qhy_ccd");
    IUFillText(&DriverInfoT[2], "DRIVER_VERSION", "Version", "0.2");
    IUFillText(&DriverInfoT[3], "DRIVER_INTERFACE", "Interface", "2");
    IUFillTextVector(&DriverInfoTP, DriverInfoT, 4, getDeviceName(), "DRIVER_INFO", "Driver Info", OPTIONS_TAB, IP_RW, 0, IPS_IDLE);
    defineProperty(&DriverInfoTP);

    loadConfig(true, RapidStackingSelection.name);
    loadConfig(true, OutputFormatSelection.name);
    loadConfig(true, PixelSizeTP.name);
    loadConfig(true, InputOptionsTP.name);
    loadConfig(true, TimeoutOptionsTP.name);
    loadConfig(true, OnlineInputOptionsP.name);
    loadConfig(true, URLPathTP.name);
    loadConfig(true, OnlineProtocolSelection.name);
    loadConfig(true, SaveRawSP.name);
    loadConfig(true, SaveRawPathTP.name);
#ifdef __linux__
    loadConfig(true, V4L2SubdevPathTP.name);
#endif

    refreshInputDevices();
    loadConfig(true, CaptureDeviceSelection.name);
    refreshInputSources();

    loadingSettings = false;
    return true;
}

//This refreshes the input device list
bool indi_qhy_v4l2::refreshInputDevices()
{
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59, 0, 100)
    AVInputFormat * d = nullptr;
#else
    const AVInputFormat * d = nullptr;
#endif
    int i = 0;
    int numDevices = getNumOfInputDevices();
#ifdef __linux__
    CaptureDevices = new ISwitch[numDevices + 2];
#else
    CaptureDevices = new ISwitch[numDevices + 1];
#endif
    while ((d = av_input_video_device_next(d)))
    {
        if(!strcmp(d->name, videoDevice.c_str()))
            IUFillSwitch(&CaptureDevices[i], d->name, d->name, ISS_ON);
        else
            IUFillSwitch(&CaptureDevices[i], d->name, d->name, ISS_OFF);
        i++;
    }
    IUFillSwitch(&CaptureDevices[numDevices], "IP Camera", "IP Camera", ISS_OFF);
#ifdef __linux__
    IUFillSwitch(&CaptureDevices[numDevices + 1], QHY_V4L2_DIRECT_DEVICE, QHY_V4L2_DIRECT_DEVICE,
                 (videoDevice == QHY_V4L2_DIRECT_DEVICE) ? ISS_ON : ISS_OFF);
    IUFillSwitchVector(&CaptureDeviceSelection, CaptureDevices, numDevices + 2, getDeviceName(), "CAPTURE_DEVICE",
                       "Capture Devices",
                       CONNECTION_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
#else
    IUFillSwitchVector(&CaptureDeviceSelection, CaptureDevices, numDevices + 1, getDeviceName(), "CAPTURE_DEVICE",
                       "Capture Devices",
                       CONNECTION_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);
#endif
    defineProperty(&CaptureDeviceSelection);

    return true;
}

//This finds the number of devices available from FFMpeg
//I had to write it because there doesn't seem to be a method available in the FFMpeg Libraries
int indi_qhy_v4l2::getNumOfInputDevices()
{
    int i = 0;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59, 0, 100)
    AVInputFormat * d = nullptr;
#else
    const AVInputFormat * d = nullptr;
#endif
    while ((d = av_input_video_device_next(d)))
    {
        i++;
    }
    return i;
}

//This finds all the sources available from the selected device.
//FFMpeg provides a way to get the sources using the list input sources method
//But on OS X, AV Foundation is not supported in this list, so I wrote a method to get them.
//There are also other cases of devices that are not supported, for them, I just load numbers for devices.
bool indi_qhy_v4l2::refreshInputSources()
{
    if (CaptureSources)
    {
        delete[] CaptureSources;
        CaptureSources = nullptr;
        deleteProperty(CaptureSourceSelection.name);
    }

    int sourceNum = 0;
    if(videoDevice == "avfoundation")
    {
        findAVFoundationVideoSources();
        sourceNum = listOfSources.size();
        CaptureSources = new ISwitch[sourceNum];
        for(int x = 0; x < sourceNum; x++)
        {
            char num[16];
            snprintf(num, 16, "%u", x);
            if(x == 0)
                IUFillSwitch(&CaptureSources[x], num, listOfSources.at(x).c_str(), ISS_ON);
            else
                IUFillSwitch(&CaptureSources[x], num, listOfSources.at(x).c_str(), ISS_OFF);
        }
    }
    else if(videoDevice == "V4L2 Direct")
    {
        std::vector<std::string> v4l2_nodes;
        DIR *dir = opendir("/dev");
        if (dir)
        {
            struct dirent *entry = nullptr;
            while ((entry = readdir(dir)) != nullptr)
            {
                if (strncmp(entry->d_name, "video", 5) == 0)
                {
                    std::string path = std::string("/dev/") + entry->d_name;
                    v4l2_nodes.push_back(path);
                }
            }
            closedir(dir);
            std::sort(v4l2_nodes.begin(), v4l2_nodes.end());
        }
        if (v4l2_nodes.empty())
        {
            v4l2_nodes.push_back("/dev/video11");
        }
        sourceNum = v4l2_nodes.size();
        CaptureSources = new ISwitch[sourceNum];
        for (int x = 0; x < sourceNum; x++)
        {
            const char *label = v4l2_nodes[x].c_str();
            if (videoSource == v4l2_nodes[x])
                IUFillSwitch(&CaptureSources[x], label, label, ISS_ON);
            else
                IUFillSwitch(&CaptureSources[x], label, label, ISS_OFF);
        }
    }
    else if(videoDevice == "IP Camera")
    {
        //No Source Buttons for IP Camera
    }
    else
    {
        int nbdev = 0;
        struct AVDeviceInfoList *devlist = nullptr;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59, 0, 100)
        AVInputFormat *iformat = av_find_input_format(videoDevice.c_str());
#else
        const AVInputFormat *iformat = av_find_input_format(videoDevice.c_str());
#endif
        nbdev = avdevice_list_input_sources(iformat, nullptr, nullptr, &devlist);

        //For this case the source list function is not implemented, we have to just list them by number
        if ((nbdev < 0 ) || (devlist->nb_devices == 0))
        {
            avdevice_free_list_devices(&devlist);
            sourceNum = 5;
            CaptureSources = new ISwitch[sourceNum];
            IUFillSwitch(&CaptureSources[0], "0", "0", ISS_ON);
            for(int x = 1; x < sourceNum; x++)
            {
                char num[16];
                snprintf(num, 16, "%u", x);
                IUFillSwitch(&CaptureSources[x], num, num, ISS_OFF);
            }
        }
        else
        {
            //For this case, we can use the names of the autodetected devices from FFMPEG
            sourceNum = devlist->nb_devices;
            CaptureSources = new ISwitch[sourceNum];
            for(int x = 0; x < sourceNum; x++)
            {
                if(!strcmp(devlist->devices[x]->device_name, videoSource.c_str()))
                    IUFillSwitch(&CaptureSources[x], devlist->devices[x]->device_name, devlist->devices[x]->device_name, ISS_ON);
                else
                    IUFillSwitch(&CaptureSources[x], devlist->devices[x]->device_name, devlist->devices[x]->device_name, ISS_OFF);
            }
            avdevice_free_list_devices(&devlist);
        }
    }

    IUFillSwitchVector(&CaptureSourceSelection, CaptureSources, sourceNum, getDeviceName(), "CAPTURE_SOURCE", "Capture Sources",
                       CONNECTION_TAB, IP_RW, ISR_1OFMANY, 60, IPS_IDLE);


    //This controls whether the Input Options controls or the IP Camera Options controls are visible

    if (videoDevice == "IP Camera")
    {
        if(protocol != "CUSTOM")
            defineProperty(&OnlineInputOptionsP);
        defineProperty(&OnlineProtocolSelection);
        defineProperty(&URLPathTP);

        deleteProperty(CaptureSourceSelection.name);
        deleteProperty(VideoSizeSelection.name);
        deleteProperty(FrameRateSelection.name);
        deleteProperty(PixelFormatSelection.name);
        deleteProperty(InputOptionsTP.name);
    }
    else
    {
        defineProperty(&InputOptionsTP);
        defineProperty(&CaptureSourceSelection);
        defineProperty(&VideoSizeSelection);
        defineProperty(&FrameRateSelection);
        defineProperty(&PixelFormatSelection);

        if(protocol != "CUSTOM")
            deleteProperty(OnlineInputOptionsP.name);
        deleteProperty(OnlineProtocolSelection.name);
        deleteProperty(URLPathTP.name);
    }

    return true;
}


void indi_qhy_v4l2::ISGetProperties(const char *dev)
{
    loadingSettings = true;
    INDI::CCD::ISGetProperties(dev);
    // Ensure DRIVER_INFO is published for clients pushing it early
    defineProperty(&DriverInfoTP);
    loadingSettings = false;
}

/********************************************************************************************
** INDI is asking us to update the properties because there is a change in CONNECTION status
** This fucntion is called whenever the device is connected or disconnected.
*********************************************************************************************/
bool indi_qhy_v4l2::updateProperties()
{
    loadingSettings = true;

    // Call parent update properties first
    INDI::CCD::updateProperties();

    // Ensure DRIVER_INFO is always visible (for clients that set it)
    defineProperty(&DriverInfoTP);
    
#ifdef __linux__
    // 连接后发布增益和偏移量属性
    if (isConnected())
    {
        if (use_v4l2_direct)
        {
            // V4L2 模式：更新范围并发布属性
            GainT[0].min = static_cast<double>(v4l2_subdev_gain_min);
            GainT[0].max = static_cast<double>(v4l2_subdev_gain_max);
            GainT[0].value = static_cast<double>(v4l2_subdev_gain);
            
            OffsetT[0].min = static_cast<double>(v4l2_subdev_offset_min);
            OffsetT[0].max = static_cast<double>(v4l2_subdev_offset_max);
            OffsetT[0].value = static_cast<double>(v4l2_subdev_offset);
            
            defineProperty(&GainTP);
            defineProperty(&OffsetTP);
            
            DEBUGF(INDI::Logger::DBG_SESSION, "Published Gain range: %d - %d, Offset range: %d - %d",
                   v4l2_subdev_gain_min, v4l2_subdev_gain_max, 
                   v4l2_subdev_offset_min, v4l2_subdev_offset_max);
        }
        else
        {
            // FFmpeg 模式：不发布属性
            DEBUG(INDI::Logger::DBG_SESSION, "Gain/Offset controls available only in V4L2 Direct mode");
        }
    }
    else
    {
        // 断开连接时删除属性
        deleteProperty(GainTP.name);
        deleteProperty(OffsetTP.name);
    }
#endif

    loadingSettings = false;
    return true;
}

bool indi_qhy_v4l2::ISNewNumber (const char *dev, const char *name, double values[], char *names[], int n)
{
    /* ignore if not ours */
    if (dev && strcmp (getDeviceName(), dev))
        return true;

    DEBUGF(INDI::Logger::DBG_SESSION, "Setting number %s", name);

    if (!strcmp(name, VideoAdjustmentsTP.name) )
    {
        IUUpdateNumber(&VideoAdjustmentsTP, values, names, n);

        brightness = IUFindNumber( &VideoAdjustmentsTP, "BRIGHTNESS" )->value;
        contrast = IUFindNumber( &VideoAdjustmentsTP, "CONTRAST" )->value;
        saturation = IUFindNumber( &VideoAdjustmentsTP, "SATURATION" )->value;

        DEBUGF(INDI::Logger::DBG_SESSION, "New Video Adjustments: brightness: %.3f, contrast: %.3f, saturation: %.3f", brightness,
               contrast, saturation);

        IDSetNumber(&VideoAdjustmentsTP, nullptr);
        VideoAdjustmentsTP.s = IPS_OK;

        updateVideoAdjustments();
        return true;
    }

    if (!strcmp(name, PixelSizeTP.name) )
    {
        IUUpdateNumber(&PixelSizeTP, values, names, n);
        pixelSize = IUFindNumber( &PixelSizeTP, "PIXEL_SIZE_um" )->value;
        DEBUGF(INDI::Logger::DBG_SESSION, "New Pixel Size: %f", pixelSize);
        IDSetNumber(&PixelSizeTP, nullptr);
        PixelSizeTP.s = IPS_OK;
        return true;
    }

    if (!strcmp(name, TimeoutOptionsTP.name) )
    {
        IUUpdateNumber(&TimeoutOptionsTP, values, names, n);
        ffmpegTimeout = IUFindNumber( &TimeoutOptionsTP, "FFMPEG_TIMEOUT" )->value;
        bufferTimeout = IUFindNumber( &TimeoutOptionsTP, "BUFFER_TIMEOUT" )->value;
        DEBUGF(INDI::Logger::DBG_SESSION, "New Timeouts: ffmpeg: %.0f, buffer: %.0f", ffmpegTimeout, bufferTimeout);
        IDSetNumber (&TimeoutOptionsTP, nullptr);
        TimeoutOptionsTP.s = IPS_OK;
        return true;
    }

    // V4L2_SUBDEV_EXPOSURE UI控件已移除
    // 曝光时间现在完全由上位机通过 CCD_EXPOSURE 属性控制
    // syncV4L2ExposureFromDuration() 会在 StartExposure() 中自动调用

#ifdef __linux__
    // 处理增益设置
    if (!strcmp(name, GainTP.name))
    {
        IUUpdateNumber(&GainTP, values, names, n);
        if (SetCCDGain(GainT[0].value))
        {
            v4l2_subdev_gain = static_cast<int32_t>(GainT[0].value);
            GainTP.s = IPS_OK;
        }
        else
        {
            GainTP.s = IPS_ALERT;
        }
        IDSetNumber(&GainTP, nullptr);
        return true;
    }
    
    // 处理偏移量设置
    if (!strcmp(name, OffsetTP.name))
    {
        IUUpdateNumber(&OffsetTP, values, names, n);
        if (SetCCDOffset(OffsetT[0].value))
        {
            v4l2_subdev_offset = static_cast<int32_t>(OffsetT[0].value);
            OffsetTP.s = IPS_OK;
        }
        else
        {
            OffsetTP.s = IPS_ALERT;
        }
        IDSetNumber(&OffsetTP, nullptr);
        return true;
    }
#endif

    return INDI::CCD::ISNewNumber(dev, name, values, names, n);
}

bool indi_qhy_v4l2::ISNewSwitch (const char *dev, const char *name, ISState *states, char *names[], int n)
{
    /* ignore if not ours */
    if (dev && strcmp (getDeviceName(), dev))
        return true;

    IText *videoDeviceText = &InputOptionsT[0];
    IText *videoSourceText = &InputOptionsT[1];
    IText *frameRateText = &InputOptionsT[2];
    IText *pixelFormatText = &InputOptionsT[3];
    IText *videoSizeText = &InputOptionsT[4];

    ISwitchVectorProperty *svp = getSwitch(name);
    if(!svp)
        return INDI::CCD::ISNewSwitch(dev, name, states, names, n);

    if (!strcmp(svp->name, CaptureDeviceSelection.name))
    {
        IUUpdateSwitch(&CaptureDeviceSelection, states, names, n);
        ISwitch *sp = IUFindOnSwitch(&CaptureDeviceSelection);
        if (sp)
        {
            //If the videodevice is the same no need to disconnect and update sources
            if(videoDevice != sp->name)
            {
                DEBUGF(INDI::Logger::DBG_SESSION, "Setting device to: %s, Refreshing Sources", sp->name);

                videoDevice = sp->name;
                if(isConnected())
                {
                    DEBUG(INDI::Logger::DBG_SESSION, "Disconnecting now.");
                    DEBUG(INDI::Logger::DBG_SESSION, "Please select a new source to connect to and then Press Connect.");
                    if(Disconnect())
                        setConnected(false, IPS_IDLE);
                }
                IUSaveText(videoDeviceText, sp->name);
                refreshInputSources();
            }
            IDSetText(&InputOptionsTP, nullptr);
            CaptureDeviceSelection.s = IPS_OK;
            IDSetSwitch(&CaptureDeviceSelection, nullptr);
            return true;
        }
        return false;
    }
    if (!strcmp(svp->name, CaptureSourceSelection.name))
    {
        IUUpdateSwitch(&CaptureSourceSelection, states, names, n);
        ISwitch *sp = IUFindOnSwitch(&CaptureSourceSelection);
        if (sp)
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "Setting source to: %s", sp->name);
            //If they are the same, just set it, if not check if the source can be changed
            if(videoSource == sp->name || ChangeSource(videoDevice, sp->name, frameRate, inputPixelFormat, videoSize))
            {
                IUSaveText(videoSourceText, sp->name);
                IDSetText(&InputOptionsTP, nullptr);
                CaptureSourceSelection.s = IPS_OK;
                IDSetSwitch(&CaptureSourceSelection, nullptr);
                return true;
            }
        }
        return false;
    }
    if (!strcmp(svp->name, FrameRateSelection.name))
    {
        IUUpdateSwitch(&FrameRateSelection, states, names, n);
        ISwitch *sp = IUFindOnSwitch(&FrameRateSelection);
        if (sp)
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "Setting frame rate to: %u frames per second", atoi(sp->name));
            //If they are the same, just set it, if not check if the frameRate can be changed
            if(frameRate == atoi(sp->name) || ChangeSource(videoDevice, videoSource, atoi(sp->name), inputPixelFormat, videoSize))
            {
                IUSaveText(frameRateText, sp->name);
                IDSetText(&InputOptionsTP, nullptr);
                FrameRateSelection.s = IPS_OK;
                IDSetSwitch(&FrameRateSelection, nullptr);
                return true;
            }
        }
        return false;
    }
    if (!strcmp(svp->name, PixelFormatSelection.name))
    {
        IUUpdateSwitch(&PixelFormatSelection, states, names, n);
        ISwitch *sp = IUFindOnSwitch(&PixelFormatSelection);
        if (sp)
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "Setting Input Pixel Format to: %s", sp->name);
            //If they are the same, just set it, if not check if the frameRate can be changed
            if(inputPixelFormat == sp->name || ChangeSource(videoDevice, videoSource, frameRate, sp->name, videoSize))
            {
                IUSaveText(pixelFormatText, sp->name);
                IDSetText(&InputOptionsTP, nullptr);
                PixelFormatSelection.s = IPS_OK;
                IDSetSwitch(&PixelFormatSelection, nullptr);
                return true;
            }
        }
        return false;
    }
    if (!strcmp(svp->name, VideoSizeSelection.name))
    {
        IUUpdateSwitch(&VideoSizeSelection, states, names, n);
        ISwitch *sp = IUFindOnSwitch(&VideoSizeSelection);
        if (sp)
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "Setting video size to: %s", sp->name);
            //If they are the same, just set it, if not check if the video Size can be changed
            if(videoSize == sp->name || ChangeSource(videoDevice, videoSource, frameRate, inputPixelFormat, sp->name))
            {
                IUSaveText(videoSizeText, sp->name);
                IDSetText(&InputOptionsTP, nullptr);
                VideoSizeSelection.s = IPS_OK;
                IDSetSwitch(&VideoSizeSelection, nullptr);
                return true;
            }
        }
        return false;
    }

    if (!strcmp(svp->name, RapidStackingSelection.name))
    {
        IUUpdateSwitch(&RapidStackingSelection, states, names, n);
        ISwitch *sp = IUFindOnSwitch(&RapidStackingSelection);
        if (sp)
        {
            if(!strcmp(sp->name, "Integration"))
            {
                webcamStacking = true;
                averaging = false;
            }
            if(!strcmp(sp->name, "Average"))
            {
                webcamStacking = true;
                averaging = true;
            }
            if(!strcmp(sp->name, "Off"))
            {
                webcamStacking = false;
                averaging = false;
            }
            RapidStackingSelection.s = IPS_OK;
            IDSetSwitch(&RapidStackingSelection, nullptr);
            return true;
        }
        return false;
    }

    if (!strcmp(svp->name, OutputFormatSelection.name))
    {
        IUUpdateSwitch(&OutputFormatSelection, states, names, n);
        ISwitch *sp = IUFindOnSwitch(&OutputFormatSelection);
        if (sp)
        {
            outputFormat = sp->name;
            OutputFormatSelection.s = IPS_OK;
            IDSetSwitch(&OutputFormatSelection, nullptr);
            return true;
        }
        return false;
    }

    if (!strcmp(svp->name, PixelSizeSelection.name))
    {
        IUUpdateSwitch(&PixelSizeSelection, states, names, n);
        ISwitch *sp = IUFindOnSwitch(&PixelSizeSelection);
        if (sp)
        {
            pixelSize = atof(sp->name);
            PixelSizeT[0].value = pixelSize;
            PixelSizeSelection.s = IPS_OK;
            IDSetSwitch(&PixelSizeSelection, nullptr);
            IDSetNumber(&PixelSizeTP, nullptr);
            return true;
        }
        return false;
    }

    if (!strcmp(svp->name, OnlineProtocolSelection.name))
    {
        IUUpdateSwitch(&OnlineProtocolSelection, states, names, n);
        ISwitch *sp = IUFindOnSwitch(&OnlineProtocolSelection);
        if (sp)
        {
            if (!strcmp(sp->name, "CUSTOM"))
            {
                deleteProperty(OnlineInputOptionsP.name);
                protocol = "CUSTOM";
                if(customURL.length() == 0)
                {
                    OnlineProtocolSelection.s = IPS_OK;
                    IDSetSwitch(&OnlineProtocolSelection, nullptr);
                    return false;
                }
                if(ChangeOnlineSource(customURL))
                {
                    OnlineProtocolSelection.s = IPS_OK;
                    IDSetSwitch(&OnlineProtocolSelection, nullptr);
                    return true;
                }
            }
            else
            {
                defineProperty(&OnlineInputOptionsP);
                if(ChangeOnlineSource(sp->name, IPAddress, port, username, password))
                {
                    OnlineProtocolSelection.s = IPS_OK;
                    IDSetSwitch(&OnlineProtocolSelection, nullptr);
                    return true;
                }
            }
        }
        return false;
    }

    if (!strcmp(svp->name, SaveRawSP.name))
    {
        IUUpdateSwitch(&SaveRawSP, states, names, n);
        ISwitch *sp = IUFindOnSwitch(&SaveRawSP);
        if (sp)
        {
            save_raw_enable = !strcmp(sp->name, "ENABLE");
            SaveRawSP.s = IPS_OK;
            IDSetSwitch(&SaveRawSP, nullptr);   
            return true;
        }
        return false;
    }

    if (!strcmp(name, RefreshSP.name))
    {
        if(videoDevice != "IP Camera")
        {
            bool a = refreshInputDevices();
            bool b = refreshInputSources();
            RefreshSP.s = (a && b) ? IPS_OK : IPS_ALERT;
        }
        IDSetSwitch(&RefreshSP, nullptr);
        RefreshS[0].s = ISS_OFF;
        return true;
    }

    return INDI::CCD::ISNewSwitch(dev, name, states, names, n);
}

bool indi_qhy_v4l2::ISNewText (const char *dev, const char *name, char *texts[], char *names[], int n)
{
    /* ignore if not ours */
    if (dev && strcmp (getDeviceName(), dev))
        return true;

    if (!strcmp(name, InputOptionsTP.name) )
    {
        InputOptionsTP.s = IPS_OK;

        IText *videoDeviceText = IUFindText( &InputOptionsTP, names[0] );
        IText *videoSourceText = IUFindText( &InputOptionsTP, names[1] );
        IText *frameRateText = IUFindText( &InputOptionsTP, names[2] );
        IText *pixelFormatText = IUFindText( &InputOptionsTP, names[3] );
        IText *videoSizeText = IUFindText( &InputOptionsTP, names[4] );

        if (!videoDeviceText || !videoSourceText || !frameRateText || !pixelFormatText || !videoSizeText)
            return false;

        if(ChangeSource(texts[0], texts[1], atoi(texts[2]), texts[3], texts[4]))
        {
            IUSaveText(videoDeviceText, texts[0]);
            IUSaveText(videoSourceText, texts[1]);
            IUSaveText(frameRateText, texts[2]);
            IUSaveText(pixelFormatText, texts[3]);
            IUSaveText(videoSizeText, texts[4]);
            IDSetText (&InputOptionsTP, nullptr);
            return true;
        }
    }

    if (!strcmp(name, OnlineInputOptionsP.name) )
    {
        OnlineInputOptionsP.s = IPS_OK;

        IText *IPAddressText = IUFindText( &OnlineInputOptionsP, names[0] );
        IText *portText = IUFindText( &OnlineInputOptionsP, names[1] );
        IText *usernameText = IUFindText( &OnlineInputOptionsP, names[2] );
        IText *passwordText = IUFindText( &OnlineInputOptionsP, names[3] );

        if (!IPAddressText || !portText || !usernameText || !passwordText)
            return false;

        if(ChangeOnlineSource(protocol, texts[0], texts[1], texts[2], texts[3]))
        {
            IUSaveText(IPAddressText, texts[0]);
            IUSaveText(portText, texts[1]);
            IUSaveText(usernameText, texts[2]);
            IUSaveText(passwordText, texts[3]);
            IDSetText (&OnlineInputOptionsP, nullptr);
            return true;
        }
    }

    if (!strcmp(name, URLPathTP.name) )
    {
        URLPathTP.s = IPS_OK;

        IText *URLText = IUFindText( &URLPathTP, names[0] );

        customURL = texts[0];
        url = texts[0];

        if (!URLText || customURL.length() == 0)
            return false;

        if(ChangeOnlineSource(customURL))
        {
            IUSaveText(URLText, customURL.c_str());
            IDSetText (&URLPathTP, nullptr);
            IUFindSwitch(&OnlineProtocolSelection, "CUSTOM")->s = ISS_ON;
            return true;
        }
    }

    if (!strcmp(name, DriverInfoTP.name))
    {
        DriverInfoTP.s = IPS_OK;
        for (int i = 0; i < n; i++)
        {
            IText *t = IUFindText(&DriverInfoTP, names[i]);
            if (t)
                IUSaveText(t, texts[i]);
        }
        IDSetText(&DriverInfoTP, nullptr);
        return true;
    }

    if (!strcmp(name, SaveRawPathTP.name) )
    {
        SaveRawPathTP.s = IPS_OK;
        IText *pathT = IUFindText(&SaveRawPathTP, names[0]);
        if (!pathT)
            return false;
        save_raw_path = texts[0];
        IUSaveText(pathT, texts[0]);
        IDSetText(&SaveRawPathTP, nullptr);
        return true;
    }

#ifdef __linux__
    if (!strcmp(name, V4L2SubdevPathTP.name))
    {
        V4L2SubdevPathTP.s = IPS_OK;
        IText *pathT = IUFindText(&V4L2SubdevPathTP, names[0]);
        if (!pathT)
            return false;
        v4l2_subdev_path = texts[0];
        IUSaveText(pathT, texts[0]);
        closeV4L2Subdevice();
        if (openV4L2Subdevice())
        {
            updateV4L2SubdevExposureRange();
            setV4L2Exposure(v4l2_subdev_exposure);
        }
        IDSetText(&V4L2SubdevPathTP, nullptr);
        return true;
    }
#endif

    return INDI::CCD::ISNewText(dev, name, texts, names, n);
}

//This sets up a single exposure, or if rapid stacking is requested,
//It can also set up a series of exposures till the time runs out.
bool indi_qhy_v4l2::StartExposure(float duration)
{
    DEBUGF(INDI::Logger::DBG_SESSION, "StartExposure called with duration=%.3f, use_v4l2_direct=%d, v4l2_streaming=%d", 
           duration, use_v4l2_direct, v4l2_streaming);
    
    //If the QHY CCD is currently streaming, it cannot capture single exposures
    if (is_streaming || is_capturing)
    {
        DEBUG(INDI::Logger::DBG_SESSION, "Device is currently streaming.");
        return false;
    }

    //This resets the stack buffer
    if(webcamStacking)
    {
        if(stackBuffer)
            free(stackBuffer);
        stackBuffer = nullptr;
    }

    // V4L2 direct path: set output characteristics based on negotiated FOURCC
    if (use_v4l2_direct)
    {
        syncV4L2ExposureFromDuration(duration);

        uint32_t fourcc = v4l2_is_mplane ? v4l2_fmt.fmt.pix_mp.pixelformat : v4l2_fmt.fmt.pix.pixelformat;
        if (fourcc == V4L2_PIX_FMT_RGB24 || fourcc == V4L2_PIX_FMT_YUYV)
        {
            PrimaryCCD.setBPP(8);
            PrimaryCCD.setNAxis(3);
            v4l2_force_16bit = false;
        }
        else
        {
            // Y12 / Bayer12 → prefer 16-bit FITS for single exposure
            PrimaryCCD.setBPP(16);
            PrimaryCCD.setNAxis(2);
            v4l2_force_16bit = true;
        }
    }
    else
    {
        //This sets up the output format for the exposure (FFMPEG path)
    if(outputFormat == "16 bit RGB")
    {
        out_pix_fmt = AV_PIX_FMT_RGB48LE;
        PrimaryCCD.setBPP(16);
        PrimaryCCD.setNAxis(3);
    }
    else if(outputFormat == "8 bit RGB")
    {
        out_pix_fmt = AV_PIX_FMT_RGB24;
        PrimaryCCD.setBPP(8);
        PrimaryCCD.setNAxis(3);
    }
    else if(outputFormat == "16 bit Grayscale")
    {
        out_pix_fmt = AV_PIX_FMT_GRAY16LE;
        PrimaryCCD.setBPP(16);
        PrimaryCCD.setNAxis(2);
    }
    else
    {
        DEBUG(INDI::Logger::DBG_SESSION, "Invalid output format.");
        return false;
        }
    }

    //Set up the stream, if there is an error, return
    if(!setupStreaming())
    {
        DEBUG(INDI::Logger::DBG_SESSION, "Error Setting up streaming from camera");
        if (use_v4l2_direct)
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "V4L2 setup failed: fd=%d, buffers=%p, mmap_ptrs=%p, count=%u, streaming=%d",
                   v4l2_fd, v4l2_buffers, v4l2_mmap_ptrs, v4l2_buffer_count, v4l2_streaming);
        }
        return false;
    }

    // For V4L2 direct sources, start stream once at the beginning of exposure
    if (use_v4l2_direct)
    {
        // 关键步骤：确保所有缓冲在每次开启流前都已 QBUF
        requeueAllV4L2Buffers();
        DEBUGF(INDI::Logger::DBG_SESSION, "StartExposure: v4l2_streaming flag is %s, v4l2_fd=%d", 
               v4l2_streaming ? "true" : "false", v4l2_fd);
        // Start stream - this is the ONLY place where stream should be started for exposure
        if (!v4l2_streaming)
        {
            enum v4l2_buf_type type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
            DEBUGF(INDI::Logger::DBG_SESSION, "Calling VIDIOC_STREAMON with fd=%d, type=%d", v4l2_fd, type);
            int ret = ioctl(v4l2_fd, VIDIOC_STREAMON, &type);
            DEBUGF(INDI::Logger::DBG_SESSION, "VIDIOC_STREAMON returned %d, errno=%d (%s)", 
                   ret, errno, ret < 0 ? strerror(errno) : "success");
            if (ret < 0)
            {
                DEBUGF(INDI::Logger::DBG_SESSION, "Failed to start V4L2 stream in StartExposure: %s", strerror(errno));
                return false;
            }
            v4l2_streaming = true;
            DEBUG(INDI::Logger::DBG_SESSION, "V4L2 stream started for exposure");
            // Wait a bit for stream to stabilize after starting
            usleep(100000); // 100ms
        }
        else
        {
            LOG_WARN("V4L2 stream already running at StartExposure - this should not happen");
        }
    }

    //This will ensure that we get the current frame, not some old frame still in the buffer
    if(!flush_frame_buffer())
        DEBUG(INDI::Logger::DBG_SESSION, "Issue in flushing buffer");

    // For V4L2 direct sources, try to synchronously grab at least one fresh frame
    if (use_v4l2_direct)
    {
        // Stream should still be running here
        if (!v4l2_streaming)
        {
            DEBUG(INDI::Logger::DBG_ERROR, "V4L2 stream stopped unexpectedly after flush");
            return false;
        }
        
        // Try to grab an initial frame, but don't fail if it doesn't work
        // TimerHit will continue trying
        bool initialGrabSuccess = false;
        int tries = 0;
        while (tries < 5 && !initialGrabSuccess)
        {
            if (grabImage())
            {
                initialGrabSuccess = true;
                break;
            }
            usleep((useconds_t)bufferTimeout);
            tries++;
        }
        if (!initialGrabSuccess)
        {
            DEBUG(INDI::Logger::DBG_DEBUG, "Initial frame grab failed, will continue in TimerHit");
        }
        // Don't reset gotAnImageAlready here - let TimerHit handle it
    }

    /*
    int ret = avformat_flush(pFormatCtx);
    if(ret != 0 )
    {
        char errbuff[200];
        av_make_error_string(errbuff, 200, ret);
        DEBUGF(INDI::Logger::DBG_SESSION, "FFMPEG Issue in flushing buffer: %d, %s.", ret, errbuff);
    }
    */

    //This sets up the exposure time settings
    ExposureRequest = duration;
    PrimaryCCD.setExposureDuration(duration);
    gettimeofday(&ExpStart, nullptr);
    timerID = SetTimer(getCurrentPollingPeriod());
    gotAnImageAlready = false;
    InExposure = true;
    return true;
}

bool indi_qhy_v4l2::AbortExposure()
{
    DEBUG(INDI::Logger::DBG_SESSION, "AbortExposure called");
    
    // Stop V4L2 stream if it's running
    if (use_v4l2_direct && v4l2_streaming)
    {
        enum v4l2_buf_type type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type) < 0)
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "Failed to stop V4L2 stream in AbortExposure: %s", strerror(errno));
        }
        else
        {
            DEBUG(INDI::Logger::DBG_SESSION, "V4L2 stream stopped in AbortExposure");
        }
        // Always reset the flag
        v4l2_streaming = false;
    }
    
    if(stackBuffer)
        free(stackBuffer);
    InExposure = false;
    return true;
}

/**************************************************************************************
** INDI 增益控制接口
***************************************************************************************/
bool indi_qhy_v4l2::SetCCDGain(double gain)
{
#ifdef __linux__
    if (use_v4l2_direct)
    {
        int32_t gain_value = static_cast<int32_t>(gain);
        if (setV4L2Gain(gain_value))
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "CCD Gain set to %.0f", gain);
            return true;
        }
        return false;
    }
#endif
    // FFmpeg 模式不支持增益控制
    DEBUG(INDI::Logger::DBG_WARNING, "Gain control only available in V4L2 Direct mode");
    return false;
}

/**************************************************************************************
** INDI 偏移量控制接口
***************************************************************************************/
bool indi_qhy_v4l2::SetCCDOffset(double offset)
{
#ifdef __linux__
    if (use_v4l2_direct)
    {
        int32_t offset_value = static_cast<int32_t>(offset);
        if (setV4L2Offset(offset_value))
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "CCD Offset set to %.0f", offset);
            return true;
        }
        return false;
    }
#endif
    // FFmpeg 模式不支持偏移量控制
    DEBUG(INDI::Logger::DBG_WARNING, "Offset control only available in V4L2 Direct mode");
    return false;
}

//This calculates the time left in the exposure.
float indi_qhy_v4l2::CalcTimeLeft()
{
    double timesince;
    double timeleft;
    struct timeval now
    {
        0, 0
    };
    gettimeofday(&now, nullptr);

    timesince = (double)(now.tv_sec * 1000.0 + now.tv_usec / 1000) -
                (double)(ExpStart.tv_sec * 1000.0 + ExpStart.tv_usec / 1000);
    timesince = timesince / 1000;

    timeleft = ExposureRequest - timesince;
    return timeleft;
}

//This method is called repeatedly during the exposure
//If qhy_v4l2 stacking is happening, it repeatedly calls for images
//It also updates the reported exposure time left
//Finally when time is up, it copies any stack to the primary buffer
//And calls finish exposure to do any subframing necessary.

void indi_qhy_v4l2::TimerHit()
{
    float timeleft;

    if (InExposure)
    {
        if (!isConnected())
        {
            InExposure = false;
            return; //  No need to reset timer if we are not connected anymore
        }

        // For V4L2 direct, stream should already be running from StartExposure
        // If it's not, something went wrong
        if (use_v4l2_direct && !v4l2_streaming)
        {
            LOG_ERROR("V4L2 stream stopped unexpectedly in TimerHit");
            // Don't try to restart - let the exposure fail
        }

        timeleft = CalcTimeLeft();
        PrimaryCCD.setExposureLeft(timeleft);
        
        // Try to grab image if stacking is enabled or we haven't got one yet
        if(webcamStacking || !gotAnImageAlready)
        {
            if (grabImage())
            {
                // Image grabbed successfully
                DEBUG(INDI::Logger::DBG_DEBUG, "Image grabbed successfully in TimerHit");
            }
            else
            {
                // If grab failed, log but continue
                DEBUGF(INDI::Logger::DBG_DEBUG, "Failed to grab image in TimerHit (timeleft=%.3f), will retry", timeleft);
            }
        }

        // The time left in the "exposure" is less than the time it takes to make an actual exposure
        // or the time left is less than the polling period, so get it now.
        if (timeleft <= 0 || timeleft < (1.0 / frameRate) || timeleft < getCurrentPollingPeriod()/1000.0)
        {
            if(webcamStacking)
                copyFinalStackToPrimaryFrameBuffer();
            PrimaryCCD.setExposureLeft(0);
            InExposure = false;
            LOG_INFO("Download complete.");
            finishExposure();
            freeMemory();
            // Verify stream was stopped
            if (use_v4l2_direct)
            {
                DEBUGF(INDI::Logger::DBG_SESSION, "After finishExposure: v4l2_streaming=%d", v4l2_streaming);
                if (v4l2_streaming)
                {
                    LOG_ERROR("WARNING: v4l2_streaming still true after finishExposure, forcing reset");
                    v4l2_streaming = false;
                }
            }
            // Stream is stopped in finishExposure() after exposure completes
            // It will be restarted when the next exposure begins
            return;
        }
    }

    SetTimer(getCurrentPollingPeriod());
}

// Downloads the image from the QHY CCD.
//If the image is an RGB, it converts it to Fits RGB
//If rapid stacking is happening, it adds the image to the stack.

bool indi_qhy_v4l2::grabImage()
{
    if(getStreamFrame())
    {
        if (use_v4l2_direct)
        {
            if (!buffer)
            {
                DEBUG(INDI::Logger::DBG_WARNING, "Buffer not allocated in grabImage");
                return false;
            }
            if(PrimaryCCD.getNAxis() == 3)
                convertINDI_RGBtoFITS_RGB(buffer, PrimaryCCD.getFrameBuffer());
            else
                memcpy(PrimaryCCD.getFrameBuffer(), buffer, numBytes);
        }
        else
    {
        if(PrimaryCCD.getNAxis() == 3)
            convertINDI_RGBtoFITS_RGB(pFrameOUT->data[0], PrimaryCCD.getFrameBuffer());
        else
            memcpy(PrimaryCCD.getFrameBuffer(), pFrameOUT->data[0], numBytes);
        }
        if(webcamStacking)
            addToStack();
        gotAnImageAlready = true;
        return true;
    }
    else
    {
        // Don't free memory on failure, just return false
        // Memory will be freed when exposure completes or aborts
        return false;
    }
}

//This adds each image to the running stack
bool indi_qhy_v4l2::addToStack()
{
    if(!stackBuffer)
    {
        stackBuffer = (float *)malloc(numBytes * sizeof(float_t));
        numberOfFramesInStack = 0;
    }

    int w = PrimaryCCD.getXRes()  * ((PrimaryCCD.getNAxis() == 3) ? 3 : 1);
    int h = PrimaryCCD.getYRes();

    for (int i = 0; i < w * h; i++)
    {
        int x = i % w;
        int y = i / w;
        if(x >= 0 && y >= 0 && x < w && y < h)
        {
            if(numberOfFramesInStack == 0)
                stackBuffer[i] = getImageDataFloatValue(x, y);
            else
                stackBuffer[i] += getImageDataFloatValue(x, y);
        }
    }
    numberOfFramesInStack++;
    return true;
}

//This gets the pixel value at an x, y position in the image
float indi_qhy_v4l2::getImageDataFloatValue(int x, int y)
{
    int w = PrimaryCCD.getXRes()  * ((PrimaryCCD.getNAxis() == 3) ? 3 : 1);
    uint8_t *primaryBuffer = PrimaryCCD.getFrameBuffer();
    if(PrimaryCCD.getBPP() == 8)
        return (float) primaryBuffer[y * w + x];
    else if(PrimaryCCD.getBPP() == 16)
    {
        uint16_t *newBuffer = reinterpret_cast<uint16_t *>(primaryBuffer);
        return (float) newBuffer[y * w + x];
    }
    else
        return 0;
}
//This sets the pixel value at an x, y position in the image
void indi_qhy_v4l2::setImageDataValueFromFloat(int x, int y, float value, bool roundAnswer)
{
    uint8_t *primaryBuffer = PrimaryCCD.getFrameBuffer();
    int w = PrimaryCCD.getXRes() * ((PrimaryCCD.getNAxis() == 3) ? 3 : 1);
    if(!stackBuffer)
        return;
    if(PrimaryCCD.getBPP() == 8)
    {
        int max = std::numeric_limits<uint8_t>::max();
        if (value > max)
            value = max;
        if(roundAnswer)
            primaryBuffer[y * w + x] = round(value);
        else
            primaryBuffer[y * w + x] = value;
    }
    else if(PrimaryCCD.getBPP() == 16)
    {
        int max = std::numeric_limits<uint16_t>::max();
        if (value > max)
            value = max;
        uint16_t *newBuffer = reinterpret_cast<uint16_t *>(primaryBuffer);
        if(roundAnswer)
            newBuffer[y * w + x] = round(value);
        else
            newBuffer[y * w + x] = value;
    }
}

//This will take the final image stack and copy it back to the primary buffer for final download.
void indi_qhy_v4l2::copyFinalStackToPrimaryFrameBuffer()
{
    int w = PrimaryCCD.getXRes()  * ((PrimaryCCD.getNAxis() == 3) ? 3 : 1);
    int h = PrimaryCCD.getYRes();
    for (int i = 0; i < w * h; i++)
    {
        int x = i % w;
        int y = i / w;
        if(x >= 0 && y >= 0 && x < w && y < h)
        {
            if(averaging)
                setImageDataValueFromFloat(x, y, round(stackBuffer[i] / numberOfFramesInStack), true);
            else //Integrating
                setImageDataValueFromFloat(x, y, round(stackBuffer[i]), true);
        }
    }

    LOGF_INFO("Final Image is a stack of %u exposures.", numberOfFramesInStack);
}

//This will crop the image to a subframe if desired.
//Then it will send the final image.
void indi_qhy_v4l2::finishExposure()
{
    // Stop V4L2 streaming FIRST, before any image processing
    // This ensures proper timing for the next exposure
    if (use_v4l2_direct && v4l2_streaming)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "finishExposure: stopping stream, v4l2_fd=%d", v4l2_fd);
        enum v4l2_buf_type type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        int ret = ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type);
        DEBUGF(INDI::Logger::DBG_SESSION, "VIDIOC_STREAMOFF returned %d, errno=%d (%s)", 
               ret, errno, ret < 0 ? strerror(errno) : "success");
        if (ret < 0)
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "Failed to stop V4L2 stream: %s", strerror(errno));
        }
        else
        {
            DEBUG(INDI::Logger::DBG_SESSION, "V4L2 stream stopped after exposure");
        }
        v4l2_streaming = false;
        
        // CRITICAL: Delay BEFORE any other operations to ensure driver cleanup
        // Rockchip CIF/MIPI-CSI2 drivers need substantial time to complete cleanup
        // Testing shows 50ms is insufficient, causing next STREAMON to succeed but not start stream
        usleep(200000); // 200ms
        DEBUG(INDI::Logger::DBG_SESSION, "Waited 200ms for driver cleanup");
    }
    
    // Now proceed with image processing and sending
    uint8_t *memptr = PrimaryCCD.getFrameBuffer();
    int w = PrimaryCCD.getXRes();
    int h = PrimaryCCD.getYRes();
    int bpp = PrimaryCCD.getBPP();
    int naxis = PrimaryCCD.getNAxis();
    uint16_t subW = PrimaryCCD.getSubW();
    uint16_t subH = PrimaryCCD.getSubH();

    if ( (subW > 0 && subH > 0) && ((subW < w && subH <= h) || (subH < h && subW <= w)))
    {
        int subX   = PrimaryCCD.getSubX();
        int subY = PrimaryCCD.getSubY();

        int subFrameSize     = subW * subH * bpp / 8 * ((naxis == 3) ? 3 : 1);
        int oneFrameSize     = subW * subH * bpp / 8;

        int lineW  = subW * bpp / 8;

        LOGF_DEBUG("Subframing... subFrameSize: %d - oneFrameSize: %d - subX: %d - subY: %d - subW: %d - subH: %d",
                   subFrameSize, oneFrameSize,
                   subX, subY, subW, subH);

        if (naxis == 2)
        {
            // JM 2020-08-29: Using memmove since regions are overlaping
            // as proposed by Camiel Severijns on INDI forums.
            for (int i = subY; i < subY + subH; i++)
                memmove(memptr + (i - subY) * lineW, memptr + (i * w + subX) * bpp / 8, lineW);
        }
        else
        {
            uint8_t * subR = memptr;
            uint8_t * subG = memptr + oneFrameSize;
            uint8_t * subB = memptr + oneFrameSize * 2;

            uint8_t *startR = memptr;
            uint8_t *startG = memptr + (w * h * bpp / 8);
            uint8_t *startB = memptr + (w * h * bpp / 8 * 2);

            for (int i = subY; i < subY + subH; i++)
            {
                memcpy(subR + (i - subY) * lineW, startR + (i * w + subX) * bpp / 8, lineW);
                memcpy(subG + (i - subY) * lineW, startG + (i * w + subX) * bpp / 8, lineW);
                memcpy(subB + (i - subY) * lineW, startB + (i * w + subX) * bpp / 8, lineW);
            }
        }

        PrimaryCCD.setFrameBuffer(memptr);
        PrimaryCCD.setFrameBufferSize(subFrameSize, false);
        PrimaryCCD.setResolution(w, h);
        PrimaryCCD.setFrame(subX, subY, subW, subH);
        PrimaryCCD.setNAxis(naxis);
        PrimaryCCD.setBPP(bpp);

        // Optional raw save
        if (save_raw_enable)
        {
            FILE *fp = fopen(save_raw_path.c_str(), "wb");
            if (fp)
            {
                fwrite(memptr, 1, subFrameSize, fp);
                fclose(fp);
            }
        }

        PrimaryCCD.setImageExtension("fits");
        ExposureComplete(&PrimaryCCD);

        // Restore old pointer and release memory
        PrimaryCCD.setFrameBuffer(memptr);
        PrimaryCCD.setFrameBufferSize(numBytes, false);
    }
    else
    {
        // Optional raw save (full frame)
        if (save_raw_enable)
        {
            FILE *fp = fopen(save_raw_path.c_str(), "wb");
            if (fp)
            {
                fwrite(memptr, 1, numBytes, fp);
                fclose(fp);
            }
        }
        PrimaryCCD.setImageExtension("fits");
        ExposureComplete(&PrimaryCCD);
    }
    // Reset 16-bit request for next operations
    v4l2_force_16bit = false;
}

bool indi_qhy_v4l2::UpdateCCDFrame(int x, int y, int w, int h)
{
    PrimaryCCD.setFrame(x, y, w, h);
    
    // If using V4L2 direct and crop is supported, set hardware crop
    if (use_v4l2_direct && v4l2_can_crop)
    {
        // Stop streaming temporarily to change crop
        bool was_streaming = v4l2_streaming;
        if (was_streaming)
        {
            enum v4l2_buf_type type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type);
            v4l2_streaming = false;
        }
        
        // Set crop rectangle
        if (setV4L2Crop(x, y, w, h))
        {
            // Update actual frame size from crop
            struct v4l2_rect crop_rect = getV4L2Crop();
            PrimaryCCD.setFrame(crop_rect.left, crop_rect.top, crop_rect.width, crop_rect.height);
        }
        
        // Restart streaming if it was active
        if (was_streaming)
        {
            enum v4l2_buf_type type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (ioctl(v4l2_fd, VIDIOC_STREAMON, &type) == 0)
                v4l2_streaming = true;
        }
    }
    
    return true;
}

void indi_qhy_v4l2::debugTriggered(bool enabled)
{
    if(enabled)
    {
        av_log_set_level(AV_LOG_DEBUG);
        DEBUG(INDI::Logger::DBG_SESSION, "Setting FFMPEG Logging to Verbose\n");
    }
    else
    {
        av_log_set_level(AV_LOG_INFO);
        DEBUG(INDI::Logger::DBG_SESSION, "Setting FFMPEG Logging to Info\n");
    }
}

//These next several methods handle streaming starting and stopping.

void indi_qhy_v4l2::start_capturing()
{
    if (is_capturing) return;
    is_capturing = true;
    capture_thread = std::thread(RunCaptureThread, this);
}

void indi_qhy_v4l2::stop_capturing()
{
    if (!is_capturing) return;
    is_capturing = false;
    if (std::this_thread::get_id() != capture_thread.get_id())
        capture_thread.join();
}

bool indi_qhy_v4l2::StartStreaming()
{
    if (is_streaming) return true;
    if (!is_capturing) start_capturing();
    is_streaming = true;
    return true;
}

bool indi_qhy_v4l2::StopStreaming()
{
    if (!is_streaming) return true;
    stop_capturing();
    is_streaming = false;
    return true;
}

void indi_qhy_v4l2::RunCaptureThread(indi_qhy_v4l2 *qhy_v4l2)
{
    qhy_v4l2->run_capture();
}

//This is the loop that runs during streaming
//Note that it ONLY supports RGB24 aka INDI_RGB format.
void indi_qhy_v4l2::run_capture()
{
    // Direct V4L2 streaming path
    if (use_v4l2_direct)
    {
        if(!setupStreaming())
            return;
        // Decide pixel format based on device FOURCC
        uint32_t fourcc = v4l2_is_mplane ? v4l2_fmt.fmt.pix_mp.pixelformat : v4l2_fmt.fmt.pix.pixelformat;
        if (fourcc == V4L2_PIX_FMT_RGB24)
        {
            PrimaryCCD.setBPP(8);
            PrimaryCCD.setNAxis(3);
            Streamer->setPixelFormat(INDI_RGB);
        }
        else
        {
            PrimaryCCD.setBPP(8);
            PrimaryCCD.setNAxis(2);
            Streamer->setPixelFormat(INDI_MONO);
        }

        int w = v4l2_is_mplane ? (int)v4l2_fmt.fmt.pix_mp.width : (int)v4l2_fmt.fmt.pix.width;
        int h = v4l2_is_mplane ? (int)v4l2_fmt.fmt.pix_mp.height : (int)v4l2_fmt.fmt.pix.height;
        Streamer->setSize(w, h);
        PrimaryCCD.setFrame(0, 0, w, h);

        if(!flush_frame_buffer())
            DEBUG(INDI::Logger::DBG_SESSION, "V4L2 Issue in flushing buffer");

        while (is_capturing && is_streaming)
        {
            if(getStreamFrame())
                Streamer->newFrame(buffer, numBytes);
            else
            {
                is_capturing = false;
                is_streaming = false;
            }
        }

        freeMemory();
        DEBUG(INDI::Logger::DBG_SESSION, "Capture thread releasing device (V4L2).");
        return;
    }

    //This sets up the output format for the exposures
    if(outputFormat == "16 bit RGB")
    {
        LOG_INFO("Note, RGB 16 bit not supported in video stream using 8 Bit RGB instead.");
        out_pix_fmt = AV_PIX_FMT_RGB24;
        PrimaryCCD.setBPP(8);
        PrimaryCCD.setNAxis(3);
        Streamer->setPixelFormat(INDI_RGB);
    }
    else if(outputFormat == "8 bit RGB")
    {
        out_pix_fmt = AV_PIX_FMT_RGB24;
        PrimaryCCD.setBPP(8);
        PrimaryCCD.setNAxis(3);
        Streamer->setPixelFormat(INDI_RGB);
    }
    else if(outputFormat == "16 bit Grayscale")
    {
        LOG_INFO("Note, 16 bit Grayscale not supported in video stream using 8 Bit Grayscale instead.");
        out_pix_fmt = AV_PIX_FMT_GRAY8;
        PrimaryCCD.setBPP(8);
        PrimaryCCD.setNAxis(2);
        Streamer->setPixelFormat(INDI_MONO);
    }
    else
        return;

    if(!setupStreaming())
        return;

    int w = pCodecCtx->width;
    int h = pCodecCtx->height;
    Streamer->setSize(w, h);
    PrimaryCCD.setFrame(0, 0, w, h);

    //This will clear the frame button before streaming is started so that the frames are all current.
    if(!flush_frame_buffer())
        DEBUG(INDI::Logger::DBG_SESSION, "FFMPEG Issue in flushing buffer");

    /*
    int ret = avformat_flush(pFormatCtx);
    if(ret != 0 )
    {
        char errbuff[200];
        av_make_error_string(errbuff, 200, ret);
        DEBUGF(INDI::Logger::DBG_SESSION, "FFMPEG Issue in flushing buffer: %d, %s.", ret, errbuff);
    }
    */

    while (is_capturing && is_streaming)
    {

        if(getStreamFrame())
            Streamer->newFrame(pFrameOUT->data[0], numBytes);
        else
        {
            is_capturing = false;
            is_streaming = false;
        }
    }

    freeMemory();

    DEBUG(INDI::Logger::DBG_SESSION, "Capture thread releasing device.");
}

//This converts an image from INDI_RGB to FITS_RGB so the FITSViewer can read it.
bool indi_qhy_v4l2::convertINDI_RGBtoFITS_RGB(uint8_t *originalImage, uint8_t *convertedImage)
{
    if(PrimaryCCD.getBPP() == 8)
    {
        int size =  numBytes / 3;
        uint8_t *r, *g, *b;
        r = (convertedImage);
        g = (convertedImage + size);
        b = (convertedImage + size * 2);
        for(int i = 0; i < numBytes; i += 3)
        {
            *r++ = *originalImage++;
            *g++ = *originalImage++;
            *b++ = *originalImage++;
        }
    }
    else if(PrimaryCCD.getBPP() == 16)
    {
        uint16_t *bigOriginalImage = reinterpret_cast<uint16_t *>(originalImage);
        uint16_t *bigConvertedImage = reinterpret_cast<uint16_t *>(convertedImage);
        int size =  numBytes / 2 / 3;
        uint16_t *r, *g, *b;
        r = (bigConvertedImage);
        g = (bigConvertedImage + size);
        b = (bigConvertedImage + size * 2);
        for(int i = 0; i < numBytes / 2; i += 3)
        {
            *r++ = *bigOriginalImage++;
            *g++ = *bigOriginalImage++;
            *b++ = *bigOriginalImage++;
        }
    }
    return true;
}

//This sets up the QHY CCD to get images
//It is used for both the streaming and exposing algorithms
bool indi_qhy_v4l2::setupStreaming()
{
    if (use_v4l2_direct)
    {
        return setupV4L2Streaming();
    }
    // Determine required buffer size and allocate buffer for pframeRGB
    numBytes = av_image_get_buffer_size(out_pix_fmt, pCodecCtx->width, pCodecCtx->height, 1);

    // Allocate video frame
    pFrame = av_frame_alloc();
    if(pFrame == nullptr)
        return false;
    // Allocate an AVFrame structure
    pFrameOUT = av_frame_alloc();
    if(pFrameOUT == nullptr)
        return false;

    // Assign appropriate parts of buffer to image planes in pFrameRGB
    buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    if(buffer == nullptr)
        return false;

    av_image_fill_arrays (pFrameOUT->data, pFrameOUT->linesize, buffer, out_pix_fmt,
                          pCodecCtx->width, pCodecCtx->height, 1);

    // initialize SWS context for software scaling
    sws_ctx = sws_getContext( pCodecCtx->width, pCodecCtx->height,
                              pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height,
                              out_pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr
                            );
    if(sws_ctx == nullptr)
        return false;

    updateVideoAdjustments();

    PrimaryCCD.setFrameBufferSize(numBytes);
    PrimaryCCD.setResolution(pCodecCtx->width, pCodecCtx->height);

    return true;
}

void indi_qhy_v4l2::updateVideoAdjustments()
{
    if(sws_ctx == nullptr)
        return;

    int src_range = 1, dst_range = 1; //These are just flags 1 for Jpeg and 2 for Mpeg
    const int* coefs = sws_getCoefficients(SWS_CS_DEFAULT);
    //Note these last 3 values are reported in 16.16 fixed point format
    sws_setColorspaceDetails(sws_ctx, coefs, src_range, coefs, dst_range,
                             (int)(brightness * 65536), (int)(contrast * 65536), (int)(saturation * 65536));
}

//This gets one image from the camera.
//It is used for both the streaming and exposing algorithms
bool indi_qhy_v4l2::getStreamFrame()
{
    if (use_v4l2_direct)
        return getStreamFrameV4L2();
    AVPacket packet;
    //If at first you don't succees to get a frame, try again.
    int ret = -1;
    while(ret < 0)
    {
        int tries = 0;
        while(tries < 10) //Try a maximum of 10 times before trying to reconnect the source
        {
            ret = av_read_frame(pFormatCtx, &packet);
            if(ret == 0)
                break;
            else
            {
                if(ret != -35) // Don't display "Resource Temporarily Unavailable"
                {
                    char errbuff[200];
                    av_make_error_string(errbuff, 200, ret);
                    DEBUGF(INDI::Logger::DBG_SESSION, "FFMPEG Error: %d, %s.", ret, errbuff);
                }
                tries++;
                usleep(bufferTimeout); //give it a moment, if it is unavailable
            }
        }
        if(ret < 0) // If it still is not working after 10 tries, we should try reconnecting the source.
        {
            if(reconnectSource())
            {
                DEBUG(INDI::Logger::DBG_SESSION, "Device successfully reconnected.");
                freeMemory();
                //Try to set up streaming again, if there is an error, return
                if(!setupStreaming())
                {
                    DEBUG(INDI::Logger::DBG_SESSION, "Error on Stream Setup.");
                    return false;
                }
            }
            else
            {
                DEBUG(INDI::Logger::DBG_SESSION, "Device did not reconnect after 10 tries.");
                av_packet_unref(&packet);
                return false;
            }
        }
    }
    if(packet.stream_index == videoStream)
    {
        int ret;
        ret = avcodec_send_packet(pCodecCtx, &packet);
        char errbuff[200];
        av_make_error_string(errbuff, 200, ret);
        if (ret < 0)
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "Error sending a packet for decoding:%s", errbuff);
            av_packet_unref(&packet);
            return false;
        }
        while (ret >= 0)
        {
            ret = avcodec_receive_frame(pCodecCtx, pFrame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                continue;
            else if (ret < 0)
            {
                DEBUG(INDI::Logger::DBG_SESSION, "Error during decoding");
                av_packet_unref(&packet);
                return false;
            }
            // We have a frame at that point
            // Convert the image from its native format to our output format
            sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,
                      pFrame->linesize, 0, pCodecCtx->height,
                      pFrameOUT->data, pFrameOUT->linesize);
            av_packet_unref(&packet);
            return true;
        }

    }
    return false;
}

//This will clear out the frame buffer of any unread frames.
//That way we are sure to get the latest frames when exposing
 bool indi_qhy_v4l2::flush_frame_buffer()
 {
     if (use_v4l2_direct)
         return flush_frame_bufferV4L2();
     int packetReceiveTime = -1;
     int num = 0;
     while(packetReceiveTime < bufferTimeout)
     {
         num++;
         struct timeval then;
         gettimeofday(&then, nullptr);
         AVPacket packet;
         int ret = av_read_frame(pFormatCtx, &packet);
         if(ret != 0) // Return value less than 0 means error
         {
             if(ret == -35) //Leave the loop since the device is not available to give frames.
                 break;
             char errbuff[200];
             av_make_error_string(errbuff, 200, ret);
             DEBUGF(INDI::Logger::DBG_SESSION, "FFMPEG Error while clearing buffer: %s.", errbuff);
         }
         struct timeval now;
         gettimeofday(&now, nullptr);
         packetReceiveTime = now.tv_usec - then.tv_usec;
         av_packet_unref(&packet);
     }
     DEBUGF(INDI::Logger::DBG_SESSION, "Buffer Cleared of %u stale frames.", num);
     return true;  //Buffer Cleared
 }

//This frees up the resources used for streaming/exposing
void indi_qhy_v4l2::freeMemory()
{
    if (use_v4l2_direct)
    {
        // For V4L2 direct mode, only free the temporary buffer, not the mmap buffers
        // The mmap buffers must remain mapped as long as the device is connected
        // They will be freed in DisconnectV4L2()
        if (buffer)
        {
            free(buffer);
            buffer = nullptr;
        }
        return;
    }
    // Free the sws_context
    if(sws_ctx)
        sws_freeContext(sws_ctx);
    sws_ctx = nullptr;

    // Free the Buffer
    if(buffer)
        av_free(buffer);
    buffer = nullptr;

    // Free the RGB image
    if(pFrameOUT)
        av_free(pFrameOUT);
    pFrameOUT = nullptr;

    // Free the input frame
    if(pFrame)
        av_free(pFrame);
    pFrame = nullptr;

}

#ifdef __linux__
static inline void yuyv_to_rgb24_line(const uint8_t *src, uint8_t *dst, int width)
{
    for (int x = 0; x < width; x += 2)
    {
        int y0 = src[0];
        int u  = src[1] - 128;
        int y1 = src[2];
        int v  = src[3] - 128;
        src += 4;

        auto clamp = [](int c) { return (uint8_t)(c < 0 ? 0 : (c > 255 ? 255 : c)); };
        int r_add = (int)(1.402 * v);
        int g_add = (int)(-0.344136 * u - 0.714136 * v);
        int b_add = (int)(1.772 * u);

        // pixel 0
        int c0 = y0;
        *dst++ = clamp(c0 + r_add);
        *dst++ = clamp(c0 + g_add);
        *dst++ = clamp(c0 + b_add);
        // pixel 1
        int c1 = y1;
        *dst++ = clamp(c1 + r_add);
        *dst++ = clamp(c1 + g_add);
        *dst++ = clamp(c1 + b_add);
    }
}

bool indi_qhy_v4l2::ConnectToSourceV4L2(std::string source)
{
    use_v4l2_direct = false;

    v4l2_fd = open(source.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (v4l2_fd < 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "Failed to open %s: %s", source.c_str(), strerror(errno));
        return false;
    }

    // Disable Rockchip CIF compact mode to prevent image stitching issues
    // This must be done before setting up the device format
    // Try common Rockchip CIF device paths
    const char* compact_test_paths[] = {
        "/sys/devices/platform/rkcif-mipi-lvds1/compact_test",
        "/sys/devices/platform/rkcif-mipi-lvds/compact_test",
        "/sys/devices/platform/rkcif/compact_test",
        nullptr
    };
    
    for (int i = 0; compact_test_paths[i] != nullptr; i++)
    {
        FILE *fp = fopen(compact_test_paths[i], "w");
        if (fp)
        {
            // Write "0 0 0 0" to disable compact mode
            if (fprintf(fp, "0 0 0 0") >= 0)
            {
                fclose(fp);
                DEBUGF(INDI::Logger::DBG_SESSION, "Disabled Rockchip CIF compact mode via %s", compact_test_paths[i]);
                break; // Successfully disabled, no need to try other paths
            }
            fclose(fp);
        }
    }
    // Not a critical error if we can't find the file - device might not be Rockchip

    struct v4l2_capability cap = {};
    if (ioctl(v4l2_fd, VIDIOC_QUERYCAP, &cap) < 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "VIDIOC_QUERYCAP failed: %s", strerror(errno));
        close(v4l2_fd);
        v4l2_fd = -1;
        return false;
    }

    unsigned int dev_caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    v4l2_is_mplane = (dev_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE);
    if (!((dev_caps & V4L2_CAP_VIDEO_CAPTURE) || (dev_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE)))
    {
        DEBUG(INDI::Logger::DBG_SESSION, "Device is not a video capture device.");
        close(v4l2_fd);
        v4l2_fd = -1;
        return false;
    }
    if (!(dev_caps & V4L2_CAP_STREAMING))
    {
        DEBUG(INDI::Logger::DBG_SESSION, "Device does not support streaming I/O.");
        close(v4l2_fd);
        v4l2_fd = -1;
        return false;
    }

    // Read current format
    memset(&v4l2_fmt, 0, sizeof(v4l2_fmt));
    v4l2_fmt.type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(v4l2_fd, VIDIOC_G_FMT, &v4l2_fmt) < 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "VIDIOC_G_FMT failed: %s", strerror(errno));
        close(v4l2_fd);
        v4l2_fd = -1;
        return false;
    }

    // If user requested a size, try to set width/height only, keep current pixel format
    unsigned int reqw = 0, reqh = 0;
    if (sscanf(videoSize.c_str(), "%ux%u", &reqw, &reqh) == 2 && reqw > 0 && reqh > 0)
    {
        if (!v4l2_is_mplane)
        {
            v4l2_fmt.fmt.pix.width = reqw;
            v4l2_fmt.fmt.pix.height = reqh;
        }
        else
        {
            v4l2_fmt.fmt.pix_mp.width = reqw;
            v4l2_fmt.fmt.pix_mp.height = reqh;
            // Let driver compute these
            v4l2_fmt.fmt.pix_mp.plane_fmt[0].bytesperline = 0;
            v4l2_fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 0;
        }
        ioctl(v4l2_fd, VIDIOC_S_FMT, &v4l2_fmt); // best effort; ignore failure, we continue with current
        // Re-read actual negotiated format
        memset(&v4l2_fmt, 0, sizeof(v4l2_fmt));
        v4l2_fmt.type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(v4l2_fd, VIDIOC_G_FMT, &v4l2_fmt);
    }

    struct v4l2_requestbuffers req = {};
    req.count = 4;
    req.type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(v4l2_fd, VIDIOC_REQBUFS, &req) < 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "VIDIOC_REQBUFS failed: %s", strerror(errno));
        close(v4l2_fd);
        v4l2_fd = -1;
        return false;
    }

    v4l2_buffer_count = req.count;
    v4l2_buffers = (struct v4l2_buffer*)calloc(v4l2_buffer_count, sizeof(struct v4l2_buffer));
    if (!v4l2_buffers)
    {
        close(v4l2_fd);
        v4l2_fd = -1;
        return false;
    }

    for (unsigned int i = 0; i < v4l2_buffer_count; i++)
    {
        struct v4l2_buffer buf = {};
        buf.type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        struct v4l2_plane planes[1];
        memset(planes, 0, sizeof(planes));
        if (v4l2_is_mplane)
        {
            buf.length = 1;
            buf.m.planes = planes;
        }
        if (ioctl(v4l2_fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "VIDIOC_QUERYBUF failed: %s", strerror(errno));
            freeV4L2Memory();
            close(v4l2_fd);
            v4l2_fd = -1;
            return false;
        }

        size_t length = v4l2_is_mplane ? planes[0].length : buf.length;
        unsigned long offset = v4l2_is_mplane ? planes[0].m.mem_offset : buf.m.offset;
        void *start = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd, offset);
        if (start == MAP_FAILED)
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "mmap failed: %s", strerror(errno));
            freeV4L2Memory();
            close(v4l2_fd);
            v4l2_fd = -1;
            return false;
        }
        v4l2_buffers[i] = buf;
        if (v4l2_mmap_ptrs == nullptr) { v4l2_mmap_ptrs = (void**)calloc(req.count, sizeof(void*)); }
        if (v4l2_mmap_lens == nullptr) { v4l2_mmap_lens = (size_t*)calloc(req.count, sizeof(size_t)); }
        v4l2_mmap_ptrs[i] = start;
        v4l2_mmap_lens[i] = length;

        if (v4l2_is_mplane)
        {
            planes[0].bytesused = planes[0].length;
            buf.m.planes = planes;
            buf.length = 1;
        }
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0)
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "VIDIOC_QBUF failed: %s", strerror(errno));
            freeV4L2Memory();
            close(v4l2_fd);
            v4l2_fd = -1;
            return false;
        }
    }

    // Don't start streaming on connect - stream will be started when exposure begins
    v4l2_streaming = false;
    use_v4l2_direct = true;
    videoSource = source;
    
    // Check crop capabilities
    v4l2_cropcap.type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_can_crop = (ioctl(v4l2_fd, VIDIOC_CROPCAP, &v4l2_cropcap) == 0);
    if (v4l2_can_crop)
    {
        v4l2_crop.type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2_crop.c = v4l2_cropcap.defrect;
        ioctl(v4l2_fd, VIDIOC_S_CROP, &v4l2_crop);
        DEBUGF(INDI::Logger::DBG_SESSION, "V4L2 Crop capabilities: bounds=(%d,%d,%d,%d) defrect=(%d,%d,%d,%d)",
               v4l2_cropcap.bounds.left, v4l2_cropcap.bounds.top, 
               v4l2_cropcap.bounds.width, v4l2_cropcap.bounds.height,
               v4l2_cropcap.defrect.left, v4l2_cropcap.defrect.top,
               v4l2_cropcap.defrect.width, v4l2_cropcap.defrect.height);
    }
    
    // Enumerate device capabilities (for debugging and information)
    enumerateV4L2Formats();
    enumerateV4L2Sizes();
    enumerateV4L2FrameRates();
    
#ifdef __linux__
    if (openV4L2Subdevice())
    {
        updateV4L2SubdevExposureRange();
        setV4L2Exposure(v4l2_subdev_exposure);
        
        // 初始化增益和偏移量
        updateV4L2GainRange();
        setV4L2Gain(v4l2_subdev_gain);
        setV4L2Offset(v4l2_subdev_offset);
    }
#endif

    return true;
}

bool indi_qhy_v4l2::DisconnectV4L2()
{
    if (v4l2_fd >= 0)
    {
        if (v4l2_streaming)
        {
    enum v4l2_buf_type type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type);
            v4l2_streaming = false;
        }
        freeV4L2Memory();
        close(v4l2_fd);
        v4l2_fd = -1;
    }
    closeV4L2Subdevice();
    use_v4l2_direct = false;
    //目前实现参数在断开重连时保留，如果需要重置参数，可以取消注释以下代码，同时也可以修改默认参数
    // v4l2_subdev_exposure = 1000.0;
    // v4l2_subdev_gain = 64;
    // v4l2_subdev_offset = 0;
    return true;
}

bool indi_qhy_v4l2::setupV4L2Streaming()
{
    if (v4l2_fd < 0)
    {
        DEBUG(INDI::Logger::DBG_SESSION, "V4L2 file descriptor invalid in setupV4L2Streaming");
        return false;
    }
    
    // Ensure mmap buffers are still valid (they should be, but check anyway)
    if (!v4l2_buffers || !v4l2_mmap_ptrs || v4l2_buffer_count == 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "V4L2 buffers not initialized: buffers=%p, mmap_ptrs=%p, count=%u", 
               v4l2_buffers, v4l2_mmap_ptrs, v4l2_buffer_count);
        return false;
    }
    
    int w = v4l2_is_mplane ? (int)v4l2_fmt.fmt.pix_mp.width : (int)v4l2_fmt.fmt.pix.width;
    int h = v4l2_is_mplane ? (int)v4l2_fmt.fmt.pix_mp.height : (int)v4l2_fmt.fmt.pix.height;
    uint32_t fourcc = v4l2_is_mplane ? v4l2_fmt.fmt.pix_mp.pixelformat : v4l2_fmt.fmt.pix.pixelformat;
    bool is12bit = (fourcc == V4L2_PIX_FMT_Y12) ||
#ifdef V4L2_PIX_FMT_SRGGB12
                   (fourcc == V4L2_PIX_FMT_SRGGB12) ||
#endif
#ifdef V4L2_PIX_FMT_SGRBG12
                   (fourcc == V4L2_PIX_FMT_SGRBG12) ||
#endif
#ifdef V4L2_PIX_FMT_SGBRG12
                   (fourcc == V4L2_PIX_FMT_SGBRG12) ||
#endif
#ifdef V4L2_PIX_FMT_SBGGR12
                   (fourcc == V4L2_PIX_FMT_SBGGR12) ||
#endif
                   (fourcc == v4l2_fourcc('R','G','1','2')) ||
                   (fourcc == v4l2_fourcc('B','A','1','2')) ||
                   (fourcc == v4l2_fourcc('G','B','1','2')) ||
                   (fourcc == v4l2_fourcc('B','G','1','2'));

    if (v4l2_force_16bit && is12bit)
        numBytes = w * h * 2;
    else if (fourcc == V4L2_PIX_FMT_RGB24)
        numBytes = w * h * 3;
    else if (fourcc == V4L2_PIX_FMT_YUYV)
        numBytes = w * h * 3; // we convert to RGB24 for streaming
    else
        numBytes = w * h;     // 8-bit mono fallback
    
    // Only allocate buffer if not already allocated or size changed
    if (!buffer || PrimaryCCD.getFrameBufferSize() != numBytes)
    {
        if (buffer)
            free(buffer);
        buffer = (uint8_t*)malloc(numBytes);
        if (!buffer)
            return false;
    }
    
    PrimaryCCD.setFrameBufferSize(numBytes);
    PrimaryCCD.setResolution(w, h);
    
    // DO NOT start stream here - it will be started explicitly in StartExposure()
    // This function only sets up buffers and parameters
    
    return true;
}

bool indi_qhy_v4l2::getStreamFrameV4L2()
{
    if (v4l2_fd < 0)
        return false;
    
    // Stream should already be running when this is called during exposure
    // Do NOT start stream here
    if (!v4l2_streaming)
    {
        DEBUG(INDI::Logger::DBG_DEBUG, "getStreamFrameV4L2 called but stream not running");
        return false;
    }
    
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(v4l2_fd, &fds);
    struct timeval tv = {0, 0};
    tv.tv_sec = 0;
    tv.tv_usec = (suseconds_t)bufferTimeout;
    int r = select(v4l2_fd + 1, &fds, nullptr, nullptr, &tv);
    if (r <= 0)
        return false;

    struct v4l2_buffer buf = {};
    buf.type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    struct v4l2_plane planes[1];
    memset(planes, 0, sizeof(planes));
    if (v4l2_is_mplane)
    {
        buf.length = 1;
        buf.m.planes = planes;
    }
    if (ioctl(v4l2_fd, VIDIOC_DQBUF, &buf) < 0)
    {
        if (errno == EAGAIN)
            return false;
        DEBUGF(INDI::Logger::DBG_SESSION, "VIDIOC_DQBUF failed: %s", strerror(errno));
        return false;
    }

    void *start = v4l2_mmap_ptrs[buf.index];
    size_t length = v4l2_is_mplane ? planes[0].bytesused : buf.bytesused;
    int w = v4l2_is_mplane ? (int)v4l2_fmt.fmt.pix_mp.width : (int)v4l2_fmt.fmt.pix.width;
    int h = v4l2_is_mplane ? (int)v4l2_fmt.fmt.pix_mp.height : (int)v4l2_fmt.fmt.pix.height;
    uint32_t fourcc = v4l2_is_mplane ? v4l2_fmt.fmt.pix_mp.pixelformat : v4l2_fmt.fmt.pix.pixelformat;
    if (fourcc == V4L2_PIX_FMT_RGB24)
    {
        size_t expected = (size_t)w * h * 3;
        if (length >= expected)
        {
            memcpy(buffer, start, expected);
            numBytes = (int)expected;
        }
        else
        {
            memcpy(buffer, start, length);
            numBytes = (int)length;
        }
    }
    else if (fourcc == V4L2_PIX_FMT_YUYV)
    {
        const uint8_t *src = reinterpret_cast<const uint8_t*>(start);
        uint8_t *dst = buffer;
        for (int y = 0; y < h; y++)
            yuyv_to_rgb24_line(src + y * w * 2, dst + y * w * 3, w);
        numBytes = w * h * 3;
    }
    else if (fourcc == V4L2_PIX_FMT_Y12
#ifdef V4L2_PIX_FMT_SRGGB12
             || fourcc == V4L2_PIX_FMT_SRGGB12
#endif
#ifdef V4L2_PIX_FMT_SGRBG12
             || fourcc == V4L2_PIX_FMT_SGRBG12
#endif
#ifdef V4L2_PIX_FMT_SGBRG12
             || fourcc == V4L2_PIX_FMT_SGBRG12
#endif
#ifdef V4L2_PIX_FMT_SBGGR12
             || fourcc == V4L2_PIX_FMT_SBGGR12
#endif
             || fourcc == v4l2_fourcc('R','G','1','2')
             || fourcc == v4l2_fourcc('B','A','1','2')
             || fourcc == v4l2_fourcc('G','B','1','2')
             || fourcc == v4l2_fourcc('B','G','1','2'))
    {
        if (v4l2_force_16bit)
        {
            // 透传16位容器，但按 bytesperline 行拷贝，避免 stride 造成的错位
            size_t expected = (size_t)w * h * 2;
            uint32_t src_bpl = 0;
            if (!v4l2_is_mplane)
                src_bpl = v4l2_fmt.fmt.pix.bytesperline;
            else
                src_bpl = v4l2_fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
            if (src_bpl == 0)
                src_bpl = w * 2;

            if (length >= (size_t)src_bpl * h)
            {
                const uint8_t *src8 = reinterpret_cast<const uint8_t*>(start);
                uint8_t *dst8 = buffer;
                size_t dst_bpl = (size_t)w * 2;
                for (int row = 0; row < h; row++)
                {
                    memcpy(dst8 + row * dst_bpl, src8 + row * src_bpl, dst_bpl);
                }
                numBytes = (int)expected;
            }
            else if (length >= expected)
            {
                memcpy(buffer, start, expected);
                numBytes = (int)expected;
            }
            else
            {
                DEBUG(INDI::Logger::DBG_SESSION, "Raw 12-bit container smaller than expected");
                if (v4l2_is_mplane)
                {
                    buf.m.planes = planes;
                    buf.length = 1;
                }
                ioctl(v4l2_fd, VIDIOC_QBUF, &buf);
                return false;
            }
        }
        else
        {
            // 8-bit mono preview/stream
            if (length >= (size_t)w * h * 2)
            {
                const uint16_t *src16 = reinterpret_cast<const uint16_t*>(start);
                uint8_t *dst = buffer;
                int pixels = w * h;
                for (int i = 0; i < pixels; i++)
                {
                    uint16_t v = src16[i];
                    *dst++ = (uint8_t)((v >> 4) & 0xFF);
                }
                numBytes = w * h;
            }
            else if (length >= (size_t)w * h)
            {
                memcpy(buffer, start, (size_t)w * h);
                numBytes = w * h;
            }
            else
            {
                DEBUG(INDI::Logger::DBG_SESSION, "Unexpected Y12/Bayer12 buffer size");
                if (v4l2_is_mplane)
                {
                    buf.m.planes = planes;
                    buf.length = 1;
                }
                ioctl(v4l2_fd, VIDIOC_QBUF, &buf);
                return false;
            }
        }
    }
    else
    {
        DEBUG(INDI::Logger::DBG_SESSION, "Unsupported V4L2 pixel format");
        if (v4l2_is_mplane)
        {
            buf.m.planes = planes;
            buf.length = 1;
        }
        ioctl(v4l2_fd, VIDIOC_QBUF, &buf);
        return false;
    }

    if (v4l2_is_mplane)
    {
        buf.m.planes = planes;
        buf.length = 1;
    }
    if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "VIDIOC_QBUF failed: %s", strerror(errno));
        return false;
    }
    return true;
}

bool indi_qhy_v4l2::flush_frame_bufferV4L2()
{
    if (v4l2_fd < 0)
        return true;
    
    // This function should only be called when stream is already running
    // Do NOT start stream here - it should be started in StartExposure before calling this
    if (!v4l2_streaming)
    {
        DEBUG(INDI::Logger::DBG_DEBUG, "flush_frame_bufferV4L2 called but stream not running, skipping flush");
        return true; // Not an error, just nothing to flush
    }
    
    int cleared = 0;
    int max_clear = 10; // Limit number of frames to clear to avoid infinite loop
    while (cleared < max_clear)
    {
        struct v4l2_buffer buf = {};
        buf.type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        struct v4l2_plane planes[1];
        memset(planes, 0, sizeof(planes));
        if (v4l2_is_mplane)
        {
            buf.length = 1;
            buf.m.planes = planes;
        }
        
        // Use select with timeout to avoid blocking
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(v4l2_fd, &fds);
        struct timeval tv = {0, 10000}; // 10ms timeout
        int r = select(v4l2_fd + 1, &fds, nullptr, nullptr, &tv);
        if (r <= 0)
            break; // No data available or timeout
        
        if (ioctl(v4l2_fd, VIDIOC_DQBUF, &buf) < 0)
        {
            if (errno == EAGAIN)
                break; // No buffer available
            // For other errors, log but continue
            DEBUGF(INDI::Logger::DBG_DEBUG, "VIDIOC_DQBUF failed in flush: %s", strerror(errno));
            break;
        }
        
        if (v4l2_is_mplane)
        {
            buf.m.planes = planes;
            buf.length = 1;
        }
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0)
        {
            DEBUGF(INDI::Logger::DBG_DEBUG, "VIDIOC_QBUF failed in flush: %s", strerror(errno));
            break;
        }
        cleared++;
    }
    if (cleared > 0)
        DEBUGF(INDI::Logger::DBG_SESSION, "V4L2 Buffer Cleared of %u stale frames.", cleared);
    return true;
}

void indi_qhy_v4l2::freeV4L2Memory()
{
    if (v4l2_buffers)
    {
        for (unsigned int i = 0; i < v4l2_buffer_count; i++)
        {
            void *start = v4l2_mmap_ptrs ? v4l2_mmap_ptrs[i] : nullptr;
            if (start && v4l2_mmap_lens && v4l2_mmap_lens[i] > 0)
                munmap(start, v4l2_mmap_lens[i]);
        }
        free(v4l2_buffers);
        v4l2_buffers = nullptr;
        v4l2_buffer_count = 0;
    }
    if (v4l2_mmap_ptrs)
    {
        free(v4l2_mmap_ptrs);
        v4l2_mmap_ptrs = nullptr;
    }
    if (v4l2_mmap_lens)
    {
        free(v4l2_mmap_lens);
        v4l2_mmap_lens = nullptr;
    }
    if (buffer)
    {
        free(buffer);
        buffer = nullptr;
    }
}

// Enumerate V4L2 pixel formats
bool indi_qhy_v4l2::enumerateV4L2Formats()
{
    if (v4l2_fd < 0)
        return false;
    
    struct v4l2_fmtdesc fmt_desc;
    memset(&fmt_desc, 0, sizeof(fmt_desc));
    fmt_desc.type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    DEBUG(INDI::Logger::DBG_SESSION, "V4L2 Supported Formats:");
    for (fmt_desc.index = 0; ioctl(v4l2_fd, VIDIOC_ENUM_FMT, &fmt_desc) == 0; fmt_desc.index++)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "  [%u] %s (0x%08x %c%c%c%c)", fmt_desc.index, fmt_desc.description,
               fmt_desc.pixelformat,
               (fmt_desc.pixelformat) & 0xFF,
               (fmt_desc.pixelformat >> 8) & 0xFF,
               (fmt_desc.pixelformat >> 16) & 0xFF,
               (fmt_desc.pixelformat >> 24) & 0xFF);
    }
    return true;
}

// Enumerate V4L2 frame sizes for current format
bool indi_qhy_v4l2::enumerateV4L2Sizes()
{
    if (v4l2_fd < 0)
        return false;
    
    struct v4l2_frmsizeenum frm_size;
    memset(&frm_size, 0, sizeof(frm_size));
    frm_size.pixel_format = v4l2_is_mplane ? v4l2_fmt.fmt.pix_mp.pixelformat : v4l2_fmt.fmt.pix.pixelformat;
    
    DEBUG(INDI::Logger::DBG_SESSION, "V4L2 Supported Sizes:");
    for (frm_size.index = 0; ioctl(v4l2_fd, VIDIOC_ENUM_FRAMESIZES, &frm_size) == 0; frm_size.index++)
    {
        switch (frm_size.type)
        {
            case V4L2_FRMSIZE_TYPE_DISCRETE:
                DEBUGF(INDI::Logger::DBG_SESSION, "  [%u] %ux%u", frm_size.index,
                       frm_size.discrete.width, frm_size.discrete.height);
                break;
            case V4L2_FRMSIZE_TYPE_STEPWISE:
                DEBUGF(INDI::Logger::DBG_SESSION, "  Stepwise: %u-%u (step %u) x %u-%u (step %u)",
                       frm_size.stepwise.min_width, frm_size.stepwise.max_width, frm_size.stepwise.step_width,
                       frm_size.stepwise.min_height, frm_size.stepwise.max_height, frm_size.stepwise.step_height);
                break;
            case V4L2_FRMSIZE_TYPE_CONTINUOUS:
                DEBUGF(INDI::Logger::DBG_SESSION, "  Continuous: %u-%u x %u-%u",
                       frm_size.stepwise.min_width, frm_size.stepwise.max_width,
                       frm_size.stepwise.min_height, frm_size.stepwise.max_height);
                break;
        }
    }
    return true;
}

// Enumerate V4L2 frame rates for current format and size
bool indi_qhy_v4l2::enumerateV4L2FrameRates()
{
    if (v4l2_fd < 0)
        return false;
    
    struct v4l2_frmivalenum frm_ival;
    memset(&frm_ival, 0, sizeof(frm_ival));
    frm_ival.pixel_format = v4l2_is_mplane ? v4l2_fmt.fmt.pix_mp.pixelformat : v4l2_fmt.fmt.pix.pixelformat;
    frm_ival.width = v4l2_is_mplane ? v4l2_fmt.fmt.pix_mp.width : v4l2_fmt.fmt.pix.width;
    frm_ival.height = v4l2_is_mplane ? v4l2_fmt.fmt.pix_mp.height : v4l2_fmt.fmt.pix.height;
    
    DEBUG(INDI::Logger::DBG_SESSION, "V4L2 Supported Frame Rates:");
    for (frm_ival.index = 0; ioctl(v4l2_fd, VIDIOC_ENUM_FRAMEINTERVALS, &frm_ival) == 0; frm_ival.index++)
    {
        switch (frm_ival.type)
        {
            case V4L2_FRMIVAL_TYPE_DISCRETE:
                DEBUGF(INDI::Logger::DBG_SESSION, "  [%u] %u/%u fps", frm_ival.index,
                       frm_ival.discrete.denominator, frm_ival.discrete.numerator);
                break;
            case V4L2_FRMIVAL_TYPE_STEPWISE:
                DEBUGF(INDI::Logger::DBG_SESSION, "  Stepwise: %u/%u - %u/%u (step %u/%u)",
                       frm_ival.stepwise.min.denominator, frm_ival.stepwise.min.numerator,
                       frm_ival.stepwise.max.denominator, frm_ival.stepwise.max.numerator,
                       frm_ival.stepwise.step.denominator, frm_ival.stepwise.step.numerator);
                break;
            case V4L2_FRMIVAL_TYPE_CONTINUOUS:
                DEBUGF(INDI::Logger::DBG_SESSION, "  Continuous: %u/%u - %u/%u",
                       frm_ival.stepwise.min.denominator, frm_ival.stepwise.min.numerator,
                       frm_ival.stepwise.max.denominator, frm_ival.stepwise.max.numerator);
                break;
        }
    }
    return true;
}

// Set V4L2 pixel format
bool indi_qhy_v4l2::setV4L2Format(uint32_t pixelformat)
{
    if (v4l2_fd < 0 || v4l2_streaming)
        return false;
    
    struct v4l2_format new_fmt = v4l2_fmt;
    if (v4l2_is_mplane)
        new_fmt.fmt.pix_mp.pixelformat = pixelformat;
    else
        new_fmt.fmt.pix.pixelformat = pixelformat;
    
    if (ioctl(v4l2_fd, VIDIOC_S_FMT, &new_fmt) < 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "Failed to set V4L2 format: %s", strerror(errno));
        return false;
    }
    
    v4l2_fmt = new_fmt;
    DEBUGF(INDI::Logger::DBG_SESSION, "V4L2 format set to 0x%08x", pixelformat);
    return true;
}

// Set V4L2 frame size
bool indi_qhy_v4l2::setV4L2Size(unsigned int width, unsigned int height)
{
    if (v4l2_fd < 0 || v4l2_streaming)
        return false;
    
    struct v4l2_format new_fmt = v4l2_fmt;
    if (v4l2_is_mplane)
    {
        new_fmt.fmt.pix_mp.width = width;
        new_fmt.fmt.pix_mp.height = height;
        new_fmt.fmt.pix_mp.plane_fmt[0].bytesperline = 0;
        new_fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 0;
    }
    else
    {
        new_fmt.fmt.pix.width = width;
        new_fmt.fmt.pix.height = height;
    }
    
    if (ioctl(v4l2_fd, VIDIOC_S_FMT, &new_fmt) < 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "Failed to set V4L2 size: %s", strerror(errno));
        return false;
    }
    
    v4l2_fmt = new_fmt;
    DEBUGF(INDI::Logger::DBG_SESSION, "V4L2 size set to %ux%u", width, height);
    return true;
}

// Set V4L2 frame rate
bool indi_qhy_v4l2::setV4L2FrameRate(unsigned int numerator, unsigned int denominator)
{
    if (v4l2_fd < 0)
        return false;
    
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    if (ioctl(v4l2_fd, VIDIOC_G_PARM, &parm) < 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "VIDIOC_G_PARM failed: %s", strerror(errno));
        return false;
    }
    
    if (v4l2_is_mplane)
    {
        parm.parm.capture.timeperframe.numerator = numerator;
        parm.parm.capture.timeperframe.denominator = denominator;
    }
    else
    {
        parm.parm.capture.timeperframe.numerator = numerator;
        parm.parm.capture.timeperframe.denominator = denominator;
    }
    
    if (ioctl(v4l2_fd, VIDIOC_S_PARM, &parm) < 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "Failed to set V4L2 frame rate: %s", strerror(errno));
        return false;
    }
    
    DEBUGF(INDI::Logger::DBG_SESSION, "V4L2 frame rate set to %u/%u", numerator, denominator);
    return true;
}

bool indi_qhy_v4l2::setV4L2Exposure(double value)
{
#ifdef __linux__
    v4l2_subdev_exposure = value;
    if (!openV4L2Subdevice())
        return false;

    ensureManualExposureMode();

    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_EXPOSURE;
    ctrl.value = static_cast<int32_t>(std::llround(value));

    if (ioctl(v4l2_subdev_fd, VIDIOC_S_CTRL, &ctrl) < 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "Failed to set sub-device exposure on %s: %s",
               v4l2_subdev_path.c_str(), strerror(errno));
        return false;
    }

    DEBUGF(INDI::Logger::DBG_SESSION, "V4L2 sub-device exposure set to %d", ctrl.value);
    return true;
#else
    return false;
#endif
}

// Query V4L2 control
bool indi_qhy_v4l2::queryV4L2Control(unsigned int ctrl_id, struct v4l2_queryctrl *queryctrl)
{
    if (v4l2_fd < 0 || !queryctrl)
        return false;
    
    memset(queryctrl, 0, sizeof(*queryctrl));
    queryctrl->id = ctrl_id;
    
    if (ioctl(v4l2_fd, VIDIOC_QUERYCTRL, queryctrl) < 0)
    {
        if (errno != EINVAL)
            DEBUGF(INDI::Logger::DBG_SESSION, "VIDIOC_QUERYCTRL failed for 0x%08x: %s", ctrl_id, strerror(errno));
        return false;
    }
    
    return true;
}

// Set V4L2 control value
bool indi_qhy_v4l2::setV4L2Control(unsigned int ctrl_id, int32_t value)
{
    if (v4l2_fd < 0)
        return false;
    
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = ctrl_id;
    ctrl.value = value;
    
    if (ioctl(v4l2_fd, VIDIOC_S_CTRL, &ctrl) < 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "Failed to set V4L2 control 0x%08x: %s", ctrl_id, strerror(errno));
        return false;
    }
    
    return true;
}

// Get V4L2 control value
bool indi_qhy_v4l2::getV4L2Control(unsigned int ctrl_id, int32_t *value)
{
    if (v4l2_fd < 0 || !value)
        return false;
    
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = ctrl_id;
    
    if (ioctl(v4l2_fd, VIDIOC_G_CTRL, &ctrl) < 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "Failed to get V4L2 control 0x%08x: %s", ctrl_id, strerror(errno));
        return false;
    }
    
    *value = ctrl.value;
    return true;
}

bool indi_qhy_v4l2::ensureManualExposureMode()
{
#ifdef __linux__
    if (!openV4L2Subdevice())
        return false;

    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_EXPOSURE_AUTO;
    ctrl.value = V4L2_EXPOSURE_MANUAL;

    if (ioctl(v4l2_subdev_fd, VIDIOC_S_CTRL, &ctrl) < 0)
    {
        if (errno != EINVAL)
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "Failed to force manual exposure mode on %s: %s",
                   v4l2_subdev_path.c_str(), strerror(errno));
            return false;
        }
        // Control not supported: not fatal
    }
    return true;
#else
    return false;
#endif
}

bool indi_qhy_v4l2::openV4L2Subdevice()
{
#ifdef __linux__
    if (v4l2_subdev_path.empty())
        return false;
    if (v4l2_subdev_fd >= 0)
        return true;

    v4l2_subdev_fd = open(v4l2_subdev_path.c_str(), O_RDWR);
    if (v4l2_subdev_fd < 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "Failed to open V4L2 sub-device %s: %s",
               v4l2_subdev_path.c_str(), strerror(errno));
        return false;
    }

    updateV4L2SubdevExposureRange();
    return true;
#else
    return false;
#endif
}

void indi_qhy_v4l2::closeV4L2Subdevice()
{
#ifdef __linux__
    if (v4l2_subdev_fd >= 0)
    {
        close(v4l2_subdev_fd);
        v4l2_subdev_fd = -1;
    }
#endif
}

void indi_qhy_v4l2::updateV4L2SubdevExposureRange()
{
#ifdef __linux__
    if (v4l2_subdev_fd < 0)
        return;

    struct v4l2_queryctrl query = {};
    query.id = V4L2_CID_EXPOSURE;

    if (ioctl(v4l2_subdev_fd, VIDIOC_QUERYCTRL, &query) == 0)
    {
        // 更新成员变量存储的曝光范围
        v4l2_subdev_exposure_min = query.minimum;
        v4l2_subdev_exposure_max = query.maximum;

        // 确保当前曝光值在有效范围内
        if (v4l2_subdev_exposure < v4l2_subdev_exposure_min)
            v4l2_subdev_exposure = v4l2_subdev_exposure_min;
        if (v4l2_subdev_exposure > v4l2_subdev_exposure_max)
            v4l2_subdev_exposure = v4l2_subdev_exposure_max;

        DEBUGF(INDI::Logger::DBG_SESSION, "V4L2 subdev exposure range updated: %.0f - %.0f ms (current: %.0f ms)",
               v4l2_subdev_exposure_min, v4l2_subdev_exposure_max, v4l2_subdev_exposure);
    }
    else
    {
        DEBUGF(INDI::Logger::DBG_WARNING, "Failed to query V4L2 subdev exposure range: %s", strerror(errno));
    }
#endif
}

void indi_qhy_v4l2::syncV4L2ExposureFromDuration(double duration) //根据上位机传过来的曝光时间和设备最大曝光时间来设置
{
#ifdef __linux__
    if (!use_v4l2_direct)
        return;

    if (duration <= 0)
        duration = 0.001;

    // 使用成员变量存储的曝光范围（从 updateV4L2SubdevExposureRange 中查询得到）
    double minValue = v4l2_subdev_exposure_min;
    double maxValue = v4l2_subdev_exposure_max;

    // 将秒转换为毫秒
    double milliseconds = duration * 1000.0;
    int32_t target = static_cast<int32_t>(std::round(milliseconds));

    // 限制在设备支持的范围内
    if (target < static_cast<int32_t>(minValue))
        target = static_cast<int32_t>(minValue);
    if (target > static_cast<int32_t>(maxValue))
        target = static_cast<int32_t>(maxValue);

    // 设置 V4L2 子设备曝光
    if (!setV4L2Exposure(target))
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "Failed to auto-sync V4L2 exposure for duration %.3f s", duration);
    }
    else
    {
        v4l2_subdev_exposure = target;  // 更新内部变量
        DEBUGF(INDI::Logger::DBG_SESSION, "Auto-synced V4L2 exposure to %d ms for duration %.3f s (range: %.0f-%.0f)",
               target, duration, minValue, maxValue);
    }
#else
    (void)duration;
#endif
}

// Set V4L2 crop rectangle
bool indi_qhy_v4l2::setV4L2Crop(int x, int y, int w, int h)
{
    if (v4l2_fd < 0 || !v4l2_can_crop)
        return false;
    
    v4l2_crop.type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_crop.c.left = x;
    v4l2_crop.c.top = y;
    v4l2_crop.c.width = w;
    v4l2_crop.c.height = h;
    
    if (ioctl(v4l2_fd, VIDIOC_S_CROP, &v4l2_crop) < 0)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "Failed to set V4L2 crop: %s", strerror(errno));
        return false;
    }
    
    DEBUGF(INDI::Logger::DBG_SESSION, "V4L2 crop set to (%d,%d) %dx%d", x, y, w, h);
    return true;
}

// Get V4L2 crop rectangle
struct v4l2_rect indi_qhy_v4l2::getV4L2Crop()
{
    struct v4l2_rect rect = {0, 0, 0, 0};
    
    if (v4l2_fd < 0 || !v4l2_can_crop)
        return rect;
    
    v4l2_crop.type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(v4l2_fd, VIDIOC_G_CROP, &v4l2_crop) == 0)
        rect = v4l2_crop.c;
    
    return rect;
}
#endif

bool indi_qhy_v4l2::requeueAllV4L2Buffers()
{
#ifdef __linux__
    if (v4l2_fd < 0 || !v4l2_buffers || v4l2_buffer_count == 0)
        return false;

    unsigned int success = 0, busy = 0;
    for (unsigned int i = 0; i < v4l2_buffer_count; i++)
    {
        struct v4l2_buffer buf = {};
        buf.type = v4l2_is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        struct v4l2_plane planes[1];
        memset(planes, 0, sizeof(planes));
        if (v4l2_is_mplane)
        {
            buf.length = 1;
            buf.m.planes = planes;
        }
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf) == 0)
        {
            success++;
            continue;
        }
        if (errno == EBUSY)
        {
            // 已在队列中，无需重复入队
            busy++;
            continue;
        }
        DEBUGF(INDI::Logger::DBG_DEBUG, "VIDIOC_QBUF (requeue) failed for idx=%u: %s", i, strerror(errno));
    }
    DEBUGF(INDI::Logger::DBG_SESSION, "Re-queued V4L2 buffers: success=%u, busy=%u, total=%u",
           success, busy, v4l2_buffer_count);
    // 只要没有致命错误，返回 true
    return true;
#else
    return true;
#endif
}

bool indi_qhy_v4l2::saveConfigItems(FILE *fp)
{
    INDI::CCD::saveConfigItems(fp);
    IUSaveConfigSwitch(fp, &CaptureDeviceSelection);
    IUSaveConfigSwitch(fp, &RapidStackingSelection);
    IUSaveConfigSwitch(fp, &OutputFormatSelection);
    IUSaveConfigSwitch(fp, &OnlineProtocolSelection);
    IUSaveConfigNumber(fp, &PixelSizeTP);
    IUSaveConfigText(fp, &InputOptionsTP);
    IUSaveConfigText(fp, &OnlineInputOptionsP);
    IUSaveConfigText(fp, &URLPathTP);
    IUSaveConfigNumber(fp, &TimeoutOptionsTP);
#ifdef __linux__
    IUSaveConfigText(fp, &V4L2SubdevPathTP);
#endif

    return true;
}

/**************************************************************************************
** V4L2 增益和偏移量控制函数
***************************************************************************************/

// 更新增益范围（从子设备查询）
void indi_qhy_v4l2::updateV4L2GainRange()
{
#ifdef __linux__
    if (v4l2_subdev_fd < 0)
        return;

    struct v4l2_queryctrl query = {};
    query.id = 0x009e0903;  // V4L2_CID_ANALOGUE_GAIN

    if (ioctl(v4l2_subdev_fd, VIDIOC_QUERYCTRL, &query) == 0)
    {
        v4l2_subdev_gain_min = query.minimum;
        v4l2_subdev_gain_max = query.maximum;

        // 确保当前增益值在有效范围内
        if (v4l2_subdev_gain < v4l2_subdev_gain_min)
            v4l2_subdev_gain = v4l2_subdev_gain_min;
        if (v4l2_subdev_gain > v4l2_subdev_gain_max)
            v4l2_subdev_gain = v4l2_subdev_gain_max;

        DEBUGF(INDI::Logger::DBG_SESSION, "V4L2 subdev gain range updated: %d - %d (current: %d)", 
               v4l2_subdev_gain_min, v4l2_subdev_gain_max, v4l2_subdev_gain);
    }
    else
    {
        DEBUGF(INDI::Logger::DBG_WARNING, "Failed to query V4L2 subdev gain range: %s", strerror(errno));
    }
#endif
}

// 设置模拟增益
bool indi_qhy_v4l2::setV4L2Gain(int32_t gain)
{
#ifdef __linux__
    if (v4l2_subdev_fd < 0)
    {
        DEBUG(INDI::Logger::DBG_WARNING, "V4L2 subdevice not open");
        return false;
    }

    // 限制在有效范围内
    if (gain < v4l2_subdev_gain_min)
        gain = v4l2_subdev_gain_min;
    if (gain > v4l2_subdev_gain_max)
        gain = v4l2_subdev_gain_max;

    struct v4l2_control ctrl = {};
    ctrl.id = 0x009e0903;  // V4L2_CID_ANALOGUE_GAIN
    ctrl.value = gain;

    if (ioctl(v4l2_subdev_fd, VIDIOC_S_CTRL, &ctrl) < 0)
    {
        DEBUGF(INDI::Logger::DBG_WARNING, "Failed to set V4L2 gain to %d: %s", gain, strerror(errno));
        return false;
    }

    v4l2_subdev_gain = gain;
    DEBUGF(INDI::Logger::DBG_SESSION, "V4L2 gain set to %d", gain);
    return true;
#else
    (void)gain;
    return false;
#endif
}

// 获取当前增益值
bool indi_qhy_v4l2::getV4L2Gain(int32_t *gain)
{
#ifdef __linux__
    if (v4l2_subdev_fd < 0 || !gain)
        return false;

    struct v4l2_control ctrl = {};
    ctrl.id = 0x009e0903;  // V4L2_CID_ANALOGUE_GAIN

    if (ioctl(v4l2_subdev_fd, VIDIOC_G_CTRL, &ctrl) < 0)
    {
        DEBUGF(INDI::Logger::DBG_WARNING, "Failed to get V4L2 gain: %s", strerror(errno));
        return false;
    }

    *gain = ctrl.value;
    v4l2_subdev_gain = ctrl.value;
    return true;
#else
    (void)gain;
    return false;
#endif
}

// 写入单个寄存器（通过 V4L2 子设备）
bool indi_qhy_v4l2::writeV4L2Register(uint16_t reg, uint8_t value)
{
#ifdef __linux__
    if (v4l2_subdev_fd < 0)
    {
        DEBUG(INDI::Logger::DBG_WARNING, "V4L2 subdevice not open");
        return false;
    }

    // 使用 VIDIOC_DBG_S_REGISTER ioctl
    struct v4l2_dbg_register dbg_reg = {};
    dbg_reg.match.type = V4L2_CHIP_MATCH_SUBDEV;
    dbg_reg.match.addr = 0;  // 子设备地址
    dbg_reg.reg = reg;
    dbg_reg.val = value;
    dbg_reg.size = 1;  // 1 字节

    if (ioctl(v4l2_subdev_fd, VIDIOC_DBG_S_REGISTER, &dbg_reg) < 0)
    {
        DEBUGF(INDI::Logger::DBG_WARNING, "Failed to write register 0x%04x = 0x%02x: %s", 
               reg, value, strerror(errno));
        return false;
    }

    DEBUGF(INDI::Logger::DBG_DEBUG, "Wrote register 0x%04x = 0x%02x", reg, value);
    return true;
#else
    (void)reg;
    (void)value;
    return false;
#endif
}

// 读取单个寄存器
bool indi_qhy_v4l2::readV4L2Register(uint16_t reg, uint8_t *value)
{
#ifdef __linux__
    if (v4l2_subdev_fd < 0 || !value)
        return false;

    struct v4l2_dbg_register dbg_reg = {};
    dbg_reg.match.type = V4L2_CHIP_MATCH_SUBDEV;
    dbg_reg.match.addr = 0;
    dbg_reg.reg = reg;
    dbg_reg.size = 1;

    if (ioctl(v4l2_subdev_fd, VIDIOC_DBG_G_REGISTER, &dbg_reg) < 0)
    {
        DEBUGF(INDI::Logger::DBG_WARNING, "Failed to read register 0x%04x: %s", 
               reg, strerror(errno));
        return false;
    }

    *value = (uint8_t)dbg_reg.val;
    DEBUGF(INDI::Logger::DBG_DEBUG, "Read register 0x%04x = 0x%02x", reg, *value);
    return true;
#else
    (void)reg;
    (void)value;
    return false;
#endif
}

// 设置偏移量（通过两个寄存器）
bool indi_qhy_v4l2::setV4L2Offset(int32_t offset)
{
#ifdef __linux__
    if (v4l2_subdev_fd < 0)
    {
        DEBUG(INDI::Logger::DBG_WARNING, "V4L2 subdevice not open");
        return false;
    }

    // 限制在有效范围内（12位：0-4095）
    if (offset < v4l2_subdev_offset_min)
        offset = v4l2_subdev_offset_min;
    if (offset > v4l2_subdev_offset_max)
        offset = v4l2_subdev_offset_max;

    // 分解为高低字节
    uint8_t offset_h = (offset >> 8) & 0x0F;  // 高4位
    uint8_t offset_l = offset & 0xFF;          // 低8位

    // 写入 REG_OFFSET_H (0x3907)
    if (!writeV4L2Register(0x3907, offset_h))
    {
        DEBUG(INDI::Logger::DBG_WARNING, "Failed to write REG_OFFSET_H");
        return false;
    }

    // 写入 REG_OFFSET_L (0x3908)
    if (!writeV4L2Register(0x3908, offset_l))
    {
        DEBUG(INDI::Logger::DBG_WARNING, "Failed to write REG_OFFSET_L");
        return false;
    }

    v4l2_subdev_offset = offset;
    DEBUGF(INDI::Logger::DBG_SESSION, "V4L2 offset set to %d (0x%03x)", offset, offset);
    return true;
#else
    (void)offset;
    return false;
#endif
}

// 获取当前偏移量
bool indi_qhy_v4l2::getV4L2Offset(int32_t *offset)
{
#ifdef __linux__
    if (v4l2_subdev_fd < 0 || !offset)
        return false;

    uint8_t offset_h = 0, offset_l = 0;

    // 读取 REG_OFFSET_H (0x3907)
    if (!readV4L2Register(0x3907, &offset_h))
    {
        DEBUG(INDI::Logger::DBG_WARNING, "Failed to read REG_OFFSET_H");
        return false;
    }

    // 读取 REG_OFFSET_L (0x3908)
    if (!readV4L2Register(0x3908, &offset_l))
    {
        DEBUG(INDI::Logger::DBG_WARNING, "Failed to read REG_OFFSET_L");
        return false;
    }

    // 组合为12位值
    *offset = ((offset_h & 0x0F) << 8) | offset_l;
    v4l2_subdev_offset = *offset;
    return true;
#else
    (void)offset;
    return false;
#endif
}
