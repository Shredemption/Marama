#include "render/render.h"
#include "event_handler/event_handler.h"
#include "frame_buffer/frame_buffer.h"
#include "camera/camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>

glm::vec4 Render::clipPlane = glm::vec4(0, 0, 0, 0);

unsigned int Render::quadVAO = 0, Render::quadVBO = 0;
float Render::quadVertices[] = {0};
float Render::waterHeight = -0.5;

bool Render::WaterPass = true;

void Render::initQuad()
{
    if (quadVAO == 0)
    {
        float quadVertices[] = {
            // positions   // texture coords
            -1.0f, 1.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f,
            1.0f, -1.0f, 1.0f, 0.0f,

            -1.0f, 1.0f, 0.0f, 1.0f,
            1.0f, -1.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f};

        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
        glBindVertexArray(0);
    }
}

void Render::render(Scene &scene)
{
    renderSceneSkyBox(scene);

    // if (Shader::waterLoaded && (EventHandler::frame % 2 == 0))
    // {
    //     WaterPass = true;
    //     renderReflectRefract(scene, clipPlane);
    //     WaterPass = false;
    // }

    clipPlane = {0, 0, 0, 0};
    renderSceneModels(scene, clipPlane);
    // renderSceneUnitPlanes(scene, clipPlane);

    // renderTestQuad(FrameBuffer::reflectionFBO.colorTexture, 0, 0);
    // renderTestQuad(FrameBuffer::refractionFBO.colorTexture, 2 * EventHandler::screenWidth / 3, 0);

    Camera::cameraMoved = false;
}

void Render::renderSceneModels(Scene &scene, glm::vec4 clipPlane)
{
    for (auto model : scene.structModels)
    {
        Shader shader = Shader::load(model.shader);

        // Send light and view position to relevant shader
        shader.setVec3("lightPos", EventHandler::lightPos);
        shader.setVec3("viewPos", Camera::cameraPosition);
        shader.setFloat("lightIntensity", EventHandler::lightInsensity);
        shader.setVec3("lightCol", EventHandler::lightCol);

        // Apply view and projection to whole scene
        shader.setMat4("u_view", Camera::u_view);
        shader.setMat4("u_projection", Camera::u_projection);

        // Set model matrix for model and draw
        shader.setMat4("u_model", model.u_model);
        shader.setMat4("u_normal", model.u_normal);

        shader.setVec4("location_plane", clipPlane);

        if (model.model->boneHierarchy.size() > 0)
        {
            model.model->updateBoneTransforms();
            shader.setMat4Array("u_boneTransforms", model.model->boneTransforms);
            shader.setMat4Array("inverse_offset", model.model->boneInverseOffsets);
        }

        renderModel(model);
    }
}

void Render::renderSceneUnitPlanes(Scene &scene, glm::vec4 clipPlane)
{
    // Render opaque planes
    for (auto unitPlane : scene.opaqueUnitPlanes)
    {
        Shader shader = Shader::load(unitPlane.shader);

        // Apply view and projection to whole scene
        shader.setMat4("u_view", Camera::u_view);
        shader.setMat4("u_projection", Camera::u_projection);

        // Set model matrix for model and draw
        shader.setMat4("u_model", unitPlane.u_model);
        shader.setMat4("u_normal", unitPlane.u_normal);

        shader.setVec4("location_plane", clipPlane);

        renderMesh(unitPlane.unitPlane);
    }

    // Sort transparent planes back to front based on distance from the camera
    std::sort(scene.transparentUnitPlanes.begin(), scene.transparentUnitPlanes.end(), [&](const UnitPlaneData &a, const UnitPlaneData &b)
              {
                  float distA = glm::distance(Camera::cameraPosition, a.position);
                  float distB = glm::distance(Camera::cameraPosition, b.position);
                  return distA > distB; // Sort by distance: farthest first, closest last
              });

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Render transparent planes
    for (auto unitPlane : scene.transparentUnitPlanes)
    {
        Shader shader = Shader::load(unitPlane.shader);

        // Apply view and projection to whole scene
        shader.setMat4("u_view", Camera::u_view);
        shader.setMat4("u_projection", Camera::u_projection);

        // Set model matrix for model and draw
        shader.setMat4("u_model", unitPlane.u_model);
        shader.setMat4("u_normal", unitPlane.u_normal);

        shader.setVec4("location_plane", clipPlane);

        if (unitPlane.shader == "water" & FrameBuffer::Water == false)
        {
            FrameBuffer::WaterFrameBuffers();
        }
        renderMesh(unitPlane.unitPlane);
    }
    glDisable(GL_BLEND);
}

