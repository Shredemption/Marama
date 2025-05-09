#include "model/model.h"

#include <assimp/postprocess.h>
#include <jsoncons/json.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "scene/scene.h"
#include "event_handler/event_handler.h"

// Texture Cache
std::unordered_map<std::string, CachedTexture> Model::textureCache;
std::mutex Model::textureCacheMutex;

// Texture Queue of to be loaded textures
std::queue<PendingTexture> Model::textureQueue;
std::mutex Model::textureQueueMutex;

// Names of textures that are to be loaded
std::unordered_set<std::string> Model::pendingTextures;
std::mutex Model::pendingTexturesMutex;
std::mutex Model::openglMutex;

// Model map and location
std::map<std::string, std::pair<std::string, ModelType>> Model::modelMap;
std::string modelMapPath = "resources/models.json";

// Json setups
JSONCONS_N_MEMBER_TRAITS(JSONModelMapData, 2, name, path);
JSONCONS_N_MEMBER_TRAITS(JSONModelMap, 0, models, yachts);

// Model Constructor
Model::Model(std::tuple<std::string, std::string, std::string> NamePathShader)
{
    this->name = std::get<0>(NamePathShader);
    this->path = std::get<1>(NamePathShader);
    loadModel(std::get<1>(NamePathShader), std::get<2>(NamePathShader));
}

// Model Destructor
Model::~Model()
{
    // For every texture
    for (const auto &texture : textures)
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

    // Combine meshes into one
    combineMeshes(scene, shaderName);

    // Generate initial bone positions
    generateBoneTransforms();
}

void Model::processNode(aiNode *node, const aiScene *scene, std::string shaderName, Bone *parentBone)
{
    // Get node name
    std::string nodeName = node->mName.C_Str();

    // If node is armatrue, extract bone
    if (nodeName.rfind("Armature", 0) == 0)
    {
        Bone *currentBone = new Bone(nodeName, boneHierarchy.size() - 1, glm::mat4(1.0f));
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
    // Else extract meshes
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

    // Process Indices
    for (unsigned int i = 0; i < mesh->mNumFaces; i++)
    {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++)
            indices.push_back(face.mIndices[j]);
    }

    // Process Mats
    if (mesh->mMaterialIndex >= 0)
    {
        aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];

        // Load textures based on shader
        if (shaderName == "default")
        {
            // Check and add diffuse maps if not already loaded
            std::vector<Texture> diffuseMaps = loadMaterialTexture(material, aiTextureType_DIFFUSE, "diffuse");
            for (const auto &texture : diffuseMaps)
            {
                if (std::find(textures.begin(), textures.end(), texture) == textures.end()) // Texture not found
                {
                    textures.push_back(texture);
                }
            }

            // Check and add normal maps if not already loaded
            std::vector<Texture> normalMaps = loadMaterialTexture(material, aiTextureType_UNKNOWN, "properties");
            for (const auto &texture : normalMaps)
            {
                if (std::find(textures.begin(), textures.end(), texture) == textures.end()) // Texture not found
                {
                    textures.push_back(texture);
                }
            }
        }

        if (shaderName == "toon")
        {
            // Check and add toon textures (e.g., highlight and shadow) if not already loaded
            std::vector<Texture> diffuseMaps = loadMaterialTexture(material, aiTextureType_DIFFUSE, "highlight");
            for (const auto &texture : diffuseMaps)
            {
                if (std::find(textures.begin(), textures.end(), texture) == textures.end()) // Texture not found
                {
                    textures.push_back(texture);
                }
            }

            std::vector<Texture> normalMaps = loadMaterialTexture(material, aiTextureType_UNKNOWN, "shadow");
            for (const auto &texture : normalMaps)
            {
                if (std::find(textures.begin(), textures.end(), texture) == textures.end()) // Texture not found
                {
                    textures.push_back(texture);
                }
            }
        }

        if (shaderName == "pbr")
        {
            // Check and add PBR textures (diffuse, normal, specular, roughness, ao) if not already loaded
            std::vector<Texture> diffuseMaps = loadMaterialTexture(material, aiTextureType_DIFFUSE, "diffuse");
            for (const auto &texture : diffuseMaps)
            {
                if (std::find(textures.begin(), textures.end(), texture) == textures.end()) // Texture not found
                {
                    textures.push_back(texture);
                }
            }

            std::vector<Texture> normalMaps = loadMaterialTexture(material, aiTextureType_NORMALS, "normal");
            for (const auto &texture : normalMaps)
            {
                if (std::find(textures.begin(), textures.end(), texture) == textures.end()) // Texture not found
                {
                    textures.push_back(texture);
                }
            }

            std::vector<Texture> specularMaps = loadMaterialTexture(material, aiTextureType_SPECULAR, "specular");
            for (const auto &texture : specularMaps)
            {
                if (std::find(textures.begin(), textures.end(), texture) == textures.end()) // Texture not found
                {
                    textures.push_back(texture);
                }
            }

            std::vector<Texture> roughnessMaps = loadMaterialTexture(material, aiTextureType_DIFFUSE_ROUGHNESS, "roughness");
            for (const auto &texture : roughnessMaps)
            {
                if (std::find(textures.begin(), textures.end(), texture) == textures.end()) // Texture not found
                {
                    textures.push_back(texture);
                }
            }

            std::vector<Texture> aoMaps = loadMaterialTexture(material, aiTextureType_AMBIENT_OCCLUSION, "ao");
            for (const auto &texture : aoMaps)
            {
                if (std::find(textures.begin(), textures.end(), texture) == textures.end()) // Texture not found
                {
                    textures.push_back(texture);
                }
            }
        }
    }

    // Process Bone ID's
    for (unsigned int i = 0; i < mesh->mNumBones; i++)
    {
        aiBone *bone = mesh->mBones[i];
        std::string boneName = bone->mName.C_Str();

        glm::mat4 offsetMatrix = glm::transpose(glm::make_mat4(&bone->mOffsetMatrix.a1));

        boneHierarchy[boneName]->offsetMatrix = glm::inverse(offsetMatrix);

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

    Mesh loadedMesh = Mesh(vertices, indices, shaderName);
    return loadedMesh;
}

