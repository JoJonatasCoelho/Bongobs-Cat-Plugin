#ifdef __linux__

#include "Hook.hpp"

#include <obs-module.h>
#include <X11/Xlib.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

#include "EventManager.hpp"
#include "View.hpp"
#include "VtuberDelegate.hpp"

namespace {

struct InputDevice {
	int fd;
	bool keyboard;
	bool mouse;
	std::string path;
};

std::vector<InputDevice> g_devices;

// X11 (posição absoluta do cursor) compartilhado com a thread de render.
std::mutex g_x11Mutex;
Display *g_x11Display = nullptr;
std::once_flag g_x11InitOnce;
std::once_flag g_waylandLogOnce;

// Aviso de permissão emitido no máximo uma vez para não inundar o log.
std::once_flag g_permissionWarned;

// Aviso emitido uma vez quando /dev/input nem existe.
std::once_flag g_noInputWarned;

bool bitSet(const unsigned long *bits, int code)
{
	const size_t bitsPerWord = sizeof(unsigned long) * 8;
	return (bits[code / bitsPerWord] >> (code % bitsPerWord)) & 1UL;
}

EventManager *getEventManager()
{
	VtuberDelegate *delegate = VtuberDelegate::GetInstance();
	if (!delegate)
		return nullptr;

	View *view = delegate->GetView();
	if (!view)
		return nullptr;

	return view->GetEventManager();
}

int keyCodeToIndex(int code)
{
	// Os keycodes do Linux NÃO são contíguos (KEY_A=30, KEY_B=48,
	// KEY_F11=87, ...), então o mapeamento precisa ser explícito.
	// Os índices seguem Define::KeyDefine (a=0 ... z=25, f1=39 ... f12=50).
	switch (code) {
	case KEY_A:
		return 0;
	case KEY_B:
		return 1;
	case KEY_C:
		return 2;
	case KEY_D:
		return 3;
	case KEY_E:
		return 4;
	case KEY_F:
		return 5;
	case KEY_G:
		return 6;
	case KEY_H:
		return 7;
	case KEY_I:
		return 8;
	case KEY_J:
		return 9;
	case KEY_K:
		return 10;
	case KEY_L:
		return 11;
	case KEY_M:
		return 12;
	case KEY_N:
		return 13;
	case KEY_O:
		return 14;
	case KEY_P:
		return 15;
	case KEY_Q:
		return 16;
	case KEY_R:
		return 17;
	case KEY_S:
		return 18;
	case KEY_T:
		return 19;
	case KEY_U:
		return 20;
	case KEY_V:
		return 21;
	case KEY_W:
		return 22;
	case KEY_X:
		return 23;
	case KEY_Y:
		return 24;
	case KEY_Z:
		return 25;
	case KEY_F1:
		return 39;
	case KEY_F2:
		return 40;
	case KEY_F3:
		return 41;
	case KEY_F4:
		return 42;
	case KEY_F5:
		return 43;
	case KEY_F6:
		return 44;
	case KEY_F7:
		return 45;
	case KEY_F8:
		return 46;
	case KEY_F9:
		return 47;
	case KEY_F10:
		return 48;
	case KEY_F11:
		return 49;
	case KEY_F12:
		return 50;
	}

	switch (code) {
	case KEY_0:
		return 26;
	case KEY_1:
		return 27;
	case KEY_2:
		return 28;
	case KEY_3:
		return 29;
	case KEY_4:
		return 30;
	case KEY_5:
		return 31;
	case KEY_6:
		return 32;
	case KEY_7:
		return 33;
	case KEY_8:
		return 34;
	case KEY_9:
		return 35;
	case KEY_SPACE:
		return 36;
	case KEY_LEFTSHIFT:
		return 37;
	case KEY_LEFTCTRL:
		return 38;
	case KEY_UP:
		return 51;
	case KEY_DOWN:
		return 52;
	case KEY_RIGHT:
		return 54;
	case KEY_LEFT:
		return 53;
	case KEY_COMMA:
		return 55;
	case KEY_DOT:
		return 56;
	case KEY_LEFTBRACE:
		return 57;
	case KEY_RIGHTBRACE:
		return 58;
	case KEY_RIGHTSHIFT:
		return 59;
	case KEY_RIGHTCTRL:
		return 60;
	default:
		return -1;
	}
}

bool deviceAlreadyOpen(const std::string &path)
{
	for (const InputDevice &device : g_devices) {
		if (device.path == path)
			return true;
	}
	return false;
}

bool classifyDevice(int fd, bool &keyboard, bool &mouse)
{
	unsigned long evBits[(EV_MAX / (8 * sizeof(unsigned long))) + 1] = {0};
	unsigned long keyBits[(KEY_MAX / (8 * sizeof(unsigned long))) + 1] = {0};
	unsigned long relBits[(REL_MAX / (8 * sizeof(unsigned long))) + 1] = {0};

	if (ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits) < 0)
		return false;