void Render::renderSceneSkyBox(Scene &scene)
{
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_DEPTH_TEST);

    Shader shader = Shader::load("skybox");

    glBindVertexArray(scene.skyBox.VAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, scene.skyBox.textureID);

    shader.setMat4("u_view", glm::mat4(glm::mat3(Camera::u_view)));
    shader.setMat4("u_projection", Camera::u_projection);
    shader.setMat4("u_model", glm::rotate(glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(100.0f)), glm::vec3(0.0f, -0.175f, 0.0f)), glm::radians(EventHandler::sunAngle), glm::vec3(0.0f, 1.0f, 0.0f)));

    shader.setInt("skybox", 0);

    glDrawArrays(GL_TRIANGLES, 0, 36);

    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);

    glBindVertexArray(0);
}

void Render::renderModel(ModelData model)
{
    for (unsigned int i = 0; i < model.model->meshes.size(); i++)
    {
        Render::renderMesh(model.model->meshes[i]);
    }
}

// Render mesh
void Render::renderMesh(Mesh mesh)
{
    if (mesh.shader == "pbr")
    {
        renderPBR(mesh);
    }
    else if (mesh.shader == "default")
    {
        renderDefault(mesh);
    }
    else if (mesh.shader == "simple")
    {
        renderSimple(mesh);
    }
    else if (mesh.shader == "water")
    {
        if (!WaterPass)
        {
            renderWater(mesh);
        }
    }
}
void Render::renderDefault(Mesh mesh)
{
    Shader shader = Shader::load("default");
    unsigned int diffuseNr = 1;

    // For every texture
    for (unsigned int i = 0; i < mesh.textures.size(); i++)
    {
        // Activate texture unit before binding
        glActiveTexture(GL_TEXTURE0 + i);

        // Retrieve texture number and type
        std::string number;
        std::string name = mesh.textures[i].type;

        // Set appropriate number for filename (eg texture_diffuse3)
        if (name == "diffuse")
        {
            number = std::to_string(diffuseNr++);
            shader.setInt(("material." + name + number).c_str(), i);
            glBindTexture(GL_TEXTURE_2D, mesh.textures[i].id);
        }
    }

    // Unload texture
    glActiveTexture(GL_TEXTURE0);

    // Draw Mesh
    glBindVertexArray(mesh.VAO);
    glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);

    glBindVertexArray(0);
}

void Render::renderPBR(Mesh mesh) // FIXME : Not showing when rendered (but is shown wrong in reflection of water fsr)
{
    unsigned int diffuseNr = 1;
    unsigned int specularNr = 1;
    unsigned int normalNr = 1;
    unsigned int roughnessNr = 1;
    unsigned int aoNr = 1;

    Shader shader = Shader::load("pbr");

    // For every texture
    for (unsigned int i = 0; i < mesh.textures.size(); i++)
    {
        // Activate texture unit before binding
        glActiveTexture(GL_TEXTURE0 + i);

        // Retrieve texture number and type
        std::string number;
        std::string name = mesh.textures[i].type;

        // Set appropriate number for filename (eg texture_diffuse3)
        if (name == "diffuse")
            number = std::to_string(diffuseNr++);
        else if (name == "specular")
            number = std::to_string(specularNr++);
        else if (name == "normal")
            number = std::to_string(normalNr++);
        else if (name == "roughness")
            number = std::to_string(roughnessNr++);
        else if (name == "ao")
            number = std::to_string(aoNr++);

        // Send texture to shader
        shader.setInt(("material." + name + number).c_str(), i);
        glBindTexture(GL_TEXTURE_2D, mesh.textures[i].id);
    }
    // Unload texture
    glActiveTexture(GL_TEXTURE0);

    // Draw Mesh
    glBindVertexArray(mesh.VAO);
    glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);

    glBindVertexArray(0);
}