void Model::combineMeshes(const aiScene *scene, std::string shaderName)
{
    std::vector<Vertex> allVertices;
    std::vector<unsigned int> allIndices;

    unsigned int indexOffset = 0;

    for (unsigned int i = 0; i < scene->mNumMeshes; i++)
    {
        aiMesh *mesh = scene->mMeshes[i];

        std::vector<Vertex> meshVertices;
        std::vector<unsigned int> meshIndices;

        // Process meshes vertices
        for (unsigned int j = 0; j < mesh->mNumVertices; j++)
        {
            Vertex vertex;
            // Process vertex positions, normals, tangents, etc.
            glm::vec3 vector;
            vector.x = mesh->mVertices[j].x;
            vector.y = mesh->mVertices[j].y;
            vector.z = mesh->mVertices[j].z;
            vertex.Position = vector;

            vector.x = mesh->mNormals[j].x;
            vector.y = mesh->mNormals[j].y;
            vector.z = mesh->mNormals[j].z;
            vertex.Normal = vector;

            vector.x = mesh->mTangents[j].x;
            vector.y = mesh->mTangents[j].y;
            vector.z = mesh->mTangents[j].z;
            vertex.Tangent = vector;

            vector.x = mesh->mBitangents[j].x;
            vector.y = mesh->mBitangents[j].y;
            vector.z = mesh->mBitangents[j].z;
            vertex.Bitangent = vector;

            if (mesh->mTextureCoords[0]) // Texture coords
            {
                glm::vec2 vec;
                vec.x = mesh->mTextureCoords[0][j].x;
                vec.y = mesh->mTextureCoords[0][j].y;
                vertex.TexCoords = vec;
            }
            else
            {
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);
            }

            meshVertices.push_back(vertex);
        }

        // Process indices with offset
        for (unsigned int j = 0; j < mesh->mNumFaces; ++j)
        {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; ++k)
            {
                meshIndices.push_back(face.mIndices[k] + indexOffset);
            }
        }

        // Append the current mesh's vertices and indices to the combined lists
        allVertices.insert(allVertices.end(), meshVertices.begin(), meshVertices.end());
        allIndices.insert(allIndices.end(), meshIndices.begin(), meshIndices.end());

        // Process bone weights and update bone hierarchy
        for (unsigned int b = 0; b < mesh->mNumBones; ++b)
        {
            aiBone *bone = mesh->mBones[b];
            std::string boneName = bone->mName.C_Str();

            int boneIndex = boneHierarchy[boneName]->index;

            for (unsigned int w = 0; w < bone->mNumWeights; ++w)
            {
                aiVertexWeight weight = bone->mWeights[w];
                unsigned int globalVertexID = weight.mVertexId + indexOffset;
                float weightValue = weight.mWeight;

                for (int i = 0; i < 4; ++i)
                {
                    if (allVertices[globalVertexID].Weights[i] == 0.0f)
                    {
                        allVertices[globalVertexID].BoneIDs[i] = boneIndex;
                        allVertices[globalVertexID].Weights[i] = weightValue;
                        break;
                    }
                }
            }
        }

        // Update index offset
        indexOffset += meshVertices.size();
    }

    // Now create a single combined mesh
    Mesh combinedMesh = Mesh(allVertices, allIndices, shaderName);

    meshes.clear();
    meshes.push_back(combinedMesh); // Replace the old meshes with the combined one
}

