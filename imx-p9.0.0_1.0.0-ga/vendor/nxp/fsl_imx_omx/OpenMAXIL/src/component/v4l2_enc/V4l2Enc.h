/**
 *  Copyright 2018 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 */
#ifndef V4L2ENC_H
#define V4L2ENC_H
#include "V4l2Filter.h"

class V4l2Enc : public V4l2Filter {
public:
    explicit V4l2Enc();
    OMX_ERRORTYPE DoAllocateBuffer(OMX_PTR *buffer, OMX_U32 nSize,OMX_U32 nPortIndex) override;
    OMX_ERRORTYPE DoFreeBuffer(OMX_PTR buffer,OMX_U32 nPortIndex) override;
    OMX_ERRORTYPE DoUseBuffer(OMX_PTR buffer,OMX_U32 nSize,OMX_U32 nPortIndex) override;

private:
    OMX_ERRORTYPE SetRoleFormat(OMX_STRING role);
    OMX_ERRORTYPE QueryVideoProfile(OMX_PTR pComponentParameterStructure);
    OMX_ERRORTYPE SetDefaultPortSetting() override;
    OMX_ERRORTYPE ProcessInit() override;
    OMX_ERRORTYPE SetParameter(OMX_INDEXTYPE nParamIndex,OMX_PTR pComponentParameterStructure) override;
    OMX_ERRORTYPE GetParameter(OMX_INDEXTYPE nParamIndex,OMX_PTR pComponentParameterStructure) override;
    OMX_ERRORTYPE GetConfig(OMX_INDEXTYPE nParamIndex,OMX_PTR pComponentParameterStructure) override;
    OMX_ERRORTYPE SetConfig(OMX_INDEXTYPE nParamIndex,OMX_PTR pComponentParameterStructure) override;
    OMX_ERRORTYPE CheckIfNeedPreProcess();
    V4l2EncInputParam sInputParam;
    OMX_U32 nProfile;
    OMX_U32 nLevel;
};
#endif
