/*
 * awinic_cali.c cali_module
 *
 * Version: v0.1.19
 *
 * Copyright (c) 2019 AWINIC Technology CO., LTD
 *
 *  Author: Nick Li <liweilei@awinic.com.cn>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <linux/debugfs.h>
#include <asm/ioctls.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include "aw882xx.h"
#include "awinic_cali.h"
#include "awinic_monitor.h"


static DEFINE_MUTEX(g_dsp_lock);
static DEFINE_MUTEX(g_msg_dsp_lock);


static const uint32_t PARAM_ID_INDEX_TABLE[][INDEX_PARAMS_ID_MAX] = {
	{
		AFE_PARAM_ID_AWDSP_RX_PARAMS,
		AFE_PARAM_ID_AWDSP_RX_SET_ENABLE,
		AFE_PARAM_ID_AWDSP_TX_SET_ENABLE,
		AFE_PARAM_ID_AWDSP_RX_VMAX_L,
		AFE_PARAM_ID_AWDSP_RX_CALI_CFG_L,
		AFE_PARAM_ID_AWDSP_RX_RE_L,
		AFE_PARAM_ID_AWDSP_RX_NOISE_L,
		AFE_PARAM_ID_AWDSP_RX_F0_L,
		AFE_PARAM_ID_AWDSP_RX_REAL_DATA_L,
		AFE_PARAM_ID_AWDSP_RX_MSG,
	},
	{
		AFE_PARAM_ID_AWDSP_RX_PARAMS,
		AFE_PARAM_ID_AWDSP_RX_SET_ENABLE,
		AFE_PARAM_ID_AWDSP_TX_SET_ENABLE,
		AFE_PARAM_ID_AWDSP_RX_VMAX_R,
		AFE_PARAM_ID_AWDSP_RX_CALI_CFG_R,
		AFE_PARAM_ID_AWDSP_RX_RE_R,
		AFE_PARAM_ID_AWDSP_RX_NOISE_R,
		AFE_PARAM_ID_AWDSP_RX_F0_R,
		AFE_PARAM_ID_AWDSP_RX_REAL_DATA_R,
		AFE_PARAM_ID_AWDSP_RX_MSG,
	},
};

#ifdef AW_CALI_STORE_EXAMPLE
/*write cali to persist file example*/
#define AWINIC_CALI_FILE_L  "/mnt/vendor/persist-lg/audio/aw_calir.bin"
#define AWINIC_CALI_FILE_R  "/mnt/vendor/persist-lg/audio/aw_calil.bin"
#define AW_INT_DEC_DIGIT 10
static int aw882xx_write_cali_re_to_file(int32_t cali_re, int channel)
{
	struct file *fp = NULL;
	char buf[50] = {0};
	loff_t pos = 0;
	mm_segment_t fs;
	if(channel == 0)
	{
		fp = filp_open(AWINIC_CALI_FILE_L, O_RDWR | O_CREAT, 0666);
		if (IS_ERR(fp)) {
			pr_err("%s:audiock channel:%d open %s failed!\n",
				__func__, channel, AWINIC_CALI_FILE_L);
			return -EINVAL;
		}
	}
	else
	{
		fp = filp_open(AWINIC_CALI_FILE_R, O_RDWR | O_CREAT, 0666);
		if (IS_ERR(fp)) {
			pr_err("%s:audiock channel:%d open %s failed!\n",
				__func__, channel, AWINIC_CALI_FILE_R);
			return -EINVAL;
		}
	}

	if (channel != AW882XX_CHANNLE_RIGHT)
		pos = AW_INT_DEC_DIGIT;

	//cali_re = ((cali_re * 1000) / 4096);
	snprintf(buf, sizeof(buf), "%10d", cali_re);

	fs = get_fs();
	set_fs(KERNEL_DS);

	vfs_write(fp, buf, strlen(buf), &pos);

	set_fs(fs);

	pr_info("%s: channel:%d buf:%s cali_re:%d\n",
		__func__, channel, buf, cali_re);

	filp_close(fp, NULL);
	return 0;
}

static int aw882xx_get_cali_re_from_file(int32_t *cali_re, int channel)
{
	struct file *fp = NULL;
	/*struct inode *node;*/
	int f_size;
	char *buf = NULL;
	int32_t int_cali_re = 0;

	loff_t pos = 0;
	mm_segment_t fs;

	if(0 == channel)
	{
		fp = filp_open(AWINIC_CALI_FILE_L, O_RDONLY, 0);
		if (IS_ERR(fp)) {
			pr_err("%s:audiock channel:%d open %s failed!\n",
				__func__, channel, AWINIC_CALI_FILE_L);
			return -EINVAL;
		}
	}
	else
	{
		fp = filp_open(AWINIC_CALI_FILE_R, O_RDONLY, 0);
		if (IS_ERR(fp)) {
			pr_err("%s:audiock channel:%d open %s failed!\n",
				__func__, channel, AWINIC_CALI_FILE_R);
			return -EINVAL;
		}
	}

	if (channel != AW882XX_CHANNLE_RIGHT)
		pos = AW_INT_DEC_DIGIT;

	/*node = fp->f_dentry->d_inode;*/
	/*f_size = node->i_size;*/
	f_size = AW_INT_DEC_DIGIT;

	buf = kzalloc(f_size + 1, GFP_ATOMIC);
	if (!buf) {
		pr_err("%s: channel:%d malloc mem %d failed!\n",
			__func__, channel, f_size);
		filp_close(fp, NULL);
		return -EINVAL;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);

	vfs_read(fp, buf, f_size, &pos);

	set_fs(fs);

	if (sscanf(buf, "%d", &int_cali_re) == 1)
		*cali_re = int_cali_re;
	else
		*cali_re = AW_ERRO_CALI_VALUE;

	pr_info("%s: channel:%d buf:%s int_cali_re: %d\n",
		__func__, channel, buf, int_cali_re);

	kfree(buf);
	filp_close(fp, NULL);

	return  0;

}
#endif

 /*custom need add to set/get cali_re form/to nv*/
int aw882xx_set_cali_re_to_nvram(int32_t cali_re, int32_t channel)
{
	/*custom add, if success return value is 0, else -1*/
#ifdef AW_CALI_STORE_EXAMPLE
	return aw882xx_write_cali_re_to_file(cali_re, channel);
#else
	return -EBUSY;
#endif
}
int aw882xx_get_cali_re_from_nvram(int32_t *cali_re, int32_t channel)
{
	/*custom add, if success return value is 0 , else -1*/
#ifdef AW_CALI_STORE_EXAMPLE
	return aw882xx_get_cali_re_from_file(cali_re, channel);
#else
	return -EBUSY;
#endif
}


