extern "C"
{
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include "libavutil/imgutils.h"
#include "unistd.h"
#include <cstdlib>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <time.h>
#include <unistd.h>
#include <sys/poll.h>
#include <time.h>
#include <unistd.h>
}
#include "sample_comm.h"
#include "rtsp_demo.h"
#include <vector>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

using namespace std;
using namespace cv;
#define DISP_WIDTH 1920
#define DISP_HEIGHT 1080

int vi_dev_init()
{
	printf("%s\n", __func__);
	int ret = 0;
	int devId = 0;
	int pipeId = devId;

	VI_DEV_ATTR_S stDevAttr;
	VI_DEV_BIND_PIPE_S stBindPipe;
	memset(&stDevAttr, 0, sizeof(stDevAttr));
	memset(&stBindPipe, 0, sizeof(stBindPipe));
	// 0. get dev config status
	ret = RK_MPI_VI_GetDevAttr(devId, &stDevAttr);
	if (ret == RK_ERR_VI_NOT_CONFIG)
	{
		// 0-1.config dev
		ret = RK_MPI_VI_SetDevAttr(devId, &stDevAttr);
		if (ret != RK_SUCCESS)
		{
			printf("RK_MPI_VI_SetDevAttr %x\n", ret);
			return -1;
		}
	}
	else
	{
		printf("RK_MPI_VI_SetDevAttr already\n");
	}
	// 1.get dev enable status
	ret = RK_MPI_VI_GetDevIsEnable(devId);
	if (ret != RK_SUCCESS)
	{
		// 1-2.enable dev
		ret = RK_MPI_VI_EnableDev(devId);
		if (ret != RK_SUCCESS)
		{
			printf("RK_MPI_VI_EnableDev %x\n", ret);
			return -1;
		}
		// 1-3.bind dev/pipe
		stBindPipe.u32Num = 1;
		stBindPipe.PipeId[0] = pipeId;
		ret = RK_MPI_VI_SetDevBindPipe(devId, &stBindPipe);
		if (ret != RK_SUCCESS)
		{
			printf("RK_MPI_VI_SetDevBindPipe %x\n", ret);
			return -1;
		}
	}
	else
	{
		printf("RK_MPI_VI_EnableDev already\n");
	}

	return 0;
}