	if (!bitSet(evBits, EV_KEY))
		return false;

	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0)
		return false;

	if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relBits)), relBits) < 0)
		memset(relBits, 0, sizeof(relBits));

	// Teclado: precisa ter o conjunto de letras + barra de espaço + pelo menos
	// uma tecla de digitação. Teclados compactos/sem teclas de função ainda
	// são reconhecidos.
	keyboard = bitSet(keyBits, KEY_A) && bitSet(keyBits, KEY_Z) &&
		   bitSet(keyBits, KEY_SPACE) &&
		   (bitSet(keyBits, KEY_ENTER) || bitSet(keyBits, KEY_1) ||
		    bitSet(keyBits, KEY_0));

	// Mouse: precisa de movimento relativo e do botão esquerdo (o direito é
	// opcional — alguns mouses só expõem BTN_LEFT).
	mouse = bitSet(evBits, EV_REL) && bitSet(relBits, REL_X) &&
		bitSet(relBits, REL_Y) && bitSet(keyBits, BTN_LEFT);

	return keyboard || mouse;
}

void rescanDevices()
{
	DIR *dir = opendir("/dev/input");
	if (!dir) {
		std::call_once(g_noInputWarned, [] {
			blog(LOG_WARNING,
			     "[bongobs-cat] Não foi possível abrir /dev/input: "
			     "a captura de teclado/mouse está desativada.");
		});
		return;
	}

	int added = 0;
	int keyboards = 0;
	int mice = 0;

	while (struct dirent *entry = readdir(dir)) {
		if (strncmp(entry->d_name, "event", 5) != 0)
			continue;

		std::string path = std::string("/dev/input/") + entry->d_name;
		if (deviceAlreadyOpen(path))
			continue;

		int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) {
			if (errno == EACCES || errno == EPERM) {
				std::call_once(g_permissionWarned, [path] {
					blog(LOG_WARNING,
					     "[bongobs-cat] Sem permissão para ler /dev/input (%s). Adicione o usuário ao grupo 'input' ou verifique as ACLs do logind.",
					     path.c_str());
				});
			}
			continue;
		}

		bool keyboard = false;
		bool mouse = false;
		if (!classifyDevice(fd, keyboard, mouse)) {
			close(fd);
			continue;
		}

		char name[256] = {0};
		if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0)
			name[0] = '\0';

		blog(LOG_INFO,
		     "[bongobs-cat] evdev: %s (%s) -> teclado=%s mouse=%s",
		     path.c_str(), name[0] ? name : "desconhecido",
		     keyboard ? "sim" : "nao", mouse ? "sim" : "nao");

		if (keyboard)
			keyboards++;
		if (mouse)
			mice++;
		added++;

		g_devices.push_back(InputDevice{fd, keyboard, mouse, path});
	}

	closedir(dir);

	if (added > 0) {
		blog(LOG_INFO,
		     "[bongobs-cat] evdev: %d dispositivo(s) aberto(s) - "
		     "%d teclado(s), %d mouse(s).",
		     added, keyboards, mice);
	}
}

// Lê todos os eventos disponíveis de um device. Retorna false se o device
// tiver sido removido (hot-unplug) ou apresentar erro fatal.
bool readDevice(const InputDevice &device, EventManager *eventManager)
{
	int relativeX = 0;
	int relativeY = 0;

	struct input_event event;
	for (;;) {
		ssize_t readSize = read(device.fd, &event, sizeof(event));
		if (readSize == static_cast<ssize_t>(sizeof(event))) {
			if (!eventManager)
				continue;

			if (event.type == EV_KEY) {
				if (device.keyboard) {
					int index = keyCodeToIndex(event.code);
					if (index >= 0) {
						if (event.value == 1)
							eventManager->KeyEventDown(index);
						else if (event.value == 0)
							eventManager->KeyEventUp(index);
						// event.value == 2: auto-repeat,
						// mantém pressionado sem reenviar down.
					}
				}

				if (device.mouse) {
					if (event.code == BTN_LEFT) {
						if (event.value == 1)
							eventManager->LeftButtonDown();
						else if (event.value == 0)
							eventManager->LeftButtonUp();
					} else if (event.code == BTN_RIGHT) {
						if (event.value == 1)
							eventManager->RightButtonDown();
						else if (event.value == 0)
							eventManager->RightButtonUp();
					}
				}
			} else if (event.type == EV_REL && device.mouse) {
				if (event.code == REL_X)
					relativeX += event.value;
				else if (event.code == REL_Y)
					relativeY += event.value;
			}
		} else if (readSize < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			break;
		} else if (readSize < 0 && errno == EINTR) {
			continue;
		} else {
			return false;
		}
	}

	if (eventManager && (relativeX != 0 || relativeY != 0))
		eventManager->SetRelativeMouse(relativeX, relativeY);

	return true;
}

