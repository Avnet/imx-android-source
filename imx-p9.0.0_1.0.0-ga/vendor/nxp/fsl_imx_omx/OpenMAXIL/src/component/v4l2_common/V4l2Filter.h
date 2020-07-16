/**
 *  Copyright 2018 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

#ifndef V4L2FILTER_H
#define V4L2FILTER_H

#include "ComponentBase.h"
#include "Mem.h"
#include "OMX_ImageConvert.h"
#include "V4l2Device.h"
#include "V4l2Object.h"
#include "VThread.h" 
#include "FrameParser.h"
#include "FrameConverter.h"
#include <sys/time.h>
#include "Process3.h"
#include "DmaBuffer.h"

#define NUM_PORTS 2
#define IN_PORT   0
#define OUT_PORT  1

#define INPUT_BUFFER_FLAGS_EOS  0x01

#define Align(ptr,align)    (((OMX_U32)(ptr)+(align)-1)/(align)*(align))
#define FRAME_ALIGN     (256)
#define ASSERT(exp) if(!(exp)) {printf("%s: %d : assert condition !!!\r\n",__FUNCTION__,__LINE__);}

#define COLOR_FORMAT_FLAG_SINGLE_PLANE 0x01
#define COLOR_FORMAT_FLAG_2_PLANE       0x02

typedef enum
{
	V4L2_FILTER_STATE_IDLE=0,
	V4L2_FILTER_STATE_INIT,
	V4L2_FILTER_STATE_QUEUE_INPUT,
    V4L2_FILTER_STATE_START_INPUT,
    V4L2_FILTER_STATE_WAIT_RES,
    V4L2_FILTER_STATE_RES_CHANGED,
    V4L2_FILTER_STATE_FLUSHED,
    V4L2_FILTER_STATE_QUEUE_OUTPUT,
    V4L2_FILTER_STATE_START_OUTPUT,
    V4L2_FILTER_STATE_RUN,
    V4L2_FILTER_STATE_END,
    V4L2_FILTER_STATE_START_ENCODE,
    V4L2_FILTER_STATE_STOP_ENCODE,
}V4l2FilterState;

typedef struct {
    //OMX_BUFFERHEADERTYPE *pBufferHdr;
    OMX_S32 fd;
    OMX_S32 fd2;
}IonBufferMapper;


class V4l2Filter : public ComponentBase {
    public:
        explicit V4l2Filter();

        OMX_ERRORTYPE ComponentReturnBuffer(OMX_U32 nPortIndex);
        OMX_ERRORTYPE HandleFormatChanged(OMX_U32 nPortIndex);
        OMX_ERRORTYPE HandleFormatChangedForIon(OMX_U32 nPortIndex);
        OMX_ERRORTYPE HandleErrorEvent();
        OMX_ERRORTYPE HandleEOSEvent(OMX_U32 nPortIndex);
        OMX_ERRORTYPE HandleInObjBuffer();
        OMX_ERRORTYPE HandleOutObjBuffer();
        OMX_ERRORTYPE HandleSkipEvent();

        friend void *filterThreadHandler(void *arg);

        OMX_U32 ConvertOmxColorFormatToV4l2Format(OMX_COLOR_FORMATTYPE color_format,OMX_U32 flag);
        OMX_U32 ConvertOmxCodecFormatToV4l2Format(OMX_VIDEO_CODINGTYPE codec_format);
        OMX_BOOL ConvertV4l2FormatToOmxColorFormat(OMX_U32 v4l2_format,OMX_COLOR_FORMATTYPE *color_format);
        OMX_BOOL ConvertV4l2FormatToOmxCodecFormat(OMX_U32 v4l2_format,OMX_VIDEO_CODINGTYPE *codec_format);
        OMX_ERRORTYPE ReturnBuffer(OMX_BUFFERHEADERTYPE *pBufferHdr,OMX_U32 nPortIndex);

    protected:
        OMX_VIDEO_PORTDEFINITIONTYPE sInFmt;
        OMX_VIDEO_PORTDEFINITIONTYPE sOutFmt;
        OMX_U32 nInPortFormatCnt;
        OMX_COLOR_FORMATTYPE eInPortPormat[MAX_PORT_FORMAT_NUM];
        OMX_U32 nOutPortFormatCnt;
        OMX_COLOR_FORMATTYPE eOutPortPormat[MAX_PORT_FORMAT_NUM];
        OMX_U32 nInBufferCnt;
        OMX_U32 nInBufferSize;
        OMX_U32 nOutBufferCnt;
        OMX_U32 nOutBufferSize;

        OMX_ImageConvert* pImageConvert; 
        OMX_CONFIG_RECTTYPE sOutputCrop;
        OMX_U8 cRole[OMX_MAX_STRINGNAME_SIZE];
        OMX_VIDEO_CODINGTYPE CodingType;

        V4l2DEV_TYPE eDevType;
        OMX_S32 nFd;

        V4l2Dev* pV4l2Dev;
        
        V4l2Object *inObj;
        V4l2Object *outObj;

        FrameParser *pParser;
        OMX_BOOL bEnabledFrameParser;

        Process3 *pPreProcess;
        OMX_BOOL bEnabledPreProcess;//only used in encoder

        Process3 *pPostProcess;
        OMX_BOOL bEnabledPostProcess;//only used in decoder

        DmaBuffer * pDmaBuffer;
        OMX_BOOL bUseDmaBuffer;

        DmaBuffer * pDmaInputBuffer;
        OMX_BOOL bUseDmaInputBuffer;

        DmaBuffer * pDmaOutputBuffer;
        OMX_BOOL bUseDmaOutputBuffer;

        FrameConverter *pCodecDataConverter;
        OMX_BOOL bEnabledAVCCConverter;

        OMX_COLOR_FORMATTYPE eDmaBufferFormat;
        OMX_U32 nDmaBufferCnt;
        OMX_U32 nDmaBufferSize[3];

        OMX_BOOL bAdaptivePlayback;
        OMX_BOOL bRefreshIntra;
        OMX_BOOL bStoreMetaData;
        OMX_CONFIG_ROTATIONTYPE Rotation;
        OMX_U32 nInputPlane;
        OMX_U32 nOutputPlane;

        OMX_BOOL bInsertSpsPps2IDR;
        OMX_BOOL bSendPortSettingChanged;

        OMX_BOOL bSetInputBufferCount;
        OMX_BOOL bSetOutputBufferCount;
        
        OMX_U32 nInputCnt;
        OMX_U32 nOutputCnt;
        OMX_S32 nInBufferNum;
        OMX_S32 nOutBufferNum;
        OMX_VIDEO_WMVFORMATTYPE eWmvFormat;
    private:
        OMX_U8 devName[32];

        VThread *inThread;
        fsl_osal_mutex sMutex;

        V4l2FilterState eState;

        OMX_PTR pCodecData;
        OMX_U32 nCodecDataLen;


        OMX_BOOL bInputEOS;
        OMX_BOOL bOutputEOS;

        OMX_BOOL bOutputStarted;
        OMX_BOOL bResGot;

        OMX_BOOL bNewSegment;
        OMX_BOOL bSendCodecData;
        OMX_BOOL bDmaBufferAllocated;
        OMX_BOOL bAllocateFailed;

        OMX_S32 nDecodeOnly;

        OMX_U32 nErrCnt;

        OMX_PTR hTsHandle;// ENABLE_TS_MANAGER

        OMX_BUFFERHEADERTYPE *pCodecDataBufferHdr;
        OMX_U32 nWidthAlign;
        OMX_U32 nHeightAlign;

        OMX_ERRORTYPE InitComponent() override;
        OMX_ERRORTYPE DeInitComponent() override;
        OMX_ERRORTYPE SetDefaultSetting();
        virtual OMX_ERRORTYPE SetDefaultPortSetting() = 0;
        virtual OMX_ERRORTYPE ProcessInit() = 0;
        virtual OMX_ERRORTYPE GetInputDataDepthThreshold(OMX_S32* pDurationThr, OMX_S32* pBufCntThr);

        OMX_ERRORTYPE FlushComponent(OMX_U32 nPortIndex) override;
        OMX_ERRORTYPE PortFormatChanged(OMX_U32 nPortIndex) override;
        OMX_ERRORTYPE updateCropInfo(OMX_U32 nPortIndex);
        OMX_ERRORTYPE DoIdle2Loaded() override;
        OMX_ERRORTYPE DoLoaded2Idle() override;
        OMX_ERRORTYPE ProcessDataBuffer() override;
        OMX_ERRORTYPE ProcessInputBuffer();
        OMX_ERRORTYPE ProcessInBufferFlags(OMX_BUFFERHEADERTYPE *pInBufferHdr);
        OMX_ERRORTYPE ProcessOutputBuffer();
        OMX_ERRORTYPE ProcessPreProcessBuffer();
        void dumpBuffer(OMX_BUFFERHEADERTYPE *pBufferHdr,OMX_U32 nPortIndex);
        void dumpBuffer(DmaBufferHdr * hdr);

        OMX_ERRORTYPE ProcessPostBuffer();
        OMX_ERRORTYPE ProcessPreBuffer();
        OMX_ERRORTYPE AllocateDmaBuffer();
};

#endif//V4L2FILTER_H
/* File EOF */

