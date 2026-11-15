/**
  Created by Weng Y on 2020/05/25.
  Copyright © 2020 Weng Y. Under GNU General Public License v2.0.
*/

#include "VtuberDelegate.hpp"
#include <GL/glew.h>
#ifdef _WIN32
#include <GLFW/glfw3.h>
#else
#include "EglContext.hpp"
#endif
#include <obs-module.h>
#include "View.hpp"
#include "Pal.hpp"
#include "Define.hpp"
#include "Live2DManager.hpp"
#include "LAppTextureManager.hpp"
#include "Hook.hpp"
#include "InfoReader.hpp"
#include "EventManager.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

using namespace Csm;
using namespace std;
using namespace Define;

namespace {
VtuberDelegate *s_instance = NULL;

    static uint16_t VtuberCount = 0;
    }

VtuberDelegate *VtuberDelegate::GetInstance()
{
    if (s_instance == NULL)
    {
		s_instance = new VtuberDelegate();
    }

    return s_instance;
}

void VtuberDelegate::ReleaseInstance()
{
    if (s_instance != NULL)
    {
        delete s_instance;
    }

    s_instance = NULL;
}

bool VtuberDelegate::LoadResource(int id)
{
	std::lock_guard<std::mutex> lock(_viewMutex);
	if (_view)
		_view->Initialize(id);
	return true;
}

void VtuberDelegate::ReleaseResource(int id) {
	// The render thread may be inside View::Render(id) right now.  Stop it
	// before View::Release() frees the per-id view data.
	StopRenderThread();

	std::lock_guard<std::mutex> lock(_viewMutex);
	_renderInfo[id].isLoadResource = false;
	if (_view)
		_view->Release(id);
}

bool VtuberDelegate::Initialize(int id)
{
	{
		std::lock_guard<std::mutex> lock(_frameMutex);
		if (_renderThreadRunning) {
			return true;
		}

		// A previous attempt may have failed and left a finished, but still
		// joinable, thread handle behind.
		if (_renderThread.joinable()) {
			_renderThread.join();
		}

		_renderThreadRunning = true;
		_renderStop = false;
		_renderInitialized = false;
		_renderInitFailed = false;
		_hasFrame = false;
		_frameWidth = 0;
		_frameHeight = 0;
	}

	// The input hook is unrelated to OpenGL and can be started on the OBS
	// thread before the dedicated render thread comes up.
	if (_hook == NULL) {
		_hook = new Hook();
		_hook->Strat();
	}

	_renderThread = std::thread(&VtuberDelegate::RenderThreadMain, this, id);

	return true;
}

