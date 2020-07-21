/**
 *  Copyright 2018 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

#include "V4l2Dec.h"
#include "G2dProcess.h"
#include "OpenCL2dProcess.h"
#if 0
#undef LOG_DEBUG
#define LOG_DEBUG printf
#undef LOG_LOG
#define LOG_LOG printf

#endif

#define DEFAULT_BUF_IN_CNT  (3)
#define DEFAULT_BUF_IN_SIZE (1024*1024)
//to speed up the playback, nOutBufferCnt should be larger than the required buffer count
//for decoders, DEFAULT_BUF_OUT_CNT+4 >= min_required + V4L2_EXTRA_BUFFER_CNT


#define DEFAULT_BUF_OUT_CNT    (3)
#define DEFAULT_DMA_BUF_CNT    (9)

#define DEFAULT_FRM_WIDTH       (2048)
#define DEFAULT_FRM_HEIGHT      (1280)
#define DEFAULT_FRM_RATE        (30 * Q16_SHIFT)

typedef struct{
OMX_U32 type;
const char* role;
const char* name;
}V4L2_DEC_ROLE;

static V4L2_DEC_ROLE role_table[]={
{OMX_VIDEO_CodingMPEG2,"video_decoder.mpeg2","OMX.Freescale.std.video_decoder.mpeg2.hw-based"},
{OMX_VIDEO_CodingH263,"video_decoder.h263","OMX.Freescale.std.video_decoder.h263.hw-based"},
{OMX_VIDEO_CodingSORENSON263,"video_decoder.sorenson","OMX.Freescale.std.video_decoder.sorenson.hw-based"},
{OMX_VIDEO_CodingMPEG4,"video_decoder.mpeg4","OMX.Freescale.std.video_decoder.mpeg4.hw-based"},
{OMX_VIDEO_CodingWMV9,"video_decoder.wmv9","OMX.Freescale.std.video_decoder.wmv.hw-based"},
{OMX_VIDEO_CodingRV,"video_decoder.rv","OMX.Freescale.std.video_decoder.rv.hw-based"},
{OMX_VIDEO_CodingAVC, "video_decoder.avc","OMX.Freescale.std.video_decoder.avc.hw-based"},
{OMX_VIDEO_CodingDIVX,"video_decoder.divx","OMX.Freescale.std.video_decoder.divx.hw-based"},
{OMX_VIDEO_CodingDIV4, "video_decoder.div4","OMX.Freescale.std.video_decoder.div4.hw-based"},
{OMX_VIDEO_CodingXVID,"video_decoder.xvid","OMX.Freescale.std.video_decoder.xvid.hw-based"},
{OMX_VIDEO_CodingMJPEG,"video_decoder.mjpeg","OMX.Freescale.std.video_decoder.mjpeg.hw-based"},
{OMX_VIDEO_CodingVP8, "video_decoder.vp8","OMX.Freescale.std.video_decoder.vp8.hw-based"},
{OMX_VIDEO_CodingVP6, "video_decoder.vp6","OMX.Freescale.std.video_decoder.vp6.hw-based"},
{OMX_VIDEO_CodingHEVC,"video_decoder.hevc","OMX.Freescale.std.video_decoder.hevc.hw-based"},
};


V4l2Dec::V4l2Dec()
{
    eDevType = V4L2_DEV_TYPE_DECODER;
}
OMX_ERRORTYPE V4l2Dec::SetDefaultPortSetting()
{
    fsl_osal_strcpy((fsl_osal_char*)name, "OMX.Freescale.std.video_decoder.avc.hw-based");

    fsl_osal_memset(&sInFmt, 0, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
    sInFmt.nFrameWidth = DEFAULT_FRM_WIDTH;
    sInFmt.nFrameHeight = DEFAULT_FRM_HEIGHT;
    sInFmt.xFramerate = DEFAULT_FRM_RATE;
    sInFmt.eColorFormat = OMX_COLOR_FormatUnused;
    sInFmt.eCompressionFormat = OMX_VIDEO_CodingAVC;

    fsl_osal_memset(&sOutFmt, 0, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
    sOutFmt.nFrameWidth = DEFAULT_FRM_WIDTH;
    sOutFmt.nFrameHeight = DEFAULT_FRM_HEIGHT;
    sOutFmt.eColorFormat = OMX_COLOR_Format16bitRGB565;
    sOutFmt.eCompressionFormat = OMX_VIDEO_CodingUnused;

    nInBufferCnt = DEFAULT_BUF_IN_CNT;
    nInBufferSize = DEFAULT_BUF_IN_SIZE;
    nOutBufferCnt = DEFAULT_BUF_OUT_CNT;
    nOutBufferSize = Align(sOutFmt.nFrameWidth,FRAME_ALIGN) * Align(sOutFmt.nFrameHeight,FRAME_ALIGN) * pxlfmt2bpp(sOutFmt.eColorFormat) / 8;
    if(sOutputCrop.nWidth == 0)
        sOutputCrop.nWidth = DEFAULT_FRM_WIDTH;
    if(sOutputCrop.nHeight == 0)
        sOutputCrop.nHeight = DEFAULT_FRM_HEIGHT;

    nInPortFormatCnt = 0;
    
    #ifdef MALONE_VPU
    nOutPortFormatCnt = 1;
    eOutPortPormat[0] = OMX_COLOR_FormatYCbYCr;
    eOutPortPormat[1] = OMX_COLOR_Format16bitRGB565;
    //disable it as opencl2d convert function has low performance
    //eOutPortPormat[2] = OMX_COLOR_FormatYUV420SemiPlanar;
    #endif

    LOG_DEBUG("V4l2Dec::SetDefaultPortSetting SUCCESS \n");

    //this will start decoding without sending the OMX_EventPortSettingsChanged.
    //android decoders prefer not to send the event
    bSendPortSettingChanged = OMX_TRUE;

    nMaxDurationMsThr=-1;
    nMaxBufCntThr=-1;
    bUseDmaBuffer = OMX_TRUE;
    pDmaBuffer = NULL;
    nDmaBufferCnt = DEFAULT_DMA_BUF_CNT;
    eDmaBufferFormat = OMX_COLOR_FormatYUV420SemiPlanar;
    nDmaBufferSize[0] = Align(sOutFmt.nFrameWidth,FRAME_ALIGN) * Align(sOutFmt.nFrameHeight,FRAME_ALIGN) * pxlfmt2bpp(eDmaBufferFormat) / 8;
    nDmaBufferSize[1] = nDmaBufferSize[0] / 2;
    nDmaBufferSize[2] = 0;

    LOG_DEBUG("V4l2Dec::SetDefaultPortSetting bUseDmaBuffer=%d \n",bUseDmaBuffer);
    bUseDmaInputBuffer = OMX_FALSE;
    pDmaInputBuffer = NULL;

    bUseDmaOutputBuffer = OMX_FALSE;
    pDmaOutputBuffer = NULL;
    
    return OMX_ErrorNone;
}
OMX_ERRORTYPE V4l2Dec::QueryVideoProfile(OMX_PTR pComponentParameterStructure)
{
    struct CodecProfileLevel {
        OMX_U32 mProfile;
        OMX_U32 mLevel;
    };

    static const CodecProfileLevel kProfileLevels[] = {
        { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel51 },
        { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel51 },
        { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel51 },
    };
    
    static const CodecProfileLevel kM4VProfileLevels[] = {
        { OMX_VIDEO_MPEG4ProfileSimple, 0x100 /*OMX_VIDEO_MPEG4Level6*/ },
        { OMX_VIDEO_MPEG4ProfileAdvancedSimple, OMX_VIDEO_MPEG4Level5 },
    };
    
    static const CodecProfileLevel kMpeg2ProfileLevels[] = {
        { OMX_VIDEO_MPEG2ProfileSimple, OMX_VIDEO_MPEG2LevelHL},
        { OMX_VIDEO_MPEG2ProfileMain, OMX_VIDEO_MPEG2LevelHL},
    };

    static const CodecProfileLevel kHevcProfileLevels[] = {
        { OMX_VIDEO_HEVCProfileMain, OMX_VIDEO_HEVCMainTierLevel51 },
    };

    static const CodecProfileLevel kH263ProfileLevels[] = {
        { OMX_VIDEO_H263ProfileBaseline, OMX_VIDEO_H263Level70 },
        { OMX_VIDEO_H263ProfileISWV2,    OMX_VIDEO_H263Level70 }
    };

    static const CodecProfileLevel kVp8ProfileLevels[] = {
        { OMX_VIDEO_VP8ProfileMain,     OMX_VIDEO_VP8Level_Version0 },
    };

    OMX_VIDEO_PARAM_PROFILELEVELTYPE  *pPara;
    OMX_S32 index;
    OMX_S32 nProfileLevels;
    
    pPara = (OMX_VIDEO_PARAM_PROFILELEVELTYPE *)pComponentParameterStructure;

    LOG_DEBUG("QueryVideoProfile CodingType=%d",CodingType);

    switch((int)CodingType)
    {
        case OMX_VIDEO_CodingAVC:
            index = pPara->nProfileIndex;

            nProfileLevels =sizeof(kProfileLevels) / sizeof(kProfileLevels[0]);
            if (index >= nProfileLevels) {
                return OMX_ErrorNoMore;
            }

            pPara->eProfile = kProfileLevels[index].mProfile;
            pPara->eLevel = kProfileLevels[index].mLevel;
            LOG_DEBUG("V4l2Dec::QueryVideoProfile pPara->eProfile=%x,level=%x\n",pPara->eProfile,pPara->eLevel);
            break;
        case OMX_VIDEO_CodingHEVC:
            index = pPara->nProfileIndex;

            nProfileLevels =sizeof(kHevcProfileLevels) / sizeof(kHevcProfileLevels[0]);
            if (index >= nProfileLevels) {
                return OMX_ErrorNoMore;
            }

            pPara->eProfile = kHevcProfileLevels[index].mProfile;
            pPara->eLevel = kHevcProfileLevels[index].mLevel;
            LOG_DEBUG("QueryVideoProfile profile=%d,level=%d",pPara->eProfile, pPara->eLevel);
            break;
        case OMX_VIDEO_CodingMPEG4:
            index = pPara->nProfileIndex;

            nProfileLevels =sizeof(kM4VProfileLevels) / sizeof(kM4VProfileLevels[0]);
            if (index >= nProfileLevels) {
                return OMX_ErrorNoMore;
            }

            pPara->eProfile = kM4VProfileLevels[index].mProfile;
            pPara->eLevel = kM4VProfileLevels[index].mLevel;
            break;
        case OMX_VIDEO_CodingMPEG2:
            index = pPara->nProfileIndex;

            nProfileLevels =sizeof(kMpeg2ProfileLevels) / sizeof(kMpeg2ProfileLevels[0]);
            if (index >= nProfileLevels) {
                return OMX_ErrorNoMore;
            }

            pPara->eProfile = kMpeg2ProfileLevels[index].mProfile;
            pPara->eLevel = kMpeg2ProfileLevels[index].mLevel;
            break;
        case OMX_VIDEO_CodingH263:
            index = pPara->nProfileIndex;

            nProfileLevels =sizeof(kH263ProfileLevels) / sizeof(kH263ProfileLevels[0]);
            if (index >= nProfileLevels) {
                return OMX_ErrorNoMore;
            }

            pPara->eProfile = kH263ProfileLevels[index].mProfile;
            pPara->eLevel = kH263ProfileLevels[index].mLevel;
            break;
        case 9://google index OMX_VIDEO_CodingVP8:
            index = pPara->nProfileIndex;

            nProfileLevels =sizeof(kVp8ProfileLevels) / sizeof(kVp8ProfileLevels[0]);
            if (index >= nProfileLevels) {
                return OMX_ErrorNoMore;
            }

            pPara->eProfile = kVp8ProfileLevels[index].mProfile;
            pPara->eLevel = kVp8ProfileLevels[index].mLevel;
            break;
        default:
            return OMX_ErrorUnsupportedIndex;
    }

    return OMX_ErrorNone;

}