/***************dsp communicate**************/
#ifdef AWINIC_ADSP_ENABLE
extern int aw_send_afe_cal_apr(uint32_t param_id,
	void *buf, int cmd_size, bool write);
extern int aw_send_afe_rx_module_enable(void *buf, int size);
extern int aw_send_afe_tx_module_enable(void *buf, int size);
#else
static int aw_send_afe_cal_apr(uint32_t param_id,
	void *buf, int cmd_size, bool write)
{
	return 0;
}
static int aw_send_afe_rx_module_enable(void *buf, int size)
{
	return 0;
}
static int aw_send_afe_tx_module_enable(void *buf, int size)
{
	return 0;
}
#endif

int aw_send_afe_module_enable(void *buf, int size, uint8_t type)
{
	int ret;

	switch (type) {
	case AW_RX_MODULE:
		ret = aw_send_afe_rx_module_enable(buf, size);
		break;
	case AW_TX_MODULE:
		ret = aw_send_afe_tx_module_enable(buf, size);
		break;
	default:
		pr_err("%s: unsupported type %d\n", __func__, type);
		return -EINVAL;
	}

	return ret;
}

static int aw_get_params_id_by_index(int index, int32_t *params_id, int channel)
{
	if (index > INDEX_PARAMS_ID_MAX || channel > 1) {
		pr_err("%s: error: index is %d, channel %d\n",
			__func__, index, channel);
		return -EINVAL;
	}
	*params_id = PARAM_ID_INDEX_TABLE[channel][index];
	return 0;
}

int aw_write_data_to_dsp(int index, void *data, int len, int channel)
{
	int ret;
	int32_t param_id;

	ret = aw_get_params_id_by_index(index, &param_id, channel);
	if (ret < 0)
		return ret;

	mutex_lock(&g_dsp_lock);
	ret = aw_send_afe_cal_apr(param_id, data, len, true);
	mutex_unlock(&g_dsp_lock);

	return ret;
}

int aw_read_data_from_dsp(int index, void *data, int len, int channel)
{
	int ret;
	int32_t param_id;

	ret = aw_get_params_id_by_index(index, &param_id, channel);
	if (ret < 0)
		return ret;

	mutex_lock(&g_dsp_lock);
	ret = aw_send_afe_cal_apr(param_id, data, len, false);
	mutex_unlock(&g_dsp_lock);

	return ret;
}

static int aw882xx_set_dsp_msg_data(struct aw882xx *aw882xx,
			char *data_ptr, int data_size, int inline_id)
{
	int32_t *dsp_msg = NULL;
	int ret;

	dsp_msg = kzalloc(sizeof(struct aw_dsp_msg_hdr) + data_size, GFP_KERNEL);
	if (!dsp_msg) {
		aw_dev_err(aw882xx->dev, "%s: inline_id:%d kzalloc dsp_msg error\n",
			__func__, inline_id);
		return -ENOMEM;
	}
	dsp_msg[0] = DSP_MSG_TYPE_DATA;
	dsp_msg[1] = inline_id;
	dsp_msg[2] = AWINIC_DSP_MSG_HDR_VER;

	memcpy(dsp_msg + (sizeof(struct aw_dsp_msg_hdr) / sizeof(int32_t)),
		data_ptr, data_size);

	ret = aw_write_data_to_dsp(INDEX_PARAMS_ID_AWDSP_RX_MSG,
			(void *)dsp_msg,
			sizeof(struct aw_dsp_msg_hdr) + data_size,
			aw882xx->chan_info.channel);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s:inline_id:%d, write data to dsp failed\n",
			__func__, inline_id);
		kfree(dsp_msg);
		return ret;
	}

	kfree(dsp_msg);
	return 0;
}

static int aw882xx_get_dsp_msg_data(struct aw882xx *aw882xx,
			char *data_ptr, int data_size, int inline_id)
{
	int32_t cmd_msg[6] = {0};
	int ret;

	cmd_msg[0] = DSP_MSG_TYPE_CMD;
	cmd_msg[1] = inline_id;
	cmd_msg[2] = AWINIC_DSP_MSG_HDR_VER;

	mutex_lock(&g_msg_dsp_lock);
	ret = aw_write_data_to_dsp(INDEX_PARAMS_ID_AWDSP_RX_MSG,
			cmd_msg, sizeof(cmd_msg),
			aw882xx->chan_info.channel);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s:inline_id:%d, write cmd to dsp failed\n",
			__func__, inline_id);
		goto dsp_msg_failed;
	}

	ret = aw_read_data_from_dsp(INDEX_PARAMS_ID_AWDSP_RX_MSG,
			data_ptr, data_size,
			aw882xx->chan_info.channel);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s:inline_id:%d, read data from dsp failed\n",
			__func__, inline_id);
		goto dsp_msg_failed;
	}

	mutex_unlock(&g_msg_dsp_lock);
	return 0;

dsp_msg_failed:
	mutex_unlock(&g_msg_dsp_lock);
	return ret;
}

static int aw882xx_store_cali_re(struct aw882xx *aw882xx, int32_t cali_re)
{
	struct aw882xx_chan_info *chan_info = &aw882xx->chan_info;

	if (aw882xx == NULL)
		return -EINVAL;
	aw882xx->cali.cali_re = cali_re;
	return aw882xx_set_cali_re_to_nvram(cali_re, chan_info->channel);
}

void aw882xx_load_cali_re(struct aw_cali *cali)
{
	int32_t cali_re = 0;
	int ret = 0;

	struct aw882xx *aw882xx =
			container_of(cali, struct aw882xx, cali);
	struct aw882xx_chan_info *chan_info = &aw882xx->chan_info;

	ret = aw882xx_get_cali_re_from_nvram(&cali_re, chan_info->channel);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s: get cali re from nv failed: %d\n",
			 __func__, ret);
		cali_re = AW_ERRO_CALI_VALUE;
	}
	aw882xx->cali.cali_re = cali_re;
}


static int aw_run_dsp_hmute(struct aw882xx *aw882xx, bool flag)
{
#ifdef AWINIC_DSP_HMUTE
	int hmute_st;

	aw_dev_dbg(aw882xx->dev, "%s: enter flag:%d\n",
			 __func__, flag);

	if (flag) {
		hmute_st = DSP_HMUTE;
	} else {
		hmute_st = DSP_UNHMUTE;
	}
	return aw882xx_set_dsp_msg_data(aw882xx,
			(char *)&hmute_st, sizeof(hmute_st),
			INLINE_PARAM_ID_ENABLE_HMUTE);
#endif
	return 0;
}

