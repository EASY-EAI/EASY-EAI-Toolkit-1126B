// Copyright 2020 Fuzhou Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#include "sample_comm.h"
#include "sample_comm_isp.h"

#define MAX_AIQ_CTX 8
static rk_aiq_sys_ctx_t *g_aiq_ctx[MAX_AIQ_CTX];
static rk_aiq_camgroup_ctx_t *g_aiq_camgroup_ctx[MAX_AIQ_CTX];
RK_S32 g_devBufCnt[MAX_AIQ_CTX] = { 0 };
rk_aiq_working_mode_t g_WDRMode[MAX_AIQ_CTX];

typedef enum rk_HDR_MODE_E {
	HDR_MODE_OFF,
	HDR_MODE_HDR2,
	HDR_MODE_HDR3,
} HDR_MODE_E;

static atomic_int g_sof_cnt = 0;
static atomic_bool g_should_quit = false;

static XCamReturn SAMPLE_COMM_ISP_SofCb(rk_aiq_metas_t *meta) {
	g_sof_cnt++;
	if (g_sof_cnt <= 2)
		printf("=== %u ===\n", meta->frame_id);
	return XCAM_RETURN_NO_ERROR;
}

RK_S32 SAMPLE_COMM_ISP_GetSofCnt(void) { return g_sof_cnt; }

static XCamReturn SAMPLE_COMM_ISP_ErrCb(rk_aiq_err_msg_t *msg) {
	if (msg->err_code == XCAM_RETURN_BYPASS)
		g_should_quit = true;
	return XCAM_RETURN_NO_ERROR;
}

RK_BOOL SAMPLE_COMM_ISP_ShouldQuit() { return g_should_quit; }

RK_S32 SAMPLE_COMM_PreInit_devBufCnt(RK_S32 CamId, RK_S32 Bufcnt) {
	if (Bufcnt < 0) {
		printf("Invlaid ISP read Buffer, please check!\n");
		return RK_FAILURE;
	}
	g_devBufCnt[CamId] = Bufcnt;
	return 0;
}

RK_S32 SAMPLE_COMM_ISP_Init(RK_S32 CamId, rk_aiq_working_mode_t WDRMode, RK_BOOL MultiCam,
                            const char *iq_file_dir) {
	int ret = 0;
	if (CamId >= MAX_AIQ_CTX) {
		printf("%s : CamId is over 3\n", __FUNCTION__);
		return -1;
	}
	// char *iq_file_dir = "iqfiles/";
	setlinebuf(stdout);
	if (iq_file_dir == NULL) {
		printf("SAMPLE_COMM_ISP_Init : not start.\n");
		g_aiq_ctx[CamId] = NULL;
		return 0;
	}

	// must set HDR_MODE, before init
	g_WDRMode[CamId] = WDRMode;
	char hdr_str[16];
	snprintf(hdr_str, sizeof(hdr_str), "%d", (int)WDRMode);
	setenv("HDR_MODE", hdr_str, 1);

	rk_aiq_sys_ctx_t *aiq_ctx;
	rk_aiq_static_info_t aiq_static_info;
	rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId(CamId, &aiq_static_info);

	printf("ID: %d, sensor_name is %s, iqfiles is %s\n", CamId,
	       aiq_static_info.sensor_info.sensor_name, iq_file_dir);
	if (WDRMode == RK_AIQ_WORKING_MODE_NORMAL)
		ret = rk_aiq_uapi2_sysctl_preInit_scene(aiq_static_info.sensor_info.sensor_name, "normal",
		                                        "day");
	else
		ret = rk_aiq_uapi2_sysctl_preInit_scene(aiq_static_info.sensor_info.sensor_name, "hdr",
		                                        "day");
	if (ret != 0)
		printf("%s: failed to set scene\n", aiq_static_info.sensor_info.sensor_name);

	if (g_devBufCnt[CamId] > 0) {
		ret = rk_aiq_uapi2_sysctl_preInit_devBufCnt(aiq_static_info.sensor_info.sensor_name,
	                                      "rkraw_rx", g_devBufCnt[CamId]);
		if (ret != 0)
			printf("%s: failed to set cif buf cnt\n", aiq_static_info.sensor_info.sensor_name);
	}

	aiq_ctx =
	    rk_aiq_uapi2_sysctl_init(aiq_static_info.sensor_info.sensor_name, iq_file_dir,
	                             SAMPLE_COMM_ISP_ErrCb, SAMPLE_COMM_ISP_SofCb);
	if (aiq_ctx == NULL) {
		printf("%s: failed to init aiq\n", aiq_static_info.sensor_info.sensor_name);
		return -1;
	}

	if (MultiCam)
		rk_aiq_uapi2_sysctl_setMulCamConc(aiq_ctx, true);

	g_aiq_ctx[CamId] = aiq_ctx;
	return 0;
}

