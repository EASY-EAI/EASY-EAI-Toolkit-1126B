#ifndef __PUT_TEXT_ZH_H__
#define __PUT_TEXT_ZH_H__

#include "opencv2/opencv.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H

class PutTextZH {
public:

	PutTextZH(const char* freeType);
	virtual ~PutTextZH();

	void getFont(int* type, cv::Scalar* size = nullptr, bool* underline = nullptr, float* diaphaneity = nullptr);

	void setFont(int* type, cv::Scalar* size = nullptr, bool* underline = nullptr, float* diaphaneity = nullptr);

	void restoreFont();

	int putText(cv::Mat& img, char* text, cv::Point pos);


	int putText(cv::Mat& img, const wchar_t* text, cv::Point pos);

	int putText(cv::Mat& img, const char* text, cv::Point pos, cv::Scalar color);

	int putText(cv::Mat& img, const wchar_t* text, cv::Point pos, cv::Scalar color);

private:
	
	PutTextZH& operator=(const PutTextZH&);

	void putWChar(cv::Mat& img, wchar_t wc, cv::Point& pos, cv::Scalar color);

	FT_Library   m_library;   // 字库
	FT_Face      m_face;      // 字体

	 // 默认的字体输出参数
	int				m_fontType;
	cv::Scalar		m_fontSize;
	bool			m_fontUnderline;
	float			m_fontDiaphaneity;
};

#endif 