std::vector<Texture> Model::loadMaterialTexture(aiMaterial *mat, aiTextureType type, std::string typeName)
{
    // Vector of textures to push to GPU
    std::vector<Texture> loadTextures;
    std::string textureName = findTextureInDirectory(directory, typeName);

    // If texture found
    if (!textureName.empty())
    {
        {
            // Check if texture already cached
            std::lock_guard<std::mutex> lock(textureCacheMutex);
            if (textureCache.find(textureName) != textureCache.end())
            {
                loadTextures.push_back(textureCache[textureName].texture);
                textureCache[textureName].refCount++;
                return loadTextures;
            }
        }

        {
            // Check if texture already pending
            std::lock_guard<std::mutex> lock(pendingTexturesMutex);
            if (pendingTextures.find(textureName) != pendingTextures.end())
            {
                // Another thread is already loading this texture
                return loadTextures;
            }

            // If not, add to pending
            pendingTextures.insert(textureName);
        }

        // Placeholder texture for model loading
        Texture placeholderTexture;
        placeholderTexture.id = 0;
        placeholderTexture.type = typeName;
        placeholderTexture.path = textureName;
        loadTextures.push_back(placeholderTexture);

        // Get texture location
        std::string filename = directory + '/' + textureName;
        int width, height, channels;
        unsigned char *data = stbi_load(filename.c_str(), &width, &height, &channels, 0);

        if (!data)
        {
            std::cerr << "Failed to load texture: " << filename << std::endl;
            {
                std::lock_guard<std::mutex> lock(pendingTexturesMutex);
                pendingTextures.erase(textureName);
            }
            return loadTextures;
        }

        // Save PendingTexture
        PendingTexture texture;
        texture.name = textureName;
        texture.width = width;
        texture.height = height;
        texture.channels = channels;
        texture.pixelData.assign(data, data + (width * height * channels));
        texture.typeName = typeName;
        texture.textureID = 0; // Placeholder ID

        // Unload data
        stbi_image_free(data);

        {
            // Push texture to queue
            std::lock_guard<std::mutex> lock(textureQueueMutex);
            textureQueue.push(std::move(texture));
        }
    }

    // If texture not found
    else
    {
        std::cout << "Failed to load " << typeName << " texture in " << directory << "\n";
    }

    return loadTextures;
}