static int isp_get_ldch_mesh_size(uint16_t *meshdata) {
	int file_size = 0;
	if (!meshdata) {
		printf("meshdata is null \n");
		return -1;
	}
	unsigned short hpic, vpic, hsize, vsize, hstep, vstep = 0;
	hpic = (unsigned short)meshdata[0];
	vpic = (unsigned short)meshdata[1];
	hsize = (unsigned short)meshdata[2];
	vsize = (unsigned short)meshdata[3];
	hstep = (unsigned short)meshdata[4];
	vstep = (unsigned short)meshdata[5];
	printf("----------lut info: [%d-%d-%d-%d-%d-%d]\n", hpic, vpic, hsize, vsize, hstep,
	       vstep);
	file_size = hsize * vsize * sizeof(unsigned short) + 12;

	return file_size;
}

XCamReturn SAMPLE_COMM_ISP_CamGroup_setMeshToLdch(int CamGrpId, uint8_t SetLdchMode,
                                                  uint16_t **LdchMesh) {
	printf("no support\n");
	return XCAM_RETURN_NO_ERROR;
}

RK_S32 SAMPLE_COMM_ISP_CamGroup_Init(RK_S32 CamGroupId, rk_aiq_working_mode_t WDRMode,
                                     bool MultiCam, int OpenLdch, void *LdchMesh[],
                                     rk_aiq_camgroup_instance_cfg_t *pCamGroupCfg) {
	int i, ret;
	char sensor_name_array[MAX_AIQ_CTX][128];
	rk_aiq_static_info_t aiq_static_info;

	if (CamGroupId >= MAX_AIQ_CTX) {
		printf("%s : CamId is over %d\n", __FUNCTION__, MAX_AIQ_CTX);
		return -1;
	}

	for (i = 0; i < pCamGroupCfg->sns_num; i++) {
		ret = rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId(i, &aiq_static_info);
		if (ret != 0) {
			printf("rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId failure \n");
			return -1;
		}

		printf("CamGroupId:%d, cam_id: %d, sensor_name is %s, iqfiles is %s\n",
		       CamGroupId, i, aiq_static_info.sensor_info.sensor_name,
		       pCamGroupCfg->config_file_dir);
		memcpy(sensor_name_array[i], aiq_static_info.sensor_info.sensor_name,
		       strlen(aiq_static_info.sensor_info.sensor_name) + 1);
		pCamGroupCfg->sns_ent_nm_array[i] = sensor_name_array[i];
		printf("pCamGroupCfg->sns_ent_nm_array[%d] is %s\n", i,
		       pCamGroupCfg->sns_ent_nm_array[i]);
		if (WDRMode == RK_AIQ_WORKING_MODE_NORMAL)
			ret = rk_aiq_uapi2_sysctl_preInit_scene(aiq_static_info.sensor_info.sensor_name,
			                                        "normal", "day");
		else
			ret = rk_aiq_uapi2_sysctl_preInit_scene(aiq_static_info.sensor_info.sensor_name, "hdr",
			                                        "day");
		if (ret != 0)
			printf("%s: failed to set scene\n", aiq_static_info.sensor_info.sensor_name);
		if (g_devBufCnt[i] > 0) {
			ret = rk_aiq_uapi2_sysctl_preInit_devBufCnt(
				aiq_static_info.sensor_info.sensor_name, "rkraw_rx", g_devBufCnt[i]);
			if (ret != 0) {
				printf("rk_aiq_uapi2_sysctl_preInit_devBufCnt failure\n");
				return -1;
			}
		}
	}

	g_aiq_camgroup_ctx[CamGroupId] = rk_aiq_uapi2_camgroup_create(pCamGroupCfg);
	if (!g_aiq_camgroup_ctx[CamGroupId]) {
		printf("create camgroup ctx error!\n");
		return -1;
	}
	/* set LDCH must before <camgroup prepare>*/
	if (OpenLdch) {
		SAMPLE_COMM_ISP_CamGroup_setMeshToLdch(CamGroupId, OpenLdch, (uint16_t **)LdchMesh);
	}

	ret = rk_aiq_uapi2_camgroup_prepare(g_aiq_camgroup_ctx[CamGroupId], WDRMode);

	ret |= rk_aiq_uapi2_camgroup_start(g_aiq_camgroup_ctx[CamGroupId]);
	if (ret != 0) {
		printf("rk_aiq_uapi2_camgroup_prepare / start failure \n");
		return -1;
	}
	printf("rk_aiq_uapi2_camgroup_start over\n");

	return ret;
}

