/**
 *  Copyright 2018 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

#include "V4l2Filter.h"
#ifdef ENABLE_TS_MANAGER
#include "Tsm_wrapper.h"
#endif

#if 0
#undef LOG_DEBUG
#define LOG_DEBUG printf
#undef LOG_LOG
#define LOG_LOG printf
#endif

//#define ENABLE_TILE_FORMAT
//#define FOR_CODA

//#define V4L2_DUMP_INPUT
//#define V4L2_DUMP_OUTPUT
//#define V4L2_DUMP_POST_PROCESS_IN

#define MAX_DUMP_FRAME  (200)

#define V4L2_DUMP_INPUT_FILE "/data/temp_in.bit"
#define V4L2_DUMP_OUTPUT_FILE "/data/temp_out.yuv"


//the value should be larger than 4 as surface flinger will keep some buffers while playing
//for some files, the min required buffer count is 1
#define V4L2_EXTRA_BUFFER_CNT (5)

typedef struct{
OMX_U32 v4l2_format;
OMX_U32 omx_format;
OMX_U32 flag;
}V4L2_FORMAT_TABLE;


static V4L2_FORMAT_TABLE codec_format_table[]={
/* compressed formats */
{V4L2_PIX_FMT_JPEG, OMX_VIDEO_CodingMJPEG,0},//V4L2_PIX_FMT_MJPEG
{V4L2_PIX_FMT_MPEG2, OMX_VIDEO_CodingMPEG2,0},
{V4L2_PIX_FMT_MPEG4, OMX_VIDEO_CodingMPEG4,0},
{V4L2_PIX_FMT_H263, OMX_VIDEO_CodingH263,0},
{V4L2_PIX_FMT_H264,  OMX_VIDEO_CodingAVC,0},
{V4L2_PIX_FMT_VP8, OMX_VIDEO_CodingVP8,0},
{V4L2_PIX_FMT_VC1_ANNEX_L, OMX_VIDEO_CodingWMV9,0},
//{V4L2_PIX_FMT_VC1_ANNEX_L, OMX_VIDEO_WMVFormatWVC1,0},
{v4l2_fourcc('H', 'E', 'V', 'C'),OMX_VIDEO_CodingHEVC,0},
{v4l2_fourcc('H', 'E', 'V', 'C'),11,0},//backup for vpu test
{v4l2_fourcc('D', 'I', 'V', 'X'), OMX_VIDEO_CodingXVID,0},
{v4l2_fourcc('V', 'P', '6', '0'), OMX_VIDEO_CodingVP6,0},
{v4l2_fourcc('D', 'I', 'V', 'X'), OMX_VIDEO_CodingDIVX,0},
{v4l2_fourcc('D', 'I', 'V', 'X'), OMX_VIDEO_CodingDIV3,0},
{v4l2_fourcc('D', 'I', 'V', 'X'), OMX_VIDEO_CodingDIV4,0},
{v4l2_fourcc('R', 'V', '0', '0'), OMX_VIDEO_CodingRV,0},
};

//need add more format
static V4L2_FORMAT_TABLE color_format_table[]={
//test for nv 12: V4L2_PIX_FMT_NV12
{V4L2_PIX_FMT_NV12, OMX_COLOR_FormatAndroidOpaque, COLOR_FORMAT_FLAG_2_PLANE},
{V4L2_PIX_FMT_NV12, OMX_COLOR_FormatYCbYCr,   COLOR_FORMAT_FLAG_2_PLANE},
{V4L2_PIX_FMT_NV12, OMX_COLOR_Format16bitRGB565,   COLOR_FORMAT_FLAG_2_PLANE},
{V4L2_PIX_FMT_NV12, OMX_COLOR_FormatYUV420SemiPlanar,COLOR_FORMAT_FLAG_2_PLANE},
//{V4L2_PIX_FMT_YUV420 , OMX_COLOR_FormatYUV420Planar,COLOR_FORMAT_FLAG_2_PLANE},
//{V4L2_PIX_FMT_NV12 , OMX_COLOR_FormatYUV420SemiPlanar,COLOR_FORMAT_FLAG_SINGLE_PLANE},
//{V4L2_PIX_FMT_NV12 , OMX_COLOR_FormatYUV420SemiPlanar,COLOR_FORMAT_FLAG_2_PLANE},
};

V4l2Filter::V4l2Filter()
{
    bInContext = OMX_FALSE;
    nPorts = NUM_PORTS;
    nFd = -1;
    inThread = NULL;
    inObj = NULL;
    outObj = NULL;
    pV4l2Dev = NULL;
    sMutex = NULL;
    bStoreMetaData = OMX_FALSE;
}
static OMX_ERRORTYPE PostProcessCallBack(Process3 *pPostProcess, OMX_PTR pAppData)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE *pOutputBufHdlr = NULL;

    if(pPostProcess == NULL || pAppData == NULL)
        return OMX_ErrorBadParameter;

    V4l2Filter * base = (V4l2Filter *)pAppData;
    LOG_LOG("PostProcessCallBack\n");

    if(OMX_ErrorNone == pPostProcess->GetOutputReturnBuffer(&pOutputBufHdlr)){
        base->ReturnBuffer(pOutputBufHdlr,OUT_PORT);
    }

    return OMX_ErrorNone;
}
static OMX_ERRORTYPE PreProcessCallBack(Process3 *pPreProcess, OMX_PTR pAppData)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE* pInputBufHdlr = NULL;

    if(pPreProcess == NULL || pAppData == NULL)
        return OMX_ErrorBadParameter;

    V4l2Filter * base = (V4l2Filter *)pAppData;
    LOG_LOG("PreProcessCallBack\n");

    if(OMX_ErrorNone == pPreProcess->GetInputReturnBuffer(&pInputBufHdlr)){
        if(pInputBufHdlr->nFlags & OMX_BUFFERFLAG_EOS)
            base->HandleEOSEvent(IN_PORT);
        base->ReturnBuffer(pInputBufHdlr,IN_PORT);
    }

    return OMX_ErrorNone;
}

void *filterThreadHandler(void *arg)
{
    V4l2Filter *base = (V4l2Filter*)arg;
    OMX_S32 ret = 0;
    OMX_S32 err = 0;
    OMX_BOOL bEOS = OMX_FALSE;
    OMX_U32 flag = base->bStoreMetaData ? V4L2_OBJECT_FLAG_METADATA_BUFFER:0;
    LOG_LOG("[%p]filterThreadHandler BEGIN \n",base);

    err = base->pV4l2Dev->Poll(base->nFd);
    LOG_LOG("[%p]filterThreadHandler END ret=%x \n",base,err);

    if(err & V4L2_DEV_POLL_RET_EVENT_RC){
        LOG_LOG("V4L2_DEV_POLL_RET_EVENT_RC \n");
        if(base->bEnabledPostProcess && base->bUseDmaBuffer)
            ret = base->HandleFormatChangedForIon(OUT_PORT);
        else
            ret = base->HandleFormatChanged(OUT_PORT);
        if(ret == OMX_ErrorStreamCorrupt)
            base->SendEvent(OMX_EventError, OMX_ErrorStreamCorrupt, 0, NULL);
    }

    if(err & V4L2_DEV_POLL_RET_EVENT_EOS){
        ret = base->HandleEOSEvent(OUT_PORT);
        LOG_LOG("V4L2_DEV_POLL_RET_EVENT_EOS ret=%d\n",ret);
    }
    if(err & V4L2_DEV_POLL_RET_EVENT_SKIP){
        ret = base->HandleSkipEvent();
        LOG_LOG("V4L2_DEV_POLL_RET_EVENT_SKIP \n");
    }

    if(base->bOutputEOS)
        return NULL;

    if(err & V4L2_DEV_POLL_RET_OUTPUT){
        LOG_LOG("V4L2_DEV_POLL_RET_OUTPUT \n");
        base->inObj->ProcessBuffer(flag);
    }

    if(base->inObj->HasOutputBuffer()){
        ret = base->HandleInObjBuffer();
        if(ret != OMX_ErrorNone)
            LOG_ERROR("HandleInObjBuffer err=%d",ret);
    }

    if(!base->bOutputStarted){
        fsl_osal_sleep(1000);
        return NULL;
    }

    if(err & V4L2_DEV_POLL_RET_CAPTURE){
        LOG_LOG("V4L2_DEV_POLL_RET_CAPTURE \n");
        base->outObj->ProcessBuffer(0);
    }

    if(base->outObj->HasOutputBuffer()){
        ret = base->HandleOutObjBuffer();
        if(ret != OMX_ErrorNone)
            LOG_ERROR("HandleOutObjBuffer err=%d",ret);
    }

    if(err & V4L2OBJECT_ERROR)
        base->HandleErrorEvent();

    return NULL;
}


OMX_ERRORTYPE V4l2Filter::InitComponent()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    pV4l2Dev = FSL_NEW(V4l2Dev,());
    if(pV4l2Dev == NULL)
        return OMX_ErrorInsufficientResources;

    ret = pV4l2Dev->LookupNode(eDevType,&devName[0]);
    LOG_LOG("[%p]InitComponent LookupNode ret=%x\n",this,ret);

    if(ret != OMX_ErrorNone)
        return ret;

    nFd = pV4l2Dev->Open(&devName[0]);
    LOG_LOG("pV4l2Dev->Open path=%s,fd=%d",devName, nFd);

    if(nFd < 0)
        return OMX_ErrorInsufficientResources;
        
    inObj = FSL_NEW(V4l2Object,());

    outObj = FSL_NEW(V4l2Object,());

    if(inObj == NULL || outObj == NULL)
        return OMX_ErrorInsufficientResources;

    if(eDevType == V4L2_DEV_TYPE_DECODER){
        nInputPlane = 1;
        if(pV4l2Dev->isV4lBufferTypeSupported(nFd,eDevType,V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE))
            ret = inObj->Create(nFd,V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,1);
        else if(pV4l2Dev->isV4lBufferTypeSupported(nFd,eDevType,V4L2_BUF_TYPE_VIDEO_OUTPUT))
            ret = inObj->Create(nFd,V4L2_BUF_TYPE_VIDEO_OUTPUT,1);

        if(ret != OMX_ErrorNone)
            return ret;

        if(pV4l2Dev->isV4lBufferTypeSupported(nFd,eDevType,V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)){
            nOutputPlane = 2;
            ret = outObj->Create(nFd,V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,nOutputPlane);
        }
        else if(pV4l2Dev->isV4lBufferTypeSupported(nFd,eDevType,V4L2_BUF_TYPE_VIDEO_CAPTURE)){
            nOutputPlane = 1;
            ret = outObj->Create(nFd,V4L2_BUF_TYPE_VIDEO_CAPTURE,nOutputPlane);
        }

    }else{
        if(pV4l2Dev->isV4lBufferTypeSupported(nFd,eDevType,V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)){
            nInputPlane = 2;
            ret = inObj->Create(nFd,V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,nInputPlane);
        }
        if(ret != OMX_ErrorNone)
            return ret;

        if(pV4l2Dev->isV4lBufferTypeSupported(nFd,eDevType,V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)){
            nOutputPlane = 1;
            ret = outObj->Create(nFd,V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,nOutputPlane);
        }

    }

    if(E_FSL_OSAL_SUCCESS != fsl_osal_mutex_init(&sMutex, fsl_osal_mutex_normal)) {
        ret = OMX_ErrorInsufficientResources;
        return ret;
    }

    if(ret != OMX_ErrorNone)
        return ret;

    inThread = FSL_NEW(VThread,());

    if(inThread == NULL)
        return OMX_ErrorInsufficientResources;
        
    ret = inThread->create(this,OMX_FALSE,filterThreadHandler);
    if(ret != OMX_ErrorNone)
        return ret;

    
    ret = SetDefaultSetting();
    if(ret != OMX_ErrorNone)
        return ret;

    LOG_LOG("InitComponent ret=%d\n",ret);
    return ret;
}
OMX_ERRORTYPE V4l2Filter::DeInitComponent()
{

    if(inThread){
        inThread->destroy();
        FSL_DELETE(inThread);
    }
    LOG_LOG("in thread destroy success\n");

    if(inObj){
        inObj->ReleaseResource();
        FSL_DELETE(inObj);
    }
    LOG_LOG("in object release success\n");
    if(outObj){
        outObj->ReleaseResource();
        FSL_DELETE(outObj);
    }
    LOG_LOG("out object release success\n");

    if(pV4l2Dev){
        pV4l2Dev->Close(nFd);
        FSL_DELETE(pV4l2Dev);
    }
    LOG_LOG("[%p]DeInitComponent\n",this);

    nOutputPlane = 1;

    if(sMutex != NULL)
        fsl_osal_mutex_destroy(sMutex);

    return OMX_ErrorNone;
}