static int aw_run_cali_cfg_to_dsp(struct aw882xx *aw882xx,
				bool flag, struct cali_cfg *store_cfg)
{
	int ret;

#ifdef AWINIC_DSP_MSG
	int cali_cfg;

	aw_dev_dbg(aw882xx->dev, "%s: enter flag:%d\n",
			 __func__, flag);

	if (flag)
		cali_cfg = DSP_SET_CALI_CFG;
	else
		cali_cfg = DSP_BACK_CALI_CFG;

	ret = aw882xx_set_dsp_msg_data(aw882xx,
			(char *)&cali_cfg, sizeof(cali_cfg),
			INLINE_PARAM_ID_ENABLE_CALI);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s:start cali failed!\n",
				__func__);
		return ret;
	}

	if (flag)
		msleep(aw882xx->cali.cali_re_time);

	return 0;

#else
	struct aw882xx_chan_info *chan_info = &aw882xx->chan_info;
	struct cali_cfg set_cfg;

	if (flag) {
		ret = aw_read_data_from_dsp(INDEX_PARAMS_ID_RX_CALI_CFG,
					(void *)store_cfg, sizeof(struct cali_cfg),
					chan_info->channel);
		if (ret < 0) {
			aw_dev_err(aw882xx->dev, "%s:read cali cfg data failed!\n",
				__func__);
			return -EBUSY;
		}

		set_cfg.data[0] = 0;
		set_cfg.data[1] = 0;
		set_cfg.data[2] = -1;

		/*set cali cfg start cali*/
		ret = aw_write_data_to_dsp(INDEX_PARAMS_ID_RX_CALI_CFG,
					(void *)&set_cfg, sizeof(struct cali_cfg),
					chan_info->channel);
		if (ret < 0) {
			aw_dev_err(aw882xx->dev, "%s:start cali failed !\n", __func__);
			aw_write_data_to_dsp(INDEX_PARAMS_ID_RX_CALI_CFG,
				(void *)store_cfg, sizeof(struct cali_cfg),
				chan_info->channel);
			return ret;
		}

		msleep(10*1000);

	} else {
		return aw_write_data_to_dsp(INDEX_PARAMS_ID_RX_CALI_CFG,
			(void *)store_cfg, sizeof(struct cali_cfg),
			chan_info->channel);
	}

	return 0;
#endif
}

static int aw_run_noise_to_dsp(struct aw882xx *aw882xx,
				bool flag)
{
	int32_t noise_enable;
	struct aw882xx_chan_info *chan_info = &aw882xx->chan_info;

	if (flag) {
		noise_enable = 1;
	} else {
		noise_enable = 0;
	}

	return aw_write_data_to_dsp(INDEX_PARAMS_ID_RX_NOISE,
		(void *)&noise_enable, sizeof(int32_t),
		chan_info->channel);

}

static int aw_cali_get_re(int32_t *cali_re, struct aw882xx *aw882xx)
{
	int ret;
	struct cali_cfg store_cfg;
	struct cali_data cali_data;
	struct aw882xx_chan_info *chan_info = &aw882xx->chan_info;

	aw882xx->cali.status = true;

	ret = aw_run_dsp_hmute(aw882xx, true);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s:set dsp hmute failed!\n",
			__func__);
		goto mute_failed;
	}

	ret = aw_run_cali_cfg_to_dsp(aw882xx, true, &store_cfg);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s:set cali cfg failed !\n",
			__func__);
		goto set_cali_cfg_failed;
	}

	/*get cali data*/
	ret = aw_read_data_from_dsp(INDEX_PARAMS_ID_RX_REAL_DATA,
				(void *)&cali_data, sizeof(struct cali_data),
				chan_info->channel);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s: read re failed!\n",
			__func__);
		goto cali_data_failed;
	}

	aw_dev_info(aw882xx->dev, "%s:cali_re : 0x%x\n",
		__func__, cali_data.data[0]);
	*cali_re = cali_data.data[0];

	/*repair cali cfg to normal status*/
	aw_run_cali_cfg_to_dsp(aw882xx, false, &store_cfg);
	aw_run_dsp_hmute(aw882xx, false);
	aw882xx->cali.status = false;
	return 0;

cali_data_failed:
	aw_run_cali_cfg_to_dsp(aw882xx, false, &store_cfg);
set_cali_cfg_failed:
	aw_run_dsp_hmute(aw882xx, false);
mute_failed:
	aw882xx->cali.status = false;
	return ret;
}

static int aw_cali_get_f0(int32_t *cali_f0, struct aw882xx *aw882xx)
{
	int ret;
	int32_t read_f0;
	struct aw882xx_chan_info *chan_info = &aw882xx->chan_info;

	aw882xx->cali.status = true;
	/*get cali data*/
	ret = aw_read_data_from_dsp(INDEX_PARAMS_ID_RX_F0,
			(void *)&read_f0, sizeof(int32_t),
			chan_info->channel);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s:read f0 failed!\n",
			__func__);
		aw882xx->cali.status = false;
		return -EBUSY;
	}

	aw_dev_info(aw882xx->dev, "%s:cali_f0 : %d\n", __func__, read_f0);
	*cali_f0 = read_f0;
	aw882xx->cali.status = false;
	return 0;
}

static int aw882xx_cali_start_up(int32_t *cali_re,
				int32_t *cali_f0, struct aw882xx *aw882xx)
{
	int ret;
	int32_t local_f0 = 0;
	struct cali_cfg store_cfg;
	struct cali_data cali_data;
	struct aw882xx_chan_info *chan_info = &aw882xx->chan_info;

	aw882xx->cali.status = true;
	ret = aw_run_dsp_hmute(aw882xx, true);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s:set dsp hmute failed!\n",
			__func__);
		goto mute_failed;
	}

	ret = aw_run_cali_cfg_to_dsp(aw882xx, true, &store_cfg);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s:set cali cfg failed !\n",
			__func__);
		aw_run_dsp_hmute(aw882xx, false);
		goto set_cali_cfg_failed;
	}

	/*get cali data*/
	ret = aw_read_data_from_dsp(INDEX_PARAMS_ID_RX_REAL_DATA,
				(void *)&cali_data, sizeof(struct cali_data),
				chan_info->channel);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s: read re failed!\n",
			__func__);
		aw_run_dsp_hmute(aw882xx, false);
		goto re_cali_failed;
	}

	aw_dev_info(aw882xx->dev, "%s:cali_re : 0x%x\n",
		__func__, cali_data.data[0]);
	*cali_re = cali_data.data[0];

	ret = aw_run_dsp_hmute(aw882xx, false);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s:set dsp unhmute failed!\n",
			__func__);
		goto unhmute_failed;
	}

	/*start white noise*/
	ret = aw_run_noise_to_dsp(aw882xx, true);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s: set noise enable failed\n",
			__func__);
		goto set_noise_failed;
	}

	/*keep 5 s, wait data stable*/
	msleep(5 * 1000);

	/*get f0 value*/
	ret = aw_read_data_from_dsp(INDEX_PARAMS_ID_RX_F0,
				(void *)&local_f0, sizeof(struct cali_data),
				chan_info->channel);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s: read f0 failed !\n", __func__);
		goto f0_cali_failed;
	}

	aw_dev_info(aw882xx->dev, "%s: f0 : %d\n", __func__, local_f0);
	*cali_f0 = local_f0;

	/*disable noise*/
	aw_run_noise_to_dsp(aw882xx, false);

	/*repair cali cfg to normal status*/
	aw_run_cali_cfg_to_dsp(aw882xx, false, &store_cfg);
	aw882xx->cali.status = false;
	return 0;