RK_S32 SAMPLE_COMM_ISP_Stop(RK_S32 CamId) {
	if (CamId >= MAX_AIQ_CTX || !g_aiq_ctx[CamId]) {
		printf("%s : CamId is over 3 or not init g_aiq_ctx[%d] = %p\n", __FUNCTION__,
		       CamId, g_aiq_ctx[CamId]);
		return -1;
	}
	printf("rk_aiq_uapi2_sysctl_stop enter\n");
	rk_aiq_uapi2_sysctl_stop(g_aiq_ctx[CamId], false);
	printf("rk_aiq_uapi2_sysctl_deinit enter\n");
	rk_aiq_uapi2_sysctl_deinit(g_aiq_ctx[CamId]);
	printf("rk_aiq_uapi2_sysctl_deinit exit\n");
	g_aiq_ctx[CamId] = NULL;
	return 0;
}

RK_S32 SAMPLE_COMM_ISP_CamGroup_Stop(RK_S32 CamGroupId) {
	if (CamGroupId >= MAX_AIQ_CTX || !g_aiq_camgroup_ctx[CamGroupId]) {
		printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
		return -1;
	}
	printf("rk_aiq_uapi2_camgroup_stop enter\n");
	rk_aiq_uapi2_camgroup_stop(g_aiq_camgroup_ctx[CamGroupId]);
	printf("rk_aiq_uapi2_camgroup_destroy enter\n");
	rk_aiq_uapi2_camgroup_destroy(g_aiq_camgroup_ctx[CamGroupId]);
	printf("rk_aiq_uapi2_camgroup_destroy exit\n");
	g_aiq_camgroup_ctx[CamGroupId] = NULL;

	return 0;
}

RK_S32 SAMPLE_COMM_ISP_Run(RK_S32 CamId) {
	if (CamId >= MAX_AIQ_CTX || !g_aiq_ctx[CamId]) {
		printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
		return -1;
	}
	if (rk_aiq_uapi2_sysctl_prepare(g_aiq_ctx[CamId], 0, 0, g_WDRMode[CamId])) {
		printf("rkaiq engine prepare failed !\n");
		g_aiq_ctx[CamId] = NULL;
		return -1;
	}
	printf("rk_aiq_uapi2_sysctl_init/prepare succeed\n");
	if (rk_aiq_uapi2_sysctl_start(g_aiq_ctx[CamId])) {
		printf("rk_aiq_uapi2_sysctl_start  failed\n");
		return -1;
	}
	printf("rk_aiq_uapi2_sysctl_start succeed\n");
	return 0;
}

