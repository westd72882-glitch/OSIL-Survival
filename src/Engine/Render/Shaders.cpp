#include "Shaders.h"
#include <SDL2/SDL.h>

// ==================== ШЕЙДЕРЫ ====================
GLuint compileShader(GLenum type, const char* src){
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok=0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if(!ok){
        char log[1024]; glGetShaderInfoLog(sh, 1024, nullptr, log);
        SDL_Log("Shader compile error: %s", log);
    }
    return sh;
}
GLuint linkProgram(const char* vsSrc, const char* fsSrc){
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok=0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if(!ok){
        char log[1024]; glGetProgramInfoLog(prog, 1024, nullptr, log);
        SDL_Log("Program link error: %s", log);
    }
    glDeleteShader(vs); glDeleteShader(fs);
    return prog;
}

// Основной 3D шейдер: позиция, UV, нормаль, direction light + текстура + экспоненциальный туман
const char* mainVS = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;
out vec2 vUV;
out vec3 vNormal;
out vec3 vWorldPos;
out float vFogDist;
void main(){
    vUV = aUV;
    vNormal = uNormalMat * aNormal;
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;   // нужен для бликов и подсветки контуров (см. mainFS)
    vec4 viewPos = uView * worldPos;
    vFogDist = length(viewPos.xyz);
    gl_Position = uProj * viewPos;
}
)";
// Освещение уровня шутеров середины 2000-х: полусферический ambient (небо сверху /
// отражённый от земли свет снизу) вместо одной плоской константы, направленный свет,
// зеркальный блик по Блинну-Фонгу и подсветка контуров. Раньше здесь был плоский
// "ambient + Ламберт", из-за которого любая геометрия выглядела картонной независимо
// от того, насколько детально она смоделирована.
const char* mainFS = R"(#version 300 es
precision mediump float;
in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;
in float vFogDist;
uniform sampler2D uTex;
uniform vec3 uLightDir;
uniform vec3 uTintColor;
uniform bool uUseTexture;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform vec3 uCamPos;
// Прозрачность и «самосвечение». Нужны там, где поверхность не освещается, а СВЕТИТСЯ
// сама и просвечивает насквозь: аномалия, её разряды, искры. Обычный путь освещения для
// такого не годится - свечение в тени погасло бы, а туман съел бы его на дистанции.
// Значения по умолчанию (1 и 0) выставляются один раз при линковке программы: в GL
// uniform'ы хранятся при самой программе и между кадрами не сбрасываются.
uniform float uOpacity;
uniform float uUnlit;
// Сила освещения: 1.0 — полдень, 0.1 — глухая ночь. Раньше свет в шейдере был
// константой «вечно пасмурной Зоны», и суточный цикл выживания просто не читался:
// ночь отличалась от дня только цветом тумана. Теперь клиент передаёт сюда
// освещённость из Environment, и полдень действительно ярче ночи.
uniform float uLightAmount;
out vec4 FragColor;
void main(){
    vec3 n = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    vec3 V = normalize(uCamPos - vWorldPos);

    vec4 base = uUseTexture ? texture(uTex, vUV) : vec4(uTintColor, 1.0);

    if(uUnlit > 0.5){
        // Свечение «поворачивается» к зрителю: грани, глядящие в камеру, прозрачнее, а
        // кромки оболочки ярче. Без этого полупрозрачный купол выглядит ровной плёнкой,
        // а не объёмом воздуха, набитым электричеством.
        float facing = abs(dot(n, V));
        float edge = pow(1.0 - facing, 2.0);
        FragColor = vec4(base.rgb * (0.70 + 0.80 * edge), uOpacity * (0.40 + 0.80 * edge));
        return;
    }

    // --- Полусферический ambient: сверху холодный свет затянутого неба, снизу тёплый
    // отражённый от земли. Даёт объём даже там, куда не попадает направленный свет —
    // именно этого не хватало плоской константе.
    float hemi = n.y * 0.5 + 0.5;
    // Свет затянутого неба — тусклый и холодный; отсвет от земли — совсем слабый.
    // Ночью остаётся холодный лунный подсвет, днём — яркое небо.
    vec3 skyAmbient = vec3(0.225, 0.245, 0.260) * (0.22 + 1.25 * uLightAmount);
    vec3 groundAmbient = vec3(0.105, 0.095, 0.075) * (0.20 + 1.20 * uLightAmount);
    vec3 ambient = mix(groundAmbient, skyAmbient, hemi);

    // --- Направленный свет с мягким переходом на теневой стороне (wrap lighting):
    // резкий терминатор выдаёт низкополигональность, размытый — маскирует её.
    float ndl = dot(n, L);
    float diff = max((ndl + 0.25) / 1.25, 0.0);
    // Солнце пробивается сквозь плотную облачность: приглушённое и обесцвеченное.
    // Раньше здесь была единица по всем каналам — отсюда и ощущение ясного дня.
    // Солнце теплеет к закату и гаснет ночью.
    vec3 sunColor = mix(vec3(0.35, 0.30, 0.34), vec3(1.05, 0.98, 0.86), clamp(uLightAmount, 0.0, 1.0));

    // --- Зеркальный блик (Блинн-Фонг). Сила блика падает на тёмных поверхностях,
    // чтобы ткань и резина не блестели как металл.
    vec3 H = normalize(L + V);
    float specPower = 24.0;
    float lum = dot(base.rgb, vec3(0.299, 0.587, 0.114));
    float specStrength = 0.10 + 0.30 * lum;
    float spec = pow(max(dot(n, H), 0.0), specPower) * specStrength * max(ndl, 0.0);

    // --- Подсветка контуров: обводит силуэт светом неба, отделяя объект от тумана.
    float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0) * 0.22;

    vec3 lit = base.rgb * (ambient * 0.95 + sunColor * diff * 0.62)
             + sunColor * spec * 0.75
             + skyAmbient * rim;

    // Лёгкое затемнение к чёрному вместо линейного спада — мягко "проваливает" тени,
    // не убивая читаемость силуэтов (в отличие от простого умножения на константу).
    lit = lit / (lit + vec3(0.85)) * 1.55;

    float fogFactor = 1.0 - exp(-uFogDensity * vFogDist * uFogDensity * vFogDist);
    fogFactor = clamp(fogFactor, 0.0, 1.0);
    vec3 finalColor = mix(lit, uFogColor, fogFactor);
    FragColor = vec4(finalColor, base.a);
}
)";