void Model::processPendingTextures()
{
    std::lock_guard<std::mutex> lock(textureQueueMutex);
    while (!textureQueue.empty())
    {
        // Take first pending texture
        PendingTexture texture = std::move(textureQueue.front());
        textureQueue.pop();

        GLuint textureID;

        // Generate empty texture for ID and bind to it
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        GLenum format;
        if (texture.channels == 1)
            format = GL_RED;
        else if (texture.channels == 3)
            format = GL_RGB;
        else if (texture.channels == 4)
            format = GL_RGBA;

        // Upload texture properties to GL
        glTexImage2D(GL_TEXTURE_2D, 0, format, texture.width, texture.height, 0, format, GL_UNSIGNED_BYTE, texture.pixelData.data());

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Fix textureID in cache
        auto &cached = textureCache[texture.name];
        cached.texture = {textureID, texture.typeName, texture.name};

        // Remove texture from Pending
        {
            std::lock_guard<std::mutex> lock(pendingTexturesMutex);
            pendingTextures.erase(texture.name);
        }
    }
}

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

inline bool Model::ends_with(std::string const &value, std::string const &ending)
{
    if (ending.size() > value.size())
        return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

void Model::loadModelMap()
{
    const std::string path = "../" + modelMapPath;
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

    JSONModelMap jsonModelMap = jsoncons::decode_json<JSONModelMap>(file);

    // If model classified as yacht
    for (JSONModelMapData yacht : jsonModelMap.yachts)
    {
        modelMap[yacht.name] = std::make_pair(yacht.path, ModelType::yacht);
    }
    // If geneneric model
    for (JSONModelMapData model : jsonModelMap.models)
    {
        modelMap[model.name] = std::make_pair(model.path, ModelType::model);
    }
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

    // Gen and bind cubemap texture
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    // Load texture
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

    // Set texture parameters

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}

void Model::generateBoneTransforms()
{

    // Resize if matrix array not large enough
    if (boneTransforms.size() != boneHierarchy.size())
    {
        boneTransforms.resize(boneHierarchy.size(), glm::mat4(1.0f));
        boneOffsets.resize(boneHierarchy.size(), glm::mat4(1.0f));
        boneInverseOffsets.resize(boneHierarchy.size(), glm::mat4(1.0f));
    }

    for (auto &rootBone : rootBones)
    {
        // Start recursion from the root bone, with identity matrix for the root's parent transform
        generateBoneTransformsRecursive(rootBone);
    }
}

void Model::generateBoneTransformsRecursive(Bone *bone)
{
    // Check if bone in range
    if (bone->index < 0 || bone->index >= boneTransforms.size())
    {
        std::cerr << "Error: Bone index out of range: " << bone->index << ", with name: " << bone->name << std::endl;
        return;
    }

    boneInverseOffsets[bone->index] = glm::inverse(bone->offsetMatrix);

    // Recursively update the transforms of the child bones
    for (Bone *child : bone->children)
    {
        if (!child)
        {
            continue; // Prevents null pointer access
        }
        generateBoneTransformsRecursive(child);
    }
}

void Model::updateBoneTransforms()
{
    for (auto &rootBone : rootBones)
    {
        // Start recursion from the root bone, with identity matrix for the root's parent transform
        updateBoneTransformsRecursive(rootBone, glm::mat4(1.0f), glm::mat4(1.0f));
    }
}

void Model::updateBoneTransformsRecursive(Bone *bone, const glm::mat4 &parentTransform, const glm::mat4 &parentInverseOffset)
{
    // Check if bone in range
    if (bone->index < 0 || bone->index >= boneTransforms.size())
    {
        std::cerr << "Error: Bone index out of range: " << bone->index << ", with name: " << bone->name << std::endl;
        return;
    }

    // Update bone transform
    boneTransforms[bone->index] = parentTransform * parentInverseOffset * bone->offsetMatrix * bone->transform;

    // Recursively update the transforms of the child bones
    for (Bone *child : bone->children)
    {
        if (!child)
        {
            continue; // Prevents null pointer access
        }
        updateBoneTransformsRecursive(child, boneTransforms[bone->index], boneInverseOffsets[bone->index]);
    }
}

void Model::uploadToGPU()
{
    // Process all pending textures of model
    processPendingTextures();

    // Update texture IDs from loaded pending textures
    for (auto &texture : textures)
    {
        texture.id = textureCache[texture.path].texture.id;
    }

    // Upload data for each mesh to GPU
    for (auto &mesh : meshes)
    {
        mesh.uploadToGPU();
    }
}