OMX_ERRORTYPE V4l2Dec::SetRoleFormat(OMX_STRING role)
{
    OMX_BOOL bGot = OMX_FALSE;
    OMX_U32 i = 0;
    for(i = 0; i < sizeof(role_table)/sizeof(V4L2_DEC_ROLE); i++){
        if(fsl_osal_strcmp(role, role_table[i].role) == 0){
            CodingType = (OMX_VIDEO_CODINGTYPE)role_table[i].type;
            bGot = OMX_TRUE;
            fsl_osal_strcpy((fsl_osal_char*)name, role_table[i].name);
            break;
        }
    }

    if(bGot)
        return OMX_ErrorNone;
    else 
        return OMX_ErrorUndefined;
}

OMX_ERRORTYPE V4l2Dec::SetParameter(
        OMX_INDEXTYPE nParamIndex,
        OMX_PTR pComponentParameterStructure)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    switch ((OMX_U32)nParamIndex) {
        case OMX_IndexParamStandardComponentRole:
        {
            fsl_osal_strcpy( (fsl_osal_char *)cRole,(fsl_osal_char *)((OMX_PARAM_COMPONENTROLETYPE*)pComponentParameterStructure)->cRole);
            ret = SetRoleFormat((OMX_STRING)cRole);
            if(ret == OMX_ErrorNone){
                if(sInFmt.eCompressionFormat!=CodingType){
                    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;

                    OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
                    sPortDef.nPortIndex = IN_PORT;
                    ports[IN_PORT]->GetPortDefinition(&sPortDef);
                    sInFmt.eCompressionFormat=CodingType;
                    //decoder
                    fsl_osal_memcpy(&(sPortDef.format.video), &sInFmt, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
                    ports[IN_PORT]->SetPortDefinition(&sPortDef);
                    HandleFormatChanged(IN_PORT);
                }
            }
            LOG_LOG("SetParameter OMX_IndexParamStandardComponentRole.type=%d\n",CodingType);
        }
        break;
        case OMX_IndexParamVideoPortFormat:
        {
            OMX_VIDEO_PARAM_PORTFORMATTYPE * pPara;
            pPara=(OMX_VIDEO_PARAM_PORTFORMATTYPE *)pComponentParameterStructure;
            ASSERT(pPara->nPortIndex==IN_PORT);
            //set port
            if((sInFmt.eCompressionFormat!=pPara->eCompressionFormat)
                ||(sInFmt.eColorFormat!=pPara->eColorFormat))
            {
                OMX_PARAM_PORTDEFINITIONTYPE sPortDef;

                OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
                sPortDef.nPortIndex = IN_PORT;
                ports[IN_PORT]->GetPortDefinition(&sPortDef);
                sInFmt.eCompressionFormat=pPara->eCompressionFormat;
                sInFmt.eColorFormat=pPara->eColorFormat;
                //sInputParam.nFrameRate=pPara->xFramerate/Q16_SHIFT;
                fsl_osal_memcpy(&(sPortDef.format.video), &sInFmt, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
                ports[IN_PORT]->SetPortDefinition(&sPortDef);
                LOG_DEBUG("SetParameter OMX_IndexParamVideoPortFormat.format=%x,f2=%x\n",
                    sInFmt.eCompressionFormat,sInFmt.eColorFormat);

            }
        }
        break;
        case OMX_IndexParamVideoRegisterFrameExt:
        {
            OMX_VIDEO_REG_FRM_EXT_INFO* pExtInfo=(OMX_VIDEO_REG_FRM_EXT_INFO*)pComponentParameterStructure;
            if(pExtInfo->nPortIndex==OUT_PORT){
                 LOG_DEBUG("set OMX_IndexParamVideoRegisterFrameExt cnt=%d",pExtInfo->nMaxBufferCnt);
                 nDmaBufferCnt = pExtInfo->nMaxBufferCnt;
                 sOutFmt.nFrameWidth = pExtInfo->nWidthStride;
                 sOutFmt.nFrameWidth = pExtInfo->nHeightStride;
                 #if 0
                OMX_U32 flag = COLOR_FORMAT_FLAG_SINGLE_PLANE;
                V4l2ObjectFormat sFormat;
                bAdaptivePlayback = OMX_TRUE;
                sFormat.width = sOutFmt.nFrameWidth = pExtInfo->nWidthStride;
                sFormat.height = sOutFmt.nFrameHeight = pExtInfo->nHeightStride;
                sFormat.stride = sOutFmt.nStride = pExtInfo->nWidthStride;
                if(nOutputPlane == 2)
                    flag = COLOR_FORMAT_FLAG_2_PLANE;
                sFormat.format = ConvertOmxColorFormatToV4l2Format(sOutFmt.eColorFormat,flag);
                sFormat.bufferSize = nOutBufferSize;
                ret = outObj->SetFormat(&sFormat);
                if(ret != OMX_ErrorNone)
                    break;
                #endif
                OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
                
                OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
                sPortDef.nPortIndex = OUT_PORT;
                ports[OUT_PORT]->GetPortDefinition(&sPortDef);
                sPortDef.format.video.nFrameWidth = sOutFmt.nFrameWidth;
                sPortDef.format.video.nFrameHeight = sOutFmt.nFrameHeight;
                sPortDef.format.video.nStride = sOutFmt.nStride;

                ports[OUT_PORT]->SetPortDefinition(&sPortDef);


            }
        }
        break;
        case OMX_IndexParamDecoderCachedThreshold:
        {
            OMX_DECODER_CACHED_THR* pDecCachedInfo=(OMX_DECODER_CACHED_THR*)pComponentParameterStructure;
            if(pDecCachedInfo->nPortIndex==IN_PORT)
            {
                nMaxDurationMsThr=pDecCachedInfo->nMaxDurationMsThreshold;
                nMaxBufCntThr=pDecCachedInfo->nMaxBufCntThreshold;
            }
        }
        break;
        case OMX_IndexParamVideoWmv:
        {
            OMX_VIDEO_PARAM_WMVTYPE  *pPara;
            pPara = (OMX_VIDEO_PARAM_WMVTYPE *)pComponentParameterStructure;
            eWmvFormat = pPara->eFormat;
        }
        break;
        default:
            ret = OMX_ErrorNotImplemented;
            break;
    }
    return ret;
}

OMX_ERRORTYPE V4l2Dec::GetParameter(
        OMX_INDEXTYPE nParamIndex,
        OMX_PTR pComponentParameterStructure)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    switch ((OMX_U32)nParamIndex) {
        case OMX_IndexParamStandardComponentRole:
        {
            fsl_osal_strcpy((OMX_STRING)((OMX_PARAM_COMPONENTROLETYPE*)pComponentParameterStructure)->cRole,(OMX_STRING)cRole);
        }
        break;
        case OMX_IndexParamVideoProfileLevelQuerySupported:
            ret = QueryVideoProfile(pComponentParameterStructure);
            break;
        case OMX_IndexParamVideoWmv:
        {
            OMX_VIDEO_PARAM_WMVTYPE  *pPara;
            pPara = (OMX_VIDEO_PARAM_WMVTYPE *)pComponentParameterStructure;
            pPara->eFormat = eWmvFormat;
        }
        break;

        default:
            ret = OMX_ErrorNotImplemented;
            break;
    }
    return ret;

}

