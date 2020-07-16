/**
 *  Copyright 2018 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */

#ifndef V4L2DEC_H
#define V4L2DEC_H
#include "V4l2Filter.h"

class V4l2Dec : public V4l2Filter {
public:
    explicit V4l2Dec();
    OMX_ERRORTYPE DoAllocateBuffer(OMX_PTR *buffer, OMX_U32 nSize,OMX_U32 nPortIndex) override; 
    OMX_ERRORTYPE DoFreeBuffer(OMX_PTR buffer,OMX_U32 nPortIndex) override;
    OMX_ERRORTYPE DoUseBuffer(OMX_PTR buffer,OMX_U32 nSize,OMX_U32 nPortIndex) override;

private:
    OMX_S32 nMaxDurationMsThr;  // control the speed of data consumed by decoder: -1 -> no threshold
    OMX_S32 nMaxBufCntThr;      // control the speed of data consumed by decoder: -1 -> no threshold

    OMX_ERRORTYPE SetRoleFormat(OMX_STRING role);
    OMX_ERRORTYPE QueryVideoProfile(OMX_PTR pComponentParameterStructure);
    OMX_ERRORTYPE SetDefaultPortSetting() override;
    OMX_ERRORTYPE ProcessInit() override;
    OMX_ERRORTYPE SetParameter(OMX_INDEXTYPE nParamIndex,OMX_PTR pComponentParameterStructure) override;
    OMX_ERRORTYPE GetParameter(OMX_INDEXTYPE nParamIndex,OMX_PTR pComponentParameterStructure) override;
    OMX_ERRORTYPE GetConfig(OMX_INDEXTYPE nParamIndex,OMX_PTR pComponentParameterStructure) override;
    OMX_ERRORTYPE SetConfig(OMX_INDEXTYPE nParamIndex,OMX_PTR pComponentParameterStructure) override;
    OMX_ERRORTYPE CheckIfNeedFrameParser();
    OMX_ERRORTYPE GetInputDataDepthThreshold(OMX_S32* pDurationThr, OMX_S32* pBufCntThr) override;
    OMX_BOOL CheckIfNeedPostProcess();

};
#endif
