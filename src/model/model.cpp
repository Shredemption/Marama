#include <model/model.h>
#include <scene/scene.h>
#include <event_handler/event_handler.h>

#include <assimp/postprocess.h>
#include <jsoncons/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <glm/gtx/string_cast.hpp>

std::unordered_map<std::string, CachedTexture> Model::textureCache;
std::map<std::string, std::string> Model::modelMap;

// Model Constructor
Model::Model(std::string const &path, std::string shaderName)
{
    loadModel(path, shaderName);
}

// Model Destructor
Model::~Model()
{
    // For every mesh
    for (const auto &mesh : meshes)
    {
        // For every texture in mesh
        for (const auto &texture : mesh.textures)
        {
            // Check if texture still in textureCache
            auto iteration = textureCache.find(texture.path);
            if (iteration != textureCache.end())
            {
                // If texture in cache, decrement count
                iteration->second.refCount--;

                // If count 0
                if (iteration->second.refCount == 0)
                {
                    // Remove unload from GPU and remove from cache
                    glDeleteTextures(1, &iteration->second.texture.id);
                    textureCache.erase(iteration);
                }
            }
        }
    }

    // Release mesh VAO, VBO and EBO from GPU
    glDeleteBuffers(1, &meshes[0].VBO);
    glDeleteBuffers(1, &meshes[0].EBO);
    glDeleteVertexArrays(1, &meshes[0].VAO);

    // Ensure meshes vector clears properly
    meshes.clear();
}

void Model::loadModel(std::string path, std::string shaderName)
{
    // Define importer and open file
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace | aiProcess_GenNormals);

    // If scene null, scene flagged as incomplete, or root node null
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        // Show error
        std::cout << "Assimp Error: " << importer.GetErrorString() << std::endl;
        return;
    }

    // Open full node recursion
    directory = path.substr(0, path.find_last_of('/'));
    processNode(scene->mRootNode, scene, shaderName);

    updateBoneTransforms();
}

void Model::processNode(aiNode *node, const aiScene *scene, std::string shaderName, Bone *parentBone)
{
    std::string nodeName = node->mName.C_Str();

    if (nodeName.rfind("Armature", 0) == 0)
    {
        Bone *currentBone = new Bone(nodeName, boneHierarchy.size(), glm::mat4(1.0f));
        boneHierarchy.emplace(nodeName, currentBone);

        if (parentBone)
        {
            parentBone->children.push_back(currentBone);
            currentBone->parent = parentBone;
        }
        else
        {
            rootBones.push_back(currentBone);
        }
    }
    else
    {
        for (unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            unsigned int meshIndex = node->mMeshes[i];
            aiMesh *mesh = scene->mMeshes[meshIndex];
            meshes.push_back(processMesh(mesh, scene, shaderName, boneHierarchy));
        }
    }

    // Recursively process children
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        processNode(node->mChildren[i], scene, shaderName, boneHierarchy[nodeName]);
    }
}

