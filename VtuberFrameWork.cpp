/**
  Created by Weng Y on 2020/05/25.
  Copyright © 2020 Weng Y. Under GNU General Public License v2.0.
*/
#include "VtuberFrameWork.hpp"
#include"VtuberDelegate.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
    static bool isLoad;

    static bool isInit;

    static int initCount = 0;
    }

void VtuberFrameWork::InitVtuber(int id)
{
    initCount++;
    if (initCount == 0) {
	isInit = false;
    }
    isLoad = VtuberDelegate::GetInstance()->LoadResource(id);
}

void VtuberFrameWork::ReanderVtuber(int targatid, char *data, int bufferWidth,
				    int bufferheight)
{
    if (!data || bufferWidth <= 0 || bufferheight <= 0)
	return;

    if (isLoad) {
    
    if (initCount>0 && !isInit) {
	    isInit = true;
	    // Starts the dedicated GL render thread.  It returns immediately;
	    // frames are published asynchronously from now on.
	    VtuberDelegate::GetInstance()->Initialize(targatid);
    }

    std::vector<uint8_t> frame;
    int frameWidth = 0;
    int frameHeight = 0;

    if (!VtuberDelegate::GetInstance()->GetLatestFrame(
		frameWidth, frameHeight, frame)) {
	// No frame published yet: clear the OBS buffer so a stale frame is
	// never shown.
	memset(data, 0,
	       (size_t)bufferWidth * (size_t)bufferheight * 4u);
	return;
    }

    // Copy only what fits in the OBS buffer.  The frame may be smaller than
    // the buffer (framebuffer clamped) and must never overflow it.
    const int copyWidth = std::min(bufferWidth, frameWidth);
    const int copyHeight = std::min(bufferheight, frameHeight);
    const size_t srcStride = (size_t)frameWidth * 4u;
    const size_t dstStride = (size_t)bufferWidth * 4u;

    for (int y = 0; y < copyHeight; ++y) {
	memcpy(data + (size_t)y * dstStride,
	       frame.data() + (size_t)y * srcStride,
	       (size_t)copyWidth * 4u);
    }

    }
}

void VtuberFrameWork::UinitVtuber(int id)
{   
    initCount--;
    if (initCount == 0 && isInit) {
	    VtuberDelegate::GetInstance()->ReleaseResource(id);
	    isInit = false;
	    VtuberDelegate::GetInstance()->Release();
	    VtuberDelegate::ReleaseInstance();
    }
}

void VtuberFrameWork::UpData(int id,double _x, double _y, int width, int height,
			     double sc,double _delayTime, bool _randomMotion,bool _break,
			     bool _eyeBlink,const char *modelPath,bool _tarck,
			     const char *mode, bool _live2d,
			     bool relative_mouse, bool _isMouseHorizontalFlip,
			     bool _isMouseVerticalFlip,bool _isUsemask)
{

	VtuberDelegate::GetInstance()->UpdataViewWindow(_x,_y,width, height,sc, id);
	VtuberDelegate::GetInstance()->updataModelSetting(
		_randomMotion, _delayTime, _break, _eyeBlink, _tarck, _isMouseHorizontalFlip, _isMouseVerticalFlip,id);
	VtuberDelegate::GetInstance()->ChangeModel(modelPath,id);
	VtuberDelegate::GetInstance()->ChangeMode(mode, _live2d, _isUsemask,id);
	VtuberDelegate::GetInstance()->ChangeMouseMovement(relative_mouse);
}

const char** VtuberFrameWork::GetModeDefine(int &_size) {
	return VtuberDelegate::GetInstance()->GetModeDefine(_size);
}

int VtuberFrameWork::GetWidth(int id)
{
	return VtuberDelegate::GetInstance()->getBufferWidth(id);
}

int VtuberFrameWork::GetHeight(int id)
{
	return VtuberDelegate::GetInstance()->getBufferHeight(id);
}