// Скайбокс: ПОЛНОСТЬЮ ПРОЦЕДУРНОЕ небо (не cubemap-текстура — 6 файлов
// up/down/left/right/front/back.png в проекте никогда не было, loadCubemap() тихо
// возвращал пустую текстуру и небо было пустым/чёрным). Цвет неба — функция от
// нормализованного направления взгляда vDir, поэтому бесшовна по построению (нет швов
// куба, нет проблем с фильтрацией на гранях). Палитра — мрачная, "зона": тёмно-серо-
// зелёный зенит, грязная жёлто-серая дымка у горизонта, тусклая процедурная луна,
// hash-звёзды, медленно плывущие fbm-облака (анимация по uTime).
// ==================== НЕБО ====================
// Небо рисуется ПОЛНОЭКРАННЫМ треугольником, а не кубом вокруг камеры. Куб давал
// видимый шов: на его рёбрах ломалась интерполяция направления, и по небу шла заметная
// линия. Здесь направление луча считается прямо из положения пикселя на экране и трёх
// векторов камеры, поэтому поле направлений непрерывно по построению — швов нет и быть
// не может, а геометрии не нужно вовсе (три вершины без единого атрибута).
const char* skyVS = R"(#version 300 es
out vec2 vNdc;
void main(){
    // Треугольник, накрывающий весь экран: вершины (-1,-1), (3,-1), (-1,3).
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    vNdc = p;
    // z = w: глубина ровно 1.0, небо всегда позади всей сцены.
    gl_Position = vec4(p, 1.0, 1.0);
}
)";

const char* skyFS = R"(#version 300 es
// highp обязателен: шум ниже перемножает координаты на большие константы, а mediump на
// мобильных GPU 16-битный — там шум вырождается в полосы, а при переполнении даёт NaN.
precision highp float;
in vec2 vNdc;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamForward;
uniform float uTanHalfFov;
uniform float uAspect;
uniform float uTime;
uniform vec3  uSunDir;       // направление НА солнце
uniform float uLightAmount;  // 0 — ночь, 1 — полдень
uniform vec3  uFogColor;     // дымка у горизонта — та же, что у мира
out vec4 FragColor;

float hash13(vec3 p){
    p = fract(p * 0.3183099 + vec3(0.71, 0.113, 0.419));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

float noise3(vec3 p){
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i), n100 = hash13(i + vec3(1,0,0));
    float n010 = hash13(i + vec3(0,1,0)), n110 = hash13(i + vec3(1,1,0));
    float n001 = hash13(i + vec3(0,0,1)), n101 = hash13(i + vec3(1,0,1));
    float n011 = hash13(i + vec3(0,1,1)), n111 = hash13(i + vec3(1,1,1));
    return mix(mix(mix(n000,n100,f.x), mix(n010,n110,f.x), f.y),
               mix(mix(n001,n101,f.x), mix(n011,n111,f.x), f.y), f.z);
}