OMX_ERRORTYPE V4l2Filter::SetDefaultSetting()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    OMX_VIDEO_PARAM_PORTFORMATTYPE sPortFormat;
    OMX_U32 bufCnt = 0;
    V4l2ObjectFormat sInFormat;
    V4l2ObjectFormat sOutFormat; 
    ret = SetDefaultPortSetting();

    if(ret != OMX_ErrorNone)
        return ret;

    if(OMX_ErrorNone == inObj->GetMinimumBufferCount(&bufCnt)){
        if(bufCnt == 0)
            bufCnt = 1;
        if(nInBufferCnt < bufCnt)
            nInBufferCnt = bufCnt;
    }
    if(OMX_ErrorNone == outObj->GetMinimumBufferCount(&bufCnt)){
        if(bufCnt == 0)
            bufCnt = 3;
        if(nOutBufferCnt < bufCnt)
            nOutBufferCnt = bufCnt;
    }

    OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
    sPortDef.nPortIndex = IN_PORT;
    sPortDef.eDir = OMX_DirInput;
    sPortDef.eDomain = OMX_PortDomainVideo;

    #ifndef FOR_CODA
    sInFormat.width = sInFmt.nFrameWidth;
    sInFormat.height = sInFmt.nFrameHeight;
    sInFormat.stride = sInFmt.nStride;
    sInFormat.plane_num = 1;
    sInFormat.bufferSize[0] = nInBufferSize;

    if(sInFmt.eColorFormat != OMX_COLOR_FormatUnused)
        sInFormat.format = ConvertOmxColorFormatToV4l2Format(sInFmt.eColorFormat,COLOR_FORMAT_FLAG_2_PLANE);
    else if(sInFmt.eCompressionFormat != OMX_VIDEO_CodingUnused)
        sInFormat.format = ConvertOmxCodecFormatToV4l2Format(sInFmt.eCompressionFormat);

    ret = inObj->SetFormat(&sInFormat);
    if(ret != OMX_ErrorNone)
        ret = OMX_ErrorNone;

    ret = inObj->GetFormat(&sInFormat);

    if(ret == OMX_ErrorNone){
        if(sInFormat.width > 0)
            sInFmt.nFrameWidth = sInFormat.width;
        if(sInFormat.height > 0)
            sInFmt.nFrameHeight = sInFormat.height;
        if(sInFormat.stride > 0)
            sInFmt.nStride = sInFormat.stride;
        if(sInFormat.bufferSize[0] > 0)
            nInBufferSize = sInFormat.bufferSize[0];
    }
    sOutFormat.width = sOutFmt.nFrameWidth;
    sOutFormat.height = sOutFmt.nFrameHeight;
    sOutFormat.stride= sOutFmt.nStride;
    sOutFormat.plane_num = 1;
    sOutFormat.bufferSize[0] = nOutBufferSize;
    if(sOutFmt.eColorFormat != OMX_COLOR_FormatUnused)
        sOutFormat.format = ConvertOmxColorFormatToV4l2Format(sOutFmt.eColorFormat,COLOR_FORMAT_FLAG_2_PLANE);
    else if(sOutFmt.eCompressionFormat != OMX_VIDEO_CodingUnused)
        sOutFormat.format = ConvertOmxCodecFormatToV4l2Format(sOutFmt.eCompressionFormat);

    ret = outObj->SetFormat(&sOutFormat);
    if(ret != OMX_ErrorNone)
        ret = OMX_ErrorNone;

    #endif
    fsl_osal_memcpy(&sPortDef.format.video, &sInFmt, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));

    sPortDef.format.video.nStride = sPortDef.format.video.nFrameWidth;
    sPortDef.format.video.nSliceHeight= sPortDef.format.video.nFrameHeight;

    sPortDef.bPopulated = OMX_FALSE;
    sPortDef.bEnabled = OMX_TRUE;
    sPortDef.nBufferCountMin = nInBufferCnt;
    sPortDef.nBufferCountActual = nInBufferCnt;
    sPortDef.nBufferSize = nInBufferSize;
    ret = ports[IN_PORT]->SetPortDefinition(&sPortDef);
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("Set port definition for in port failed.\n");
        return ret;
    }
    LOG_DEBUG("in format w=%d,h=%d, bufferSize=%d format=%d\n",sInFmt.nFrameWidth,sInFmt.nFrameHeight,nInBufferSize,sPortDef.format.video.eCompressionFormat);
    for (OMX_U32 i=0; i<nInPortFormatCnt; i++) {
        OMX_INIT_STRUCT(&sPortFormat, OMX_VIDEO_PARAM_PORTFORMATTYPE);
        sPortFormat.nPortIndex = IN_PORT;
        sPortFormat.nIndex = i;
        sPortFormat.eCompressionFormat = sPortDef.format.video.eCompressionFormat;
        sPortFormat.eColorFormat = eInPortPormat[i];
        sPortFormat.xFramerate = sPortDef.format.video.xFramerate;
        LOG_DEBUG("Set support color format: %d\n", eInPortPormat[i]);
        ret = ports[IN_PORT]->SetPortFormat(&sPortFormat);
        if(ret != OMX_ErrorNone) {
            LOG_ERROR("Set port format for in port failed.\n");
            return ret;
        }
    }

    sPortDef.nPortIndex = OUT_PORT;
    sPortDef.eDir = OMX_DirOutput;
    sPortDef.nBufferCountMin = nOutBufferCnt;
    sPortDef.nBufferCountActual = nOutBufferCnt;

    #if 0
    ret = outObj->GetFormat(&sOutFormat);

    if(ret == OMX_ErrorNone){
        if(sOutFormat.width > 0)
            sOutFmt.nFrameWidth = sOutFormat.width;
        if(sOutFormat.height > 0)
            sOutFmt.nFrameHeight = sOutFormat.height;
        if(sOutFormat.stride > 0)
            sOutFmt.nStride = sOutFormat.stride;
        if(sOutFormat.bufferSize > 0)
            nOutBufferSize = sOutFormat.bufferSize;
    }
    #endif

    fsl_osal_memcpy(&sPortDef.format.video, &sOutFmt, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));


    sPortDef.format.video.nStride = sPortDef.format.video.nFrameWidth;
    sPortDef.format.video.nSliceHeight= sPortDef.format.video.nFrameHeight;

    sPortDef.nBufferSize=nOutBufferSize;
    ret = ports[OUT_PORT]->SetPortDefinition(&sPortDef);
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("Set port definition for out port failed.\n");
        return ret;
    }
    LOG_DEBUG("default out format w=%d,h=%d,format=%d\n",sOutFmt.nFrameWidth,sOutFmt.nFrameHeight,sPortDef.format.video.eColorFormat);

    for (OMX_U32 i=0; i<nOutPortFormatCnt; i++) {
        OMX_INIT_STRUCT(&sPortFormat, OMX_VIDEO_PARAM_PORTFORMATTYPE);
        sPortFormat.nPortIndex = OUT_PORT;
        sPortFormat.nIndex = i;
        sPortFormat.eCompressionFormat = sPortDef.format.video.eCompressionFormat;
        sPortFormat.eColorFormat = eOutPortPormat[i];
        sPortFormat.xFramerate = sPortDef.format.video.xFramerate;
        LOG_DEBUG("Set support color format: %d\n", eOutPortPormat[i]);
        ret = ports[OUT_PORT]->SetPortFormat(&sPortFormat);
        if(ret != OMX_ErrorNone) {
            LOG_ERROR("Set port format for in port failed.\n");
            return ret;
        }
    }

    pImageConvert = NULL;
    fsl_osal_memset(&sOutputCrop,0,sizeof(OMX_CONFIG_RECTTYPE));

    pCodecData = NULL;
    nCodecDataLen = 0;

    bSetInputBufferCount = OMX_FALSE;
    bSetOutputBufferCount = OMX_FALSE;
    bInputEOS = OMX_FALSE;
    bOutputEOS = OMX_FALSE;
    bOutputStarted = OMX_FALSE;

    bNewSegment = OMX_FALSE;

    nDecodeOnly = 0; 
    nInBufferNum = 0;
    nOutBufferNum = 0;

    LOG_LOG("SetDefaultSetting SUCCESS\n");
    nOutputCnt = 0;
    nInputCnt = 0;
    nErrCnt = 0;
    pParser = NULL;
    bEnabledFrameParser = OMX_FALSE;

    pPreProcess = NULL;
    bEnabledPreProcess = OMX_FALSE;
    pPostProcess = NULL;
    bEnabledPostProcess = OMX_FALSE;

    bAdaptivePlayback = OMX_FALSE;
    bRefreshIntra = OMX_FALSE;
    fsl_osal_memset(&Rotation,0,sizeof(OMX_CONFIG_ROTATIONTYPE));

    bSendCodecData = OMX_FALSE;
    pCodecDataBufferHdr = NULL;


    bEnabledAVCCConverter = OMX_FALSE;
    pCodecDataConverter = NULL;

    bInsertSpsPps2IDR = OMX_FALSE;
    bResGot = OMX_FALSE;
    eState = V4L2_FILTER_STATE_IDLE;

    hTsHandle = NULL;

    pDmaBuffer = NULL;
    bDmaBufferAllocated = OMX_FALSE;
    bAllocateFailed = OMX_FALSE;

    eWmvFormat = (enum OMX_VIDEO_WMVFORMATTYPE)0;

    nWidthAlign = 1;
    nHeightAlign = 1;
    if(eDevType == V4L2_DEV_TYPE_DECODER){
        nWidthAlign = FRAME_ALIGN;
        nHeightAlign = FRAME_ALIGN;
    }

    return ret;

}
OMX_U32 V4l2Filter::ConvertOmxColorFormatToV4l2Format(OMX_COLOR_FORMATTYPE color_format,OMX_U32 flag)
{
    OMX_U32 i=0;
    OMX_BOOL bGot=OMX_FALSE;
    OMX_U32 out = 0;
    for(i = 0; i < sizeof(color_format_table)/sizeof(V4L2_FORMAT_TABLE);i++){
        if(color_format == color_format_table[i].omx_format && (flag & color_format_table[i].flag)){
            bGot = OMX_TRUE;
            out = color_format_table[i].v4l2_format;
            break;
        }
    }

    if(bGot)
        return out;
    else
        return 0;
}
OMX_U32 V4l2Filter::ConvertOmxCodecFormatToV4l2Format(OMX_VIDEO_CODINGTYPE codec_format)
{
    OMX_U32 i=0;
    OMX_BOOL bGot=OMX_FALSE;
    OMX_U32 out = 0;
    for(i = 0; i < sizeof(codec_format_table)/sizeof(V4L2_FORMAT_TABLE);i++){
        if(codec_format == codec_format_table[i].omx_format){
            bGot = OMX_TRUE;
            out = codec_format_table[i].v4l2_format;
            break;
        }
    }
    if(bGot && eWmvFormat != 0){
        if(eWmvFormat == OMX_VIDEO_WMVFormatWVC1)
            out = V4L2_PIX_FMT_VC1_ANNEX_G;
    }

    if(bGot)
        return out;
    else
        return 0;
}
OMX_BOOL V4l2Filter::ConvertV4l2FormatToOmxColorFormat(OMX_U32 v4l2_format,OMX_COLOR_FORMATTYPE *color_format)
{
    OMX_U32 i=0;
    OMX_BOOL bGot=OMX_FALSE;
    OMX_U32 out = 0;
    for(i = 0; i < sizeof(color_format_table)/sizeof(V4L2_FORMAT_TABLE);i++){
        if(v4l2_format == color_format_table[i].v4l2_format){
            bGot = OMX_TRUE;
            *color_format = (OMX_COLOR_FORMATTYPE)color_format_table[i].omx_format;
            break;
        }
    }

    return bGot;
}
OMX_BOOL V4l2Filter::ConvertV4l2FormatToOmxCodecFormat(OMX_U32 v4l2_format,OMX_VIDEO_CODINGTYPE *codec_format)
{
    OMX_U32 i=0;
    OMX_BOOL bGot=OMX_FALSE;
    OMX_U32 out = 0;
    for(i = 0; i < sizeof(codec_format_table)/sizeof(V4L2_FORMAT_TABLE);i++){
        if(v4l2_format == codec_format_table[i].v4l2_format){
            bGot = OMX_TRUE;
            *codec_format = (OMX_VIDEO_CODINGTYPE)codec_format_table[i].omx_format;
            break;
        }
    }

    return bGot;
}

