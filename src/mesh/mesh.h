#ifndef MESH_H
#define MESH_H

#include <glm/glm.hpp>
#include <string>
#include <vector>

#include <shader/shader.h>

struct Vertex
{
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 Tangent;
    glm::vec3 Bitangent;
    glm::vec3 Color;
    int BoneIDs[4] = {0, 0, 0, 0};
    float Weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct Texture
{
    unsigned int id;
    std::string type;
    std::string path;
};

struct Bone
{
    std::string name;
    int index;
    glm::mat4 offsetMatrix;
    glm::mat4 transform = glm::mat4(1.0f);
    glm::mat4 globalTransform;
    Bone *parent;
    std::vector<Bone *> children;

    Bone(std::string boneName, int boneIndex, const glm::mat4 &offset, Bone *parentBone = nullptr)
        : name(boneName), index(boneIndex), offsetMatrix(offset), parent(parentBone) {}
};

class Mesh
{
public:
    // Mesh data
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;
    std::string shader;
    unsigned int VAO, VBO, EBO;

    // Mesh Constructor
    Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<Texture> textures, std::string shaderName);

    // Mesh Destructor
    ~Mesh();

    static Mesh genUnitPlane(glm::vec3 color, std::string shaderName);

    static unsigned int setupSkyBoxMesh();

private:
    // Rendering Data to GPU
    void setupPBRMesh();
    void setupDefaultMesh();
    void setupSimpleMesh();
    void setupWaterMesh();
};

#endif