float fbm3(vec3 p){
    float v = 0.0, a = 0.5;
    for(int i = 0; i < 5; i++){ v += a * noise3(p); p *= 2.02; a *= 0.5; }
    return v;
}

void main(){
    // Луч через пиксель: непрерывен по всему экрану, поэтому небо бесшовно.
    vec3 dir = normalize(uCamForward
                       + uCamRight * (vNdc.x * uTanHalfFov * uAspect)
                       + uCamUp    * (vNdc.y * uTanHalfFov));

    float up = clamp(dir.y, -1.0, 1.0);
    float day = clamp(uLightAmount, 0.0, 1.0);
    vec3 sun = normalize(uSunDir);
    float sunDot = clamp(dot(dir, sun), 0.0, 1.0);

    // Палитра. Зенит и горизонт разведены по высоте плавной степенной кривой — резкой
    // границы между «верхом» и «низом» неба не остаётся.
    vec3 dayZenith   = vec3(0.16, 0.38, 0.78);
    vec3 dayHorizon  = vec3(0.68, 0.80, 0.92);
    vec3 nightZenith = vec3(0.015, 0.025, 0.06);
    vec3 nightHorizon= vec3(0.05, 0.07, 0.13);

    vec3 zenith  = mix(nightZenith,  dayZenith,  day);
    vec3 horizon = mix(nightHorizon, dayHorizon, day);

    // Заря: чем ниже солнце, тем шире тёплая полоса вокруг него у горизонта.
    float dusk = (1.0 - abs(sun.y)) * day;
    vec3 duskTint = vec3(0.98, 0.52, 0.24);
    horizon = mix(horizon, duskTint, dusk * 0.65 * pow(sunDot, 1.2));

    float t = pow(clamp(up * 0.5 + 0.5, 0.0, 1.0), 0.65);
    vec3 col = mix(horizon, zenith, t);

    // Дымка у самого горизонта — той же краской, что туман мира: небо и земля сходятся
    // без видимой линии стыка.
    col = mix(col, uFogColor, smoothstep(0.10, -0.06, up));

    // Солнце: диск с мягким краем и два гало разной ширины.
    float disc = smoothstep(0.9992, 0.9997, sunDot);
    float glow = pow(sunDot, 300.0) * 0.55 + pow(sunDot, 14.0) * 0.14;
    vec3 sunColor = mix(vec3(0.95, 0.55, 0.30), vec3(1.0, 0.97, 0.88), clamp(sun.y * 2.0, 0.0, 1.0));
    col += (disc * 2.4 + glow) * sunColor * day;

    // Луна с обратной стороны от солнца — ночью небо не остаётся пустым.
    vec3 moon = -sun;
    float moonDot = clamp(dot(dir, moon), 0.0, 1.0);
    float moonDisc = smoothstep(0.9993, 0.99975, moonDot);
    col += (moonDisc * 1.6 + pow(moonDot, 400.0) * 0.4) * vec3(0.85, 0.88, 0.95) * (1.0 - day);

    // Звёзды: редкие яркие точки, привязанные к направлению, поэтому не «плывут» при
    // повороте камеры и не собираются в сетку. Степень высокая намеренно — при мягком
    // пороге небо превращалось в сплошную крупу, что выглядит не ночью, а помехами.
    float starHash = hash13(floor(dir * 220.0));
    float stars = pow(max(starHash - 0.35, 0.0) / 0.65, 130.0) * 1.6;
    col += vec3(stars) * (1.0 - day) * smoothstep(0.02, 0.30, up);

    // Облака: слой, спроецированный на купол. Знаменатель со сдвигом (dir.y + 0.28)
    // убирает разрыв у горизонта, который давало деление на dir.y.
    vec2 cp = dir.xz / (dir.y + 0.28);
    float clouds = fbm3(vec3(cp * 0.55 + vec2(uTime * 0.004, uTime * 0.0025), uTime * 0.01));
    clouds = smoothstep(0.48, 0.86, clouds) * smoothstep(-0.02, 0.28, up);
    vec3 cloudLit = mix(vec3(0.12, 0.13, 0.16), vec3(0.98, 0.97, 0.95), day);
    // Облака подсвечиваются со стороны солнца — плоско-белые выглядят наклейкой.
    cloudLit = mix(cloudLit, sunColor, pow(sunDot, 6.0) * 0.5 * day);
    col = mix(col, cloudLit, clouds * 0.8);

    FragColor = vec4(col, 1.0);
}
)";