f0_cali_failed:
	aw_run_noise_to_dsp(aw882xx, false);
set_noise_failed:
unhmute_failed:
re_cali_failed:
	aw_run_cali_cfg_to_dsp(aw882xx, false, &store_cfg);
set_cali_cfg_failed:
mute_failed:
	aw882xx->cali.status = false;
	return ret;
}

/***************cali debug fs***************/
/*unit mOhms*/
static int R0_MAX = 15000;
static int R0_MIN = 5000;

int  aw_cali_range_open(struct inode *inode, struct file *file)
{
	struct aw882xx *aw882xx = (struct aw882xx *)inode->i_private;

	file->private_data = (void *)aw882xx;
	aw_dev_info(aw882xx->dev, "%s: open success", __func__);
	return 0;
}

ssize_t aw_cali_range_read(struct file *file,
	char __user *buf, size_t len, loff_t *ppos)
{
	int ret;
	char local_buf[50];
	struct aw882xx *aw882xx = (struct aw882xx *)file->private_data;

	if (*ppos)
		return 0;
	memset(local_buf, 0, sizeof(local_buf));
	if (len < sizeof(local_buf)) {
		aw_dev_err(aw882xx->dev, "%s: buf len not enough\n", __func__);
		return -ENOSPC;
	}

	snprintf(local_buf, sizeof(local_buf),
			" Min:%d mOhms, Max:%d mOhms\n", R0_MIN, R0_MAX);

	ret = simple_read_from_buffer(buf, len, ppos, local_buf, ret);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s: copy failed!\n", __func__);
		return -ENOMEM;
	}
	return ret;
}

ssize_t aw_cali_range_write(struct file *file,
	const char __user *buf, size_t len, loff_t *ppos)
{
	struct aw882xx *aw882xx = (struct aw882xx *)file->private_data;
	uint32_t time;
	int ret;

	if (*ppos)
		return 0;

	ret = kstrtouint_from_user(buf, len, 0, &time);
	if (ret)
		return len;

	if (time < 400) {
		aw_dev_err(aw882xx->dev, "%s:time:%d is too short, no set\n",
			__func__, time);
		return -EINVAL;
	}

	aw882xx->cali.cali_re_time = time;

	return len;
}

static const struct file_operations aw_cali_range_fops = {
	.open = aw_cali_range_open,
	.read = aw_cali_range_read,
	.write = aw_cali_range_write,
};

int  aw_cali_open(struct inode *inode, struct file *file)
{
	struct aw882xx *aw882xx = (struct aw882xx *)inode->i_private;

	file->private_data = (void *)aw882xx;
	aw_dev_dbg(aw882xx->dev, "%s: open success\n", __func__);
	return 0;
}

ssize_t aw_cali_read(struct file *file,
	char __user *buf, size_t len, loff_t *ppos)
{
	int ret;
	char ret_value[20] = { 0 };
	int local_len = 0;
	int32_t re_cali = 0;
	struct aw882xx *aw882xx = (struct aw882xx *)file->private_data;

	if (*ppos)
		return 0;
	memset(ret_value, 0, sizeof(ret_value));
	if (len < sizeof(ret_value)) {
		aw_dev_err(aw882xx->dev, "%s:buf len no enough\n", __func__);
		aw882xx->cali.re = 0;
		return -ENOMEM;
	}

	ret = aw_cali_get_re(&re_cali, aw882xx);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s:cali failed\n", __func__);
		aw882xx->cali.re = 0;
		return ret;
	}

	/*factor form 12bit(4096) to 1000*/
	re_cali = (re_cali * 1000) >> 12;

	aw882xx->cali.re = re_cali;

	ret = snprintf(ret_value + local_len,
		PAGE_SIZE - local_len, "%d\n", re_cali);

	return simple_read_from_buffer(buf, len, ppos, ret_value, ret);
}

ssize_t aw_cali_write(struct file *file,
	const char __user *buf, size_t len, loff_t *ppos)
{

	return 0;
}

static const struct file_operations aw_cali_fops = {
	.open = aw_cali_open,
	.read = aw_cali_read,
	.write = aw_cali_write,
};

int aw_f0_open(struct inode *inode, struct file *file)
{
	struct aw882xx *aw882xx = (struct aw882xx *)inode->i_private;

	file->private_data = (void *)aw882xx;
	aw_dev_dbg(aw882xx->dev, "%s: open success\n", __func__);
	return 0;
}

ssize_t aw_f0_read(struct file *file,
	char __user *buf, size_t len, loff_t *ppos)
{
	int ret;
	char ret_value[20] = { 0 };
	int local_len = 0;
	int32_t ret_f0 = 0;
	struct aw882xx *aw882xx = (struct aw882xx *)file->private_data;

	if (*ppos)
		return 0;

	memset(ret_value, 0, sizeof(ret_value));
	if (len < sizeof(ret_value)) {
		aw_dev_err(aw882xx->dev, "%s:buf len no enough\n", __func__);
		aw882xx->cali.f0 = 0;
		return -ENOMEM;
	}

	ret = aw_cali_get_f0(&ret_f0, aw882xx);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s:cali failed\n", __func__);
		aw882xx->cali.f0 = 0;
		return ret;
	}

	aw882xx->cali.f0 = ret_f0;

	ret = snprintf(ret_value + local_len,
		PAGE_SIZE - local_len, "%d\n", ret_f0);

	ret = simple_read_from_buffer(buf, len, ppos, ret_value, ret);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s:copy failed!\n", __func__);
		return -ENOMEM;
	}
	return ret;
}