OMX_ERRORTYPE V4l2Filter::PortFormatChanged(OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    V4l2ObjectFormat sFormat;
    OMX_COLOR_FORMATTYPE color_format = OMX_COLOR_FormatUnused;
    OMX_VIDEO_CODINGTYPE codec_format = OMX_VIDEO_CodingUnused;
    LOG_LOG("PortFormatChanged index=%d\n",nPortIndex);
    OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);

    sPortDef.nPortIndex = nPortIndex;
    ret = ports[nPortIndex]->GetPortDefinition(&sPortDef);
    if(ret != OMX_ErrorNone){
        LOG_ERROR("GetPortDefinition FAILED\n");
        return ret;
    }
    sFormat.format = 0;
    sFormat.bufferSize[0] = sFormat.bufferSize[1] = sFormat.bufferSize[2] = 0;

    sFormat.width = sPortDef.format.video.nFrameWidth;
    sFormat.height = sPortDef.format.video.nFrameHeight;

    if(sPortDef.format.video.eColorFormat != OMX_COLOR_FormatUnused){
        OMX_U32 flag = COLOR_FORMAT_FLAG_SINGLE_PLANE;
        if(nPortIndex == OUT_PORT && nOutputPlane == 2)
            flag = COLOR_FORMAT_FLAG_2_PLANE;
        if(nPortIndex == IN_PORT && nInputPlane == 2)
            flag = COLOR_FORMAT_FLAG_2_PLANE;

        sFormat.format = ConvertOmxColorFormatToV4l2Format(sPortDef.format.video.eColorFormat,flag);
        LOG_LOG("PortFormatChanged eColorFormat format=%x\n",sPortDef.format.video.eColorFormat);
    }
    else if(sPortDef.format.video.eCompressionFormat != OMX_VIDEO_CodingUnused){
        LOG_LOG("PortFormatChanged sPortDef.format.video.eCompressionFormat=%d",sPortDef.format.video.eCompressionFormat);
        sFormat.format = ConvertOmxCodecFormatToV4l2Format(sPortDef.format.video.eCompressionFormat);
        LOG_LOG("PortFormatChanged sFormat.format=%d",sFormat.format);
    }else{
        LOG_ERROR("sPortDef.format.video.eCompressionFormat =%d",sPortDef.format.video.eCompressionFormat);
    }

    if(eDevType == V4L2_DEV_TYPE_DECODER && nPortIndex == OUT_PORT){
        nWidthAlign = FRAME_ALIGN;
        nHeightAlign = FRAME_ALIGN;
    }else if(eDevType == V4L2_DEV_TYPE_ENCODER && nPortIndex == IN_PORT){
        OMX_U32 width = 0;
        OMX_U32 height = 0;
        if(OMX_ErrorNone == pV4l2Dev->GetFrameAlignment(nFd,sFormat.format, &width, &height)){
            nWidthAlign = width;
            nHeightAlign = height;
        }
    }
    sFormat.stride = Align(sFormat.width, nWidthAlign);
    if(eDevType == V4L2_DEV_TYPE_ENCODER){
        sFormat.width = Align(sFormat.width, nWidthAlign);
        sFormat.height = Align(sFormat.height, nHeightAlign);
    }

    LOG_LOG("PortFormatChanged w=%d,h=%d,s=%d,format=%x\n",sFormat.width,sFormat.height,sFormat.stride,sFormat.format);

    if(nPortIndex == IN_PORT){
        LOG_LOG("PortFormatChanged IN_PORT buffer size 0=%d,sPortDef.nBufferSize=%d\n",nInBufferSize,sPortDef.nBufferSize);

        //when enable bStoreMetaData, port buffer size is not actual frame buffer size.
        if(!bStoreMetaData){
            sFormat.bufferSize[0] = nInBufferSize = sPortDef.nBufferSize;
            sFormat.plane_num = nInputPlane;
        }else{
            sFormat.bufferSize[0] = nInBufferSize;
            sFormat.plane_num = 1;
        }

        LOG_LOG("inObj->SetFormat buffer size=%d",sFormat.bufferSize[0]);
        ret = inObj->SetFormat(&sFormat);
        if(ret != OMX_ErrorNone)
            return ret;

        ret = inObj->GetFormat(&sFormat);
        if(ret != OMX_ErrorNone)
            return ret;

        LOG_LOG("PortFormatChanged IN_PORT buffer size 1=%d\n",sFormat.bufferSize[0]);
        
        sInFmt.nFrameWidth = sFormat.width;
        sInFmt.nFrameHeight = sFormat.height;
        sInFmt.nStride = sFormat.stride;
        sInFmt.nSliceHeight = sFormat.height;

        if(0 == sInFmt.nStride)
            sInFmt.nStride = sFormat.width;

        if(ConvertV4l2FormatToOmxColorFormat(sFormat.format,&color_format))
            sInFmt.eColorFormat = color_format;
        else if(ConvertV4l2FormatToOmxCodecFormat(sFormat.format,&codec_format))
            sInFmt.eCompressionFormat = codec_format;

        if(eDevType == V4L2_DEV_TYPE_ENCODER && !bStoreMetaData)
            sInFmt.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;

        nInBufferCnt = sPortDef.nBufferCountActual;

        nInBufferSize = sFormat.bufferSize[0] + sFormat.bufferSize[1] + sFormat.bufferSize[2];
        LOG_LOG("PortFormatChanged  sInFmt.eColorFormat=%x,sInFmt.eCompressionFormat=%x\n",sInFmt.eColorFormat,sInFmt.eCompressionFormat);
        fsl_osal_memcpy(&sPortDef.format.video, &sInFmt, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
        sPortDef.nBufferCountActual = nInBufferCnt;

        if(!bStoreMetaData)
            sPortDef.nBufferSize = nInBufferSize;
        LOG_LOG("PortFormatChanged IN_PORT buffer size=%d nInBufferCnt=%d\n",nInBufferSize,nInBufferCnt);

    }else if(nPortIndex == OUT_PORT){

        OMX_U32 pad_width = Align(sFormat.width, nWidthAlign);
        OMX_U32 pad_height = Align(sFormat.height, nHeightAlign);

        nOutBufferSize = pad_width * pad_height * pxlfmt2bpp(sOutFmt.eColorFormat) / 8;
        if(nOutBufferSize < sPortDef.nBufferSize)
            nOutBufferSize = sPortDef.nBufferSize;

        sFormat.bufferSize[0] = nOutBufferSize;

        //use one plane_num for decoder output port. size of each plane is calculated by outObj
        if(eDevType == V4L2_DEV_TYPE_DECODER)
            sFormat.plane_num = 1;
        else
            sFormat.plane_num = nOutputPlane;

        if(eDevType == V4L2_DEV_TYPE_DECODER && sFormat.format == 0){
            //set default format for decoder
            sFormat.format = V4L2_PIX_FMT_NV12;
        }

        LOG_DEBUG("set OUT_PORT format %x,width=%d,height=%d,stride=%d,bufferSize=%d\n",sFormat.format,sFormat.width,
            sFormat.height,sFormat.stride,sFormat.bufferSize);

        LOG_DEBUG("set nOutBufferSize =%d",nOutBufferSize);
        ret = outObj->SetFormat(&sFormat);
        #ifndef FOR_CODA
        if(ret != OMX_ErrorNone)
            return ret;
        #endif

        ret = outObj->GetFormat(&sFormat);
        if(ret != OMX_ErrorNone)
            return ret;

        LOG_DEBUG("PortFormatChanged modify buffer size to=%d,%d, format=%d,w=%d,h=%d,stride=%d\n",
            sFormat.width,sFormat.height,sFormat.format,sFormat.width,sFormat.height,sFormat.stride);

        sOutFmt.nFrameWidth = pad_width;
        sOutFmt.nFrameHeight = pad_height;

        sOutFmt.nStride = sFormat.stride;
        if(0 == sOutFmt.nStride)
            sOutFmt.nStride = sOutFmt.nFrameWidth;
        sOutFmt.nSliceHeight = sOutFmt.nFrameHeight;

        if(ConvertV4l2FormatToOmxColorFormat(sFormat.format,&color_format)){
            sOutFmt.eColorFormat = color_format;
        }else if(ConvertV4l2FormatToOmxCodecFormat(sFormat.format,&codec_format)){
            sOutFmt.eCompressionFormat = codec_format;
        }
        LOG_LOG("PortFormatChanged  sOutFmt.eColorFormat=%x,sOutFmt.eCompressionFormat=%x\n",sOutFmt.eColorFormat,sOutFmt.eCompressionFormat);

        LOG_DEBUG("PortFormatChanged buffer count=%d",sPortDef.nBufferCountActual);
        nOutBufferCnt = sPortDef.nBufferCountActual;

        #ifdef MALONE_VPU
        if(sFormat.format == V4L2_PIX_FMT_NV12 && sOutFmt.eColorFormat == OMX_COLOR_FormatAndroidOpaque){
            sOutFmt.eColorFormat = OMX_COLOR_FormatYCbYCr;
        }
        #endif

        fsl_osal_memcpy(&sPortDef.format.video, &sOutFmt, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
        sPortDef.nBufferCountActual = nOutBufferCnt;

        sPortDef.nBufferSize = nOutBufferSize;
        
        LOG_DEBUG("PortFormatChanged update output buffer cnt=%d, size=%d\n",nOutBufferCnt,nOutBufferSize);

    }
    sPortDef.eDomain = OMX_PortDomainVideo;
    LOG_DEBUG("SetPortDefinition STRIDE=%d,SliceHeight=%d",sPortDef.format.video.nStride,sPortDef.format.video.nSliceHeight);
    ret = ports[nPortIndex]->SetPortDefinition(&sPortDef);

    if(ret != OMX_ErrorNone)
        return ret;

    ret = updateCropInfo(nPortIndex);
    if(ret != OMX_ErrorNone)
        return ret;

    if(nPortIndex == IN_PORT && bEnabledFrameParser && pParser != NULL)
        pParser->Reset(sInFmt.eCompressionFormat);

    return OMX_ErrorNone;

}

OMX_ERRORTYPE V4l2Filter::updateCropInfo(OMX_U32 nPortIndex){
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_VIDEO_PORTDEFINITIONTYPE *pFmt = NULL;

    if(nPortIndex == OUT_PORT){
        pFmt = &sOutFmt;
    }else if(nPortIndex == IN_PORT){
        pFmt = &sInFmt;
    }else{
        ret = OMX_ErrorBadParameter;
        return ret;
    }

    if(sOutputCrop.nWidth != pFmt->nFrameWidth ||
        sOutputCrop.nHeight != pFmt->nFrameHeight){

        sOutputCrop.nWidth = pFmt->nFrameWidth;
        sOutputCrop.nHeight = pFmt->nFrameHeight;
        sOutputCrop.nLeft = 0;
        sOutputCrop.nTop = 0;
        LOG_LOG("updateCropInfo w=%d,h=%d",sOutputCrop.nWidth,sOutputCrop.nHeight);

        ret = outObj->SetCrop(&sOutputCrop);
        if(ret != OMX_ErrorNone)
           return ret;

        ret = outObj->GetCrop(&sOutputCrop);
    }

    return ret;
}
OMX_ERRORTYPE V4l2Filter::FlushComponent(OMX_U32 nPortIndex)
{
    OMX_BUFFERHEADERTYPE * bufHdlr;
    LOG_LOG("FlushComponent index=%d,in num=%d,out num=%d\n",nPortIndex,ports[IN_PORT]->BufferNum(),ports[OUT_PORT]->BufferNum());

    if(nPortIndex == IN_PORT){
        bInputEOS = OMX_FALSE;
        if(bEnabledPreProcess){
            OMX_BUFFERHEADERTYPE *pBufHdr = NULL;
            DmaBufferHdr *pDmaBufHdr = NULL;
            pPreProcess->Flush();
            while(OMX_ErrorNone == pPreProcess->GetInputBuffer(&pBufHdr) && pBufHdr != NULL)
                ReturnBuffer(pBufHdr,IN_PORT);

            while(OMX_ErrorNone == pPreProcess->GetInputReturnBuffer(&pBufHdr) && pBufHdr != NULL)
                ReturnBuffer(pBufHdr,IN_PORT);

            while(OMX_ErrorNone == pPreProcess->GetOutputReturnBuffer(&pDmaBufHdr) && pDmaBufHdr != NULL)
                pDmaBuffer->Add(pDmaBufHdr);
        }

        #ifdef ENABLE_TS_MANAGER
        tsmFlush(hTsHandle);
        #endif

        inObj->Flush();
        nInputCnt = 0;
        while(inObj->HasBuffer()){
            OMX_BUFFERHEADERTYPE *pBufHdr = NULL;
            DmaBufferHdr *pDmaBufHdr = NULL;
            if(!bEnabledPreProcess){
                if(OMX_ErrorNone == inObj->GetBuffer(&pBufHdr)){
                        ReturnBuffer(pBufHdr,IN_PORT);
                }
            }else{
                if(OMX_ErrorNone == inObj->GetBuffer(&pDmaBufHdr)){
                      pDmaBuffer->Add(pDmaBufHdr);
                }
            }
        }
        while(ports[IN_PORT]->BufferNum() > 0) {
            ports[IN_PORT]->GetBuffer(&bufHdlr);
            if(bufHdlr != NULL)
                ports[IN_PORT]->SendBuffer(bufHdlr);
        }
    }

    if(nPortIndex == OUT_PORT){
        //inThread->pause();
        bOutputEOS = OMX_FALSE;
        outObj->Flush();
        bOutputStarted = OMX_FALSE;
        nOutputCnt = 0;

        if(bEnabledPostProcess){

            pPostProcess->Flush();
            LOG_LOG("bEnabledPostProcess flush 2 \n");
            
            if(bUseDmaBuffer){
                DmaBufferHdr *pBufHdr = NULL;
                while(outObj->HasBuffer()){
                    if(OMX_ErrorNone == outObj->GetBuffer(&pBufHdr)){
                        pBufHdr->bReadyForProcess = OMX_FALSE;
                        pBufHdr->flag = 0;
                        if(bDmaBufferAllocated)
                            pDmaBuffer->Add(pBufHdr);
                    }
                }
            }

            while(bUseDmaBuffer){
                DmaBufferHdr *pBufHdr = NULL;
                if(OMX_ErrorNone == pPostProcess->GetInputReturnBuffer(&pBufHdr)){
                    pBufHdr->bReadyForProcess = OMX_FALSE;
                    pBufHdr->flag = 0;
                    if(bDmaBufferAllocated)
                        pDmaBuffer->Add(pBufHdr);
                }else
                    break;
            }
 
            while(1){
                if(OMX_ErrorNone == pPostProcess->GetOutputReturnBuffer(&bufHdlr)){
                    ReturnBuffer(bufHdlr,OUT_PORT);
                }else
                    break;
            }
        }

        if(!bSendCodecData && pCodecDataBufferHdr != NULL){
            ports[OUT_PORT]->SendBuffer(pCodecDataBufferHdr);
            pCodecDataBufferHdr = NULL;
            LOG_DEBUG("FlushComponent send codec data buffer hdr\n");
        }
        bSendCodecData = OMX_FALSE;

        if(bEnabledPostProcess && bUseDmaBuffer){
            DmaBufferHdr *pBufHdr = NULL;
            while(outObj->HasBuffer()){
                if(OMX_ErrorNone == outObj->GetBuffer(&pBufHdr)){
                    pBufHdr->bReadyForProcess = OMX_FALSE;
                    pBufHdr->flag = 0;
                    if(bDmaBufferAllocated)
                        pDmaBuffer->Add(pBufHdr);
                }
            }
        }else{
            while(outObj->HasBuffer()){
                if(OMX_ErrorNone == outObj->GetBuffer(&bufHdlr)){
                    ReturnBuffer(bufHdlr,OUT_PORT);
                }
            }
        }

        LOG_DEBUG("FlushComponent index=1 port num=%d\n",ports[OUT_PORT]->BufferNum());
        while(ports[OUT_PORT]->BufferNum() > 0) {
            ports[OUT_PORT]->GetBuffer(&bufHdlr);
            if(bufHdlr != NULL)
                ports[OUT_PORT]->SendBuffer(bufHdlr);
        }

        eState = V4L2_FILTER_STATE_FLUSHED;

    }
    nErrCnt = 0;
    LOG_DEBUG("FlushComponent index=%d END\n",nPortIndex);
    return OMX_ErrorNone;
}
OMX_ERRORTYPE V4l2Filter::DoIdle2Loaded()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    LOG_LOG("DoIdle2Loaded BEGIN");

    if(pImageConvert)
        pImageConvert->delete_it(pImageConvert);
        
    inThread->pause();
    bOutputStarted = OMX_FALSE;
    bInputEOS = OMX_FALSE;
    bOutputEOS = OMX_FALSE;

    FSL_FREE(pCodecData);

    FSL_DELETE(pParser);

    if(pPreProcess != NULL){
        pPreProcess->Destroy();
        FSL_DELETE(pPreProcess);
    }

    if(pCodecDataConverter!= NULL){
        pCodecDataConverter->Destroy();
        FSL_DELETE(pCodecDataConverter);
    }

    if(pPostProcess != NULL){
        pPostProcess->Destroy();
        FSL_DELETE(pPostProcess);
    }

    if(pDmaBuffer != NULL){
        pDmaBuffer->FreeAll();
        FSL_DELETE(pDmaBuffer);
        bDmaBufferAllocated = OMX_FALSE;
    }

    if(pDmaInputBuffer != NULL){
        FSL_DELETE(pDmaInputBuffer);
        bUseDmaInputBuffer = OMX_FALSE;
    }

    if(pDmaOutputBuffer != NULL){
        FSL_DELETE(pDmaOutputBuffer);
        bUseDmaOutputBuffer = OMX_FALSE;
    }

    #ifdef ENABLE_TS_MANAGER
    tsmDestroy(hTsHandle);
    #endif

    //free driver buffers
    if(bSetInputBufferCount)
        inObj->SetBufferCount(0, V4L2_MEMORY_MMAP, 1);
    if(bSetOutputBufferCount)
        outObj->SetBufferCount(0, V4L2_MEMORY_MMAP, nOutputPlane);

    ret=SetDefaultSetting();

    LOG_LOG("DoIdle2Loaded ret=%x",ret);

    return ret;
}
OMX_ERRORTYPE V4l2Filter::DoLoaded2Idle()
{
    #ifdef ENABLE_TS_MANAGER
    hTsHandle = tsmCreate();
    if(hTsHandle == NULL) {
        LOG_ERROR("Create Ts manager failed.\n");
        return OMX_ErrorUndefined;
    }
    #endif
    return OMX_ErrorNone;

}