const char* uiVS = R"(#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uProj;
out vec2 vUV;
void main(){
    vUV = aUV;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)";
const char* uiFS = R"(#version 300 es
precision mediump float;
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uColor;
uniform bool uUseTexture;
out vec4 FragColor;
void main(){
    if(uUseTexture) {
        FragColor = texture(uTex, vUV) * uColor;
    } else {
        FragColor = uColor;
    }
}
)";

// Пост-обработка: сэмплирует уже отрисованную сцену (offscreen sceneColorTex, см.
// Render.h/ensureSceneFBO) один раз при переносе на экран и красит её в мрачную
// сталкерскую палитру — десатурация + холодный зелёный tint + виньетка + лёгкое
// анимированное зерно. Геометрия — тот же fullscreen-quad/VAO, что и у uiProg
// (одинаковый layout аттрибутов location=0/1), см. main.cpp — новый шейдер, старый VAO.
const char* postVS = R"(#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uProj;
out vec2 vUV;
void main(){
    vUV = aUV;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)";
// ВАЖНО (mediump и чёрный экран): здесь highp, а НЕ mediump. На реальных мобильных GPU
// mediump — это честный 16-битный float с максимумом ~65504, тогда как на десктопе его
// обычно трактуют как highp, поэтому проблема проявляется только на телефоне. Прошлая
// версия этого шейдера считала зерно как
//   fract(sin(dot(vUV * uResolution + uTime * 60.0, vec2(12.9898, 78.233))) * 43758.5453)
// где vUV * uResolution при 1920x1080 даёт ~(1920,1080), dot(...) с (12.9898, 78.233)
// вылетает за 100000 -> +inf в mediump -> sin(inf) = NaN -> NaN расползается на весь цвет
// -> ВЕСЬ ЭКРАН ЧЁРНЫЙ (HUD при этом рисуется отдельной программой uiProg и остаётся
// виден — ровно та картина, что была на устройстве). Ниже: highp + зерно считается на
// маленьких числах (vUV в 0..1, время обёрнуто через fract), так что переполнения нет
// даже если запустить игру на многочасовую сессию.
const char* postFS = R"(#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uTex;
uniform float uTime;
uniform vec2 uResolution;
out vec4 FragColor;
void main(){
    vec3 c = texture(uTex, vUV).rgb;

    // Мягкая цветокоррекция: лёгкая десатурация + чуть холодный оттенок.
    float gray = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(c, vec3(gray), 0.15);
    c *= vec3(0.97, 1.0, 0.96);

    vec2 uv = vUV - 0.5;
    float vig = 1.0 - dot(uv, uv) * 0.35;
    c *= clamp(vig, 0.65, 1.0);

    // ==================== ПЛЁНОЧНОЕ ЗЕРНО ====================
    // Зерно накладывается ЗДЕСЬ, в пост-обработке, то есть на всю трёхмерную сцену
    // разом и ни в коем случае не на интерфейс: HUD рисуется уже после этого прохода,
    // поверх готовой картинки, и остаётся чистым.
    //
    // ТРИ ПРАВКИ ПРОТИВ МЕРЦАНИЯ В ТЕНЯХ (прошлый заход был перебором):
    //
    // 1. Сила зерна в ТЁМНОМ теперь МИНИМАЛЬНА, а не максимальна. Раньше было наоборот
    //    (0.085 в тенях против 0.030 в светах) — а глаз именно в тенях и различает шум
    //    лучше всего, потому что там он сравним с самим сигналом. Настоящая плёнка тоже
    //    зернит сильнее всего в СРЕДНИХ тонах, а в глубокой тени и в пересвете почти
    //    гладкая. Кривая ниже это и повторяет.
    // 2. Общая амплитуда снижена вчетверо: 0.022 в пике вместо 0.085.
    // 3. Рисунок зерна обновляется не каждый кадр, а ступенями по времени. Кадр за
    //    кадром меняющийся шум читается как ДРОЖАНИЕ картинки; при обновлении ступенями
    //    он воспринимается как фактура плёнки. Это же убирает зависимость вида шума от
    //    частоты кадров: на 120 герцах и на 60 он выглядит одинаково.
    float step = floor(uTime * 14.0);          // ступень времени
    float t = fract(step * 0.113);             // и её остаток — аргумент шума
    vec2 px = vUV * uResolution;
    float g1 = fract(sin(dot(px + t * 137.0, vec2(12.9898, 78.233))) * 43758.5453);
    float g2 = fract(sin(dot(px.yx - t * 91.0, vec2(39.3467, 11.1357))) * 24634.6345);
    float grain = (g1 + g2) * 0.5 - 0.5;
    float lum = clamp(dot(c, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);
    // Колокол по яркости: 0 у чёрного, максимум в полутонах, снова 0 у белого.
    float bell = 4.0 * lum * (1.0 - lum);
    c += grain * 0.022 * bell;
    FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

// Процедурная инстансированная трава (см. Grass.h/Grass.cpp): базовый "крест" из двух
// перекрещенных quad'ов на инстанс (aPos/aUV, обычный VBO) + per-instance трансформ
// (iPos/iParams, отдельный VBO с glVertexAttribDivisor=1). Форма листа и цвет считаются
// в шейдере — без текстур. Ветер — синус по времени с индивидуальной фазой на инстанс.
// ==================== ДЕРЕВЬЯ (инстансинг) ====================
// Отдельная программа, а не основная: положение/поворот/масштаб дерева приходят
// per-instance, поэтому одним вызовом отрисовки рисуется весь ближний лес. У основной
// программы модельная матрица — обычный uniform, и там пришлось бы делать вызов на
// каждое дерево.
//
// aMat — номер материала вершины: 0 = кора, 1 = хвоя/листва. Раскраска идёт в шейдере,
// текстур у деревьев нет вовсе.
// Ветер качает только крону (aMat=1) и тем сильнее, чем выше вершина: ствол при этом
// остаётся неподвижным, иначе дерево «плавает» целиком, как водоросль.
const char* treeVS = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in float aMat;
layout(location=3) in vec3 iPos;
layout(location=4) in vec4 iParams; // x=rotY, y=scale, z=tint, w=phase
uniform mat4 uView;
uniform mat4 uProj;
uniform float uTime;
out vec3 vNormal;
out float vMat;
out float vTint;
out float vHeight;
out float vFogDist;
void main(){
    float c = cos(iParams.x), s = sin(iParams.x);
    mat2 rot = mat2(c, -s, s, c);
    vec3 p = aPos;
    p.xz = rot * p.xz;
    p *= iParams.y;

    vec3 n = aNormal;
    n.xz = rot * n.xz;

    float sway = sin(uTime * 0.9 + iParams.w) * 0.055 + sin(uTime * 1.7 + iParams.w * 1.7) * 0.025;
    float amount = aMat * max(p.y - 0.6, 0.0);
    p.x += sway * amount;
    p.z += sway * 0.6 * amount;

    vec3 worldPos = p + iPos;
    vec4 viewPos = uView * vec4(worldPos, 1.0);
    vFogDist = length(viewPos.xyz);
    vNormal = n;
    vMat = aMat;
    vTint = iParams.z;
    vHeight = aPos.y;
    gl_Position = uProj * viewPos;
}
)";
const char* treeFS = R"(#version 300 es
precision mediump float;
in vec3 vNormal;
in float vMat;
in float vTint;
in float vHeight;
in float vFogDist;
uniform vec3 uLightDir;
uniform vec3 uFogColor;
uniform float uFogDensity;
out vec4 FragColor;
void main(){
    vec3 n = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    // Полусферический ambient и мягкий wrap-свет — те же приёмы, что в основном шейдере,
    // иначе лес выпадет из общей картины освещения.
    float diff = max(dot(n, L) * 0.5 + 0.5, 0.0);
    float sky = n.y * 0.5 + 0.5;

    vec3 barkLow  = vec3(0.075, 0.062, 0.048);
    vec3 barkHigh = vec3(0.155, 0.128, 0.098);
    vec3 bark = mix(barkLow, barkHigh, clamp(vHeight * 0.14, 0.0, 1.0));

    // Листва: от тёмно-хвойной к бурой — разброс задаётся оттенком экземпляра, поэтому
    // соседние деревья не выглядят клонами.
    vec3 leafGreen = vec3(0.085, 0.135, 0.070);
    vec3 leafDry   = vec3(0.150, 0.125, 0.062);
    vec3 leaf = mix(leafGreen, leafDry, clamp(vTint, 0.0, 1.0));
    // Низ кроны темнее верха — объём читается даже без теней
    leaf *= 0.62 + 0.38 * clamp((vHeight - 1.2) * 0.16, 0.0, 1.0);

    vec3 base = mix(bark, leaf, step(0.5, vMat));
    vec3 ambient = mix(vec3(0.055, 0.058, 0.052), vec3(0.105, 0.112, 0.118), sky);
    vec3 lit = base * (ambient + vec3(0.42, 0.41, 0.36) * diff);
    lit = lit / (lit + vec3(0.85)) * 1.55;

    float f = vFogDist * uFogDensity;
    float fog = 1.0 - exp(-f * f);
    FragColor = vec4(mix(lit, uFogColor, clamp(fog, 0.0, 1.0)), 1.0);
}
)";