static const struct file_operations aw_f0_fops = {
	.open = aw_f0_open,
	.read = aw_f0_read,
};

int  aw_cali_status_open(struct inode *inode, struct file *file)
{
	struct aw882xx *aw882xx = (struct aw882xx *)inode->i_private;

	file->private_data = (void *)aw882xx;
	aw_dev_dbg(aw882xx->dev, "%s: open success\n", __func__);
	return 0;
}

ssize_t aw_cali_status_read(struct file *file,
	char __user *buf, size_t len, loff_t *ppos)
{
	int ret;
	char status_value[20];
	int local_len = 0;
	struct cali_data cali_data;
	int32_t real_r0;
	struct aw882xx *aw882xx = (struct aw882xx *)file->private_data;
	struct aw882xx_chan_info *chan_info = &aw882xx->chan_info;

	if (*ppos)
		return 0;

	if (len < sizeof(status_value)) {
		aw_dev_err(aw882xx->dev, "%s:buf len no enough\n", __func__);
		return -ENOSPC;
	}

	/*get cali data*/
	ret = aw_read_data_from_dsp(INDEX_PARAMS_ID_RX_REAL_DATA,
				(void *)&cali_data, sizeof(struct cali_data),
				chan_info->channel);
	if (ret) {
		aw_dev_err(aw882xx->dev, "%s:read speaker status failed!\n",
			__func__);
		return -EBUSY;
	}
	/*R0 factor form 4096 to 1000*/
	real_r0 = (cali_data.data[0] * 1000) >> 12;
	ret = snprintf(status_value + local_len, PAGE_SIZE - local_len,
				"%d : %d\n", real_r0, cali_data.data[1]);

	ret = simple_read_from_buffer(buf, len, ppos, status_value, ret);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s:copy failed!", __func__);
		return -ENOMEM;
	}
	return ret;
}

static const struct file_operations aw_cali_status_fops = {
	.open = aw_cali_status_open,
	.read = aw_cali_status_read,
};

static void aw_cali_debugfs_init(struct aw882xx *aw882xx)
{
	const char *debugfs_dir = "awinic_cali";
	struct aw_dbg_cali *dbg_fs = &aw882xx->cali.dbg_fs;

	aw882xx_append_suffix("%s_%s", &debugfs_dir, aw882xx);

	dbg_fs->dbg_dir = debugfs_create_dir(debugfs_dir, NULL);
	if (dbg_fs->dbg_dir == NULL) {
		aw_dev_err(aw882xx->dev, "create cali debugfs failed !\n");
		return;
	}
	dbg_fs->dbg_range = debugfs_create_file("range", S_IFREG|S_IRUGO,
			dbg_fs->dbg_dir, aw882xx, &aw_cali_range_fops);
	if (dbg_fs->dbg_range == NULL) {
		aw_dev_err(aw882xx->dev, "create cali debugfs range failed !\n");
		return;
	}
	dbg_fs->dbg_cali = debugfs_create_file("cali", S_IFREG|S_IRUGO|S_IWUGO,
			dbg_fs->dbg_dir, aw882xx, &aw_cali_fops);
	if (dbg_fs->dbg_cali == NULL) {
		aw_dev_err(aw882xx->dev, "create cali debugfs cali failed !\n");
		return;
	}
	dbg_fs->dbg_f0 = debugfs_create_file("f0", S_IFREG|S_IRUGO,
			dbg_fs->dbg_dir, aw882xx, &aw_f0_fops);
	if (dbg_fs->dbg_f0 == NULL) {
		aw_dev_err(aw882xx->dev, "create cali debugfs cali failed !\n");
		return;
	}
	dbg_fs->dbg_status = debugfs_create_file("status", S_IFREG|S_IRUGO,
			dbg_fs->dbg_dir, aw882xx, &aw_cali_status_fops);
	if (dbg_fs->dbg_status == NULL) {
		aw_dev_err(aw882xx->dev, "create cali debugfs status failed !\n");
		return;
	}
}

void aw_cali_debugfs_deinit(struct aw882xx *aw882xx)
{
	struct aw_dbg_cali *dbg_fs = &aw882xx->cali.dbg_fs;

	debugfs_remove(dbg_fs->dbg_range);
	debugfs_remove(dbg_fs->dbg_cali);
	debugfs_remove(dbg_fs->dbg_f0);
	debugfs_remove(dbg_fs->dbg_status);
	debugfs_remove(dbg_fs->dbg_dir);
}


/***********************cali misc device*********************/
static int aw882xx_file_open(struct inode *inode, struct file *file)
{
	struct miscdevice *device = NULL;
	struct aw_misc_cali *misc_ptr = NULL;
	struct aw_cali *cali_ptr = NULL;
	struct aw882xx *aw882xx = NULL;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;
	device = (struct miscdevice *)file->private_data;

	misc_ptr = container_of(device, struct aw_misc_cali, misc_device);
	cali_ptr = container_of(misc_ptr, struct aw_cali, misc);
	aw882xx = container_of(cali_ptr, struct aw882xx, cali);

	file->private_data = (void *)aw882xx;

	aw_dev_dbg(aw882xx->dev, "%s: misc open success\n", __func__);
	return 0;
}

static int aw882xx_file_release(struct inode *inode, struct file *file)
{
	file->private_data = (void *)NULL;

	pr_debug("misc release successi\n");
	return 0;
}