Mesh Model::processMesh(aiMesh *mesh, const aiScene *scene, std::string shaderName, std::map<std::string, Bone *> &boneHierarchy)
{
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;

    for (unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
        Vertex vertex;
        // Process vertex positions
        glm::vec3 vector;
        vector.x = mesh->mVertices[i].x;
        vector.y = mesh->mVertices[i].y;
        vector.z = mesh->mVertices[i].z;
        vertex.Position = vector;

        // Process normals
        vector.x = mesh->mNormals[i].x;
        vector.y = mesh->mNormals[i].y;
        vector.z = mesh->mNormals[i].z;
        vertex.Normal = vector;

        // Process Tangent
        vector.x = mesh->mTangents[i].x;
        vector.y = mesh->mTangents[i].y;
        vector.z = mesh->mTangents[i].z;
        vertex.Tangent = vector;

        // Process Bitangent
        vector.x = mesh->mBitangents[i].x;
        vector.y = mesh->mBitangents[i].y;
        vector.z = mesh->mBitangents[i].z;
        vertex.Bitangent = vector;

        // Process texture coords
        if (mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
        {
            glm::vec2 vec;
            vec.x = mesh->mTextureCoords[0][i].x;
            vec.y = mesh->mTextureCoords[0][i].y;
            vertex.TexCoords = vec;
        }
        else
            vertex.TexCoords = glm::vec2(0.0f, 0.0f);

        vertices.push_back(vertex);
    }
    // Process Indx
    for (unsigned int i = 0; i < mesh->mNumFaces; i++)
    {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++)
            indices.push_back(face.mIndices[j]);
    }
    // Process Mats
    if (mesh->mMaterialIndex >= 0)
    {
        // Get material
        aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];

        std::vector<Texture> diffuseMaps = loadMaterialTexture(material, aiTextureType_DIFFUSE, "diffuse");
        textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());

        if (shaderName == "default")
        {
            std::vector<Texture> normalMaps = loadMaterialTexture(material, aiTextureType_UNKNOWN, "properties");
            textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
        }

        if (shaderName == "pbr")
        {
            // Instert PBR textures
            std::vector<Texture> normalMaps = loadMaterialTexture(material, aiTextureType_NORMALS, "normal");
            textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());

            std::vector<Texture> specularMaps = loadMaterialTexture(material, aiTextureType_SPECULAR, "specular");
            textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());

            std::vector<Texture> roughnessMaps = loadMaterialTexture(material, aiTextureType_DIFFUSE_ROUGHNESS, "roughness");
            textures.insert(textures.end(), roughnessMaps.begin(), roughnessMaps.end());

            std::vector<Texture> aoMaps = loadMaterialTexture(material, aiTextureType_AMBIENT_OCCLUSION, "ao");
            textures.insert(textures.end(), aoMaps.begin(), aoMaps.end());
        }
    }

    // Process Bone ID's
    for (unsigned int i = 0; i < mesh->mNumBones; i++)
    {
        aiBone *bone = mesh->mBones[i];
        std::string boneName = bone->mName.C_Str();

        glm::mat4 offsetMatrix;
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                offsetMatrix[row][col] = bone->mOffsetMatrix[row][col]; // Copy the data
            }
        }

        boneHierarchy[boneName]->offsetMatrix = glm::transpose(offsetMatrix);

        int boneIndex = boneHierarchy[boneName]->index;

        // Process weights per bone
        for (unsigned int j = 0; j < bone->mNumWeights; j++)
        {
            aiVertexWeight weight = bone->mWeights[j];
            int vertexID = weight.mVertexId;
            float weightValue = weight.mWeight;

            // Add this bone's influence to the vertex
            auto &vertex = vertices[vertexID];
            for (int k = 0; k < 4; k++)
            {
                if (vertex.Weights[k] == 0.0f)
                { // Find an empty slot
                    vertex.BoneIDs[k] = boneIndex;
                    vertex.Weights[k] = weightValue;
                    break;
                }
            }
        }
    }

    // Remove "Scene" bone if it is present
    auto it = boneHierarchy.find("Scene");
    if (it != boneHierarchy.end())
    {
        boneHierarchy.erase(it); // Remove the "Scene" bone from the hierarchy
    }

    Mesh loadedMesh = Mesh(vertices, indices, textures, shaderName);
    return loadedMesh;
}

std::vector<Texture> Model::loadMaterialTexture(aiMaterial *mat, aiTextureType type, std::string typeName)
{
    // Vector of textures to push to GPU
    std::vector<Texture> textures;

    // Find texture manually
    std::string textureName = findTextureInDirectory(directory, typeName);
    if (!textureName.empty())
    {
        // If texture already loaded
        if (textureCache.find(textureName) != textureCache.end())
        {
            // Use cached texture
            textures.push_back(textureCache[textureName].texture);
            textureCache[textureName].refCount++;
        }
        else
        {
            // Define and load new texture to texture cache
            Texture texture;
            texture.id = TextureFromFile(textureName.c_str(), directory);
            texture.type = typeName;
            texture.path = textureName.c_str();
            textures.push_back(texture);
            textureCache[textureName].texture = texture;
        }
    }

    else
    {
        std::cout << "Failed to load " << typeName << " texture in " << directory << "\n";
    }

    return textures;
}