const char* grassVS = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec3 iPos;
layout(location=3) in vec4 iParams; // x=rotY, y=scale, z=hue, w=phase
uniform mat4 uView;
uniform mat4 uProj;
uniform float uTime;
uniform vec3 uGrassCentre;   // центр кольца травы
uniform float uGrassRadius;  // его радиус
out vec2 vUV;
out float vHue;
out float vFogDist;
void main(){
    float c = cos(iParams.x), s = sin(iParams.x);
    vec3 p = aPos;
    p.xz = mat2(c, -s, s, c) * p.xz;
    // Травинка УХОДИТ В ЗЕМЛЮ у края кольца. Без этого граница пересева видна как
    // ровная кромка, за которой трава обрывается, а при пересеве новая полоса
    // выскакивает разом. С затуханием края нет вовсе: травинки плавно вырастают и
    // так же плавно оседают, и момент пересева не поймать глазом.
    float edge = distance(iPos.xz, uGrassCentre.xz) / max(uGrassRadius, 0.001);
    float grow = clamp((1.0 - edge) / 0.22, 0.0, 1.0);
    p *= iParams.y * grow;

    float sway = sin(uTime * 1.6 + iParams.w) * 0.12 * aUV.y;
    p.x += sway;

    vec3 worldPos = p + iPos;
    vec4 viewPos = uView * vec4(worldPos, 1.0);
    vFogDist = length(viewPos.xyz);
    vUV = aUV;
    vHue = iParams.z;
    gl_Position = uProj * viewPos;
}
)";
const char* grassFS = R"(#version 300 es
precision mediump float;
in vec2 vUV;
in float vHue;
in float vFogDist;
uniform vec3 uLightDir;
uniform vec3 uFogColor;
uniform float uFogDensity;
out vec4 FragColor;
void main(){
    // Форму травинки теперь задаёт САМА ГЕОМЕТРИЯ (сужающаяся кверху трапеция, см.
    // buildGrassBladeMesh в Grass.cpp), поэтому discard больше не нужен — уходит и рваный
    // край, и лишний overdraw, который discard провоцировал на мобильном GPU.
    // Здесь остаётся только цвет: сухой бурый у земли -> живой зеленоватый к верхушке.
    vec3 dryLow   = vec3(0.085, 0.078, 0.042);
    vec3 dryHigh  = vec3(0.30,  0.27,  0.13);
    vec3 grnLow   = vec3(0.062, 0.088, 0.040);
    vec3 grnHigh  = vec3(0.24,  0.33,  0.14);
    vec3 low  = mix(grnLow,  dryLow,  vHue);
    vec3 high = mix(grnHigh, dryHigh, vHue);
    // Нелинейный градиент: низ куста дольше остаётся тёмным, как в реальной траве, где
    // свет до основания почти не доходит.
    float t = vUV.y * vUV.y * (3.0 - 2.0 * vUV.y);
    vec3 col = mix(low, high, t);

    // Подсветка кромок листа — дешёвая имитация просвечивания на солнце
    float edge = abs(vUV.x - 0.5) * 2.0;
    col += vec3(0.05, 0.06, 0.02) * edge * t;

    float light = 0.62 + 0.38 * max(dot(vec3(0.0,1.0,0.0), normalize(-uLightDir)), 0.0);
    col *= light;

    float fogFactor = 1.0 - exp(-uFogDensity * vFogDist * uFogDensity * vFogDist);
    fogFactor = clamp(fogFactor, 0.0, 1.0);
    col = mix(col, uFogColor, fogFactor);

    FragColor = vec4(col, 1.0);
}
)";

