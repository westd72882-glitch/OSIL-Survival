#!/usr/bin/env python3
# ==================== ПОДГОТОВКА АССЕТОВ ====================
# Исходные текстуры лежат в корне репозитория как их выложил художник: 1024x1024 и
# по три мегабайта штука. В APK их класть нельзя — четыре такие картинки весят больше,
# чем весь остальной клиент. Скрипт уменьшает их до игрового размера и раскладывает в
# assets/ под теми именами, которые ждёт код (см. src/Engine/Render/BlockTextures.cpp).
#
# Зависимостей нет намеренно: Pillow в сборочном окружении может не оказаться, а PNG
# без чересстрочности читается и пишется десятком строк на zlib.
import os, struct, zlib, sys

def read_png(path):
    d = open(path, 'rb').read()
    pos, idat, W, H, ct = 8, b'', 0, 0, 6
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]
        typ = d[pos+4:pos+8]
        data = d[pos+8:pos+8+ln]
        if typ == b'IHDR':
            W, H, bd, ct, comp, filt, inter = struct.unpack('>IIBBBBB', data[:13])
            if bd != 8 or inter != 0:
                raise ValueError('поддерживается только 8 бит на канал без чересстрочности')
        elif typ == b'IDAT':
            idat += data
        pos += 12 + ln
    raw = zlib.decompress(idat)
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    stride = W * ch
    px = bytearray(W * H * 4)
    prev = bytearray(stride)
    p = 0
    for y in range(H):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p+stride]); p += stride
        # Развёртка фильтров PNG (см. спецификацию, раздел Filtering).
        for i in range(stride):
            a = line[i-ch] if i >= ch else 0
            b = prev[i]
            c = prev[i-ch] if i >= ch else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp-a), abs(pp-b), abs(pp-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        for x in range(W):
            if ch == 4: r, g, bl, al = line[x*4], line[x*4+1], line[x*4+2], line[x*4+3]
            elif ch == 3: r, g, bl, al = line[x*3], line[x*3+1], line[x*3+2], 255
            elif ch == 2: r = g = bl = line[x*2]; al = line[x*2+1]
            else: r = g = bl = line[x]; al = 255
            o = (y*W + x) * 4
            px[o], px[o+1], px[o+2], px[o+3] = r, g, bl, al
        prev = line
    return W, H, px

def resize(W, H, px, nw, nh):
    # Усреднение по блоку: при уменьшении в 4 раза «ближайший сосед» даёт рябь.
    out = bytearray(nw * nh * 4)
    for y in range(nh):
        y0, y1 = y * H // nh, max(y * H // nh + 1, (y + 1) * H // nh)
        for x in range(nw):
            x0, x1 = x * W // nw, max(x * W // nw + 1, (x + 1) * W // nw)
            r = g = b = a = n = 0
            for sy in range(y0, y1):
                for sx in range(x0, x1):
                    o = (sy * W + sx) * 4
                    r += px[o]; g += px[o+1]; b += px[o+2]; a += px[o+3]; n += 1
            o = (y * nw + x) * 4
            out[o], out[o+1], out[o+2], out[o+3] = r//n, g//n, b//n, a//n
    return out

def write_png(path, W, H, px):
    raw = b''
    for y in range(H):
        raw += b'\x00' + bytes(px[y*W*4:(y+1)*W*4])
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 6, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 9))
           + chunk(b'IEND', b''))
    open(path, 'wb').write(png)

# Что во что превращается. Ключ — имя в assets/, значение — (исходник, размер).
# Размеры: блоки 256 (на телефоне больше не видно), интерфейс 128, фон меню как есть.
JOBS = {
    'block_ground.png':  ('T_Ground_Sand_02_A_Sm.png', 256),   # песок: он же зима и земля под фильтром
    'block_stone.png':   ('tex_stone.png', 256),
    'block_road.png':    ('T_field_road_01_A_T.png', 256),
    'block_wood.png':    ('Trunk.png', 256),
    'block_sulfur.png':  ('tex_sulfur_ore.png', 256),
    'block_metal.png':   ('tex_iron_ore.png', 256),
    'block_planks.png':  ('Wall_Wood_0.png', 256),
    'block_leaves.png':  ('Grass_02.png', 256),
    'block_dirt.png':    ('Dirt_01.png', 256),
    'ui_dig.png':        ('use-button.png', 128),
    'ui_place.png':      ('accept-button.png', 128),
    'ui_interact.png':   ('interact-button.png', 128),
    'ui_inventory.png':  ('inventory-button.png', 128),
    'ui_craft.png':      ('crafting-button.png', 128),
    'ui_map.png':        ('zoom-button.png', 128),
    'ui_settings.png':   ('settings.png', 128),
    'ui_close.png':      ('X.png', 128),
    'ui_jump.png':       ('jump-button.png', 128),
    'ui_run.png':        ('run-button.png', 128),
    'ui_crouch.png':     ('crouch-button.png', 128),
    'ui_joystick_base.png':  ('base-joystick.png', 160),
    'ui_joystick_stick.png': ('stick-joystick.png', 128),
    'ui_player_marker.png':  ('self.png', 48),
    'ui_damage.png':     ('Damage Indicator.png', 256),
    'item_torch.png':    ('Torch_0.png', 128),
    # Значки предметов для ячеек инвентаря: те же картинки, что и у блоков жил.
    'item_sulfur.png':   ('Sulfur Ore.png', 128),
    'item_scrap.png':    ('Scrap.png', 128),
    'item_axe.png':      ('crafting-button.png', 128),
    # Значки ресурсов-блоков — те же картинки, что и у самих блоков: предмет в ячейке
    # выглядит ровно тем, что игрок только что выбил.
    'item_iron.png':     ('tex_iron_ore.png', 128),
    'item_stone.png':    ('tex_stone.png', 128),
    'item_wood.png':     ('Wooden Log.png', 128),
    'item_leaves.png':   ('Grass_02.png', 128),
    'item_planks.png':   ('Wall_Wood_0.png', 128),
    'item_dirt.png':     ('Dirt_01.png', 128),
    'item_sand.png':     ('T_Ground_Sand_02_A_Sm.png', 128),
    'menu_bg.png':       ('menu-bg-update.png', 0),            # 0 — оставить как есть
}

def main():
    os.makedirs('assets', exist_ok=True)
    for dst, (src, size) in JOBS.items():
        if not os.path.exists(src):
            print('нет исходника:', src); continue
        W, H, px = read_png(src)
        # Текстуры блоков едут в GL_TEXTURE_2D_ARRAY, а он берёт только квадратные слои
        # одного формата — поэтому их приводим к квадрату здесь, а не растягиваем при
        # загрузке. Иконкам интерфейса пропорции сохраняем: их рисуют как есть.
        if dst.startswith('block_') and size:
            if (W, H) != (size, size):
                px = resize(W, H, px, size, size)
                W = H = size
        elif size and (W > size or H > size):
            nw = size
            nh = max(1, H * size // W)
            px = resize(W, H, px, nw, nh)
            W, H = nw, nh
        write_png(os.path.join('assets', dst), W, H, px)
        print(f'{src} -> assets/{dst}  {W}x{H}  {os.path.getsize("assets/" + dst)//1024} КБ')

if __name__ == '__main__':
    main()