void VtuberDelegate::RenderThreadMain(int id)
{
#ifdef _WIN32
	// ---- GLFW: this thread is the exclusive owner ----------------------
	if (glfwInit() == GLFW_FALSE) {
		blog(LOG_ERROR,
		     "[bongobs-cat] glfwInit failed; no display available. "
		     "The source will stay blank.");
		std::lock_guard<std::mutex> lock(_frameMutex);
		_renderInitFailed = true;
		_renderThreadRunning = false;
		_frameCv.notify_all();
		return;
	}

	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	glfwWindowHint(GLFW_SAMPLES, 16);
	_window = glfwCreateWindow(RenderTargetWidth, RenderTargetHeight,
				   "bongo cat", NULL, NULL);
	if (_window == NULL) {
		blog(LOG_ERROR,
		     "[bongobs-cat] glfwCreateWindow failed; the source will "
		     "stay blank.");
		glfwTerminate();
		std::lock_guard<std::mutex> lock(_frameMutex);
		_renderInitFailed = true;
		_renderThreadRunning = false;
		_frameCv.notify_all();
		return;
	}

	glfwMakeContextCurrent(_window);

	if (glfwRawMouseMotionSupported())
		glfwSetInputMode(_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
#else
	// ---- EGL: offscreen context, no X11/Wayland connection needed -----
	if (!EglContext::Create(RenderTargetWidth, RenderTargetHeight)) {
		blog(LOG_ERROR,
		     "[bongobs-cat] não foi possível criar um contexto EGL; "
		     "a source ficará em branco.");
		std::lock_guard<std::mutex> lock(_frameMutex);
		_renderInitFailed = true;
		_renderThreadRunning = false;
		_frameCv.notify_all();
		return;
	}
#endif

	// ---- GLEW: on Linux it resolves GL calls through EGL ---------------
	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK) {
		blog(LOG_ERROR,
		     "[bongobs-cat] glewInit failed; the source will stay blank.");
#ifdef _WIN32
		glfwMakeContextCurrent(NULL);
		glfwDestroyWindow(_window);
		_window = NULL;
		glfwTerminate();
#else
		EglContext::Destroy();
#endif
		std::lock_guard<std::mutex> lock(_frameMutex);
		_renderInitFailed = true;
		_renderThreadRunning = false;
		_frameCv.notify_all();
		return;
	}

	// ---- Cubism / View initialisation (needs the GL context) -----------
	{
		std::lock_guard<std::mutex> lock(_viewMutex);
		InitializeCubism();
		_view->InitializeSpirite(id);
		_view->InitializeModel(id);
	}

	{
		std::lock_guard<std::mutex> lock(_frameMutex);
		_renderInitialized = true;
		_frameCv.notify_all();
	}

	std::vector<uint8_t> readback;
	while (true) {
		{
			std::unique_lock<std::mutex> lock(_frameMutex);
			if (_renderStop) {
				break;
			}
		}

		int fbWidth = 0;
		int fbHeight = 0;
#ifdef _WIN32
		glfwGetFramebufferSize(_window, &fbWidth, &fbHeight);
#else
		fbWidth = EglContext::Width();
		fbHeight = EglContext::Height();
#endif

		// Never read more pixels than the actual framebuffer or than the
		// size we advertised for the readback buffer.
		const int readWidth = std::min(fbWidth, (int)RenderTargetWidth);
		const int readHeight = std::min(fbHeight, (int)RenderTargetHeight);

		if (readWidth > 0 && readHeight > 0) {
			const size_t readSize = (size_t)readWidth *
						(size_t)readHeight * 4u;
			if (readback.size() != readSize) {
				readback.resize(readSize);
			}

			{
				std::lock_guard<std::mutex> lock(_viewMutex);
				glEnable(GL_MULTISAMPLE);
				glViewport(0, 0, fbWidth, fbHeight);
				glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				_view->Render(id);
			}

			glReadPixels(0, 0, readWidth, readHeight, GL_RGBA,
				     GL_UNSIGNED_BYTE, readback.data());

			{
				std::unique_lock<std::mutex> lock(_frameMutex);
				_frameBuffer.swap(readback);
				_frameWidth = readWidth;
				_frameHeight = readHeight;
				++_frameSequence;
				_hasFrame = true;
				_frameCv.notify_all();

				// ~60 FPS pacing; the wait also makes shutdown snappy.
				_frameCv.wait_for(
					lock, std::chrono::milliseconds(16),
					[this] { return _renderStop; });
				if (_renderStop) {
					break;
				}
			}
		} else {
			std::unique_lock<std::mutex> lock(_frameMutex);
			_frameCv.wait_for(
				lock, std::chrono::milliseconds(16),
				[this] { return _renderStop; });
			if (_renderStop) {
				break;
			}
		}
	}

	// Release the thread's ownership of the context before Release()
	// destroys the window.  All GL objects are torn down by Release(),
	// which runs only after this thread has been joined.
#ifdef _WIN32
	glfwMakeContextCurrent(NULL);
#else
	EglContext::Destroy();
#endif

	{
		std::lock_guard<std::mutex> lock(_frameMutex);
		_hasFrame = false;
		_renderThreadRunning = false;
		_frameCv.notify_all();
	}
}

void VtuberDelegate::StopRenderThread()
{
	{
		std::lock_guard<std::mutex> lock(_frameMutex);
		_renderStop = true;
	}
	_frameCv.notify_all();

	if (_renderThread.joinable()) {
		_renderThread.join();
	}
}