GLuint mainProg = 0, uiProg = 0, skyProg = 0, postProg = 0, grassProg = 0;
GLint uModelLoc, uViewLoc, uProjLoc, uNormalMatLoc, uTexLoc, uLightDirLoc, uTintColorLoc, uUseTextureLoc;
GLint uFogColorLoc, uFogDensityLoc, uCamPosLoc, uOpacityLoc, uUnlitLoc;
GLint uLightAmountLoc = -1;
GLint uiProjLoc, uiTexLoc, uiColorLoc, uiUseTextureLoc;
GLint skyViewLoc, skyProjLoc, skyTimeLoc;
GLint skySunDirLoc = -1, skyLightAmountLoc = -1;
GLint skyCamRightLoc = -1, skyCamUpLoc = -1, skyCamForwardLoc = -1;
GLint skyTanHalfFovLoc = -1, skyAspectLoc = -1, skyFogColorLoc = -1;
GLuint voxelProg = 0;
GLint voxelViewLoc = -1, voxelProjLoc = -1, voxelLightDirLoc = -1, voxelLightAmountLoc = -1;
GLint voxelFogColorLoc = -1, voxelFogDensityLoc = -1, voxelCamPosLoc = -1, voxelAlphaLoc = -1;
GLint voxelBlocksLoc = -1, voxelTexturedLoc = -1;
GLint postProjLoc, postTexLoc, postTimeLoc, postResLoc;
GLint grassViewLoc, grassProjLoc, grassTimeLoc, grassLightDirLoc, grassFogColorLoc, grassFogDensityLoc;
GLint grassCentreLoc, grassRadiusLoc;


