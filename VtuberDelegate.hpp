/**
  Created by Weng Y on 2020/05/25.
  Copyright © 2020 Weng Y. Under GNU General Public License v2.0.
*/

#pragma once

#include <GL/glew.h>
#include "LAppAllocator.hpp"
#ifdef _WIN32
#include <GLFW/glfw3.h>
#endif

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#define MAXMODELCOUNT 1024

class View;
class LAppTextureManager;
class Hook;
/**
* @brief   アプリケーションクラス。
*   Cubism SDK の管理を行う。
*/
class VtuberDelegate {

	struct RenderInfo {
		bool isLoadResource = false;
		//view
		double viewPoint_x = 0.0;
		double viewPoint_y = 0.0;
		int windowWidth = 0;
		int windowHeight = 0;
		double Scale = 1.0;
	};

public:

	static VtuberDelegate *GetInstance();

	static void ReleaseInstance();

    /*
    *@breif 不处理opengl相关的初始化
    */
    bool LoadResource(int id);

    void ReleaseResource(int id);

    bool Initialize(int id);

    void Release();

    bool isLoadResource(int id);

    int getBufferWidth(int id);

    int getBufferHeight(int id);

    double getScale(int id);

    double GetX(int id);

    double GetY(int id);

    void UpdataViewWindow(double _x,double _y,int _width, int _height,double _scale, int _id);


    View* GetView() { return _view; }

    LAppTextureManager* GetTextureManager() { return _textureManager; }


    /**
    * @brief 渲染一帧画面到指定缓冲
    */
    void Reader(int targatid, char *data, int bufferWidth, int bufferheight);


    /**
    * @brief 复制最后一次由渲染线程发布的画面。
    *
    * 该函数绝不会访问 OpenGL，只读取 CPU 侧的双缓冲快照。调用方线程可以
    * 是 OBS 的视频渲染线程。若还没有可用画面则返回 false。
    *
    * @param[out] width   发布画面的宽度
    * @param[out] height  发布画面的高度
    * @param[out] out     RGBA8 像素拷贝（width * height * 4 字节）
    */
    bool GetLatestFrame(int &width, int &height, std::vector<uint8_t> &out);


    void ChangeModel(const char *ModelName, int id);


    void updataModelSetting(bool _randomMotion, double _delayTime, bool _breath,
			    bool _eyeBlink, bool _track,
			    bool _isMouseHorizontalFlip,bool _isMouseVerticalFlip,
		            int id);

    const char ** GetModeDefine(int &size);

    void ChangeMode(const char *_mode, bool _live2D,bool _isUseMask, int id);

    void ChangeMouseMovement(bool _mouse);

    GLuint CreateShader();

    bool CheckShader(GLuint shaderId);
private:
    /**
    * @brief   コンストラクタ
    */
	VtuberDelegate();

    /**
    * @brief   デストラクタ
    */
	~VtuberDelegate();

    /**
    * @brief   Cubism SDK の初期化
    */
	void InitializeCubism();

    /**
    * @brief 渲染线程入口：独占 EGL/GLFW/GLEW/Cubism 的生命周期。
    */
    void RenderThreadMain(int id);

    /**
    * @brief 请求渲染线程退出并等待其 join。
    */
    void StopRenderThread();


    LAppAllocator _cubismAllocator;              ///< Cubism SDK Allocator
    Csm::CubismFramework::Option _cubismOption;  ///< Cubism SDK Option
#ifdef _WIN32
    GLFWwindow* _window;                         ///< OpenGL ウィンドウ
#endif
    View* _view;                             ///< View情報
    Hook *_hook;
    LAppTextureManager* _textureManager;         ///< テクスチャマネージャー

    char *ModelFileName[MAXMODELCOUNT];		///模型文件夹的名称集合
    int ModelFileCount;				///模型数量

    RenderInfo _renderInfo[MAXMODELCOUNT];

    std::thread _renderThread;                   ///< 専用レンダースレッド
    std::mutex _viewMutex;                       ///< View/Live2D 同時アクセス保護
    std::mutex _frameMutex;                      ///< フレーム公開バッファ保護
    std::condition_variable _frameCv;            ///< 公開/停止/初期化通知

    std::vector<uint8_t> _frameBuffer;           ///< 公開済み RGBA8 フレーム
    int _frameWidth = 0;
    int _frameHeight = 0;
    uint64_t _frameSequence = 0;
    bool _hasFrame = false;

    bool _renderThreadRunning = false;
    bool _renderStop = false;
    bool _renderInitialized = false;
    bool _renderInitFailed = false;
    };