void VtuberDelegate::Release()
{
	// 1. The render thread must not be inside View::Render() while we free
	//    Cubism/Live2D/View.
	StopRenderThread();

	// 2. Stop and destroy the input hook before the View/EventManager it may
	//    use are destroyed.  On Linux Stop() joins the input thread.
	if (_hook) {
		_hook->Stop();
		delete _hook;
		_hook = NULL;
	}

	// 3. Tear down the rendering context.  No GL calls are made by the OBS
	//    render thread; the context was created and released by the dedicated
	//    thread, and this only runs after that thread has exited.
#ifdef _WIN32
	if (_window) {
		glfwMakeContextCurrent(NULL);
		glfwDestroyWindow(_window);
		_window = NULL;
		glfwTerminate();
	}
#else
	EglContext::Destroy();
#endif

	// 4. Free the CPU/GL resources.
	delete _textureManager;
	_textureManager = NULL;

	delete _view;
	_view = NULL;

	Live2DManager::ReleaseInstance();

	CubismFramework::CleanUp();
	CubismFramework::Dispose();
}

bool VtuberDelegate::GetLatestFrame(int &width, int &height,
				    std::vector<uint8_t> &out)
{
	std::lock_guard<std::mutex> lock(_frameMutex);

	if (!_hasFrame || _frameWidth <= 0 || _frameHeight <= 0 ||
	    _frameBuffer.empty()) {
		return false;
	}

	width = _frameWidth;
	height = _frameHeight;
	// Hand the published buffer over instead of copying ~4 MB per frame.
	// The producer always resizes its own buffer before writing, so the
	// swap is safe and keeps both sides allocation-free in steady state.
	out.swap(_frameBuffer);
	return true;
}

void VtuberDelegate::Reader(int id, char *buffer, int bufferWidth,
			    int bufferheight)
{
	(void)id;

	if (buffer == NULL || bufferWidth <= 0 || bufferheight <= 0) {
		return;
	}

	std::vector<uint8_t> frame;
	int frameWidth = 0;
	int frameHeight = 0;

	if (!GetLatestFrame(frameWidth, frameHeight, frame)) {
		// No frame published yet: clear the OBS buffer so a stale frame
		// is never shown.
		memset(buffer, 0, (size_t)bufferWidth * (size_t)bufferheight * 4u);
		return;
	}

	const int copyWidth = std::min(bufferWidth, frameWidth);
	const int copyHeight = std::min(bufferheight, frameHeight);
	const size_t srcStride = (size_t)frameWidth * 4u;
	const size_t dstStride = (size_t)bufferWidth * 4u;

	for (int y = 0; y < copyHeight; ++y) {
		memcpy(buffer + (size_t)y * dstStride,
		       frame.data() + (size_t)y * srcStride,
		       (size_t)copyWidth * 4u);
	}
}

void VtuberDelegate::UpdataViewWindow(double _x, double _y, int _width,
				      int _height, double _scale, int _id)
{
	std::lock_guard<std::mutex> lock(_viewMutex);
	_renderInfo[_id].viewPoint_x = _x;
	_renderInfo[_id].viewPoint_y = _y;
	_renderInfo[_id].windowWidth = _width/32*32;
	_renderInfo[_id].windowHeight = _height;
	_renderInfo[_id].Scale = _scale;
}

void VtuberDelegate::updataModelSetting(bool _randomMotion, double _delayTime,
					bool _breath, bool _eyeBlink,bool _istrack,
					bool _isMouseHorizontalFlip,bool _isMouseVerticalFlip,
					int id)
{
	std::lock_guard<std::mutex> lock(_viewMutex);
	Live2DManager::GetInstance()->UpdateModelSetting(
		_randomMotion, _delayTime, _breath, _eyeBlink, _istrack,
		_isMouseHorizontalFlip, _isMouseVerticalFlip);
}

const char **VtuberDelegate::GetModeDefine(int &size) {
	std::lock_guard<std::mutex> lock(_viewMutex);
	InfoReader *_infoReader = _view->GetInfoReader();

	size = _infoReader->ModeCount;
	return (const char**)_infoReader->ModePath;
}

void VtuberDelegate::ChangeMode(const char *_mode,bool _live2d,bool _isUseMask, int id)
{
	std::lock_guard<std::mutex> lock(_viewMutex);
	InfoReader *_infoReader = _view->GetInfoReader();
	for (int i = 0; i < _infoReader->ModeCount; i++) {
		if (strcmp(_mode, _infoReader->ModePath[i]) == 0) {
			_view->setMod(i);
			
		}			
	}
	_view->Update(_live2d,_isUseMask);
}