void closeAllDevices()
{
	for (InputDevice &device : g_devices)
		close(device.fd);
	g_devices.clear();
}

void closeX11()
{
	std::lock_guard<std::mutex> lock(g_x11Mutex);
	if (g_x11Display) {
		XCloseDisplay(g_x11Display);
		g_x11Display = nullptr;
	}
}

} // namespace

Hook::Hook() : isExist(false), th(nullptr) {}

Hook::~Hook()
{
	Stop();
}

void Hook::Strat()
{
	Stop();
	isExist.store(false);
	th = new std::thread(&Hook::Run, this);
}

void Hook::Stop()
{
	isExist.store(true);

	if (th) {
		if (th->joinable())
			th->join();
		delete th;
		th = nullptr;
	}

	closeAllDevices();
	closeX11();
}

bool Hook::IsAbsoluteCursorAvailable()
{
	// A posição global do cursor só existe em sessões X11 (inclusive via
	// XWayland).  Em Wayland puro e dentro do sandbox do OBS Flatpak não há
	// DISPLAY, então o plugin usa o modo relativo.
	const char *displayEnv = getenv("DISPLAY");
	return displayEnv != nullptr && displayEnv[0] != '\0';
}

bool Hook::GetCursorPosition(int &x, int &y)
{
	const char *displayEnv = getenv("DISPLAY");
	if (!displayEnv || displayEnv[0] == '\0') {
		const char *waylandEnv = getenv("WAYLAND_DISPLAY");
		if (waylandEnv && waylandEnv[0] != '\0') {
			std::call_once(g_waylandLogOnce, [] {
				blog(LOG_INFO,
				     "[bongobs-cat] Sessão Wayland pura detectada (WAYLAND_DISPLAY definido, DISPLAY ausente): a posição absoluta global do cursor não está disponível. O plugin usará o modo relativo existente.");
			});
		}
		return false;
	}

	std::lock_guard<std::mutex> lock(g_x11Mutex);

	if (!g_x11Display) {
		std::call_once(g_x11InitOnce, [] { XInitThreads(); });
		g_x11Display = XOpenDisplay(nullptr);
		if (!g_x11Display)
			return false;
	}

	Window rootReturn = 0;
	Window childReturn = 0;
	int rootX = 0;
	int rootY = 0;
	int winX = 0;
	int winY = 0;
	unsigned int mask = 0;

	Window root = DefaultRootWindow(g_x11Display);
	if (!XQueryPointer(g_x11Display, root, &rootReturn, &childReturn,
			   &rootX, &rootY, &winX, &winY, &mask))
		return false;

	x = rootX;
	y = rootY;
	return true;
}

void Hook::Run()
{
	const std::chrono::seconds rescanInterval(2);
	auto lastScan = std::chrono::steady_clock::now() - rescanInterval;

	while (!isExist.load()) {
		auto now = std::chrono::steady_clock::now();
		if (now - lastScan >= rescanInterval) {
			rescanDevices();
			lastScan = now;
		}

		std::vector<pollfd> pollFds;
		pollFds.reserve(g_devices.size());
		for (const InputDevice &device : g_devices) {
			pollfd pfd;
			pfd.fd = device.fd;
			pfd.events = POLLIN;
			pfd.revents = 0;
			pollFds.push_back(pfd);
		}

		int ready = poll(pollFds.data(), pollFds.size(), 200);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (ready == 0)
			continue;

		EventManager *eventManager = getEventManager();

		std::vector<size_t> failed;
		for (size_t i = 0; i < pollFds.size(); ++i) {
			if (pollFds[i].revents &
			    (POLLIN | POLLERR | POLLHUP | POLLNVAL)) {
				if (!readDevice(g_devices[i], eventManager))
					failed.push_back(i);
			}
		}

		for (auto it = failed.rbegin(); it != failed.rend(); ++it) {
			size_t index = *it;
			close(g_devices[index].fd);
			g_devices.erase(g_devices.begin() + index);
		}
	}

	closeAllDevices();
}

#endif // __linux__