int vi_init()
{

	vi_dev_init();
	printf("初始化VI成功");
	// 3：初始化 VI
	// FRAME_RATE_CTRL_S   stFrameRate = {};
	VI_CHN_ATTR_S viattr = {0};
	viattr.bFlip = RK_FALSE;   // 是否翻转
	viattr.bMirror = RK_FALSE; // 镜像
	viattr.enAllocBufType = VI_ALLOC_BUF_TYPE_INTERNAL;
	viattr.enCompressMode = COMPRESS_MODE_NONE;	 // 编码压缩模式
	viattr.enDynamicRange = DYNAMIC_RANGE_SDR10; // 动态范围
	viattr.enPixelFormat = RK_FMT_YUV420SP;		 // NV12
	// viattr.stFrameRate={30,1};//帧率
	viattr.stIspOpt.enCaptureType = VI_V4L2_CAPTURE_TYPE_VIDEO_CAPTURE;
	viattr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF; // DMA加速
	viattr.stIspOpt.stMaxSize = {1920, 1080};
	viattr.stIspOpt.stWindow = {0, 0, 1920, 1080};
	viattr.stIspOpt.u32BufCount = 3;
	viattr.stIspOpt.u32BufSize = 1920 * 1080 * 2;
	viattr.stSize.u32Height = 1080;
	viattr.stSize.u32Width = 1920;
	viattr.u32Depth = 3;
	int ret = RK_MPI_VI_SetChnAttr(0, 0, &viattr);
	ret |= RK_MPI_VI_EnableChn(0, 0);
	if (ret)
	{
		printf("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}
	return 0;
}

int vi_chn_init(int channelId, int width, int height)
{
	int ret;
	int buf_cnt = 2;
	// VI init
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType =
		VI_V4L2_MEMORY_TYPE_DMABUF; // VI_V4L2_MEMORY_TYPE_MMAP;
	vi_chn_attr.stSize.u32Width = width;
	vi_chn_attr.stSize.u32Height = height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE; // COMPRESS_AFBC_16x16;
	vi_chn_attr.u32Depth = 2;						 // 0, get fail, 1 - u32BufCount, can get, if bind to other device, must be < u32BufCount
	ret = RK_MPI_VI_SetChnAttr(0, channelId, &vi_chn_attr);
	ret |= RK_MPI_VI_EnableChn(0, channelId);
	if (ret)
	{
		printf("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}
	RK_MPI_VI_StartPipe(0);
	return ret;
}

int venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType)
{
	printf("%s\n", __func__);
	VENC_RECV_PIC_PARAM_S stRecvParam;
	VENC_CHN_ATTR_S stAttr;
	memset(&stAttr, 0, sizeof(VENC_CHN_ATTR_S));

	if (enType == RK_VIDEO_ID_AVC)
	{
		// stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
		// stAttr.stRcAttr.stH264Cbr.u32BitRate = 5* 1024;//5M
		// stAttr.stRcAttr.stH264Cbr.u32Gop = 60;
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
		stAttr.stRcAttr.stH264Vbr.u32Gop = 30;
		stAttr.stRcAttr.stH264Vbr.u32BitRate = 5 * 1024;
		stAttr.stRcAttr.stH264Vbr.u32MaxBitRate = 8 * 1024;
		stAttr.stRcAttr.stH264Vbr.u32MinBitRate = 2 * 1024;
	}
	else if (enType == RK_VIDEO_ID_HEVC)
	{
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
		stAttr.stRcAttr.stH265Cbr.u32BitRate = 10 * 1024;
		stAttr.stRcAttr.stH265Cbr.u32Gop = 60;
	}
	else if (enType == RK_VIDEO_ID_MJPEG)
	{
		stAttr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;
		stAttr.stRcAttr.stMjpegCbr.u32BitRate = 10 * 1024;
	}

	stAttr.stVencAttr.enType = enType;
	stAttr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	if (enType == RK_VIDEO_ID_AVC)
		stAttr.stVencAttr.u32Profile = H264E_PROFILE_HIGH;
	stAttr.stVencAttr.u32PicWidth = width;
	stAttr.stVencAttr.u32PicHeight = height;
	stAttr.stVencAttr.u32VirWidth = width;
	stAttr.stVencAttr.u32VirHeight = height;
	stAttr.stVencAttr.u32StreamBufCnt = 2;
	stAttr.stVencAttr.u32BufSize = width * height * 3 / 2;
	stAttr.stVencAttr.enMirror = MIRROR_NONE;

	RK_MPI_VENC_CreateChn(chnId, &stAttr);

	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = -1;
	RK_MPI_VENC_StartRecvFrame(chnId, &stRecvParam);

	return 0;
}
void Init_ISP(void)
{
	RK_BOOL multi_sensor = RK_FALSE;
	const char *iq_dir = "/etc/iqfiles";
	rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
	// hdr_mode = RK_AIQ_WORKING_MODE_ISP_HDR2;
	SAMPLE_COMM_ISP_Init(0, hdr_mode, multi_sensor, iq_dir);
	SAMPLE_COMM_ISP_Run(0);
}

void Init_VenC(void)
{

	// venc init
	RK_CODEC_ID_E enCodecType = RK_VIDEO_ID_AVC;
	venc_init(0, 1920, 1080, enCodecType);
	// bind
	MPP_CHN_S src;
	MPP_CHN_S dst;
	src.enModId = RK_ID_VI;
	src.s32ChnId = 0;
	src.s32DevId = 0;

	dst.enModId = RK_ID_VENC;
	dst.s32ChnId = 0;
	dst.s32DevId = 0;
	// RK_MPI_SYS_Bind(&src,&dst);
}

int main(int argc, char *argv[])
{

	/**
	 * 通过系统调用执行一个名为 RkLunch-stop.sh 的 Shell 脚本
	 * 该脚本的作用是停止 RK 平台上的一些服务，例如视频编码服务、视频解码服务等。
	 */
	system("RkLunch-stop.sh");

	// 1：初始化 ISP
	RK_BOOL multi_sensor = RK_FALSE;	 // 设置为单传感器模式（如果需要同时接入多个摄像头，需设为RK_TRUE）。
	const char *iq_dir = "/etc/iqfiles"; // 指定 IQ（Image Quality）配置文件的路径。这些文件是传感器和 ISP 的校准参数（如不同光线条件下的图像处理参数），由硬件厂商提供，用于适配具体摄像头型号，保证图像质量。
	/**此处为RK_AIQ_WORKING_MODE_NORMAL（普通模式）；注释部分是 HDR 模式（高动态范围，适合明暗对比强烈的场景），可根据需求切换。 */
	rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
	// hdr_mode = RK_AIQ_WORKING_MODE_ISP_HDR2;

	/**
	 * SAMPLE_COMM_ISP_Init(...)：瑞芯微 SDK 提供的 ISP 初始化函数，参数包括：
	 * 0：ISP 实例 ID（单 ISP 场景下通常为 0）；
	 * hdr_mode：工作模式；
	 * multi_sensor：是否多传感器；
	 * iq_dir：IQ 配置文件路径。
	 * 该函数会加载 IQ 参数、初始化 ISP 硬件寄存器、配置图像处理流程等。
	 * SAMPLE_COMM_ISP_Run(0)：启动 ISP 工作，开始接收摄像头的原始数据并进行实时处理
	 */
	SAMPLE_COMM_ISP_Init(0, hdr_mode, multi_sensor, iq_dir);
	SAMPLE_COMM_ISP_Run(0);
	printf("初始化 ISP 成功！\r\n");
	// 2：初始化 RKMPI
	if (RK_MPI_SYS_Init() != RK_SUCCESS)
	{
		RK_LOGE("rk mpi sys init fail!");
		return -1;
	}
	printf("初始化 MPI 系统成功！\r\n");

	vi_dev_init();
	printf("初始化VI成功");

	// 3：初始化 VI
	// FRAME_RATE_CTRL_S   stFrameRate = {};
	VI_CHN_ATTR_S viattr = {0};
	viattr.bFlip = RK_FALSE;   // 是否翻转
	viattr.bMirror = RK_FALSE; // 镜像
	viattr.enAllocBufType = VI_ALLOC_BUF_TYPE_INTERNAL;
	viattr.enCompressMode = COMPRESS_MODE_NONE;	 // 编码压缩模式
	viattr.enDynamicRange = DYNAMIC_RANGE_SDR10; // 动态范围
	viattr.enPixelFormat = RK_FMT_YUV420SP;		 // NV12
	// viattr.stFrameRate={30,1};//帧率
	viattr.stIspOpt.enCaptureType = VI_V4L2_CAPTURE_TYPE_VIDEO_CAPTURE;
	viattr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF; // DMA加速
	viattr.stIspOpt.stMaxSize = {1920, 1080};
	viattr.stIspOpt.stWindow = {0, 0, 1920, 1080};
	viattr.stIspOpt.u32BufCount = 3;
	viattr.stIspOpt.u32BufSize = 1920 * 1080 * 2;
	viattr.stSize.u32Height = 1080;
	viattr.stSize.u32Width = 1920;
	viattr.u32Depth = 3;
	int ret = RK_MPI_VI_SetChnAttr(0, 0, &viattr);
	ret |= RK_MPI_VI_EnableChn(0, 0);
	if (ret)
	{
		printf("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}

	Init_VenC();

	VIDEO_FRAME_INFO_S stViFrame;
	VENC_STREAM_S stFrame;
	stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
	int fps_count = 0;

	while (1)
	{

		RK_MPI_VI_GetChnFrame(0, 0, &stViFrame, -1);
		RK_MPI_VENC_SendFrame(0, &stViFrame, -1);
		ret = RK_MPI_VENC_GetStream(0, &stFrame, -1);
		if (ret == RK_SUCCESS)
		{

			printf("获取视频数据且编码成功 当前帧数据长度==%d 当前帧时间戳==%lld\r\n", stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS);
			//(uint8_t *)RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			RK_MPI_VI_ReleaseChnFrame(0, 0, &stViFrame);
			RK_MPI_VENC_ReleaseStream(0, &stFrame);
		}
	}

	return 0;
}