OMX_ERRORTYPE V4l2Filter::ProcessDataBuffer()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    switch(eState){
        case V4L2_FILTER_STATE_IDLE:
            LOG_LOG("V4l2Filter eState V4L2_FILTER_STATE_IDLE \n");
            eState = V4L2_FILTER_STATE_INIT;
            break;
        case V4L2_FILTER_STATE_INIT:
            LOG_LOG("V4l2Filter eState V4L2_FILTER_STATE_INIT \n");
            ret = ProcessInit();
            if(ret != OMX_ErrorNone)
                break;

            if(bEnabledPostProcess){
                if(pPostProcess != NULL){
                    PROCESS3_CALLBACKTYPE * callback = &PostProcessCallBack;
                    pPostProcess->SetCallbackFunc(callback,this);
                }else{
                    LOG_ERROR("bEnabledPostProcess BUT pPostProcess is NULL");
                    return OMX_ErrorResourcesLost;
                }
            }

            if(bEnabledPreProcess){
                if(pPreProcess != NULL){
                    PROCESS3_CALLBACKTYPE * callback = &PreProcessCallBack;
                    pPreProcess->SetCallbackFunc(callback,this);
                }else{
                    LOG_ERROR("bEnabledPreProcess BUT pPreProcess is NULL");
                    return OMX_ErrorResourcesLost;
                }
            }

            if(V4L2_DEV_TYPE_DECODER == eDevType)
                eState = V4L2_FILTER_STATE_QUEUE_INPUT;
            else
                eState = V4L2_FILTER_STATE_START_ENCODE;
            break;

        case V4L2_FILTER_STATE_QUEUE_INPUT:
            LOG_LOG("V4l2Filter eState V4L2_FILTER_STATE_QUEUE_INPUT \n");
            ret = ProcessInputBuffer();

            if(ret == OMX_ErrorNoMore && bInputEOS == OMX_TRUE)
                ret = OMX_ErrorNone;

            if(ret != OMX_ErrorNone)
                break;
            if(nInputCnt == nInBufferCnt || bInputEOS == OMX_TRUE)
                eState = V4L2_FILTER_STATE_START_INPUT;

            break;

        case V4L2_FILTER_STATE_START_INPUT:
            LOG_LOG("V4l2Filter eState V4L2_FILTER_STATE_START_INPUT \n");

            ret = inThread->start();
            if(ret == OMX_ErrorNone){
                eState = V4L2_FILTER_STATE_WAIT_RES;
            }
            break;

        case V4L2_FILTER_STATE_WAIT_RES:
            fsl_osal_sleep(1000);
            fsl_osal_mutex_lock(sMutex);
            ProcessInputBuffer();
            fsl_osal_mutex_unlock(sMutex);
            LOG_LOG("V4l2Filter eState V4L2_FILTER_STATE_WAIT_RES \n");
            break;

        case V4L2_FILTER_STATE_RES_CHANGED:
            LOG_LOG("V4l2Filter eState V4L2_FILTER_STATE_RES_CHANGED \n");
            fsl_osal_sleep(1000);
            break;

        case V4L2_FILTER_STATE_FLUSHED:
            LOG_LOG("V4l2Filter eState V4L2_FILTER_STATE_FLUSHED \n");
            if(bSetOutputBufferCount){
                eState = V4L2_FILTER_STATE_RUN;
            }else
                fsl_osal_sleep(1000);
            break;

        case V4L2_FILTER_STATE_RUN:
        {
            OMX_ERRORTYPE ret_in = OMX_ErrorNone;
            OMX_ERRORTYPE ret_other = OMX_ErrorNone;

            if(bOutputEOS){
                LOG_LOG("bOutputEOS OMX_ErrorNoMore");
                ret = OMX_ErrorNoMore;
                break;
            }

            if(bUseDmaBuffer && !bDmaBufferAllocated)
                AllocateDmaBuffer();

            if(bAllocateFailed){
                LOG_LOG("bAllocateFailed return OMX_ErrorNoMore");
                ret = OMX_ErrorNoMore;
                break;
            }

            fsl_osal_mutex_lock(sMutex);
            ret = ProcessOutputBuffer();
            fsl_osal_mutex_unlock(sMutex);

            if(bEnabledPostProcess && pPostProcess->OutputBufferAdded()){
                ret_other = ProcessPostBuffer();
                if(ret_other == OMX_ErrorNone){
                    LOG_LOG("ProcessOutputBuffer OMX_ErrorNone");
                    return OMX_ErrorNone;
                }
            }

            if(ret != OMX_ErrorNone){
                fsl_osal_mutex_lock(sMutex);
                ret_in = ProcessInputBuffer();
                fsl_osal_mutex_unlock(sMutex);
                //LOG_LOG("ProcessInputBuffer ret=%x",ret);
            }

            if(bEnabledPreProcess && pPreProcess->InputBufferAdded()){
                ret_other = ProcessPreBuffer();
                if(ret_other == OMX_ErrorNone)
                    return OMX_ErrorNone;
            }

            if(ret == OMX_ErrorNoMore && ret_in == OMX_ErrorNoMore){
                ret = OMX_ErrorNoMore;
            }else{
                ret = OMX_ErrorNone;
            }

            if(V4L2_DEV_TYPE_ENCODER == eDevType && bInputEOS && !bOutputEOS && 0 == nInputCnt){
                eState = V4L2_FILTER_STATE_STOP_ENCODE;
                LOG_LOG("V4l2Filter eState goto V4L2_FILTER_STATE_STOP_ENCODE \n");
                break;
            }

            if(ret == OMX_ErrorNone && !bOutputStarted){
                bOutputStarted = OMX_TRUE;
                inThread->start();
                outObj->Start();
            }

            break;
        }
        case V4L2_FILTER_STATE_START_ENCODE:
            bNewSegment = OMX_TRUE;
            eState = V4L2_FILTER_STATE_RUN;
            break;
        case V4L2_FILTER_STATE_STOP_ENCODE:
            fsl_osal_mutex_lock(sMutex);
            OMX_BUFFERHEADERTYPE * pBufHdlr;
            if(ports[OUT_PORT]->BufferNum() == 0){
                fsl_osal_mutex_unlock(sMutex);
                return OMX_ErrorNoMore;
            }

            ports[OUT_PORT]->GetBuffer(&pBufHdlr);
            if(pBufHdlr == NULL){
                fsl_osal_mutex_unlock(sMutex);
                return OMX_ErrorNoMore;
            }

            pBufHdlr->nFlags |= OMX_BUFFERFLAG_EOS;
            ret = ReturnBuffer(pBufHdlr,OUT_PORT);
            eState = V4L2_FILTER_STATE_END;
            LOG_LOG("V4l2Filter eState V4L2_FILTER_STATE_STOP_ENCODE \n");
            fsl_osal_mutex_unlock(sMutex);
            break;
        default:
            break;
    }

    return ret;
}
OMX_ERRORTYPE V4l2Filter::ProcessPreProcessBuffer()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE *pInBufHdr = NULL;
    if(ports[IN_PORT]->BufferNum() > 0 && inObj->HasEmptyBuffer()) {
        OMX_BUFFERHEADERTYPE *pBufferHdr = NULL;
        ports[IN_PORT]->GetBuffer(&pBufferHdr);
        if(pBufferHdr == NULL){
            return OMX_ErrorUndefined;
        }

        ret = ProcessInBufferFlags(pBufferHdr);
        if(ret != OMX_ErrorNone)
            return ret;

        if(V4L2_DEV_TYPE_ENCODER == eDevType && pBufferHdr->nFilledLen == 0
            && !(pBufferHdr->nFlags &OMX_BUFFERFLAG_EOS)){
            LOG_LOG("ignore empty input buffer for encoder\n");
            ports[IN_PORT]->SendBuffer(pBufferHdr);
            return OMX_ErrorNone;
        }
        if(!(pBufferHdr->nFlags &OMX_BUFFERFLAG_EOS))
            pPreProcess->AddInputFrame(pBufferHdr);
    }

    if(OMX_ErrorNone == pPreProcess->GetInputReturnBuffer(&pInBufHdr) && pInBufHdr != NULL)
        ReturnBuffer(pInBufHdr,IN_PORT);

    return ret;
}

