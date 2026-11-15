#pragma once

#if defined(__linux__)

namespace EglContext {
    bool Create(int width, int height);  // cria display/contexto/pbuffer e torna current
    void Destroy();                       // libera tudo
    int Width();
    int Height();
}

#endif
