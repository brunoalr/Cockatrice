#include "flush_player_data.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

void flushPlayerData()
{
#ifdef __EMSCRIPTEN__
    MAIN_THREAD_ASYNC_EM_ASM(globalThis.cockatricePersist?.flush().catch(() => {}));
#endif
}