OMX_ERRORTYPE V4l2Dec::GetConfig(
        OMX_INDEXTYPE nParamIndex,
        OMX_PTR pComponentParameterStructure)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    switch (nParamIndex) {
        case OMX_IndexConfigCommonOutputCrop:
        {
            OMX_CONFIG_RECTTYPE *pCropDef;
            pCropDef = (OMX_CONFIG_RECTTYPE*)pComponentParameterStructure;
            OMX_CHECK_STRUCT(pCropDef, OMX_CONFIG_RECTTYPE, ret);
            if(ret != OMX_ErrorNone)
                break;

            if(pCropDef->nPortIndex != OUT_PORT){
                ret = OMX_ErrorBadParameter;
                break;
            }

            OMX_CONFIG_RECTTYPE rect;
            ret = outObj->GetCrop(&rect);

            if(rect.nTop >= 0 && rect.nLeft >= 0 && rect.nWidth > 0 && rect.nHeight > 0){
                pCropDef->nTop = rect.nTop;
                pCropDef->nLeft = rect.nLeft;
                pCropDef->nWidth = rect.nWidth;
                pCropDef->nHeight = rect.nHeight;
            }
            else{
                pCropDef->nTop = sOutputCrop.nTop;
                pCropDef->nLeft = sOutputCrop.nLeft;
                pCropDef->nWidth = sOutputCrop.nWidth;
                pCropDef->nHeight = sOutputCrop.nHeight;
            }
            LOG_DEBUG("GetConfig OMX_IndexConfigCommonOutputCrop OUT_PORT top=%d,left=%d,w=%d,h=%d\n",
                pCropDef->nTop,pCropDef->nLeft,pCropDef->nWidth,pCropDef->nHeight);
        }
        break;
        default:
            ret = OMX_ErrorNotImplemented;
            break;
    }
    return ret;
}
OMX_ERRORTYPE V4l2Dec::SetConfig(
        OMX_INDEXTYPE nParamIndex,
        OMX_PTR pComponentParameterStructure)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    switch ((OMX_U32)nParamIndex) {
        case OMX_IndexConfigCommonOutputCrop:
        {
            OMX_CONFIG_RECTTYPE *pCropDef;
            pCropDef = (OMX_CONFIG_RECTTYPE*)pComponentParameterStructure;
            OMX_CHECK_STRUCT(pCropDef, OMX_CONFIG_RECTTYPE, ret);
            if(ret != OMX_ErrorNone)
                break;

            if(pCropDef->nPortIndex != OUT_PORT){
                ret = OMX_ErrorBadParameter;
                break;
            }
            ret = outObj->SetCrop(pCropDef);

            fsl_osal_memcpy(&sOutputCrop, pCropDef, sizeof(OMX_CONFIG_RECTTYPE));
        LOG_DEBUG("SetConfig OMX_IndexConfigCommonOutputCrop OUT_PORT crop w=%d\n",pCropDef->nWidth);
        }
        break;
        default:
            ret = OMX_ErrorNotImplemented;
            break;
    }
    return ret;
}
OMX_ERRORTYPE V4l2Dec::ProcessInit()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    CheckIfNeedFrameParser();
    if(bEnabledFrameParser){
        pParser = FSL_NEW(FrameParser,());
        if(pParser ==NULL)
            return OMX_ErrorInsufficientResources;
        ret = pParser->Create(sInFmt.eCompressionFormat);
        if(ret != OMX_ErrorNone)
            return ret;
    }

    bEnabledPostProcess = CheckIfNeedPostProcess();
    if(bEnabledPostProcess && pPostProcess == NULL){
        if(sOutFmt.eColorFormat == OMX_COLOR_Format16bitRGB565 || sOutFmt.eColorFormat == OMX_COLOR_FormatYCbYCr)
            pPostProcess = FSL_NEW( G2dProcess,());
        else if(sOutFmt.eColorFormat == OMX_COLOR_FormatYUV420SemiPlanar)
            pPostProcess = FSL_NEW( OpenCL2dProcess,());

        if(pPostProcess == NULL)
            return OMX_ErrorInsufficientResources;

        ret = pPostProcess->Create(PROCESS3_BUF_DMA_IN_OMX_OUT);
        if(ret != OMX_ErrorNone)
            return ret;

        ret = pPostProcess->Start();
        if(ret != OMX_ErrorNone)
            return ret;
    }

    if(bUseDmaBuffer && pDmaBuffer == NULL){
        pDmaBuffer = FSL_NEW( DmaBuffer,());
        if(pDmaBuffer == NULL)
            return OMX_ErrorInsufficientResources;

        ret = pDmaBuffer->Create(nOutputPlane);
        if(ret != OMX_ErrorNone)
            return ret;

        LOG_DEBUG("V4l2Dec::ProcessInit pDmaBuffer->Create SUCCESS \n");
    }

    return ret;
}
OMX_ERRORTYPE V4l2Dec::CheckIfNeedFrameParser()
{
    switch((int)sInFmt.eCompressionFormat){
        case OMX_VIDEO_CodingAVC:
        case OMX_VIDEO_CodingHEVC:
            bEnabledFrameParser = OMX_TRUE;
            LOG_DEBUG("CheckIfNeedFrameParser bEnabledFrameParser\n");
            break;
        default:
            bEnabledFrameParser = OMX_FALSE;
            break;
    }
    return OMX_ErrorNone;
}
OMX_ERRORTYPE V4l2Dec::GetInputDataDepthThreshold(OMX_S32* pDurationThr, OMX_S32* pBufCntThr)
{
    /*
      for some application, such rtsp/http, we need to set some thresholds to avoid input data is consumed by decoder too fast.
      -1: no threshold
    */
    *pDurationThr=nMaxDurationMsThr;
    *pBufCntThr=nMaxBufCntThr;
    return OMX_ErrorNone;
}

