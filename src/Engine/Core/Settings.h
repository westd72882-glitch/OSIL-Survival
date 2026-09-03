#pragma once
// ==================== НАСТРОЙКИ КЛИЕНТА ====================
// Хранятся в писабельной директории приложения (см. Paths.h) и переживают переустановку
// не переживают, но переживают перезапуск — этого достаточно.
//
// Структура унаследована от движка A.N.O.D.E, но урезана под OSIL: там были координаты
// кнопок КПК и оружия из «Сталкера», здесь — свой набор (действие, атака, инвентарь,
// крафт). Общий принцип оставлен прежним и он важен: у КАЖДОЙ кнопки свой признак
// «задана ли позиция» (*NormY == -1 — не задана), поэтому перенос одной кнопки в
// редакторе управления не сбрасывает остальные на чужие координаты.

// TURBO — режим ради кадров и только ради них: разрешение рендера режется вчетверо по
// площади, текстуры ужимаются при загрузке, дальность прорисовки урезается. Картинка
// заметно грубее — осознанная плата за плавность на слабом телефоне.
enum class Quality { TURBO = 0, LOW = 1, MEDIUM = 2, HIGH = 3 };

struct Settings {
    bool musicOn = true;
    bool sfxOn = true;
    // Явный потолок кадров: 30 стабильных кадров экономят батарею и греют корпус вдвое
    // меньше, чем 60 рваных. 0 — без ограничения.
    int  fpsLimit = 60;
    Quality quality = Quality::HIGH;
    // Дальность прорисовки в метрах (она же в блоках). Отдельно от качества: на одном
    // телефоне хочется видеть далеко и терпеть 30 кадров, на другом наоборот.
    int viewDistance = 112;
    bool showDebugInfo = false;
    float lookSensitivity = 1.0f;

    // Раскладка сенсорного управления: доли экрана (0..1). -1 по Y — «не задано,
    // использовать стандартную позицию».
    int   layoutVersion = 0;
    // Раскладка по умолчанию снята с телефона игрока и округлена до ровных долей:
    // правый столбик выстроен по одной вертикали 0.930 с шагом 0.180 по высоте, окна
    // вверху на одной строке 0.088.
    float stickNormX = 0.15f,  stickNormY = -1.0f;   // джойстик движения (появляется под пальцем)
    float actionNormX= 0.930f, actionNormY= 0.340f;  // действие (E): подобрать, открыть
    float attackNormX= 0.930f, attackNormY= 0.520f;  // удар / добыча
    float jumpNormX  = 0.930f, jumpNormY  = 0.700f;  // прыжок
    float crouchNormX= 0.930f, crouchNormY= 0.880f;  // присед
    float sprintNormX= 0.853f, sprintNormY= 0.783f;  // бег
    float placeNormX = 0.62f,  placeNormY = -1.0f;   // поставить блок (кнопки на экране нет)
    float invNormX   = 0.886f, invNormY   = 0.088f;  // инвентарь
    float craftNormX = 0.806f, craftNormY = 0.088f;  // крафт
    float mapNormX   = 0.78f,  mapNormY   = -1.0f;   // карта (кнопки на экране нет, только пауза)
    float optionsNormX = 0.70f, optionsNormY = -1.0f; // настройки
};
extern Settings settings;

// Увеличивать при КАЖДОМ изменении стандартной раскладки управления: иначе сохранённые
// с прошлой версии координаты перекроют новую раскладку, и она включится только после
// захода в редактор управления.
const int CONTROL_LAYOUT_VERSION = 1;

float qualityRenderScale();     // множитель разрешения offscreen-буфера сцены
int   qualityTextureLimit();    // предел стороны текстуры при загрузке (0 — как есть)
float qualityViewDistanceScale();
const char* qualityLabel();

const int VIEW_DISTANCE_OPTIONS[] = { 48, 80, 112, 160, 208 };
const int VIEW_DISTANCE_OPTION_COUNT = (int)(sizeof(VIEW_DISTANCE_OPTIONS)/sizeof(VIEW_DISTANCE_OPTIONS[0]));

const int FPS_LIMIT_OPTIONS[] = { 30, 60, 120, 0 };
const int FPS_LIMIT_OPTION_COUNT = (int)(sizeof(FPS_LIMIT_OPTIONS)/sizeof(FPS_LIMIT_OPTIONS[0]));

void loadSettings();
void saveSettings();