// ==================== СКИННИНГ ====================
// Позиция и нормаль вершины пересчитываются как взвешенная сумма по четырём костям.
// Матрицы костей лежат в UNIFORM-БУФЕРЕ: в GLES 3.0 обычных вершинных uniform'ов
// гарантированно всего 256 векторов, и уже 64 матрицы (по 4 вектора каждая) съели бы их
// целиком, не оставив места матрицам вида и проекции. UBO даёт минимум 16 КБ, поэтому
// сюда влезают все 96 костей (см. SKINNED_MAX_BONES — размеры обязаны совпадать).
// uSkinned=false превращает шейдер в обычный статический — так одна программа
// обслуживает и персонажей, и неподвижные модели локаций.
const char* skinVS = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec3 aNormal;
layout(location=3) in vec4 aJoints;
layout(location=4) in vec4 aWeights;

layout(std140) uniform BoneBlock {
    mat4 uBones[96];
};

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform bool uSkinned;

out vec2 vUV;
out vec3 vNormal;
out vec3 vWorldPos;
out float vFogDist;

void main(){
    vec4 localPos = vec4(aPos, 1.0);
    vec3 localNrm = aNormal;

    if(uSkinned){
        mat4 skin =
            uBones[int(aJoints.x)] * aWeights.x +
            uBones[int(aJoints.y)] * aWeights.y +
            uBones[int(aJoints.z)] * aWeights.z +
            uBones[int(aJoints.w)] * aWeights.w;
        localPos = skin * localPos;
        localNrm = mat3(skin) * localNrm;
    }

    vec4 worldPos = uModel * localPos;
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uModel) * localNrm;
    vUV = aUV;
    vec4 viewPos = uView * worldPos;
    vFogDist = length(viewPos.xyz);
    gl_Position = uProj * viewPos;
}
)";

// Освещение повторяет mainFS один в один, чтобы модели из файлов не выбивались из
// картины по свету рядом с процедурной геометрией.
const char* skinFS = R"(#version 300 es
precision mediump float;
in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;
in float vFogDist;
uniform sampler2D uTex;
uniform vec3 uLightDir;
uniform vec3 uTintColor;
uniform bool uUseTexture;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform vec3 uCamPos;
out vec4 FragColor;
void main(){
    vec3 n = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    vec3 V = normalize(uCamPos - vWorldPos);

    vec4 base = uUseTexture ? texture(uTex, vUV) : vec4(uTintColor, 1.0);
    // Прозрачные места текстуры (листва, ремни, сетка) отбрасываем, иначе они
    // рисуются чёрными прямоугольниками поверх сцены.
    if(base.a < 0.35) discard;
    if(uUseTexture) base.rgb *= uTintColor;

    float hemi = n.y * 0.5 + 0.5;
    // Ночью остаётся холодный лунный подсвет, днём — яркое небо.
    vec3 skyAmbient = vec3(0.225, 0.245, 0.260) * (0.22 + 1.25 * uLightAmount);
    vec3 groundAmbient = vec3(0.105, 0.095, 0.075) * (0.20 + 1.20 * uLightAmount);
    vec3 ambient = mix(groundAmbient, skyAmbient, hemi);

    float ndl = dot(n, L);
    float diff = max((ndl + 0.25) / 1.25, 0.0);
    // Солнце теплеет к закату и гаснет ночью.
    vec3 sunColor = mix(vec3(0.35, 0.30, 0.34), vec3(1.05, 0.98, 0.86), clamp(uLightAmount, 0.0, 1.0));

    vec3 H = normalize(L + V);
    float lum = dot(base.rgb, vec3(0.299, 0.587, 0.114));
    float specStrength = 0.10 + 0.30 * lum;
    float spec = pow(max(dot(n, H), 0.0), 24.0) * specStrength * max(ndl, 0.0);

    float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0) * 0.22;

    vec3 lit = base.rgb * (ambient * 0.95 + sunColor * diff * 0.62)
             + sunColor * spec * 0.75
             + skyAmbient * rim;
    lit = lit / (lit + vec3(0.85)) * 1.55;

    float fogFactor = 1.0 - exp(-uFogDensity * vFogDist * uFogDensity * vFogDist);
    fogFactor = clamp(fogFactor, 0.0, 1.0);
    FragColor = vec4(mix(lit, uFogColor, fogFactor), 1.0);
}
)";

