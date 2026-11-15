#pragma once

#include <atomic>
#include <thread>

class Hook {
public:
	Hook();

	~Hook();

	void Strat();

	void Stop();

	/**
	 * Obtém a posição absoluta do cursor (coordenadas globais de tela).
	 *
	 * @return true se a posição absoluta estiver disponível.
	 */
	static bool GetCursorPosition(int &x, int &y);

	/**
	 * Informa se a posição absoluta do cursor pode ser obtida nesta
	 * plataforma/sessão.  No Linux, sessões Wayland (sem XWayland) e o
	 * sandbox do OBS Flatpak não expõem a posição global do cursor.
	 *
	 * @return true se houver uma posição absoluta disponível.
	 */
	static bool IsAbsoluteCursorAvailable();

private:
	void Run();

	std::atomic<bool> isExist;

	std::thread *th;
};
