#ifndef RENDER_H
#define RENDER_H

#include "scene/scene.h"
#include "glm/glm.hpp"

class Render
{
public:
    static void initQuad();
    static void render(Scene &scene);
    static glm::vec4 clipPlane;
    static float waterHeight;

    static Texture LoadStandaloneTexture(std::string fileName);

private:
    static void renderSceneModels(Scene &scene, glm::vec4 clipPlane);
    static void renderSceneUnitPlanes(Scene &scene, glm::vec4 clipPlane);
    static void renderSceneSkyBox(Scene &scene);

    static void renderModel(ModelData model);
    static void renderMesh(Mesh mesh);

    static void renderDefault(Mesh mesh);
    static void renderPBR(Mesh mesh);
    static void renderSimple(Mesh mesh);
    static void renderWater(Mesh mesh);

    static void renderReflectRefract(Scene &scene, glm::vec4 clipPlane);
    static void renderTestQuad(GLuint texture, int x, int y);

    static unsigned int quadVAO;
    static unsigned int quadVBO;
    static float quadVertices[];
    static bool WaterPass;
};

#endif