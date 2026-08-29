package com.osil.survival;

import org.libsdl.app.SDLActivity;

/**
 * Точка входа Android-приложения. Вся игра — нативная (C++), Java здесь ровно столько,
 * сколько требует SDL: назвать библиотеки, которые надо загрузить, и в каком порядке.
 * Порядок важен: libmain зависит от SDL2 и её расширений, поэтому идёт последним.
 */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "SDL2_image",
            "SDL2_ttf",
            "SDL2_mixer",
            "main"
        };
    }
}