OMX_BOOL V4l2Dec::CheckIfNeedPostProcess()
{
    return OMX_TRUE;
}
OMX_ERRORTYPE V4l2Dec::DoAllocateBuffer(OMX_PTR *buffer, OMX_U32 nSize,OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    LOG_LOG("DoAllocateBuffer port=%d,size=%d\n",nPortIndex,nSize);
    if(nPortIndex == IN_PORT){
        if(!bSetInputBufferCount){
            LOG_LOG("DoAllocateBuffer set inport buffer count=%d\n",nInBufferCnt);
            ret = inObj->SetBufferCount(nInBufferCnt,V4L2_MEMORY_MMAP,nInputPlane);

            if(ret != OMX_ErrorNone){
                LOG_ERROR("inObj->SetBufferCount FAILD");
                return ret;
            }

            bSetInputBufferCount = OMX_TRUE;
        }

        if(nInBufferNum > (OMX_S32)nInBufferCnt){
            LOG_ERROR("nInBufferNum=%d,nInBufferCnt=%d",nInBufferNum, nInBufferCnt);
            return OMX_ErrorInsufficientResources;
        }

        *buffer = inObj->AllocateBuffer(nSize);

        if(*buffer != NULL)
            nInBufferNum++;

    }else if(nPortIndex == OUT_PORT){

        if(bUseDmaBuffer){

            if(!bSetOutputBufferCount){
                LOG_LOG("DoAllocateBuffer set dma buffer count=%d\n",nDmaBufferCnt);
                ret = outObj->SetBufferCount(nDmaBufferCnt,V4L2_MEMORY_DMABUF,nOutputPlane);

                if(ret != OMX_ErrorNone)
                    return ret;

                bSetOutputBufferCount = OMX_TRUE;
            }
            bUseDmaOutputBuffer = OMX_TRUE;

            if(bUseDmaOutputBuffer && pDmaOutputBuffer == NULL){
                pDmaOutputBuffer = FSL_NEW( DmaBuffer,());
                if(pDmaOutputBuffer == NULL)
                    return OMX_ErrorInsufficientResources;
                //do not call create because only call AllocateForOutput() and Free
                //ret = pDmaInputBuffer->Create(nInputPlane);
                //if(ret != OMX_ErrorNone)
                //    return ret;
            }

            LOG_LOG("pDmaBuffer->AllocateForOutput ");
            ret = pDmaOutputBuffer->AllocateForOutput(nSize, buffer);
            if(ret == OMX_ErrorNone){
                nOutBufferNum++;
                return OMX_ErrorNone;
            }else
                return OMX_ErrorInsufficientResources;
        }

        if(!bSetOutputBufferCount){
            LOG_LOG("DoAllocateBuffer set outport buffer count=%d\n",nOutBufferCnt);
            ret = outObj->SetBufferCount(nOutBufferCnt,V4L2_MEMORY_MMAP,nOutputPlane);

            if(ret != OMX_ErrorNone)
                return ret;

            bSetOutputBufferCount = OMX_TRUE;
        }

        if(nOutBufferNum > (OMX_S32)nOutBufferCnt)
            return OMX_ErrorInsufficientResources;

        *buffer = outObj->AllocateBuffer(nSize);
        if(*buffer != NULL)
            nOutBufferNum++;
    }

    if (*buffer == NULL){
        LOG_ERROR("DoAllocateBuffer OMX_ErrorInsufficientResources");
        return OMX_ErrorInsufficientResources;
    }

    return OMX_ErrorNone;
}
OMX_ERRORTYPE V4l2Dec::DoFreeBuffer(OMX_PTR buffer,OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    if(nPortIndex == IN_PORT){

        ret = inObj->FreeBuffer(buffer);

        if(ret != OMX_ErrorNone)
            return ret;
        if(nInBufferNum > 0)
            nInBufferNum --;

    }else if(nPortIndex == OUT_PORT){

        if(bUseDmaOutputBuffer){
            LOG_LOG("call pDmaBuffer->Free");
            ret = pDmaOutputBuffer->Free(buffer);
            if(ret == OMX_ErrorNone)
                nOutBufferNum --;
            return ret;
        }

        if((OMX_U64)buffer == 0x00000001){
            LOG_LOG("DoFreeBuffer output 01 %d\n",nOutBufferNum);
            return OMX_ErrorNone;
        }

        LOG_LOG("DoFreeBuffer output %d\n",nOutBufferNum);
        ret = outObj->FreeBuffer(buffer);
        if(ret != OMX_ErrorNone)
            return ret;
        nOutBufferNum --;
    }

    return OMX_ErrorNone;
}
OMX_ERRORTYPE V4l2Dec::DoUseBuffer(OMX_PTR buffer,OMX_U32 nSize,OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    LOG_LOG("DoUseBuffer nPortIndex=%d",nPortIndex);
    if(nPortIndex == IN_PORT){
        if(nInBufferNum != 0)
            return OMX_ErrorInsufficientResources;
        if(!bSetInputBufferCount){
            LOG_DEBUG("DoUseBuffer set inport buffer count=%d",nInBufferCnt);

            if(eDevType == V4L2_DEV_TYPE_DECODER){
                ret = inObj->SetBufferCount(nInBufferCnt,V4L2_MEMORY_USERPTR,nInputPlane);
            }
            bSetInputBufferCount = OMX_TRUE;
        }

    }else if(nPortIndex == OUT_PORT){
        if(nOutBufferNum != 0)
            return OMX_ErrorInsufficientResources;
        if(!bSetOutputBufferCount){

            LOG_DEBUG("V4l2Dec::DoUseBuffer bUseDmaBuffer=%d \n",bUseDmaBuffer);
            if(bUseDmaBuffer){
                LOG_DEBUG("DoUseBuffer set outport buffer nDmaBufferCnt=%d plane num=%d",nDmaBufferCnt,nOutputPlane);
                ret = outObj->SetBufferCount(nDmaBufferCnt, V4L2_MEMORY_DMABUF, nOutputPlane);
            }else if(bEnabledPostProcess){
                if(nOutBufferCnt <= PROCESS3_BUF_CNT)
                    return OMX_ErrorInsufficientResources;
                ret = outObj->SetBufferCount(nOutBufferCnt - PROCESS3_BUF_CNT,V4L2_MEMORY_USERPTR,nOutputPlane);
            }else
                ret = outObj->SetBufferCount(nOutBufferCnt,V4L2_MEMORY_USERPTR,nOutputPlane);

            bSetOutputBufferCount = OMX_TRUE;
        }
    }

    if(ret != OMX_ErrorNone)
        LOG_ERROR("DoUseBuffer failed,ret=%d",ret);

    return ret;

}

/**< C style functions to expose entry point for the shared library */
extern "C"
{
    OMX_ERRORTYPE VpuDecoderInit(OMX_IN OMX_HANDLETYPE pHandle)
    {
        OMX_ERRORTYPE ret = OMX_ErrorNone;
        V4l2Dec *obj = NULL;
        ComponentBase *base = NULL;

        obj = FSL_NEW(V4l2Dec, ());
        if(obj == NULL)
        {
            return OMX_ErrorInsufficientResources;
        }
        base = (ComponentBase*)obj;
        ret = base->ConstructComponent(pHandle);
        if(ret != OMX_ErrorNone)
        {
            return ret;
        }
        return ret;
    }
}