RK_S32 SAMPLE_COMM_ISP_SetFrameRate(RK_S32 CamId, RK_U32 uFps) {
	if (CamId >= MAX_AIQ_CTX || !g_aiq_ctx[CamId]) {
		printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
		return -1;
	}
	int ret;
	frameRateInfo_t info;
	info.mode = OP_MANUAL;
	info.fps  = uFps;
	ret = rk_aiq_uapi2_setFrameRate(g_aiq_ctx[CamId], info);
	if (ret != 0)
		printf("%s : Cam %d set framerate %d failed %#X!\n", __func__, CamId, uFps, ret);
	else
		printf("%s : Cam %d set framerate %d success\n", __func__, CamId, uFps);
	return ret;
}

RK_S32 SAMPLE_COMM_ISP_CamGroup_SetFrameRate(RK_S32 CamId, RK_U32 uFps) {
	if (CamId >= MAX_AIQ_CTX || !g_aiq_camgroup_ctx[CamId]) {
		printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
		return -1;
	}
	int ret;
	frameRateInfo_t info;
	info.mode = OP_MANUAL;
	info.fps  = uFps;
	ret = rk_aiq_uapi2_setFrameRate((rk_aiq_sys_ctx_t *)g_aiq_camgroup_ctx[CamId], info);
	if (ret != 0)
		printf("%s : Cam Group %d set framerate %d failed %#X!\n", __func__, CamId, uFps, ret);
	else
		printf("%s : Cam Group %d set framerate %d success\n", __func__, CamId, uFps);
	return ret;
}

RK_S32 SAMPLE_COMM_ISP_SetMirrorFlip(int CamId, int mirror, int flip) {
	if (CamId >= MAX_AIQ_CTX || !g_aiq_ctx[CamId]) {
		printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
		return -1;
	}
	return rk_aiq_uapi2_setMirrorFlip(g_aiq_ctx[CamId], mirror, flip, 4); // skip 4 frame
}

RK_S32 SAMPLE_comm_ISP_SWITCH_SCENE(int CamId, const char *main_scene,
                                    const char *sub_scene) {
	if (CamId >= MAX_AIQ_CTX || !g_aiq_ctx[CamId]) {
		printf("%s : CamId is over 3 or not init\n", __FUNCTION__);
		return -1;
	}
	return rk_aiq_uapi2_sysctl_switch_scene(g_aiq_ctx[CamId], main_scene, sub_scene);
}

XCamReturn SAMPLE_COMM_ISP_SetLDCH(RK_U32 CamId, RK_U32 u32Level, RK_BOOL bIfEnable) {
	printf("no support\n");
	return XCAM_RETURN_NO_ERROR;
}

RK_S32 SAMPLE_COMM_ISP_CamGroup_SetLDCH(RK_U32 CamId, RK_U32 u32Level,
                                        RK_BOOL bIfEnable) {
	printf("no support\n");
	return XCAM_RETURN_NO_ERROR;
}

RK_S32 SAMPLE_COMM_ISP_GetAINrParams(RK_S32 CamId, rk_ainr_param *param) {
	RK_S32 ret = RK_SUCCESS;
	ret = rk_aiq_uapi2_sysctl_getAinrParams(g_aiq_ctx[CamId], param);
	if (ret != RK_SUCCESS) {
		printf("rk_aiq_uapi2_sysctl_getAinrParams failure %#X\n", ret);
		return ret;
	}
	return ret;
}

RK_S32 SAMPLE_COMM_ISP_EnablsAiisp(RK_S32 CamId) {
	RK_S32 ret = RK_SUCCESS;
	ret = rk_aiq_uapi2_sysctl_initAiisp(g_aiq_ctx[CamId], NULL, NULL);
	if (ret != RK_SUCCESS) {
		printf("rk_aiq_uapi2_sysctl_initAiisp failure %#X\n", ret);
		return ret;
	}
	return ret;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