OMX_ERRORTYPE V4l2Filter::ProcessInputBuffer()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    OMX_U32 flags = 0;
    OMX_BUFFERHEADERTYPE *pBufferHdr = NULL;
    if(ports[IN_PORT]->BufferNum() == 0)
        return OMX_ErrorNoMore;

#ifdef ENABLE_TS_MANAGER

        if(OMX_TRUE != tsmHasEnoughSlot(hTsHandle)) {
            LOG_LOG("tsmHasEnoughSlot FALSE");
            return OMX_ErrorNone;
        }
#endif

    ports[IN_PORT]->GetBuffer(&pBufferHdr);

    if(pBufferHdr == NULL){
        return OMX_ErrorNoMore;
    }
    LOG_LOG("Get Inbuffer %p,len=%d,ts=%lld,flag=%x,offset=%d\n", pBufferHdr->pBuffer, pBufferHdr->nFilledLen, pBufferHdr->nTimeStamp, pBufferHdr->nFlags,pBufferHdr->nOffset);

    ret = ProcessInBufferFlags(pBufferHdr);
    if(ret != OMX_ErrorNone)
        return ret;

    if(bEnabledFrameParser && pParser != NULL)
        pParser->Parse(pBufferHdr->pBuffer,&pBufferHdr->nFilledLen);

    if(bRefreshIntra){
        flags |= V4L2_OBJECT_FLAG_KEYFRAME;
        bRefreshIntra = OMX_FALSE;
    }
    if(bStoreMetaData)
        flags |= V4L2_OBJECT_FLAG_METADATA_BUFFER;

    if(pBufferHdr->nFilledLen > 0){
        LOG_LOG("set Input buffer BEGIN ts=%lld\n",pBufferHdr->nTimeStamp);
        #ifdef V4L2_DUMP_INPUT
        dumpBuffer(pBufferHdr,IN_PORT);
        #endif
        #ifdef ENABLE_TS_MANAGER
        tsmSetBlkTs(hTsHandle, pBufferHdr->nFilledLen, pBufferHdr->nTimeStamp);
        #endif
        if(bEnabledPreProcess){
            if(flags & V4L2_OBJECT_FLAG_KEYFRAME)
                pBufferHdr->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;
            ret = pPreProcess->AddInputFrame(pBufferHdr);
        }else
            ret = inObj->SetBuffer(pBufferHdr, flags);

        nInputCnt ++;
        LOG_LOG("ProcessInputBuffer ret=%x nInputCnt=%d\n",ret,nInputCnt);
        if(ret != OMX_ErrorNone) {
            ports[IN_PORT]->SendBuffer(pBufferHdr);
            LOG_ERROR("set buffer err=%x\n",ret);
            return ret;
        }

    }else{
        ports[IN_PORT]->SendBuffer(pBufferHdr);
        pBufferHdr = NULL;
    }

    //handle last buffer here when it has data and eos flag
    if(pBufferHdr->nFilledLen > 0 && (pBufferHdr->nFlags & OMX_BUFFERFLAG_EOS)){
        LOG_LOG("OMX_BUFFERFLAG_EOS nFilledLen > 0");
        if(bEnabledPreProcess){
            pPreProcess->AddInputFrame(pBufferHdr);
            return OMX_ErrorNoMore;
        }else{
            HandleEOSEvent(IN_PORT);
            ports[IN_PORT]->SendBuffer(pBufferHdr);
            return OMX_ErrorNoMore;
        }
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE V4l2Filter::ProcessInBufferFlags(OMX_BUFFERHEADERTYPE *pInBufferHdr)
{
    if(pInBufferHdr->nFlags & OMX_BUFFERFLAG_STARTTIME){
        bNewSegment = OMX_TRUE;
        bRefreshIntra = OMX_TRUE;
    }

    #ifdef ENABLE_TS_MANAGER
    if(pInBufferHdr->nFlags & OMX_BUFFERFLAG_STARTTIME) {
        OMX_S32 nDurationThr,nBufCntThr;
        //could not get consume length now, use FIFO mode
        if(pInBufferHdr->nFlags & OMX_BUFFERFLAG_STARTTRICK) {
            LOG_DEBUG("Set ts manager to FIFO mode.\n");
            tsmReSync(hTsHandle, pInBufferHdr->nTimeStamp, MODE_FIFO);
        }
        else {
            LOG_DEBUG("Set ts manager to AI mode. ts=%lld\n",pInBufferHdr->nTimeStamp);
            tsmReSync(hTsHandle, pInBufferHdr->nTimeStamp, MODE_AI);
        }
        GetInputDataDepthThreshold(&nDurationThr, &nBufCntThr);
        LOG_INFO("nDurationThr: %d, nBufCntThr: %d\n", nDurationThr, nBufCntThr);
        tsmSetDataDepthThreshold(hTsHandle, nDurationThr, nBufCntThr);
    }
    #endif

    if(pInBufferHdr->nFlags & OMX_BUFFERFLAG_DECODEONLY)
        nDecodeOnly ++;

    if(pInBufferHdr->nFlags & OMX_BUFFERFLAG_EOS){
        LOG_LOG("get OMX_BUFFERFLAG_EOS for input buffer");
        bInputEOS = OMX_TRUE;

        //when the eos buffer has data, need to call inObj->SetBuffer before stop command
        if(0 == pInBufferHdr->nFilledLen){
            if(bEnabledPreProcess){
                pPreProcess->AddInputFrame(pInBufferHdr);
                return OMX_ErrorNoMore;
            }else{
                HandleEOSEvent(IN_PORT);
                ports[IN_PORT]->SendBuffer(pInBufferHdr);
                return OMX_ErrorNoMore;
            }
        }
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE V4l2Filter::ProcessOutputBuffer()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    
    if(ports[OUT_PORT]->BufferNum() == 0)
        return OMX_ErrorNoMore;

    OMX_BUFFERHEADERTYPE *pBufferHdr = NULL;

    ports[OUT_PORT]->GetBuffer(&pBufferHdr);
    if(pBufferHdr == NULL){
        return OMX_ErrorNoMore;
    }
    LOG_LOG("Get output buffer %p,len=%d,alloLen=%d,flag=%x,ts=%lld,bufferCnt=%d\n",
        pBufferHdr->pBuffer,pBufferHdr->nFilledLen,pBufferHdr->nAllocLen,pBufferHdr->nFlags,pBufferHdr->nTimeStamp,ports[OUT_PORT]->BufferNum());

    if(eDevType == V4L2_DEV_TYPE_ENCODER){
        if(!bSendCodecData && pCodecDataBufferHdr == NULL){
            pCodecDataBufferHdr = pBufferHdr;
            LOG_DEBUG("store codec data bufer for output ptr=%p\n",pCodecDataBufferHdr->pBuffer);
            return OMX_ErrorNone;
        }
    }

    if(pBufferHdr->nFlags & OMX_BUFFERFLAG_EOS)
        bOutputEOS = OMX_TRUE;

    //v4l2 buffer has alignment requirement
    //replace the allocated buffer size if it is larger than required buffer size
    if(nOutBufferSize > 0 && pBufferHdr->nAllocLen > nOutBufferSize && !bEnabledPostProcess){
        pBufferHdr->nAllocLen = nOutBufferSize;
        LOG_DEBUG("reset nAllocLen to %d\n",pBufferHdr->nAllocLen);
    }

    #ifdef ENABLE_TS_MANAGER
    if(eDevType == V4L2_DEV_TYPE_DECODER){
        pBufferHdr->nTimeStamp = -1;
    }
    #endif

    LOG_LOG("Set output buffer BEGIN,pBufferHdr=%p \n",pBufferHdr);
    if(bEnabledPostProcess)
        ret = pPostProcess->AddOutputFrame(pBufferHdr);
    else
        ret = outObj->SetBuffer(pBufferHdr,0);
    LOG_LOG("ProcessOutputBuffer ret=%x\n",ret);

    if(ret != OMX_ErrorNone) {
        pBufferHdr->nFilledLen = 0;
        ports[OUT_PORT]->SendBuffer(pBufferHdr);
        return OMX_ErrorNoMore;
    }

    return OMX_ErrorNone;
}
OMX_ERRORTYPE V4l2Filter::ReturnBuffer(OMX_BUFFERHEADERTYPE *pBufferHdr,OMX_U32 nPortIndex)
{
    if(pBufferHdr == NULL){
        LOG_LOG("ReturnBuffer failed\n");
        return OMX_ErrorBadParameter;
    }

    if(nPortIndex == IN_PORT){
        if(pBufferHdr->nFlags & OMX_BUFFERFLAG_EOS)
            HandleEOSEvent(IN_PORT);
        ports[IN_PORT]->SendBuffer(pBufferHdr);
        LOG_LOG("ReturnBuffer input =%p,ts=%lld,len=%d,offset=%d flag=%x nInputCnt=%d\n",
            pBufferHdr->pBuffer, pBufferHdr->nTimeStamp,pBufferHdr->nFilledLen,pBufferHdr->nOffset,pBufferHdr->nFlags, nInputCnt);

    }else if(nPortIndex == OUT_PORT) {
 
        if(nDecodeOnly > 0){
            nDecodeOnly --;
            pBufferHdr->nFilledLen = 0;
            pBufferHdr->nFlags = 0;
            outObj->SetBuffer(pBufferHdr,0);
            LOG_DEBUG("send output buffer for nDecodeOnly \n");
            return OMX_ErrorNone;
        }

        if(!bSendCodecData && pCodecDataBufferHdr != NULL && bEnabledAVCCConverter){
            //send codec data for encoder
            OMX_U32 offset = 0;
            if(OMX_ErrorNone == pCodecDataConverter->CheckSpsPps(pBufferHdr->pBuffer, pBufferHdr->nFilledLen, &offset)){
                OMX_U8* pData = NULL;
                if(OMX_ErrorNone != pCodecDataConverter->GetSpsPpsPtr(&pData, &offset)){
                    LOG_ERROR("failed to get codec data");
                    return OMX_ErrorUndefined;
                }

                fsl_osal_memcpy(pCodecDataBufferHdr->pBuffer, pData, offset);
                pCodecDataBufferHdr->nFilledLen = offset;
                pCodecDataBufferHdr->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;
                pCodecDataBufferHdr->nOffset = 0;
                pCodecDataBufferHdr->nTimeStamp = 0;
#ifdef V4L2_DUMP_OUTPUT
                dumpBuffer(pCodecDataBufferHdr, OUT_PORT);
#endif
                ports[OUT_PORT]->SendBuffer(pCodecDataBufferHdr);
                bSendCodecData = OMX_TRUE;
                pBufferHdr->nOffset = offset;
                LOG_DEBUG("send codec data buffer len=%d,consume=%d.ptr=%p\n",
                    pCodecDataBufferHdr->nFilledLen,pBufferHdr->nOffset,pCodecDataBufferHdr->pBuffer);
                pBufferHdr->nFilledLen -= pBufferHdr->nOffset;
                pCodecDataBufferHdr = NULL;
            }
        }

        if(bInsertSpsPps2IDR &&( pBufferHdr->nFlags & OMX_BUFFERFLAG_SYNCFRAME)){
            OMX_U32 nLen = 0;
            OMX_U8* pData = NULL;
            if(OMX_ErrorNone == pCodecDataConverter->GetSpsPpsPtr(&pData,&nLen)){
                fsl_osal_memmove(pBufferHdr->pBuffer+nLen,pBufferHdr->pBuffer,pBufferHdr->nFilledLen);
                fsl_osal_memcpy(pBufferHdr->pBuffer+pBufferHdr->nOffset,pData,nLen);
                pBufferHdr->nFilledLen += nLen;
            }
        }

        if(bNewSegment){
            pBufferHdr->nFlags |= OMX_BUFFERFLAG_STARTTIME;
            bNewSegment = OMX_FALSE;
            LOG_DEBUG("send buffer OMX_BUFFERFLAG_STARTTIME\n");

        }
        if((pBufferHdr->nFlags & OMX_BUFFERFLAG_EOS)){
            LOG_DEBUG("send buffer OMX_BUFFERFLAG_EOS\n");
            bOutputEOS = OMX_TRUE;
            //vpu encoder will add an eos nal for last frame, ignore the buffer.
            if(4 == pBufferHdr->nFilledLen)
                pBufferHdr->nFilledLen = 0;

        }
        #ifdef V4L2_DUMP_OUTPUT
            dumpBuffer(pBufferHdr, OUT_PORT);
        #endif

        #ifdef ENABLE_TS_MANAGER
        if(!(pBufferHdr->nFlags & OMX_BUFFERFLAG_CODECCONFIG))
            pBufferHdr->nTimeStamp = tsmGetFrmTs(hTsHandle, NULL);
        #endif
        nOutputCnt++;

        LOG_LOG("ReturnBuffer output buffer,ts=%lld,ptr=%p, offset=%d, len=%d,flags=%x,alloc len=%d nOutputCnt=%d\n",
            pBufferHdr->nTimeStamp, pBufferHdr->pBuffer, pBufferHdr->nOffset, pBufferHdr->nFilledLen,pBufferHdr->nFlags,pBufferHdr->nAllocLen,nOutputCnt);
        ports[OUT_PORT]->SendBuffer(pBufferHdr);

        if(bOutputEOS){
            LOG_LOG("call inThread->pause\n");
            inThread->pause();
        }
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE V4l2Filter::ComponentReturnBuffer(OMX_U32 nPortIndex)
{
    LOG_LOG("ComponentReturnBuffer port=%d\n",nPortIndex);

    if(nPortIndex == IN_PORT) {
        FlushComponent(IN_PORT);
    }else if(nPortIndex == OUT_PORT) {
        FlushComponent(OUT_PORT);
    }
    
    return OMX_ErrorNone;
}


OMX_ERRORTYPE V4l2Filter::HandleFormatChanged(OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;


    if(nPortIndex == IN_PORT){
        PortFormatChanged(IN_PORT);
        return ret;
    }

    OMX_BOOL bResourceChanged = OMX_FALSE;
    OMX_BOOL bSuppress = OMX_FALSE;
    OMX_BOOL bSendEvent = OMX_FALSE;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    OMX_CONFIG_RECTTYPE sCropDef;
    V4l2ObjectFormat sFormat;
    OMX_U32 numCnt = 0;
    OMX_U32 buffer_size = 0;
    OMX_U32 pad_width = 0;
    OMX_U32 pad_height = 0;
    OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);

    sPortDef.eDomain = OMX_PortDomainVideo;
    sPortDef.nPortIndex = OUT_PORT;
    ports[nPortIndex]->GetPortDefinition(&sPortDef);

    ret = outObj->GetFormat(&sFormat);
    if(ret != OMX_ErrorNone)
        return ret;

    ret = outObj->GetMinimumBufferCount(&numCnt);
    #ifdef FOR_CODA
        //add one buffer for gm player to make it reallocate port buffer
        numCnt = nOutBufferCnt+1;
    #else
    if(ret != OMX_ErrorNone)
        return ret;
    #endif

    //for imx8qxp v4l2 decoder
    //numCnt += V4L2_EXTRA_BUFFER_CNT;

    //add two to avoid hang issue
    numCnt = numCnt+2;

    if(bEnabledPostProcess)
        numCnt += PROCESS3_BUF_CNT;

    LOG_LOG("HandleFormatChanged OUT_PORT numCnt=%d\n",numCnt);
    
    pad_width = Align(sFormat.width, nWidthAlign);
    pad_height = Align(sFormat.height, nHeightAlign);

    if(pad_width > sOutFmt.nFrameWidth ||
        pad_height > sOutFmt.nFrameHeight){
        bResourceChanged = OMX_TRUE;
    }else if(pad_width < sOutFmt.nFrameWidth ||
        pad_height < sOutFmt.nFrameHeight){
        bResourceChanged = OMX_TRUE;
        if(bAdaptivePlayback){
            bSuppress = OMX_TRUE;
        }
    }
    buffer_size = sFormat.bufferSize[0] + sFormat.bufferSize[1] + sFormat.bufferSize[2];
    //no need to reallocate the output buffer when current buffer count is larger than the required buffer count.
    if(nOutBufferCnt != numCnt || buffer_size != nOutBufferSize){
        bResourceChanged = OMX_TRUE;
        if(numCnt+4 <= nOutBufferCnt && buffer_size <= nOutBufferSize){
            bSuppress = OMX_TRUE;
        }
    }

    if(!bSendPortSettingChanged || (bResourceChanged && !bSuppress)){
        OMX_U32 calcBufferSize = 0;
        sPortDef.format.video.nFrameWidth = pad_width;
        sPortDef.format.video.nFrameHeight = pad_height;
        //stride use frame width
        sPortDef.format.video.nStride= sPortDef.format.video.nFrameWidth;
        sPortDef.nBufferCountActual = nOutBufferCnt = numCnt;
        sPortDef.nBufferCountMin = numCnt;
        calcBufferSize = pad_width * pad_height * pxlfmt2bpp(sOutFmt.eColorFormat) / 8;

        if(buffer_size <= nOutBufferSize)
            sPortDef.nBufferSize = nOutBufferSize;
        else
            sPortDef.nBufferSize = buffer_size;

        if(sPortDef.nBufferSize < calcBufferSize)
            sPortDef.nBufferSize = calcBufferSize;

        ports[nPortIndex]->SetPortDefinition(&sPortDef);

        LOG_LOG("send OMX_EventPortSettingsChanged output port cnt=%d \n", sPortDef.nBufferCountActual);
        eState = V4L2_FILTER_STATE_RES_CHANGED;
        fsl_osal_memcpy(&sOutFmt, &sPortDef.format.video, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));

        bSendEvent = OMX_TRUE;
        bSetOutputBufferCount = OMX_FALSE;
        bSendPortSettingChanged = OMX_TRUE;
        SendEvent(OMX_EventPortSettingsChanged, OUT_PORT, 0, NULL);
        LOG_LOG("send OMX_EventPortSettingsChanged \n");
    }else{
        LOG_LOG("HandleFormatChanged do not send OMX_EventPortSettingsChanged\n");
        eState = V4L2_FILTER_STATE_RUN;
        outObj->Start();
    }

    (void)outObj->GetCrop(&sCropDef);

    if(sCropDef.nWidth != sOutputCrop.nWidth || sCropDef.nHeight != sOutputCrop.nHeight){
        fsl_osal_memcpy(&sOutputCrop, &sCropDef, sizeof(OMX_CONFIG_RECTTYPE));
        if(!bSendEvent){
            SendEvent(OMX_EventPortSettingsChanged, OUT_PORT, OMX_IndexConfigCommonOutputCrop, NULL);
            LOG_LOG("send OMX_EventPortSettingsChanged OMX_IndexConfigCommonOutputCrop\n");
        }
    }

    if(bEnabledPostProcess){ 
        if(ret != OMX_ErrorNone)
            return ret;
        PROCESS3_FORMAT fmt;
        fmt.bufferSize = nOutBufferSize;
        fmt.format = OMX_COLOR_FormatYUV420SemiPlanar8x128Tiled;
        fmt.width = sOutFmt.nFrameWidth;
        fmt.height = sOutFmt.nFrameHeight;
        fmt.stride = sOutFmt.nStride;
        ret = pPostProcess->ConfigInput(&fmt);
        if(ret != OMX_ErrorNone)
            return ret;
    
        fmt.format = OMX_COLOR_Format16bitRGB565;
        ret = pPostProcess->ConfigOutput(&fmt);
        if(ret != OMX_ErrorNone)
            return ret;
    }

    return OMX_ErrorNone;
}
OMX_ERRORTYPE V4l2Filter::HandleFormatChangedForIon(OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    if(nPortIndex != OUT_PORT)
        return OMX_ErrorBadParameter;

    OMX_BOOL bResourceChanged = OMX_FALSE;
    OMX_BOOL bSuppress = OMX_FALSE;
    OMX_BOOL bSendEvent = OMX_FALSE;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    OMX_CONFIG_RECTTYPE sCropDef;
    V4l2ObjectFormat sFormat;
    OMX_U32 numCnt = 0;
    OMX_U32 buffer_size = 0;
    OMX_U32 pad_width = 0;
    OMX_U32 pad_height = 0;
    OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);

    sPortDef.eDomain = OMX_PortDomainVideo;
    sPortDef.nPortIndex = OUT_PORT;
    ports[nPortIndex]->GetPortDefinition(&sPortDef);

    ret = outObj->GetFormat(&sFormat);
    if(ret != OMX_ErrorNone)
        return ret;

    ret = outObj->GetMinimumBufferCount(&numCnt);

    if(ret != OMX_ErrorNone)
        return ret;

    //add extra 3 buffers so the buffer count will be equal to that in gstreamer.
    numCnt = numCnt+3;

    LOG_LOG("HandleFormatChangedForIon OUT_PORT numCnt=%d\n",numCnt);
    pad_width = Align(sFormat.width, nWidthAlign);
    pad_height = Align(sFormat.height, nHeightAlign);

    if(pad_width > sOutFmt.nFrameWidth ||
        pad_height > sOutFmt.nFrameHeight){
        bResourceChanged = OMX_TRUE;
    }else if(pad_width < sOutFmt.nFrameWidth ||
        pad_height < sOutFmt.nFrameHeight){
        bResourceChanged = OMX_TRUE;
        if(bAdaptivePlayback){
            bSuppress = OMX_TRUE;
        }
    }

    if(sFormat.format == v4l2_fourcc('N', 'T', '1', '2')){
        nErrCnt = 10;
        HandleErrorEvent();
    }

    buffer_size = sFormat.bufferSize[0] + sFormat.bufferSize[1] + sFormat.bufferSize[2];
    //no need to reallocate the output buffer when current buffer count is larger than the required buffer count.
    if(nDmaBufferCnt != numCnt || sFormat.bufferSize[0] != nDmaBufferSize[0]
        || sFormat.bufferSize[1] != nDmaBufferSize[1] || sFormat.bufferSize[2] != nDmaBufferSize[2]){
        bResourceChanged = OMX_TRUE;

        if(numCnt <= nDmaBufferCnt && 
            (sFormat.bufferSize[0] <= nDmaBufferSize[0]) && 
            (sFormat.bufferSize[1] <= nDmaBufferSize[1]) &&
            (sFormat.bufferSize[2] <= nDmaBufferSize[2])){
            bSuppress = OMX_TRUE;
            LOG_LOG("sFormat Buffer Size=%d,%d,%d, dma buffer size=%d,%d,%d",
                sFormat.bufferSize[0],sFormat.bufferSize[1],sFormat.bufferSize[2],
                nDmaBufferSize[0],nDmaBufferSize[1],nDmaBufferSize[2]);

            if(sPortDef.format.video.nStride != (OMX_S32)sFormat.stride)
                bSuppress = OMX_FALSE;
        }
    }

    OMX_U32 calcBufferSize = 0;
    sPortDef.format.video.nFrameWidth = pad_width;
    sPortDef.format.video.nFrameHeight = pad_height;
 
    sPortDef.format.video.nStride = sFormat.stride;
    LOG_LOG("HandleFormatChangedForIon w=%d,h=%d,stride=%d",sPortDef.format.video.nFrameWidth,sPortDef.format.video.nFrameHeight,sPortDef.format.video.nStride);
    sPortDef.nBufferCountActual = nOutBufferCnt;
    sPortDef.nBufferCountMin = nOutBufferCnt;
    calcBufferSize = pad_width * pad_height * pxlfmt2bpp(sOutFmt.eColorFormat) / 8;

    if(buffer_size < calcBufferSize)
        buffer_size = calcBufferSize;
    sPortDef.nBufferSize = nOutBufferSize = buffer_size;

    LOG_LOG("HandleFormatChangedForIon nDmaBufferSize=%d,%d,%d nDmaBufferCnt=%d,nOutBufferCnt=%d\n",
        nDmaBufferSize[0],nDmaBufferSize[1],nDmaBufferSize[2],nDmaBufferCnt,nOutBufferCnt);

    if(!bSendPortSettingChanged || (bResourceChanged && !bSuppress)){

        ports[nPortIndex]->SetPortDefinition(&sPortDef);

        nDmaBufferSize[0] = sFormat.bufferSize[0];
        nDmaBufferSize[1] = sFormat.bufferSize[1];
        nDmaBufferSize[2] = sFormat.bufferSize[2];
        nDmaBufferCnt = numCnt;

        LOG_LOG("send OMX_EventPortSettingsChanged output port cnt=%d \n", sPortDef.nBufferCountActual);
        eState = V4L2_FILTER_STATE_RES_CHANGED;
        fsl_osal_memcpy(&sOutFmt, &sPortDef.format.video, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));

        if(bDmaBufferAllocated){
            pDmaBuffer->FreeAll();
            bDmaBufferAllocated = OMX_FALSE;
        }

        bSendEvent = OMX_TRUE;
        bSetOutputBufferCount = OMX_FALSE;
        bSendPortSettingChanged = OMX_TRUE;

        SendEvent(OMX_EventPortSettingsChanged, OUT_PORT, 0, NULL);
        LOG_LOG("send OMX_EventPortSettingsChanged \n");
    }else{

        ports[nPortIndex]->SetPortDefinition(&sPortDef);
        fsl_osal_memcpy(&sOutFmt, &sPortDef.format.video, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));

        LOG_LOG("HandleFormatChanged do not send OMX_EventPortSettingsChanged\n");
        eState = V4L2_FILTER_STATE_RUN;
        outObj->Start();
    }

    (void)outObj->GetCrop(&sCropDef);

    if(sCropDef.nWidth != sOutputCrop.nWidth || sCropDef.nHeight != sOutputCrop.nHeight){
        fsl_osal_memcpy(&sOutputCrop, &sCropDef, sizeof(OMX_CONFIG_RECTTYPE));
        if(!bSendEvent){
            SendEvent(OMX_EventPortSettingsChanged, OUT_PORT, OMX_IndexConfigCommonOutputCrop, NULL);
            LOG_LOG("send OMX_EventPortSettingsChanged OMX_IndexConfigCommonOutputCrop\n");
        }
    }

    if(bEnabledPostProcess){ 
        if(ret != OMX_ErrorNone)
            return ret;
        PROCESS3_FORMAT fmt;
        fmt.bufferSize = nOutBufferSize;
        fmt.format = OMX_COLOR_FormatYUV420SemiPlanar8x128Tiled;
        fmt.width = sOutFmt.nFrameWidth;
        fmt.height = sOutFmt.nFrameHeight;
        fmt.stride = sOutFmt.nStride;
        ret = pPostProcess->ConfigInput(&fmt);
        if(ret != OMX_ErrorNone)
            return ret;

        fmt.format = sOutFmt.eColorFormat;
        ret = pPostProcess->ConfigOutput(&fmt);
        if(ret != OMX_ErrorNone)
            return ret;
    }
    return ret;
}

