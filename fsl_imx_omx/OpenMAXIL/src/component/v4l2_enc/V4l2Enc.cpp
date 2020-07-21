/**
 *  Copyright 2018 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

#include "V4l2Enc.h"
#include "G2dProcess.h"

#if 0
#undef LOG_DEBUG
#define LOG_DEBUG printf
#undef LOG_LOG
#define LOG_LOG printf

#endif

#define DEFAULT_FRM_WIDTH       (1280)
#define DEFAULT_FRM_HEIGHT      (720)
#define DEFAULT_FRM_RATE        (30 * Q16_SHIFT)

#define DEFAULT_BUF_IN_CNT  (4)
#define DEFAULT_BUF_IN_SIZE (DEFAULT_FRM_WIDTH*DEFAULT_FRM_HEIGHT*3/2)

#define DEFAULT_BUF_OUT_CNT    (3)//at least 2 buffers, one for codec data
#define DEFAULT_BUF_OUT_SIZE    (1024*1024*3/2)

#define DEFAULT_DMA_BUF_CNT    (3)

typedef struct{
OMX_U32 type;
const char* role;
const char* name;
}V4L2_ENC_ROLE;

static V4L2_ENC_ROLE role_table[]={
{OMX_VIDEO_CodingAVC,"video_encoder.avc","OMX.Freescale.std.video_encoder.avc.hw-based"},
};


V4l2Enc::V4l2Enc()
{
    eDevType = V4L2_DEV_TYPE_ENCODER;
}
OMX_ERRORTYPE V4l2Enc::SetDefaultPortSetting()
{
    fsl_osal_strcpy((fsl_osal_char*)name, "OMX.Freescale.std.video_encoder.avc.hw-based");

    fsl_osal_memset(&sInFmt, 0, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
    sInFmt.nFrameWidth = DEFAULT_FRM_WIDTH;
    sInFmt.nFrameHeight = DEFAULT_FRM_HEIGHT;
    sInFmt.xFramerate = DEFAULT_FRM_RATE;
    sInFmt.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
    sInFmt.eCompressionFormat = OMX_VIDEO_CodingUnused;

    fsl_osal_memset(&sOutFmt, 0, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
    sOutFmt.nFrameWidth = DEFAULT_FRM_WIDTH;
    sOutFmt.nFrameHeight = DEFAULT_FRM_HEIGHT;
    sOutFmt.eColorFormat = OMX_COLOR_FormatUnused;
    sOutFmt.eCompressionFormat = OMX_VIDEO_CodingAVC;

    nInBufferCnt = DEFAULT_BUF_IN_CNT;
    nInBufferSize = DEFAULT_BUF_IN_SIZE;
    nOutBufferCnt = DEFAULT_BUF_OUT_CNT;
    nOutBufferSize = DEFAULT_BUF_OUT_SIZE;

    nInPortFormatCnt = 3;
    eInPortPormat[0] = OMX_COLOR_FormatYUV420SemiPlanar;
    eInPortPormat[1] = OMX_COLOR_FormatYUV420Planar;
    eInPortPormat[2] = (OMX_COLOR_FORMATTYPE)OMX_COLOR_FormatAndroidOpaque;

    nOutPortFormatCnt = 1;
    eOutPortPormat[0] = OMX_COLOR_FormatUnused;

    bUseDmaBuffer = OMX_FALSE;
    pDmaBuffer = NULL;
    nDmaBufferCnt = 0;
    eDmaBufferFormat = OMX_COLOR_FormatYUV420SemiPlanar;
    nDmaBufferSize[0] = 0;
    nDmaBufferSize[1] = 0;
    nDmaBufferSize[2] = 0;

    bUseDmaInputBuffer = OMX_TRUE;
    pDmaInputBuffer = NULL;

    bUseDmaOutputBuffer = OMX_FALSE;
    pDmaOutputBuffer = NULL;

    bSendPortSettingChanged = OMX_FALSE;

    nProfile = OMX_VIDEO_AVCProfileBaseline;
    nLevel = OMX_VIDEO_AVCLevel1;
    LOG_DEBUG("V4l2Enc::SetDefaultPortSetting SUCCESS\n");
    //android cts will enable the bStoreMetaData only once, so do not reset the bStoreMetaData
    if(bStoreMetaData)
        nInBufferSize = 12;
    return OMX_ErrorNone;
}
OMX_ERRORTYPE V4l2Enc::QueryVideoProfile(OMX_PTR pComponentParameterStructure)
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

    OMX_VIDEO_PARAM_PROFILELEVELTYPE  *pPara;
    OMX_S32 index;
    OMX_S32 nProfileLevels;
    
    pPara = (OMX_VIDEO_PARAM_PROFILELEVELTYPE *)pComponentParameterStructure;


    switch(CodingType)
    {
        case OMX_VIDEO_CodingAVC:
            index = pPara->nProfileIndex;

            nProfileLevels =sizeof(kProfileLevels) / sizeof(kProfileLevels[0]);
            if (index >= nProfileLevels) {
                return OMX_ErrorNoMore;
            }

            pPara->eProfile = kProfileLevels[index].mProfile;
            pPara->eLevel = kProfileLevels[index].mLevel;
            LOG_DEBUG("V4l2Enc::QueryVideoProfile pPara->eProfile=%x,level=%x\n",pPara->eProfile,pPara->eLevel);
            break;
        default:
            return OMX_ErrorUnsupportedIndex;
            break;
    }

    return OMX_ErrorNone;

}

OMX_ERRORTYPE V4l2Enc::SetRoleFormat(OMX_STRING role)
{
    OMX_BOOL bGot = OMX_FALSE;
    OMX_U32 i = 0;
    for(i = 0; i < sizeof(role_table)/sizeof(V4L2_ENC_ROLE); i++){
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

OMX_ERRORTYPE V4l2Enc::SetParameter(
        OMX_INDEXTYPE nParamIndex,
        OMX_PTR pComponentParameterStructure)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    LOG_DEBUG("GetParameter index=%x",nParamIndex);

    switch ((OMX_U32)nParamIndex) {
        case OMX_IndexParamStandardComponentRole:
        {
            fsl_osal_strcpy( (fsl_osal_char *)cRole,(fsl_osal_char *)((OMX_PARAM_COMPONENTROLETYPE*)pComponentParameterStructure)->cRole);
            ret = SetRoleFormat((OMX_STRING)cRole);
            if(ret == OMX_ErrorNone){
                if(sOutFmt.eCompressionFormat != CodingType){
                    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;

                    OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
                    sPortDef.nPortIndex = OUT_PORT;
                    ports[OUT_PORT]->GetPortDefinition(&sPortDef);
                    sOutFmt.eCompressionFormat=CodingType;
                    //decoder
                    fsl_osal_memcpy(&(sPortDef.format.video), &sOutFmt, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
                    ports[OUT_PORT]->SetPortDefinition(&sPortDef);
                }
            }
            LOG_LOG("SetParameter OMX_IndexParamStandardComponentRole.type=%d\n",CodingType);
        }
        break;
        case OMX_IndexParamStoreMetaDataInBuffers:
        {
            OMX_CONFIG_BOOLEANTYPE *pParams = (OMX_CONFIG_BOOLEANTYPE*)pComponentParameterStructure;
            bStoreMetaData = pParams->bEnabled;
            LOG_DEBUG("SetParameter OMX_IndexParamStoreMetaDataInBuffers.bStoreMetaData=%d\n",bStoreMetaData);
        }
        break;
        case OMX_IndexParamVideoPortFormat:
        {
            OMX_VIDEO_PARAM_PORTFORMATTYPE * pPara;
            pPara=(OMX_VIDEO_PARAM_PORTFORMATTYPE *)pComponentParameterStructure;
            if(pPara->nPortIndex == IN_PORT){
                if(pPara->eCompressionFormat != OMX_VIDEO_CodingUnused ||
                    (pPara->eColorFormat != eInPortPormat[0] && pPara->eColorFormat != eInPortPormat[1]
                    && pPara->eColorFormat != eInPortPormat[2]))
                    return OMX_ErrorUnsupportedSetting;

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
                    LOG_DEBUG("SetParameter OMX_IndexParamVideoPortFormat IN_PORT eCompressionFormat=%x,eColorFormat=%x\n",
                        sInFmt.eCompressionFormat,sInFmt.eColorFormat);
                }
            }else if(pPara->nPortIndex == OUT_PORT){
                if(pPara->eCompressionFormat != OMX_VIDEO_CodingAVC || pPara->eColorFormat != OMX_COLOR_FormatUnused)
                    return OMX_ErrorUnsupportedSetting;

                //set port
                if((sOutFmt.eCompressionFormat!=pPara->eCompressionFormat)
                    ||(sOutFmt.eColorFormat!=pPara->eColorFormat))
                {
                    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;

                    OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
                    sPortDef.nPortIndex = OUT_PORT;
                    ports[OUT_PORT]->GetPortDefinition(&sPortDef);
                    sOutFmt.eCompressionFormat=pPara->eCompressionFormat;
                    sOutFmt.eColorFormat=pPara->eColorFormat;
                    //sInputParam.nFrameRate=pPara->xFramerate/Q16_SHIFT;
                    fsl_osal_memcpy(&(sPortDef.format.video), &sInFmt, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
                    ports[OUT_PORT]->SetPortDefinition(&sPortDef);
                    LOG_DEBUG("SetParameter OMX_IndexParamVideoPortFormat OUT_PORT eCompressionFormat=%x,eColorFormat=%x\n",
                        sOutFmt.eCompressionFormat,sOutFmt.eColorFormat);
                }

            }
        }
        break;
        case OMX_IndexParamVideoAvc:
        {
            OMX_VIDEO_PARAM_AVCTYPE * pPara;
            pPara=(OMX_VIDEO_PARAM_AVCTYPE *)pComponentParameterStructure;
            ASSERT(pPara->nPortIndex==OUT_PORT);
            //set AVC GOP size
            sInputParam.nGOPSize=pPara->nPFrames+pPara->nBFrames+1;
            nProfile = pPara->eProfile;
            nLevel = pPara->eLevel;
            LOG_DEBUG("SetParameter OMX_IndexParamVideoAvc.nGOPSize=%d\n",sInputParam.nGOPSize);
        }
        break;
        case OMX_IndexParamVideoH263:
        {
            OMX_VIDEO_PARAM_H263TYPE * pPara;
            pPara=(OMX_VIDEO_PARAM_H263TYPE *)pComponentParameterStructure;
            ASSERT(pPara->nPortIndex==IN_PORT);
            //set H263 GOP size
            sInputParam.nGOPSize=pPara->nPFrames+pPara->nBFrames+1;
        }
        break;
        case OMX_IndexParamVideoMpeg4:
        {
            OMX_VIDEO_PARAM_MPEG4TYPE * pPara;
            pPara=(OMX_VIDEO_PARAM_MPEG4TYPE *)pComponentParameterStructure;
            ASSERT(pPara->nPortIndex==IN_PORT);
            //set MPEG4 GOP size
            sInputParam.nGOPSize=pPara->nPFrames+pPara->nBFrames+1;
        }
        break;
        case OMX_IndexParamVideoIntraRefresh:
        {
            OMX_VIDEO_PARAM_INTRAREFRESHTYPE * pPara;
            pPara=(OMX_VIDEO_PARAM_INTRAREFRESHTYPE *)pComponentParameterStructure;
            ASSERT(pPara->nPortIndex==OUT_PORT);
        
            switch(pPara->eRefreshMode){
                case OMX_VIDEO_IntraRefreshCyclic:
                    sInputParam.nIntraFreshNum=pPara->nCirMBs;
                    break;
                case OMX_VIDEO_IntraRefreshAdaptive:
                    sInputParam.nIntraFreshNum=pPara->nAirMBs;
                    break;
                case OMX_VIDEO_IntraRefreshBoth:
                case OMX_VIDEO_IntraRefreshMax:
                default:
                    break;
                }
        }
        LOG_DEBUG("SetParameter OMX_IndexParamVideoIntraRefresh.nIntraFreshNum=%d\n",sInputParam.nIntraFreshNum);
        break;
        case OMX_IndexParamVideoBitrate:
        {
            OMX_VIDEO_PARAM_BITRATETYPE * pPara;
            pPara=(OMX_VIDEO_PARAM_BITRATETYPE *)pComponentParameterStructure;
            ASSERT(pPara->nPortIndex==OUT_PORT);
            //set bit rate
            switch (pPara->eControlRate)
            {
                case OMX_Video_ControlRateDisable:
                    //in this mode the encoder will ignore nTargetBitrate setting
                    //and use the appropriate Qp (nQpI, nQpP, nQpB) values for encoding
                    break;
                case OMX_Video_ControlRateVariable:
                    sInputParam.nBitRate=pPara->nTargetBitrate;
                    //Variable bit rate
                    break;
                case OMX_Video_ControlRateConstant:
                    //the encoder can modify the Qp values to meet the nTargetBitrate target
                    sInputParam.nBitRate=pPara->nTargetBitrate;
                    break;
                case OMX_Video_ControlRateVariableSkipFrames:
                    //Variable bit rate with frame skipping
                    //sInputParam.nEnableAutoSkip=1;
                    break;
                case OMX_Video_ControlRateConstantSkipFrames:
                    //Constant bit rate with frame skipping
                    //the encoder cannot modify the Qp values to meet the nTargetBitrate target.
                    //Instead, the encoder can drop frames to achieve nTargetBitrate
                    //sInputParam.nEnableAutoSkip=1;
                    break;
                case OMX_Video_ControlRateMax:
                    //Maximum value
                    if(sInputParam.nBitRate>(OMX_S32)pPara->nTargetBitrate)
                    {
                        sInputParam.nBitRate=pPara->nTargetBitrate;
                    }
                    break;
                default:
                    //unknown
                    ret = OMX_ErrorUnsupportedIndex;
                    break;
            }
        LOG_DEBUG("SetParameter OMX_IndexParamVideoBitrate bitrate=%d\n",sInputParam.nBitRate);

        }
        break;
        case OMX_IndexParamUseAndroidPrependSPSPPStoIDRFrames:
        {
            OMX_PARAM_PREPEND_SPSPPS_TO_IDR * pPara;
            pPara=(OMX_PARAM_PREPEND_SPSPPS_TO_IDR*)pComponentParameterStructure;
            bInsertSpsPps2IDR=pPara->bEnableSPSToIDR;
            LOG_DEBUG("SetParameter OMX_IndexParamUseAndroidPrependSPSPPStoIDRFrames bEnabledSPSIDR=%d\n",pPara->bEnableSPSToIDR);
        }
        break;
        default:
            ret = OMX_ErrorNotImplemented;
            break;
    }
    return ret;
}
OMX_ERRORTYPE V4l2Enc::GetParameter(
        OMX_INDEXTYPE nParamIndex,
        OMX_PTR pComponentParameterStructure)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    LOG_DEBUG("GetParameter index=%x",nParamIndex);
    switch ((OMX_U32)nParamIndex) {
        case OMX_IndexParamStandardComponentRole:
        {
            fsl_osal_strcpy((OMX_STRING)((OMX_PARAM_COMPONENTROLETYPE*)pComponentParameterStructure)->cRole,(OMX_STRING)cRole);
        }
        break;
        case OMX_IndexParamVideoProfileLevelQuerySupported:
            ret = QueryVideoProfile(pComponentParameterStructure);
        break;
        case OMX_IndexParamVideoBitrate:
        {
            OMX_VIDEO_PARAM_BITRATETYPE * pPara;
            pPara=(OMX_VIDEO_PARAM_BITRATETYPE *)pComponentParameterStructure;
            LOG_DEBUG("GetParameter OMX_VIDEO_PARAM_BITRATETYPE index=%d\n",pPara->nPortIndex);
            ASSERT(pPara->nPortIndex==OUT_PORT);
            //get bit rate
            if(0==sInputParam.nBitRate)
            {
            	//in this mode the encoder will ignore nTargetBitrate setting
            	//and use the appropriate Qp (nQpI, nQpP, nQpB) values for encoding
            	pPara->eControlRate=OMX_Video_ControlRateDisable;
            	pPara->nTargetBitrate=0;
            }
            else
            {
            	pPara->eControlRate=OMX_Video_ControlRateConstant;
            	pPara->nTargetBitrate=sInputParam.nBitRate;
            }
        }
        break;
        case OMX_IndexParamVideoMpeg4:
        {
            OMX_VIDEO_PARAM_MPEG4TYPE* pPara;
            pPara=(OMX_VIDEO_PARAM_MPEG4TYPE *)pComponentParameterStructure;
            pPara->eProfile=OMX_VIDEO_MPEG4ProfileSimple;
            pPara->eLevel=OMX_VIDEO_MPEG4Level0;
            pPara->nPFrames=sInputParam.nGOPSize-1;
            pPara->nBFrames=0;
        }
        break;
        case OMX_IndexParamVideoAvc:
        {
            OMX_VIDEO_PARAM_AVCTYPE* pPara;
            pPara=(OMX_VIDEO_PARAM_AVCTYPE *)pComponentParameterStructure;
            pPara->eProfile=(OMX_VIDEO_AVCPROFILETYPE)nProfile;
            pPara->eLevel=(OMX_VIDEO_AVCLEVELTYPE)nLevel;
            pPara->nPFrames=sInputParam.nGOPSize-1;
            pPara->nBFrames=0;
        }
        break;
        case OMX_IndexParamVideoH263:
        {
            OMX_VIDEO_PARAM_H263TYPE* pPara;
            pPara=(OMX_VIDEO_PARAM_H263TYPE *)pComponentParameterStructure;
            pPara->eProfile=OMX_VIDEO_H263ProfileBaseline;
            pPara->eLevel=OMX_VIDEO_H263Level10;
            pPara->nPFrames=sInputParam.nGOPSize-1;
            pPara->nBFrames=0;
        }
        break;
        case OMX_IndexParamVideoPortFormat:
        {
            OMX_VIDEO_PARAM_PORTFORMATTYPE * pPara;
            pPara=(OMX_VIDEO_PARAM_PORTFORMATTYPE *)pComponentParameterStructure;

            if(pPara->nPortIndex >= 2)
                return OMX_ErrorBadPortIndex;

            ret = ports[pPara->nPortIndex]->GetPortFormat(pPara);
            LOG_DEBUG("GET OMX_IndexParamVideoPortFormat index=%d, index=%d, format=%d,color=%x",
                pPara->nPortIndex,pPara->nIndex, pPara->eCompressionFormat,pPara->eColorFormat);
            break;
        }
        default:
            ret = OMX_ErrorNotImplemented;
            break;
    }
    return ret;

}
OMX_ERRORTYPE V4l2Enc::SetConfig(
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
        case OMX_IndexConfigVideoBitrate:
            {
            OMX_VIDEO_CONFIG_BITRATETYPE * pPara;
            pPara=(OMX_VIDEO_CONFIG_BITRATETYPE *)pComponentParameterStructure;
            //set bit rate
            ASSERT(pPara->nPortIndex==IN_PORT);
            sInputParam.nBitRate=pPara->nEncodeBitrate;
            LOG_DEBUG("SetConfig OMX_IndexConfigVideoBitrate\n");
            }
        break;
        case OMX_IndexConfigVideoIntraVOPRefresh://force i frame
            {
            OMX_CONFIG_INTRAREFRESHVOPTYPE * pPara;
            pPara=(OMX_CONFIG_INTRAREFRESHVOPTYPE *)pComponentParameterStructure;
            //set IDR refresh manually
            ASSERT(pPara->nPortIndex==IN_PORT);
            bRefreshIntra = pPara->IntraRefreshVOP;
            LOG_DEBUG("SetConfig OMX_IndexConfigVideoIntraVOPRefresh\n");
            }
        break;
        case OMX_IndexConfigCommonRotate:
            {
            OMX_CONFIG_ROTATIONTYPE * pPara;
            pPara=(OMX_CONFIG_ROTATIONTYPE *)pComponentParameterStructure;
            ASSERT(pPara->nPortIndex==IN_PORT);
            fsl_osal_memcpy(&Rotation, pPara, sizeof(OMX_CONFIG_ROTATIONTYPE));
            ret = pV4l2Dev->SetEncoderRotMode(nFd,Rotation.nRotation);
            LOG_DEBUG("SetConfig OMX_IndexConfigCommonRotate rotate=%d\n",pPara->nRotation);
            }
        break;
        case OMX_IndexConfigGrallocBufferParameter:
            {
            GRALLOC_BUFFER_PARAMETER * pPara;
            pPara=(GRALLOC_BUFFER_PARAMETER *)pComponentParameterStructure;
            ASSERT(pPara->nPortIndex==IN_PORT);
            if(sInFmt.eColorFormat!=pPara->eColorFormat){
                OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
                OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
                sPortDef.nPortIndex = IN_PORT;
                ports[IN_PORT]->GetPortDefinition(&sPortDef);
                sInFmt.eColorFormat=pPara->eColorFormat;
                fsl_osal_memcpy(&(sPortDef.format.video), &sInFmt, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
                ports[IN_PORT]->SetPortDefinition(&sPortDef);
            }
            LOG_DEBUG("SetConfig OMX_IndexConfigGrallocBufferParameter %x\n",sInFmt.eColorFormat);
            }
        break;
        case OMX_IndexConfigIntraRefresh:
            {
            OMX_VIDEO_CONFIG_INTRAREFRESHTYPE *pPara;
            pPara = (OMX_VIDEO_CONFIG_INTRAREFRESHTYPE*)pComponentParameterStructure;
            ASSERT(pPara->nPortIndex == OUT_PORT);
            sInputParam.nIntraFreshNum = pPara->nRefreshPeriod;
            }
        break;
        default:
            ret = OMX_ErrorNotImplemented;
            break;
    }
    return ret;
}
OMX_ERRORTYPE V4l2Enc::GetConfig(
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

            ret = outObj->GetCrop(&sOutputCrop);

            pCropDef->nTop = sOutputCrop.nTop;
            pCropDef->nLeft = sOutputCrop.nLeft;
            pCropDef->nWidth = sOutputCrop.nWidth;
            pCropDef->nHeight = sOutputCrop.nHeight;
        }
        break;
        case OMX_IndexConfigIntraRefresh:
        {
            OMX_VIDEO_CONFIG_INTRAREFRESHTYPE *pPara;
            pPara = (OMX_VIDEO_CONFIG_INTRAREFRESHTYPE*)pComponentParameterStructure;
            ASSERT(pPara->nPortIndex == OUT_PORT);
            pPara->nRefreshPeriod = sInputParam.nIntraFreshNum;
        }
        break;
        default:
            ret = OMX_ErrorNotImplemented;
            break;
    }
    return ret;
}
OMX_ERRORTYPE V4l2Enc::ProcessInit() 
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    LOG_DEBUG("ProcessDataBuffer bInit\n");

    CheckIfNeedPreProcess();
    if(bEnabledPreProcess && pPreProcess == NULL){
        OMX_U32 i = 0;
        OMX_BUFFERHEADERTYPE * pBuf = NULL;
        pPreProcess = FSL_NEW(G2dProcess,());
        if(pPreProcess == NULL)
            return OMX_ErrorInsufficientResources;

        ret = pPreProcess->Create(PROCESS3_BUF_OMX_IN_DMA_OUT);
        if(ret != OMX_ErrorNone)
            return ret;

        bUseDmaBuffer = OMX_TRUE;
        nDmaBufferCnt = DEFAULT_DMA_BUF_CNT;
        eDmaBufferFormat = OMX_COLOR_FormatYUV420SemiPlanar;
        nDmaBufferSize[0] = Align(sInFmt.nFrameWidth,FRAME_ALIGN) * Align(sInFmt.nFrameHeight,FRAME_ALIGN) * pxlfmt2bpp(eDmaBufferFormat) / 8;
        nDmaBufferSize[1] = nDmaBufferSize[0] / 2;
        nDmaBufferSize[2] = 0;

        ret = pPreProcess->Start();
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

    if(ret != OMX_ErrorNone)
        return ret;

    ret = pV4l2Dev->SetEncoderPrama(nFd,&sInputParam);

    if(ret != OMX_ErrorNone){
        LOG_WARNING("V4l2Enc::ProcessInit SetEncoderPrama has error.\n");
        ret = OMX_ErrorNone;
    }

    if(CodingType == OMX_VIDEO_CodingAVC){
        ret = pV4l2Dev->SetH264EncoderProfileAndLevel(nFd, nProfile, nLevel);
        if(ret != OMX_ErrorNone){
            LOG_WARNING("V4l2Enc::ProcessInit SetH264EncoderProfileAndLevel has error.\n");
            ret = OMX_ErrorNone;
        }

        pCodecDataConverter = FSL_NEW(FrameConverter,());
        if(OMX_ErrorNone == pCodecDataConverter->Create(CodingType))
            bEnabledAVCCConverter = OMX_TRUE;
    }

    return ret;
}

OMX_ERRORTYPE V4l2Enc::CheckIfNeedPreProcess()
{
    switch((int)sInFmt.eColorFormat){
        case OMX_COLOR_Format16bitRGB565:
        case OMX_COLOR_Format24bitRGB888:
        case OMX_COLOR_Format24bitBGR888:
        case OMX_COLOR_Format32bitBGRA8888:
        case OMX_COLOR_Format32bitRGBA8888:
        case OMX_COLOR_FormatYUV420Planar:
            bEnabledPreProcess = OMX_TRUE;
            LOG_DEBUG("CheckIfNeedPreProcess bEnabledPreProcess\n");
            break;
        default:
            bEnabledPreProcess = OMX_FALSE;
            break;
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE V4l2Enc::DoAllocateBuffer(OMX_PTR *buffer, OMX_U32 nSize,OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    LOG_LOG("V4l2Enc DoAllocateBuffer port=%d,size=%d\n",nPortIndex,nSize);
    if(nPortIndex == IN_PORT){

        if(bStoreMetaData){
            if(!bSetInputBufferCount){
                LOG_LOG("V4l2Enc DoAllocateBuffer set dma buffer count=%d\n",nInBufferCnt);
                ret = inObj->SetBufferCount(nInBufferCnt,V4L2_MEMORY_DMABUF,nInputPlane);

                if(ret != OMX_ErrorNone)
                    return ret;

                bSetInputBufferCount = OMX_TRUE;
            }

            *buffer = FSL_MALLOC(nSize);
        }else{

            if(!bSetInputBufferCount){
                LOG_LOG("V4l2Enc DoAllocateBuffer set dma buffer count=%d\n",nInBufferCnt);
                ret = inObj->SetBufferCount(nInBufferCnt,V4L2_MEMORY_USERPTR,nInputPlane);

                if(ret != OMX_ErrorNone)
                    return ret;

                bSetInputBufferCount = OMX_TRUE;
            }

            if(bUseDmaInputBuffer && pDmaInputBuffer == NULL){
                LOG_DEBUG("V4l2Enc::pDmaBuffer->Create BEGIN \n");
                pDmaInputBuffer = FSL_NEW( DmaBuffer,());
                if(pDmaInputBuffer == NULL)
                    return OMX_ErrorInsufficientResources;
                //do not call create because only call AllocateForOutput() and Free
                //ret = pDmaInputBuffer->Create(nInputPlane);
                //if(ret != OMX_ErrorNone)
                //    return ret;
                LOG_DEBUG("V4l2Enc::ProcessInit pDmaBuffer->Create SUCCESS \n");
            }

            LOG_LOG("V4l2Enc V4L2_DEV_TYPE_ENCODER pDmaBuffer->AllocateForOutput ");
            ret = pDmaInputBuffer->AllocateForOutput(nSize, buffer);
            if(ret != OMX_ErrorNone)
                return ret;
        }

        if(*buffer != NULL)
            nInBufferNum++;

    }else if(nPortIndex == OUT_PORT){

        if(!bSetOutputBufferCount){
            LOG_LOG("V4l2Enc DoAllocateBuffer set outport buffer count=%d\n",nOutBufferCnt);
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
        LOG_ERROR("V4l2Enc DoAllocateBuffer OMX_ErrorInsufficientResources");
        return OMX_ErrorInsufficientResources;
    }
    return OMX_ErrorNone;
}
OMX_ERRORTYPE V4l2Enc::DoFreeBuffer(OMX_PTR buffer,OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    if(nPortIndex == IN_PORT){

        if(bStoreMetaData){
            FSL_FREE(buffer);
        }else{
             if(bUseDmaInputBuffer){
                LOG_LOG("V4l2Enc call pDmaBuffer->Free");
                ret = pDmaInputBuffer->Free(buffer);
            }
        }

        if(ret != OMX_ErrorNone)
            return ret;

        if(nInBufferNum > 0)
            nInBufferNum --;

    }else if(nPortIndex == OUT_PORT){

        LOG_LOG("V4l2Enc DoFreeBuffer output %d\n",nOutBufferNum);
        ret = outObj->FreeBuffer(buffer);
        if(ret != OMX_ErrorNone)
            return ret;

        if(nInBufferNum > 0)
            nOutBufferNum --;

    }

    return OMX_ErrorNone;
}
OMX_ERRORTYPE V4l2Enc::DoUseBuffer(OMX_PTR buffer,OMX_U32 nSize,OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    LOG_LOG("V4l2Enc DoUseBuffer nPortIndex=%d",nPortIndex);
    if(nPortIndex == IN_PORT){

        if(nInBufferNum != 0)
            return OMX_ErrorInsufficientResources;

        if(!bSetInputBufferCount){
            LOG_DEBUG("V4l2Enc DoUseBuffer set inport buffer count=%d",nInBufferCnt);
            ret = inObj->SetBufferCount(nInBufferCnt,V4L2_MEMORY_USERPTR,nInputPlane);
            bSetInputBufferCount = OMX_TRUE;
        }

    }else if(nPortIndex == OUT_PORT){

        if(nOutBufferNum != 0)
            return OMX_ErrorInsufficientResources;

        if(!bSetOutputBufferCount){
            ret = outObj->SetBufferCount(nOutBufferCnt,V4L2_MEMORY_USERPTR,nOutputPlane);
            bSetOutputBufferCount = OMX_TRUE;
        }
    }

    if(ret != OMX_ErrorNone)
        LOG_ERROR("V4l2Enc DoUseBuffer failed,ret=%d",ret);
    return ret;
}

/**< C style functions to expose entry point for the shared library */
extern "C"
{
    OMX_ERRORTYPE VpuEncoderInit(OMX_IN OMX_HANDLETYPE pHandle)
    {
        OMX_ERRORTYPE ret = OMX_ErrorNone;
        V4l2Enc *obj = NULL;
        ComponentBase *base = NULL;

        obj = FSL_NEW(V4l2Enc, ());
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
