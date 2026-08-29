#pragma once
// Формат вершины всей игровой геометрии: позиция, UV, нормаль.
struct Vertex {
    float px, py, pz;
    float u, v;
    float nx, ny, nz;
};
