/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "EventManager.hpp"
#include <cstdlib>
#include <math.h>
#include "Hook.hpp"

EventManager::EventManager()
    : _startY(0.0f)
    , _startX(0.0f)
    , _lastX(0.0f)
    , _lastY(0.0f)
    , _lastX1(0.0f)
    , _lastY1(0.0f)
    , _lastX2(0.0f)
    , _lastY2(0.0f)
    , _deltaX(0.0f)
    , _deltaY(0.0f)
    , _scale(1.0f)
    ,_leftButton(false)
    ,_rightButton(false)
    , _relativemouseX(0)
    , _relativemouseY(0)
    ,_mousePosX(0)
    ,_mousePosY(0)
{
	for (int i = 0; i < KEYAMOUT; i++)
		_keyEvent[i].KeyBoardSignal = false;
	
}

EventManager::~EventManager() {

}

void EventManager::MouseEventBegan(float deviceX, float deviceY)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _lastX = deviceX;
    _lastY = deviceY;
    _startX = deviceX;
    _startY = deviceY;
}

void EventManager::MouseEventMoved(float deviceX, float deviceY)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _lastX = deviceX;
    _lastY = deviceY;
}

void EventManager::MouseEventMoved(float deviceX1, float deviceY1,
				   float deviceX2, float deviceY2)
{
    float distance = CalculateDistance(deviceX1, deviceY1, deviceX2, deviceY2);
    float centerX = (deviceX1 + deviceX2) * 0.5f;
    float centerY = (deviceY1 + deviceY2) * 0.5f;
    (void)distance;

    std::lock_guard<std::mutex> lock(_mutex);
    _lastX = centerX;
    _lastY = centerY;
    _lastX1 = deviceX1;
    _lastY1 = deviceY1;
    _lastX2 = deviceX2;
    _lastY2 = deviceY2;

}

void EventManager::MouseEventMoved(int width, int height, float deviceX,
				   float deviceY)
{
	std::lock_guard<std::mutex> lock(_mutex);

	if (deviceX - _startX > 0) {
		if (deviceX - _startX > width / 2) {
			_startX += deviceX - _startX - width / 2;	
		} 
		_lastX = deviceX;
	} else {
		if (abs(deviceX - _startX) > width / 2) {
			_startX -= abs(deviceX - _startX) - width / 2;
		}	
		_lastX = deviceX;
	}

	if (deviceY - _startY > 0) {
		if (deviceY - _startY > height / 2) {
			_startY += deviceY - _startY - height / 2;
		}
		_lastY = deviceY;
	} else {
		if (abs(deviceY - _startY) > height / 2) {
			_startY -= abs(deviceY - _startY) - height / 2;
		}
		_lastY = deviceY;
	}
}

void EventManager::KeyEventDown(int key)
{
	if (key < 0 || key >= KEYAMOUT)
		return;
	std::lock_guard<std::mutex> lock(_mutex);
	_keyEvent[key].KeyBoardSignal = true;
}

void EventManager::KeyEventUp(int key) {
	if (key < 0 || key >= KEYAMOUT)
		return;
	std::lock_guard<std::mutex> lock(_mutex);
	_keyEvent[key].KeyBoardSignal = false;
}

void EventManager::LeftButtonDown() {
	std::lock_guard<std::mutex> lock(_mutex);
	_leftButton = true;
}

void EventManager::LeftButtonUp() {
	std::lock_guard<std::mutex> lock(_mutex);
	_leftButton = false;
}

void EventManager::RightButtonDown() {
	std::lock_guard<std::mutex> lock(_mutex);
	_rightButton = true;
}

void EventManager::RightButtonUp() {
	std::lock_guard<std::mutex> lock(_mutex);
	_rightButton = false;
}

void EventManager::SetRelativeMouse(int _rx, int _ry) {
	std::lock_guard<std::mutex> lock(_mutex);
	_relativemouseX += _rx;
	_relativemouseY += _ry;
}

void EventManager::GetRelativeMouse(int &_rx, int &_ry) {
	std::lock_guard<std::mutex> lock(_mutex);
	_rx = _relativemouseX;
	_ry = _relativemouseY;
}

void EventManager::GetCurrentMousePosition(int &_x, int &_y) {
	int x = 0;
	int y = 0;
	bool available = Hook::GetCursorPosition(x, y);

	std::lock_guard<std::mutex> lock(_mutex);
	if (available) {
		_mousePosX = x;
		_mousePosY = y;
	}
	// Se a posição absoluta não estiver disponível (ex.: Wayland puro),
	// mantém a última posição conhecida (inicialmente 0,0).
	_x = _mousePosX;
	_y = _mousePosY;
}

float EventManager::GetFlickDistance() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return CalculateDistance(_startX, _startY, _lastX, _lastY);
}

float EventManager::CalculateDistance(float x1, float y1, float x2, float y2) const
{
    return sqrtf((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
}

float EventManager::CalculateMovingAmount(float v1, float v2)
{
    if ((v1 > 0.0f) != (v2 > 0.0f))
    {
        return 0.0f;
    }

    float sign = v1 > 0.0f ? 1.0f : -1.0f;
    float absoluteValue1 = fabsf(v1);
    float absoluteValue2 = fabsf(v2);
    return sign * ((absoluteValue1 < absoluteValue2) ? absoluteValue1 : absoluteValue2);
}