void Render::renderSimple(Mesh mesh)
{
    // Draw Mesh
    glBindVertexArray(mesh.VAO);
    glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);

    glBindVertexArray(0);
}

void Render::renderWater(Mesh mesh)
{
    // Load surface textures
    Texture DuDv = LoadStandaloneTexture("waterDUDV.png");
    Texture normal = LoadStandaloneTexture("waterNormal.png");

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, FrameBuffer::reflectionFBO.colorTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, FrameBuffer::refractionFBO.colorTexture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, DuDv.id);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, normal.id);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, FrameBuffer::refractionFBO.depthTexture);

    Shader shader = Shader::load("water");
    shader.setInt("reflectionTexture", 0);
    shader.setInt("refractionTexture", 1);
    shader.setInt("dudvMap", 2);
    shader.setInt("normalMap", 3);
    shader.setInt("depthMap", 4);
    shader.setFloat("moveOffset", EventHandler::time);
    shader.setVec3("cameraPosition", Camera::cameraPosition);
    shader.setVec3("lightPos", EventHandler::lightPos);
    shader.setVec3("lightCol", EventHandler::lightCol);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw Mesh
    glBindVertexArray(mesh.VAO);
    glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);

    glDisable(GL_BLEND);

    glBindVertexArray(0);
}

void Render::renderReflectRefract(Scene &scene, glm::vec4 clipPlane)
{
    // ===== REFLECTOIN =====
    // Bind reflection buffer
    FrameBuffer::bindFrameBuffer(FrameBuffer::reflectionFBO);

    clipPlane = {0, 1, 0, -waterHeight};
    Camera::pitch = -Camera::pitch;
    Camera::setCamDirection();
    float distance = 2 * (Camera::cameraPosition[1] - waterHeight);
    Camera::cameraPosition[1] -= distance;
    Camera::genViewMatrix();

    // Draw to it
    glClear(GL_COLOR_BUFFER_BIT);
    glClear(GL_DEPTH_BUFFER_BIT);
    renderSceneSkyBox(scene);
    renderSceneModels(scene, clipPlane);
    renderSceneUnitPlanes(scene, clipPlane);

    // ===== REFRACTION =====
    // Bind refraction buffer
    FrameBuffer::bindFrameBuffer(FrameBuffer::refractionFBO);

    clipPlane = {0, -1, 0, waterHeight};
    Camera::pitch = -Camera::pitch;
    Camera::setCamDirection();
    Camera::cameraPosition[1] += distance;
    Camera::genViewMatrix();

    // Draw to it
    glClear(GL_COLOR_BUFFER_BIT);
    glClear(GL_DEPTH_BUFFER_BIT);
    renderSceneSkyBox(scene);
    renderSceneModels(scene, clipPlane);
    renderSceneUnitPlanes(scene, clipPlane);

    // Unbind buffers, bind default one
    FrameBuffer::unbindCurrentFrameBuffer();
}

void Render::renderTestQuad(GLuint texture, int x, int y)
{
    glViewport(x, y, EventHandler::screenWidth / 3, EventHandler::screenHeight / 3);

    Shader quadShader = Shader::load("gui");
    quadShader.setInt("screenTexture", 0);

    // Bind the framebuffer texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    // Render the quad
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    // restore viewport
    glViewport(0, 0, EventHandler::screenWidth, EventHandler::screenHeight);
}

Texture Render::LoadStandaloneTexture(std::string fileName)
{
    Texture loadTexture;
    // If texture already loaded
    if (Model::textureCache.find(fileName) != Model::textureCache.end())
    {
        // Use cached texture
        loadTexture = Model::textureCache[fileName].texture;
        Model::textureCache[fileName].refCount++;
    }
    else
    {
        // Define and load new texture to texture cache
        Texture texture;
        texture.id = Model::TextureFromFile(fileName.c_str(), "../resources/textures");
        texture.type = "standalone";
        texture.path = fileName.c_str();

        loadTexture = texture;
        Model::textureCache[fileName].texture = texture;
    }

    return loadTexture;
}