GLuint skinProg = 0;
GLint skinModelLoc, skinViewLoc, skinProjLoc, skinTexLoc, skinLightDirLoc, skinTintLoc,
      skinUseTextureLoc, skinFogColorLoc, skinFogDensityLoc, skinCamPosLoc, skinSkinnedLoc;
GLuint treeProg = 0;
GLint treeViewLoc=-1, treeProjLoc=-1, treeTimeLoc=-1, treeLightDirLoc=-1,
      treeFogColorLoc=-1, treeFogDensityLoc=-1, treeCamPosLoc=-1;
GLuint skinBoneUBO = 0;
const int SKIN_BONE_BINDING = 0;

// ==================== ШЕЙДЕР КУБИЧЕСКОГО МИРА ====================
// Отличия от основной программы и зачем они:
//   * цвет приходит В ВЕРШИНЕ, а не uniform'ом: в одном меше чанка сотни блоков разных
//     типов, и рисовать их одним вызовом можно только так;
//   * матрицы модели нет вовсе — геометрия чанка сразу в мировых координатах, значит
//     на кадр уходит один uniform-набор на все чанки;
//   * освещение простое и «плоское»: свет зависит только от нормали грани, поэтому
//     верх кубов светлее боков, а бока — темнее, и форма читается без текстур.
const char* voxelVS = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec3 aColor;
layout(location=3) in vec3 aTex;   // xy — координаты текстуры, z — слой массива
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uCamPos;
out vec3 vNormal;
out vec3 vColor;
out vec3 vTex;
out float vFogDist;
void main(){
    vNormal = aNormal;
    vColor = aColor;
    vTex = aTex;
    vFogDist = length(aPos - uCamPos);
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)";

const char* voxelFS = R"(#version 300 es
precision mediump float;
precision mediump sampler2DArray;
in vec3 vNormal;
in vec3 vColor;
in vec3 vTex;
in float vFogDist;
uniform vec3 uLightDir;
uniform float uLightAmount;   // 0 — ночь, 1 — полдень
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uAlpha;         // 1 — камень и земля, <1 — вода
uniform sampler2DArray uBlocks;
uniform float uTextured;      // 0 — рисуем плоским цветом (ассеты не найдены)
out vec4 FragColor;
void main(){
    vec3 n = normalize(vNormal);
    // Ступенчатое затенение по направлению грани — именно оно даёт «кубическую»
    // картинку: верх ярче всего, север/юг чуть темнее, запад/восток ещё темнее, низ
    // самый тёмный. Плавного освещения здесь не нужно: грани и должны быть плоскими.
    float face = 0.62;
    if(n.y > 0.5)       face = 1.00;
    else if(n.y < -0.5) face = 0.45;
    else if(abs(n.x) > 0.5) face = 0.72;
    else                face = 0.86;

    // Солнце добавляет направленности, но не ломает ступенчатость: половина света —
    // постоянная «небесная», половина — от солнца.
    float ndl = max(dot(n, normalize(uLightDir)), 0.0);
    float light = (0.55 + 0.45 * ndl) * (0.18 + 0.92 * clamp(uLightAmount, 0.0, 1.2));

    // Текстура блока: цвет вершины работает как фильтр поверх неё (в нём же затенение
    // по углам и разброс яркости). Без ассетов остаётся прежняя плоская заливка.
    vec3 albedo = vColor;
    if(uTextured > 0.5) albedo = texture(uBlocks, vTex).rgb * vColor;

    vec3 col = albedo * face * light;
    float fog = 1.0 - exp(-uFogDensity * vFogDist * uFogDensity * vFogDist);
    col = mix(col, uFogColor, clamp(fog, 0.0, 1.0));
    FragColor = vec4(col, uAlpha);
}
)";
