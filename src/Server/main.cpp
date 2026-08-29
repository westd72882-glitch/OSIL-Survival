// ==================== ТОЧКА ВХОДА ВЫДЕЛЕННОГО СЕРВЕРА ====================
// Запуск:  ./osil_server --config config/server.cfg +world.seed 12345
// Всё остальное — в ServerApp.
#include "ServerApp.h"

int main(int argc, char** argv){
    ServerApp app;
    return app.run(argc, argv);
}
