#pragma once
// ==================== MESH ====================
#include "GL.h"
#include "Vertex.h"
#include <vector>

struct Mesh {
    GLuint vao=0, vbo=0;
    int vertexCount=0;
    GLuint texture=0;
};

Mesh uploadMesh(const std::vector<Vertex>& verts, GLuint tex);