unsigned int Model::TextureFromFile(const char *name, const std::string &directory)
{
    // Get texture location
    std::string filename = std::string(name);
    filename = directory + '/' + filename;

    // Generate empty texture
    unsigned int textureID;
    glGenTextures(1, &textureID);

    // Load texture file
    int width, height, nrComponents;
    unsigned char *data = stbi_load(filename.c_str(), &width, &height, &nrComponents, 0);
    if (data)
    {
        // Detect what kind of texture
        GLenum format;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;

        // Bind texture and upload
        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        // Set texture looping and scaling parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Unload texture
        stbi_image_free(data);
    }
    else
    {
        // Error if texture cant be loaded
        std::cout << "Texture failed to load at path: " << name << std::endl;
        stbi_image_free(data);
    }
    return textureID;
};

std::string Model::findTextureInDirectory(const std::string &directory, const std::string &typeName)
{
    // Define common texture file extensions
    const std::vector<std::string> extensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};

    // Iterate through files in the directory
    for (const auto &entry : std::filesystem::directory_iterator(directory))
    {
        std::string filename = entry.path().filename().string();

        // Check if the filename contains the texture type (e.g., "normal", "roughness", etc.)
        if (filename.find(typeName) != std::string::npos)
        {
            // Check if the file has a valid texture extension
            for (const auto &ext : extensions)
            {
                if (ends_with(filename, ext))
                {
                    return filename; // Return the first matching texture
                }
            }
        }
    }

    return ""; // Return empty if no matching texture is found
}

inline bool Model::ends_with(std::string const &value, std::string const &ending)
{
    if (ending.size() > value.size())
        return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

std::map<std::string, std::string> Model::loadModelMap(const std::string &filePath)
{
    const std::string path = "../" + filePath;
    // Check if the file exists
    if (!std::filesystem::exists(path))
    {
        throw std::runtime_error("File not found: " + path);
    }

    // Open the file
    std::ifstream file(path);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open file: " + path);
    }

    // Parse the JSON
    jsoncons::json j;
    try
    {
        j = jsoncons::json::parse(file);
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("Failed to parse JSON: " + std::string(e.what()));
    }

    // Create a map from the parsed JSON
    std::map<std::string, std::string> modelMap;
    for (const auto &kv : j["models"].object_range())
    {
        modelMap[kv.key()] = kv.value().as<std::string>();
    }

    return modelMap;
}

unsigned int Model::LoadSkyBoxTexture(SkyBoxData skybox)
{
    std::vector<Texture> loadTextures;

    std::vector<std::string> SkyBoxTextureNames =
        {
            skybox.right,
            skybox.left,
            skybox.up,
            skybox.down,
            skybox.front,
            skybox.back,
        };

    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    int width, height, nrChannels;
    unsigned char *data;
    for (unsigned int i = 0; i < 6; i++)
    {
        data = stbi_load(SkyBoxTextureNames[i].c_str(), &width, &height, &nrChannels, 0);

        if (data)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);

            // Unload texture
            stbi_image_free(data);
        }
        else
        {
            std::cout << "Cubemap tex failed to load at path: " << SkyBoxTextureNames[i] << std::endl;
            stbi_image_free(data);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}

void Model::updateBoneTransforms()
{

    // Resize if matrix array not large enough
    if (boneTransforms.size() != boneHierarchy.size())
    {
        boneTransforms.resize(boneHierarchy.size(), glm::mat4(1.0f));
        boneInverseOffsets.resize(boneHierarchy.size(), glm::mat4(1.0f));
    }

    for (auto &rootBone : rootBones)
    {
        // Start recursion from the root bone, with identity matrix for the root's parent transform
        updateBoneTransformsRecursive(rootBone, glm::mat4(1.0f));
    }
}

void Model::updateBoneTransformsRecursive(Bone *bone, const glm::mat4 &parentTransform)
{

    // Compute the global transform of the current bone
    bone->transform = glm::rotate(glm::mat4(1.0f), glm::radians(EventHandler::time), glm::vec3(0.0f, 1.0f, 0.0f));
    // bone->transform = glm::mat4(1.0f);

    bone->globalTransform = parentTransform * bone->transform;

    boneTransforms[bone->index] = bone->offsetMatrix * bone->globalTransform;
    boneInverseOffsets[bone->index] = glm::inverse(bone->offsetMatrix);

    // Recursively update the transforms of the child bones
    for (Bone *child : bone->children)
    {
        updateBoneTransformsRecursive(child, bone->globalTransform);
    }
}