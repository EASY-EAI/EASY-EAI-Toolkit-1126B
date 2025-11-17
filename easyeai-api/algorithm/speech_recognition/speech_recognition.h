#ifndef __WHISPER_H__
#define __WHISPER_H__

#include <vector>
#include <string>
#include "rknn_api.h"
#include "audio_utils.h"

#define ENABLE_NEON			1							// 使用NEON加速
#define M_PI				3.14159265358979323846		// pi

#define VOCAB_NUM			51865						// 词汇数
#define MAX_TOKENS			12
#define N_MELS				80							// 梅尔频谱频带数
#define MELS_FILTERS_SIZE	201							// (N_FFT / 2 + 1)
#define SAMPLE_RATE			16000						// 音频采样率
#define CHUNK_LENGTH		30 							// 单次处理的音频块长度（单位：秒）
#define HOP_LENGTH			160							// 梅尔频谱的帧移
#define MAX_AUDIO_LENGTH	CHUNK_LENGTH * SAMPLE_RATE
#define N_FFT				400
#define ENCODER_INPUT_SIZE CHUNK_LENGTH * 100
#define ENCODER_OUTPUT_SIZE CHUNK_LENGTH * 50 * 768 //768		// 384/512/1024 for tiny/base/medium models respectively
#define DECODER_INPUT_SIZE ENCODER_OUTPUT_SIZE


/**< @brief 词汇信息 */
typedef struct{
	int index;
	char *token;
}vocab_entry_t;

/**< @brief whisper模型上下文 */
typedef struct{
	rknn_context			rknn_ctx;
	rknn_input_output_num	io_num;
	rknn_tensor_attr		*input_attrs;
	rknn_tensor_attr		*output_attrs;
} rknn_whisper_context_t;


/**< @brief whisper模型结构体 */
typedef struct {
	rknn_whisper_context_t	encoder_context;		// 音频编码器（将音频转换为更适合语言生成的中间表示）
	rknn_whisper_context_t	decoder_context;		// 音频解码器（语音识别）
	vocab_entry_t			vocabs[VOCAB_NUM];		// 词汇表
	float					*p_mel_filters;			// 滤波器
	float					*p_encoder_output;		// 编码输出
} rknn_whisper_t;


#ifdef __cplusplus
extern "C" {
#endif
	/**
	* @brief  speech_recognition_init初始化函数
	*
	* @param[in]		p_encoder_path			编码模型
	* @param[in]		p_decoder_path			解码模型
	* @param[in]		p_filter_path			滤波器频谱
	* @param[in]		p_vocab_path			词组文件
	* @param[i/o]		p_whisper				语音识别对象
	* @return									状态码，0表成功
	*/
	int speech_recognition_init(const char *p_encoder_path, const char *p_decoder_path, const char *p_filter_path, 
		const char *p_vocab_path, rknn_whisper_t *p_whisper);


	/**
	* @brief  speech_recognition_run函数
	*
	* @param[in]								语音识别对象
	* @param[in]		audio					待识别音频信息
	* @param[in]		task_code				语音识别任务
	* @param[i/o]		recognized_text			语音识别结果
	* @return									状态码，0表成功
	*/
	int speech_recognition_run(rknn_whisper_t *p_whisper, audio_buffer_t audio, int task_code, std::vector<std::string> &recognized_text);


	/**
	* @brief  speech_recognition_release释放函数
	*
	* @param[i/o]							    语音识别对象
	* @return									模型释放结果
	*/
	int speech_recognition_release(rknn_whisper_t *p_whisper);


#ifdef __cplusplus
}
#endif


#endif // !__WHISPER_H__
