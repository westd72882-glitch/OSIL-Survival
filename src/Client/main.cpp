// ==================== ТОЧКА ВХОДА КЛИЕНТА OSIL SURVIVAL ====================
// На Android SDL сама подменяет main() своим SDL_main и запускает его из Java-активити
// (см. android/app/src/main/java/.../MainActivity.java). На настольной машине это обычная
// точка входа — тот же код собирается и запускается для отладки без телефона.
#include "GameClient.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>

int main(int argc, char* argv[]){
    GameClient client;
    return client.run(argc, argv);
}