OMX_ERRORTYPE V4l2Filter::HandleErrorEvent()
{
    nErrCnt ++;

    if(nErrCnt > 10){
        SendEvent(OMX_EventError, OMX_ErrorStreamCorrupt, 0, NULL);
        LOG_ERROR("HandleErrorEvent send event\n");
        nErrCnt = 0;
        if(eDevType == V4L2_DEV_TYPE_DECODER){
            pV4l2Dev->StopDecoder(nFd);
        }
    }
    return OMX_ErrorNone;
}
OMX_ERRORTYPE V4l2Filter::HandleEOSEvent(OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    if(nPortIndex == IN_PORT){
        if(eDevType == V4L2_DEV_TYPE_DECODER){
            pV4l2Dev->StopDecoder(nFd);
        }

        if(eDevType == V4L2_DEV_TYPE_ENCODER){
            pV4l2Dev->StopEncoder(nFd);
        }
        LOG_LOG("send stop command");
    }else{
        //get one buffer from post process to send eos event
        if(eDevType == V4L2_DEV_TYPE_DECODER && bEnabledPostProcess){
            DmaBufferHdr * pBufHdlr;
            OMX_S32 i = 20;
            while(i > 0){
                if(OMX_ErrorNone == outObj->GetBuffer(&pBufHdlr)){
                    pBufHdlr->bReadyForProcess = OMX_FALSE;
                    pBufHdlr->flag |= DMA_BUF_EOS;
                    LOG_LOG("AddInputFrame OMX_BUFFERFLAG_EOS 1");
                    ret = pPostProcess->AddInputFrame(pBufHdlr);
                    return ret;
                }
                fsl_osal_sleep(1000);
                i--;
            }

            //if can't dequeue buffer from capture port, then get one from post process buffer queue
            i = 20;
            while(i > 0){
                if(pPostProcess != NULL && OMX_ErrorNone == pPostProcess->GetInputReturnBuffer(&pBufHdlr)){
                    pBufHdlr->bReadyForProcess = OMX_FALSE;
                    pBufHdlr->flag |= DMA_BUF_EOS;
                    LOG_LOG("AddInputFrame OMX_BUFFERFLAG_EOS 2");
                    ret = pPostProcess->AddInputFrame(pBufHdlr);
                    return ret;
                }
                fsl_osal_sleep(1000);
                i--;
            }
            {
                OMX_BUFFERHEADERTYPE *pBufferHdr = NULL;
                ports[OUT_PORT]->GetBuffer(&pBufferHdr);
                if(pBufferHdr != NULL){
                    pBufferHdr->nFilledLen = 0;
                    pBufferHdr->nFlags = OMX_BUFFERFLAG_EOS;
                    pBufferHdr->nTimeStamp = -1;
                    LOG_LOG("HandleEOSEvent use outport buffer to send eos flag");
                    ret = ReturnBuffer(pBufferHdr,OUT_PORT);
                    return ret;
                }
                else{
                    LOG_ERROR("Can't get outport buffer to send eos flag");
                }
            }
            ret = OMX_ErrorOverflow;
        }
        
        if(eDevType == V4L2_DEV_TYPE_ENCODER){

            LOG_LOG("get eos event, wait for last frame");
            return OMX_ErrorNone;
            #if 0
            OMX_BUFFERHEADERTYPE * pBufHdlr;
            OMX_S32 i = 20;
            while(i > 0){
                if(OMX_ErrorNone == outObj->GetBuffer(&pBufHdlr)){
                    pBufHdlr->nFlags |= OMX_BUFFERFLAG_EOS;
                    LOG_LOG("HandleEOSEvent V4L2_DEV_TYPE_ENCODER");
                    ret = ReturnBuffer(pBufHdlr, OUT_PORT);
                    return ret;
                }
                fsl_osal_sleep(1000);
                i--;
            }
            #endif
        }

    }

    return ret;
}