static int aw882xx_file_get_index(unsigned int cmd, int32_t *index)
{
	switch (cmd) {
	case AW882XX_IOCTL_GET_CALI_CFG:
	case AW882XX_IOCTL_SET_CALI_CFG:
		*index = INDEX_PARAMS_ID_RX_CALI_CFG;
		break;
	case AW882XX_IOCTL_GET_CALI_DATA:
		*index = INDEX_PARAMS_ID_RX_REAL_DATA;
		break;
	case AW882XX_IOCTL_SET_NOISE:
		*index = INDEX_PARAMS_ID_RX_NOISE;
		break;
	case AW882XX_IOCTL_GET_F0:
		*index = INDEX_PARAMS_ID_RX_F0;
		break;
	case AW882XX_IOCTL_GET_CALI_RE:
	case AW882XX_IOCTL_SET_CALI_RE:
		*index = INDEX_PARAMS_ID_RX_RE;
		break;
	case AW882XX_IOCTL_GET_VMAX:
	case AW882XX_IOCTL_SET_VMAX:
		*index = INDEX_PARAMS_ID_RX_VMAX;
		break;
	case AW882XX_IOCTL_SET_PARAM:
	case AW882XX_IOCTL_SET_PTR_PARAM_NUM:
		*index = INDEX_PARAMS_ID_RX_PARAMS;
		break;
	case AW882XX_IOCTL_ENABLE_CALI:
		break;
	case AW882XX_IOCTL_GET_F0_Q:
	case AW882XX_IOCTL_SET_DSP_HMUTE:
	case AW882XX_IOCTL_SET_CALI_CFG_FLAG:
		*index = INDEX_PARAMS_ID_AWDSP_RX_MSG;
		break;
	default:
		pr_err("%s: unsupported cmd %d\n", __func__, cmd);
		return -EINVAL;
	}

	return 0;
}
/*
static int aw882xx_cali_get_f0_q(struct aw882xx *aw882xx,
				char *data_ptr, int data_size)
{

	int32_t get_f0_q_msg[6] = {0};

	get_f0_q_msg[0] = DSP_MSG_TYPE_CMD;
	get_f0_q_msg[1] = INLINE_PARAM_ID_F0_Q;
	get_f0_q_msg[2] = AWINIC_DSP_MSG_HDR_VER;

	aw_write_data_to_dsp(INDEX_PARAMS_ID_AWDSP_RX_MSG,
			get_f0_q_msg, sizeof(get_f0_q_msg),
			aw882xx->chan_info.channel);

	aw_read_data_from_dsp(INDEX_PARAMS_ID_AWDSP_RX_MSG,
			data_ptr, data_size, aw882xx->chan_info.channel);
	return 0;
}
*/

static int aw882xx_cali_operation(struct aw882xx *aw882xx,
			unsigned int cmd, unsigned long arg)
{
	int16_t data_len = _IOC_SIZE(cmd);
	int ret = 0;
	char *data_ptr = NULL;
	uint32_t index = 0;
	struct aw882xx_chan_info *chan_info = &aw882xx->chan_info;
	struct ptr_params_data *p_params = NULL;
	int32_t *p_data = NULL;

	aw_dev_info(aw882xx->dev, "cmd : %d, data_len%d\n", cmd , data_len);

	data_ptr = kmalloc(data_len, GFP_KERNEL);
	if (!data_ptr)
		return -ENOMEM;

	ret = aw882xx_file_get_index(cmd, &index);
	if (ret < 0)
		goto exit;

	switch (cmd) {
	case AW882XX_IOCTL_ENABLE_CALI:
		if (copy_from_user(data_ptr,
				(void __user *)arg, data_len)) {
			ret = -EFAULT;
			goto exit;
		}
		aw882xx->cali.status = (int8_t)data_ptr[0];
		aw_dev_info(aw882xx->dev, "%s:set cali %s", __func__,
			(aw882xx->cali.status == 0) ? ("disable") : ("enable"));
		break;
	case AW882XX_IOCTL_SET_CALI_CFG:
	case AW882XX_IOCTL_SET_NOISE:
	case AW882XX_IOCTL_SET_VMAX:
	case AW882XX_IOCTL_SET_PARAM:
		if (copy_from_user(data_ptr,
				(void __user *)arg, data_len)) {
			ret = -EFAULT;
			goto exit;
		}
		ret = aw_write_data_to_dsp(index, data_ptr,
					data_len, chan_info->channel);
		if (ret) {
			aw_dev_err(aw882xx->dev, "%s: dsp_msg_write error: %d\n",
				__func__, index);
			goto exit;
		}
		break;
	case AW882XX_IOCTL_SET_PTR_PARAM_NUM:
		if (copy_from_user(data_ptr,
				(void __user *)arg, data_len)) {
			ret = -EFAULT;
			goto exit;
		}
		p_params = (struct ptr_params_data *)data_ptr;
		if (p_params->data == NULL || (!p_params->len)) {
			aw_dev_err(aw882xx->dev, "%s: p_params error\n",
				__func__);
			ret = -EFAULT;
			goto exit;
		}
		p_data = kzalloc(p_params->len, GFP_KERNEL);
		if (!p_data) {
			aw_dev_err(aw882xx->dev,
				"%s: error allocating memory\n", __func__);
			ret = -ENOMEM;
			goto exit;
		}

		if (copy_from_user(p_data,
				(void __user *)p_params->data,
				p_params->len)) {
			kfree(p_data);
			ret = -EFAULT;
			goto exit;
		}

		ret = aw_write_data_to_dsp(index, p_data,
					p_params->len, chan_info->channel);
		if (ret < 0) {
			aw_dev_err(aw882xx->dev, "%s: dsp_msg_write error: %d\n",
				__func__, index);
			kfree(p_data);
			goto exit;
		}
		kfree(p_data);
		break;
	case AW882XX_IOCTL_SET_CALI_RE:
		if (copy_from_user(data_ptr,
			(void __user *)arg, data_len)) {
			ret = -EFAULT;
			goto exit;
		}
		ret = aw_write_data_to_dsp(index, data_ptr,
					data_len, chan_info->channel);
		if (ret < 0) {
			aw_dev_err(aw882xx->dev, "%s: dsp_msg_write error: %d\n",
				__func__, index);
			goto exit;
		}
		ret = aw882xx_store_cali_re(aw882xx, *((int32_t *)data_ptr));
		if (ret < 0) {
			aw_dev_err(aw882xx->dev, "%s: store cali re error\n",
				__func__);
			goto exit;
		}
		break;
	case AW882XX_IOCTL_SET_DSP_HMUTE:
		if (copy_from_user(data_ptr,
			(void __user *)arg, data_len)) {
			ret = -EFAULT;
			goto exit;
		}
		ret = aw882xx_set_dsp_msg_data(aw882xx,
			data_ptr, data_len, INLINE_PARAM_ID_ENABLE_HMUTE);
		if (ret < 0)
			goto exit;
		break;
	case AW882XX_IOCTL_SET_CALI_CFG_FLAG:
		if (copy_from_user(data_ptr,
			(void __user *)arg, data_len)) {
			ret = -EFAULT;
			goto exit;
		}
		ret = aw882xx_set_dsp_msg_data(aw882xx,
			data_ptr, data_len, INLINE_PARAM_ID_ENABLE_CALI);
		if (ret < 0)
			goto exit;
		break;
	case AW882XX_IOCTL_GET_CALI_CFG:
	case AW882XX_IOCTL_GET_CALI_DATA:
	case AW882XX_IOCTL_GET_F0:
	case AW882XX_IOCTL_GET_CALI_RE:
	case AW882XX_IOCTL_GET_VMAX:
		ret = aw_read_data_from_dsp(index, data_ptr,
					data_len, chan_info->channel);
		if (ret) {
			aw_dev_err(aw882xx->dev, "%s: dsp_msg_read error: %d\n",
				__func__, index);
			ret = -EFAULT;
			goto exit;
		}
		if (copy_to_user((void __user *)arg,
			data_ptr, data_len)) {
			ret = -EFAULT;
			goto exit;
		}
		break;
	case AW882XX_IOCTL_GET_F0_Q:
		ret = aw882xx_get_dsp_msg_data(aw882xx,
			data_ptr, data_len, INLINE_PARAM_ID_F0_Q);
		if (ret < 0)
			goto exit;
		if (copy_to_user((void __user *)arg,
			data_ptr, data_len)) {
			ret = -EFAULT;
			goto exit;
		}
		break;
	default:
		aw_dev_err(aw882xx->dev, "%s : cmd %d\n",
			__func__, cmd);
		break;
	}
exit:
	kfree(data_ptr);
	return ret;
}

