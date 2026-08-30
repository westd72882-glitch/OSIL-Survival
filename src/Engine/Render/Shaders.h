#pragma once
// ==================== ШЕЙДЕРЫ ====================
#include "GL.h"

GLuint compileShader(GLenum type, const char* src);
GLuint linkProgram(const char* vsSrc, const char* fsSrc);

extern const char* mainVS;
extern const char* mainFS;
// Небо — полностью процедурное (без cubemap-текстур): бесшовно по построению, т.к.
// считается как функция от направления взгляда.
extern const char* skyVS;
extern const char* skyFS;
extern const char* uiVS;
extern const char* uiFS;
// Пост-обработка всей сцены (цветокоррекция/виньетка/зерно) при переносе offscreen-буфера.
extern const char* postVS;
extern const char* postFS;
// Процедурная инстансированная трава.
extern const char* grassVS;
extern const char* grassFS;
extern const char* treeVS;
extern const char* treeFS;
// Скиннинг: те же свет и туман, что в mainFS, но позиция и нормаль вершины
// пересчитываются по матрицам костей из uniform-буфера (см. SkinnedModel.h).
extern const char* skinVS;
extern const char* skinFS;

extern GLuint mainProg, uiProg, skyProg, postProg, grassProg, treeProg, skinProg;
// Кубический мир: своя программа с цветом в вершине (текстур у блоков нет — цвет грани
// и затенение по нормали дают узнаваемую «кубическую» подачу и стоят дёшево).
extern const char* voxelVS;
extern const char* voxelFS;
extern GLuint voxelProg;
extern GLint voxelViewLoc, voxelProjLoc, voxelLightDirLoc, voxelLightAmountLoc,
             voxelFogColorLoc, voxelFogDensityLoc, voxelCamPosLoc, voxelAlphaLoc;
extern GLint uModelLoc, uViewLoc, uProjLoc, uNormalMatLoc, uTexLoc, uLightDirLoc, uTintColorLoc, uUseTextureLoc;
extern GLint uFogColorLoc, uFogDensityLoc, uCamPosLoc;
// Полупрозрачное самосвечение основной программы: uOpacity - множитель альфы,
// uUnlit != 0 - не освещать поверхность, а выдать её цвет как свет. См. mainFS.
extern GLint uOpacityLoc, uUnlitLoc;
// Освещённость сцены (0 — ночь, 1 — полдень): суточный цикл в шейдере света и неба.
extern GLint uLightAmountLoc;
extern GLint uiProjLoc, uiTexLoc, uiColorLoc, uiUseTextureLoc;
extern GLint skyViewLoc, skyProjLoc, skyTimeLoc, skySunDirLoc, skyLightAmountLoc;
// Небо рисуется полноэкранным треугольником: вместо матриц ему нужны векторы
// камеры и раствор объектива — по ним пиксель сам восстанавливает направление луча.
extern GLint skyCamRightLoc, skyCamUpLoc, skyCamForwardLoc,
             skyTanHalfFovLoc, skyAspectLoc, skyFogColorLoc;
extern GLint postProjLoc, postTexLoc, postTimeLoc, postResLoc;
extern GLint grassViewLoc, grassProjLoc, grassTimeLoc, grassLightDirLoc, grassFogColorLoc, grassFogDensityLoc;
extern GLint grassCentreLoc, grassRadiusLoc;
extern GLint treeViewLoc, treeProjLoc, treeTimeLoc, treeLightDirLoc, treeFogColorLoc, treeFogDensityLoc, treeCamPosLoc;
extern GLint skinModelLoc, skinViewLoc, skinProjLoc, skinTexLoc, skinLightDirLoc, skinTintLoc,
             skinUseTextureLoc, skinFogColorLoc, skinFogDensityLoc, skinCamPosLoc, skinSkinnedLoc;
extern GLuint skinBoneUBO;        // буфер матриц костей
extern const int SKIN_BONE_BINDING; // точка привязки uniform-блока