OMX_ERRORTYPE V4l2Filter::HandleInObjBuffer()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    fsl_osal_mutex_lock(sMutex);

    OMX_BUFFERHEADERTYPE * pBufHdlr;
    DmaBufferHdr * pDmaBufHdlr;
    LOG_LOG("send input buffer BEGIN \n");
    if(bEnabledPreProcess && bUseDmaBuffer){
        if(OMX_ErrorNone == inObj->GetOutputBuffer(&pDmaBufHdlr)){
            ret = pDmaBuffer->Add(pDmaBufHdlr);
        }
    }else if(OMX_ErrorNone == inObj->GetOutputBuffer(&pBufHdlr)){
        ret = ReturnBuffer(pBufHdlr,IN_PORT);
        LOG_LOG("send input buffer END ret=%x\n",ret);
    }
    fsl_osal_mutex_unlock(sMutex);

    return ret;
}
OMX_ERRORTYPE V4l2Filter::HandleOutObjBuffer()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    fsl_osal_mutex_lock(sMutex);

    LOG_LOG("send output buffer BEGIN \n");
    if(bEnabledPostProcess && bUseDmaBuffer){
        DmaBufferHdr * pBufHdlr;
        if(OMX_ErrorNone == outObj->GetOutputBuffer(&pBufHdlr)){
        #ifdef V4L2_DUMP_POST_PROCESS_IN
             dumpBuffer(pBufHdlr);
        #endif
            ret = pPostProcess->AddInputFrame(pBufHdlr);
        }
        LOG_LOG("send dma output buffer END ret=%x \n", ret);
    }else{
        OMX_BUFFERHEADERTYPE * pBufHdlr;
        if(OMX_ErrorNone == outObj->GetOutputBuffer(&pBufHdlr))
            ret = ReturnBuffer(pBufHdlr,OUT_PORT);
        LOG_LOG("send output buffer END ret=%x \n", ret);
    }
     fsl_osal_mutex_unlock(sMutex);
    return ret;
}