static long aw882xx_file_unlocked_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct aw882xx *aw882xx = NULL;
	if(file == NULL)
	{
		printk( "%s: file is null\n", __func__);
		return -EINVAL;
	}
	if(NULL == file->private_data){
		printk("%s: file->private_data is NULL\n", __func__);
		return -EINVAL;
	}
	aw882xx = (struct aw882xx *)file->private_data;
	if (((_IOC_TYPE(cmd)) != (AW882XX_IOCTL_MAGIC))) {
		aw_dev_err(aw882xx->dev, "%s: cmd magic err\n", __func__);
		return -EINVAL;
	}

	ret = aw882xx_cali_operation(aw882xx, cmd, arg);
	if (ret)
		return -EINVAL;

	return 0;
}

#ifdef CONFIG_COMPAT
static long aw882xx_file_compat_ioctl(struct file *file,
	unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct aw882xx *aw882xx = NULL;
	if(file == NULL){
		printk( "%s: file is null\n", __func__);
		return -EINVAL;
	}
	aw882xx = (struct aw882xx *)file->private_data;
	if (((_IOC_TYPE(cmd)) != (AW882XX_IOCTL_MAGIC))) {
		aw_dev_err(aw882xx->dev, "%s: cmd magic err\n", __func__);
		return -EINVAL;
	}
	ret = aw882xx_cali_operation(aw882xx, cmd, arg);
	if (ret)
		return -EINVAL;

	return 0;
}
#endif

static const struct file_operations aw882xx_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = aw882xx_file_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = aw882xx_file_compat_ioctl,
#endif
	.open = aw882xx_file_open,
	.release = aw882xx_file_release,
};

static void aw_cali_misc_init(struct aw882xx *aw882xx)
{
	int ret;
	struct miscdevice *device = &aw882xx->cali.misc.misc_device;
	const char *aw_misc_name = "aw882xx_smartpa";

	aw882xx_append_suffix("%s_%s", &aw_misc_name, aw882xx);

	device->minor = MISC_DYNAMIC_MINOR;
	device->name  = aw_misc_name;
	device->fops  = &aw882xx_fops;

	ret = misc_register(device);
	if (ret) {
		aw_dev_err(aw882xx->dev, "%s: misc register fail: %d\n",
			__func__, ret);
		return;
	}
	aw_dev_dbg(aw882xx->dev, "%s: misc register success\n", __func__);
}

static void aw_cali_misc_deinit(struct aw882xx *aw882xx)
{
	misc_deregister(&aw882xx->cali.misc.misc_device);
	aw_dev_dbg(aw882xx->dev, "%s: misc unregister done\n", __func__);
}

/*****************ATTR FOR Calibration**********************************/
static ssize_t aw882xx_cali_time_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct aw882xx *aw882xx = dev_get_drvdata(dev);
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len, "%us ms\n",
				aw882xx->cali.cali_re_time);

	return len;
}

static ssize_t aw882xx_cali_time_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct aw882xx *aw882xx = dev_get_drvdata(dev);
	uint32_t time;

	ret = kstrtoint(buf, 0, &time);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s, read buf %s failed\n",
			__func__, buf);
		return ret;
	}

	if (time < 400) {
		aw_dev_err(aw882xx->dev, "%s:time:%d is too short, no set\n",
			__func__, time);
		return -EINVAL;
	}

	aw882xx->cali.cali_re_time = time;
	aw_dev_info(aw882xx->dev, "%s:time:%d\n",
			__func__, aw882xx->cali.cali_re_time);

	return count;
}

static ssize_t aw882xx_cali_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int32_t local_re = 0, local_f0 = 0;
	int ret;
	struct aw882xx *aw882xx = dev_get_drvdata(dev);

	if (strncmp("start_cali", buf, strlen("start_cali"))) {
		aw_dev_err(aw882xx->dev, "%s: not define cmd %s\n",
			__func__, buf);

		ret = -EINVAL;
		goto cali_failed;
	}

	ret = aw882xx_cali_start_up(&local_re, &local_f0, aw882xx);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s: cali failed\n", __func__);
		goto cali_failed;
	}

	//local_re = (local_re * 1000) >> 12;
	aw882xx->cali.re = local_re;
	aw882xx->cali.f0 = local_f0;

	return count;

cali_failed:
	aw882xx->cali.re = 0;
	aw882xx->cali.f0 = 0;
	return ret;
}

static ssize_t aw882xx_cali_re_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int32_t local_re = 0;
	int ret;
	struct aw882xx *aw882xx = dev_get_drvdata(dev);

	if (strncmp("start_cali_re", buf, strlen("start_cali_re"))) {
		aw_dev_err(aw882xx->dev, "%s: not define cmd %s\n",
			__func__, buf);
		aw882xx->cali.re = 0;
		return -EINVAL;
	}

	ret = aw_cali_get_re(&local_re, aw882xx);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s: cali failed\n", __func__);
		aw882xx->cali.re = 0;
		return -EPERM;
	}

	//local_re = (local_re * 1000) >> 12;
	aw882xx->cali.re = local_re;
	return count;
}

static ssize_t aw882xx_cali_f0_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int32_t local_f0 = 0;
	int ret;
	struct aw882xx *aw882xx = dev_get_drvdata(dev);

	if (strncmp("start_cali_f0", buf, strlen("start_cali_f0"))) {
		aw_dev_err(aw882xx->dev, "%s: not define cmd %s\n",
			__func__, buf);
		aw882xx->cali.f0 = 0;
		return -EINVAL;
	}

	ret = aw_cali_get_f0(&local_f0, aw882xx);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s: cali failed\n", __func__);
		aw882xx->cali.f0 = 0;
		return -EPERM;
	}

	aw882xx->cali.f0 = local_f0;
	return count;
}

