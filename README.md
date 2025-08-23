// 视频输入设备驱动（vi）
class VideoInputDriver {
public:
    int init(const VideoInputConfig& config); // 初始化参数（如分辨率、接口类型）
    int start(); // 启动视频输入
    int stop();  // 停止视频输入
    // 提供数据获取接口（如获取原始视频帧的回调/指针）
    void setFrameCallback(std::function<void(VideoFrame*)> callback);
};

// 视频编码设备驱动（venc）
class VideoEncoderDriver {
public:
    int init(const VideoEncodeConfig& config); // 初始化参数（如编码格式、码率）
    int start(); 
    int stop();
    // 提供编码输入接口（接收vi的原始帧并编码）
    int encodeFrame(VideoFrame* frame);
};

// 音频输入设备驱动（ai）
class AudioInputDriver { ... }; // 类似视频输入，管理音频采集

// 音频编码设备驱动（aenc）
class AudioEncoderDriver { ... }; // 类似视频编码，管理音频编码

// 设备管理器（负责设备绑定、协同初始化）
class MediaDeviceManager {
public:
    // 绑定视频输入到视频编码（建立数据通路）
    int bind(VideoInputDriver* vi, VideoEncoderDriver* venc);
    // 绑定音频输入到音频编码
    int bind(AudioInputDriver* ai, AudioEncoderDriver* aenc);
    // 统一初始化所有设备（按依赖顺序）
    int initAllDevices();
    // 统一启动所有设备
    int startAllDevices();
};



core: 核心模块，负责设备管理、数据流控制、编解码调度等

// 媒体流处理核心类（负责音视频业务流程）
class MediaStreamProcessor {
private:
    // 依赖driver层的设备（通过接口依赖，而非直接操作）
    VideoInputDriver* viDriver;
    VideoEncoderDriver* vencDriver;
    AudioInputDriver* aiDriver;
    AudioEncoderDriver* aencDriver;
    MediaDeviceManager* deviceMgr;

public:
    // 构造时传入driver层对象（依赖注入，降低耦合）
    MediaStreamProcessor(MediaDeviceManager* mgr, ...);
    
    // 核心业务：启动完整的音视频采集编码流程
    int startStream();
    
    // 业务逻辑：动态调整编码参数（如网络差时降低码率）
    int adjustEncodeParams(int bitrate, int fps);
    
    // 业务逻辑：音视频同步处理
    int syncAudioVideo(AudioFrame* aFrame, VideoFrame* vFrame);
};

// 媒体会话管理（如多流并发、会话生命周期）
class MediaSessionManager { ... };



app: 应用层，负责业务逻辑、用户交互等

// 应用控制器（入口类）
class AppController {
private:
    MediaStreamProcessor* streamProcessor; // 依赖core层核心逻辑
    ConfigParser* configParser; // 配置解析器

public:
    AppController();
    ~AppController();
    
    // 应用初始化：解析配置、创建各层实例
    int init(const std::string& configPath);
    
    // 启动应用：调用core层启动业务流程
    int run();
    
    // 停止应用：释放资源
    int shutdown();
    
    // 处理用户命令（如通过CLI接收“开始录制”“停止”等指令）
    int handleUserCommand(const std::string& cmd);
};


driver层：以Driver或Device为后缀，前缀体现设备类型（如VideoInputDriver、AudioEncoderDevice）；
core层：以功能为核心，可包含Processor（处理器）、Manager（管理器）等后缀（如MediaStreamProcessor、SyncManager）；
app层：以Controller（控制器）、App为核心（如AppController、MediaApp）。



driver 层：管好 “硬件设备”（初始化、绑定、启停），提供抽象接口；
core 层：用好 “设备能力”（业务流程、数据处理），实现核心功能；
app 层：控好 “应用入口”（配置、交互、流程启停），衔接用户与业务。





main -> app 

app.init -> media_engine_->init
                dev_mgr_->initAllDevices
                    mpi_manager_->init()
                    isp_driver_->init();
                    vi_driver_->init();
                    venc_driver_->init();

                dev_mgr_->createStreamProcessor
                    new core::MediaStreamProcessor








========================================================================================
#pragma once

#include <memory>
#include "core/RTSPStreamer.hpp"
#include "core/AudioEngine.hpp"
#include "core/VideoEngine.hpp"

namespace app
{

class Application
{
public:
    Application();
    ~Application();
    
    bool init();
    void run();
    void cleanup();
    
private:
    std::shared_ptr<core::RTSPStreamer> rtsp_streamer_;
    std::shared_ptr<core::AudioEngine> audio_engine_;
    std::shared_ptr<core::VideoEngine> video_engine_;
    bool is_running_ = false;
};

} // namespace app





#include "app/Application.hpp"
#include "infra/logging/logger.h"

namespace app
{

Application::Application() {
    LOGI("Application created");
}

Application::~Application() {
    cleanup();
    LOGI("Application destroyed");
}

bool Application::init() {
    // 创建RTSP流推流器
    rtsp_streamer_ = std::make_shared<core::RTSPStreamer>(
        554, 
        "/live/camera",
        RTSP_CODEC_ID_VIDEO_H265,
        RTSP_CODEC_ID_AUDIO_G711A  // 使用G.711A音频编码
    );
    
    if (!rtsp_streamer_->init()) {
        LOGE("Failed to initialize RTSP streamer");
        return false;
    }
    
    // 创建音频引擎
    audio_engine_ = std::make_shared<core::AudioEngine>();
    if (!audio_engine_->init(rtsp_streamer_)) {
        LOGE("Failed to initialize audio engine");
        return false;
    }
    
    // 创建视频引擎
    video_engine_ = std::make_shared<core::VideoEngine>();
    if (!video_engine_->init(rtsp_streamer_)) {
        LOGE("Failed to initialize video engine");
        return false;
    }
    
    LOGI("Application initialized successfully");
    return true;
}

void Application::run() {
    if (is_running_) return;
    
    // 启动音频引擎
    audio_engine_->start();
    
    // 启动视频引擎
    video_engine_->start();
    
    is_running_ = true;
    LOGI("Application started");
    
    // 主循环
    while (is_running_) {
        // 处理RTSP事件
        rtsp_streamer_->handleEvents();
        
        // 短暂休眠避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void Application::cleanup() {
    if (!is_running_) return;
    
    // 停止引擎
    audio_engine_->stop();
    video_engine_->stop();
    
    is_running_ = false;
    LOGI("Application stopped");
}

} // namespace app