void VtuberDelegate::ChangeMouseMovement(bool _mouse) {
	std::lock_guard<std::mutex> lock(_viewMutex);
	Live2DManager::GetInstance()->ChangeMouseMovement(_mouse);
}


void VtuberDelegate::ChangeModel(const char *ModelName, int id)
{
	
}

VtuberDelegate::VtuberDelegate()
	: _cubismOption(),
#ifdef _WIN32
	  _window(NULL),
#endif
	  _view(NULL),
	  _hook(NULL),
	  _textureManager(NULL),
	  ModelFileCount(0)
{
    _view = new View();
    _textureManager = new LAppTextureManager();
}

VtuberDelegate::~VtuberDelegate() {}

void VtuberDelegate::InitializeCubism()
{
    _cubismOption.LogFunction = Pal::PrintMessage;
    _cubismOption.LoggingLevel = Define::CubismLoggingLevel;
    Csm::CubismFramework::StartUp(&_cubismAllocator, &_cubismOption);

    //Initialize cubism
    CubismFramework::Initialize();

    Live2DManager::GetInstance();

    //default proj
    CubismMatrix44 projection;
}

GLuint VtuberDelegate::CreateShader()
{
	//バーテックスシェーダのコンパイル
	GLuint vertexShaderId = glCreateShader(GL_VERTEX_SHADER);
	const char *vertexShader = "#version 120\n"
				   "attribute vec3 position;"
				   "attribute vec2 uv;"
				   "varying vec2 vuv;"
				   "void main(void){"
				   "    gl_Position = vec4(position, 1.0);"
				   "    vuv = uv;"
				   "}";
	glShaderSource(vertexShaderId, 1, &vertexShader, NULL);
	glCompileShader(vertexShaderId);
	if (!CheckShader(vertexShaderId)) {
		return 0;
	}

	//フラグメントシェーダのコンパイル
	GLuint fragmentShaderId = glCreateShader(GL_FRAGMENT_SHADER);
	const char *fragmentShader =
		"#version 120\n"
		"varying vec2 vuv;"
		"uniform sampler2D texture;"
		"uniform vec4 baseColor;"
		"void main(void){"
		"    gl_FragColor = texture2D(texture, vuv) * baseColor;"
		"}";
	glShaderSource(fragmentShaderId, 1, &fragmentShader, NULL);
	glCompileShader(fragmentShaderId);
	if (!CheckShader(fragmentShaderId)) {
		return 0;
	}

	//プログラムオブジェクトの作成
	GLuint programId = glCreateProgram();
	glAttachShader(programId, vertexShaderId);
	glAttachShader(programId, fragmentShaderId);

	// リンク
	glLinkProgram(programId);

	glUseProgram(programId);

	return programId;
}

bool VtuberDelegate::CheckShader(GLuint shaderId)
{
	GLint status;
	GLint logLength;
	glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);
	if (logLength > 0) {
		GLchar *log = reinterpret_cast<GLchar *>(CSM_MALLOC(logLength));
		glGetShaderInfoLog(shaderId, logLength, &logLength, log);
		CubismLogError("Shader compile log: %s", log);
		CSM_FREE(log);
	}

	glGetShaderiv(shaderId, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE) {
		glDeleteShader(shaderId);
		return false;
	}

	return true;
}

bool VtuberDelegate::isLoadResource(int id)
{
	return _renderInfo[id].isLoadResource;
}

int VtuberDelegate::getBufferWidth(int id)
{
	return _renderInfo[id].windowWidth;
}

int VtuberDelegate::getBufferHeight(int id)
{
	return _renderInfo[id].windowHeight;
}

double VtuberDelegate::getScale(int id)
{
	return _renderInfo[id].Scale;
}

double VtuberDelegate::GetX(int id)
{
	return _renderInfo[id].viewPoint_x;
}

double VtuberDelegate::GetY(int id)
{
	return _renderInfo[id].viewPoint_y;
}