static ssize_t aw882xx_re_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct aw882xx *aw882xx = dev_get_drvdata(dev);
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", aw882xx->cali.re);

	return len;
}

static ssize_t aw882xx_f0_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct aw882xx *aw882xx = dev_get_drvdata(dev);
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len, "%d\n",
		aw882xx->cali.f0);

	return len;
}

static ssize_t aw882xx_dsp_re_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct aw882xx *aw882xx = dev_get_drvdata(dev);
	int ret = -1;
	ssize_t len = 0;
	uint32_t re = 0;

	ret = aw_read_data_from_dsp(INDEX_PARAMS_ID_RX_RE,
				&re, sizeof(uint32_t),
				aw882xx->chan_info.channel);
	if (ret < 0)
		aw_dev_err(aw882xx->dev, "%s : get dsp re failed\n",
			__func__);

	//re = (re * 1000) / (1 << 12);
	len += snprintf(buf+len, PAGE_SIZE-len, "dsp_re:%ud\n", re);

	return len;
}

static ssize_t aw882xx_dsp_re_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	struct aw882xx *aw882xx = dev_get_drvdata(dev);
	int32_t data;
	int32_t cali_re;

	ret = kstrtoint(buf, 0, &data);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s, read buf %s failed\n",
			__func__, buf);
		return ret;
	}

	cali_re = (data * (1 << 12)) / 1000;

	ret = aw_write_data_to_dsp(INDEX_PARAMS_ID_RX_RE, (void *)&cali_re,
				sizeof(uint32_t), aw882xx->chan_info.channel);
	if (ret) {
		aw_dev_err(aw882xx->dev, "%s: write cali_re to dsp failed\n",
			__func__);
		return -EBUSY;
	}
	ret = aw882xx_store_cali_re(aw882xx, cali_re);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "%s: store cali re error\n",
			__func__);
		return -EPERM;
	}

	aw_dev_dbg(aw882xx->dev, "%s: re:0x%x",
			__func__, aw882xx->cali.cali_re);

	return count;
}

static DEVICE_ATTR(cali_time, S_IRUGO | S_IWUSR,
	aw882xx_cali_time_show, aw882xx_cali_time_store);
static DEVICE_ATTR(cali, S_IWUSR ,
	NULL, aw882xx_cali_store);
static DEVICE_ATTR(cali_re, S_IWUSR,
	NULL, aw882xx_cali_re_store);
static DEVICE_ATTR(cali_f0, S_IWUSR,
	NULL, aw882xx_cali_f0_store);
static DEVICE_ATTR(re_show, S_IRUGO,
	aw882xx_re_show, NULL);
static DEVICE_ATTR(f0_show, S_IRUGO,
	aw882xx_f0_show, NULL);
static DEVICE_ATTR(dsp_re, S_IWUSR | S_IRUGO,
	aw882xx_dsp_re_show, aw882xx_dsp_re_store);


static struct attribute *aw882xx_cali_attr[] = {
	&dev_attr_cali_time.attr,
	&dev_attr_cali.attr,
	&dev_attr_cali_re.attr,
	&dev_attr_cali_f0.attr,
	&dev_attr_re_show.attr,
	&dev_attr_f0_show.attr,
	&dev_attr_dsp_re.attr,
	NULL
};

static struct attribute_group aw882xx_cali_attr_group = {
	.attrs = aw882xx_cali_attr
};

static void aw_cali_attr_init(struct aw882xx *aw882xx)
{
	int ret;

	ret = sysfs_create_group(&aw882xx->dev->kobj, &aw882xx_cali_attr_group);
	if (ret < 0) {
		aw_dev_info(aw882xx->dev, "%s error creating sysfs cali attr files\n",
			__func__);
	}
}

static void aw_cali_attr_deinit(struct aw882xx *aw882xx)
{
	aw_dev_info(aw882xx->dev, "%s attr files deinit\n", __func__);
}

void aw_cali_init(struct aw_cali *cali)
{
	struct aw882xx *aw882xx =
			container_of(cali, struct aw882xx, cali);

	aw_dev_info(aw882xx->dev, "%s enter\n", __func__);
	cali->status = 0;
	aw882xx->cali.cali_re_time = AW_CALI_RE_DEFAULT_TIMER;
	if (cali->cali_mode == AW_CALI_MODE_DBGFS)
		aw_cali_debugfs_init(aw882xx);
	else if (cali->cali_mode == AW_CALI_MODE_MISC)
		aw_cali_misc_init(aw882xx);

	aw_cali_attr_init(aw882xx);
}

void aw_cali_deinit(struct aw_cali *cali)
{
	struct aw882xx *aw882xx =
			container_of(cali, struct aw882xx, cali);

	aw_dev_info(aw882xx->dev, "%s enter\n", __func__);
	if (cali->cali_mode == AW_CALI_MODE_DBGFS)
		aw_cali_debugfs_deinit(aw882xx);
	else if (cali->cali_mode == AW_CALI_MODE_MISC)
		aw_cali_misc_deinit(aw882xx);

	aw_cali_attr_deinit(aw882xx);
}

/*****************************************************
 *
 * device tree parse cali mode
 *
 *****************************************************/
void aw882xx_parse_cali_mode_dt(struct aw_cali *cali)
{
	int ret = -1;
	const char *cali_mode_str = NULL;
	struct aw882xx *aw882xx =
			container_of(cali, struct aw882xx, cali);
	struct device_node *np = aw882xx->dev->of_node;

	ret = of_property_read_string(np, "aw-cali-mode", &cali_mode_str);
	if (ret < 0) {
		dev_info(aw882xx->dev, "%s: aw-cali-mode get failed ,user default attr way\n",
				__func__);
		cali->cali_mode = AW_CALI_MODE_NONE;
		return;
	}

	if (!strcmp(cali_mode_str, "aw_debugfs"))
		cali->cali_mode = AW_CALI_MODE_DBGFS;
	else if (!strcmp(cali_mode_str, "aw_misc"))
		cali->cali_mode = AW_CALI_MODE_MISC;
	else
		cali->cali_mode = AW_CALI_MODE_NONE;

	aw_dev_info(aw882xx->dev, "%s:cali mode str:%s num:%d\n",
			__func__, cali_mode_str, aw882xx->cali.cali_mode);
}