OMX_ERRORTYPE V4l2Filter::GetInputDataDepthThreshold(OMX_S32* pDurationThr, OMX_S32* pBufCntThr)
{
    /*
      for some application, such rtsp/http, we need to set some thresholds to avoid input data is consumed by decoder too fast.
      -1: no threshold
    */
    *pDurationThr=-1;
    *pBufCntThr=-1;
    return OMX_ErrorNone;
}

void V4l2Filter::dumpBuffer(OMX_BUFFERHEADERTYPE *pBufferHdr,OMX_U32 nPortIndex)
{
    FILE * pfile = NULL;

    if(nPortIndex == OUT_PORT && (nOutputCnt <MAX_DUMP_FRAME)){
        pfile = fopen(V4L2_DUMP_OUTPUT_FILE,"ab");
    }else if(nPortIndex == IN_PORT && nInputCnt<MAX_DUMP_FRAME){
        pfile = fopen(V4L2_DUMP_INPUT_FILE,"ab");
    }

    if(pfile){
        if(eDevType == V4L2_DEV_TYPE_DECODER){
            fwrite(pBufferHdr->pBuffer,1,pBufferHdr->nFilledLen,pfile);
            fclose(pfile);
        }
    }
    return;
}
void V4l2Filter::dumpBuffer(DmaBufferHdr * hdr)
{
    FILE * pfile = NULL;

    if((nOutputCnt%30 == 0) && (nOutputCnt/30)<MAX_DUMP_FRAME){
        pfile = fopen(V4L2_DUMP_OUTPUT_FILE,"ab");
    }

    if(pfile){
        fwrite((OMX_U8*)hdr->plane[0].vaddr,1,hdr->plane[0].size,pfile);
        fwrite((OMX_U8*)hdr->plane[1].vaddr,1,hdr->plane[1].size,pfile);

        fclose(pfile);
    }
    return;
}

OMX_ERRORTYPE V4l2Filter::ProcessPostBuffer()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    DmaBufferHdr *buf = NULL;

    if(OMX_ErrorNone == pPostProcess->GetInputReturnBuffer(&buf)){
        ret = pDmaBuffer->Add(buf);
        if(OMX_ErrorNone != ret){
            return ret;
        }
        LOG_LOG("ProcessPostBuffer add dma buffer success\n");
    }


    buf = NULL;

    ret = pDmaBuffer->Get(&buf);
    if(OMX_ErrorNone != ret){
        return ret;
    }
    LOG_LOG("ProcessPostBuffer vaddr1=%lx,vaddr2=%lx",buf->plane[0].vaddr,buf->plane[1].vaddr);
    buf->bReadyForProcess = OMX_FALSE;
    ret = outObj->SetBuffer(buf, 0);
    LOG_LOG("ProcessPostBuffer ret=%d\n",ret);

    return ret;
}
OMX_ERRORTYPE V4l2Filter::ProcessPreBuffer()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    DmaBufferHdr *buf = NULL;

    if(OMX_ErrorNone == pPreProcess->GetOutputReturnBuffer(&buf)){
        if(buf->flag & DMA_BUF_EOS){
            HandleEOSEvent(IN_PORT);
            return ret;
        }
        OMX_U32 flag = 0;
        if(buf->flag & DMA_BUF_SYNC)
            flag |= V4L2_OBJECT_FLAG_KEYFRAME;
        ret = inObj->SetBuffer(buf, flag);
        if(OMX_ErrorNone != ret){
            return ret;
        }
        LOG_LOG("ProcessPreBuffer inobj set buffer success\n");
    }

    buf = NULL;

    ret = pDmaBuffer->Get(&buf);
    if(OMX_ErrorNone != ret){
        return ret;
    }

    buf->bReadyForProcess = OMX_FALSE;
    ret = pPreProcess->AddOutputFrame(buf);
    LOG_LOG("ProcessPreBuffer ret=%d\n",ret);

    return ret;
}
OMX_ERRORTYPE V4l2Filter::AllocateDmaBuffer()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    LOG_DEBUG("V4l2Filter::AllocateDmaBuffer cnt=%d,size0=%d,size1=%d,size2=%d\n",nDmaBufferCnt,nDmaBufferSize[0],nDmaBufferSize[1],nDmaBufferSize[2]);
    ret = pDmaBuffer->Allocate(nDmaBufferCnt, &nDmaBufferSize[0], eDmaBufferFormat);
    //set it to true even if not all buffers are allocated. some buffer will be freed when change to loaded state
    bDmaBufferAllocated = OMX_TRUE;
    if(ret != OMX_ErrorNone){
        LOG_ERROR("ProcessDmaBuffer Allocate failed");
        bAllocateFailed = OMX_TRUE;
        nErrCnt = 10;
        HandleErrorEvent();
        SendEvent(OMX_EventError, OMX_ErrorInsufficientResources, 0, NULL);
        return ret;
    }

    return ret;
}
OMX_ERRORTYPE V4l2Filter::HandleSkipEvent()
{
    tsmGetFrmTs(hTsHandle, NULL);
    return OMX_ErrorNone;
}

