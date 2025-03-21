#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>

class Camera
{
public:
    // Global Camera Variables
    static glm::vec3 worldUp, cameraPositionFree, cameraPosition, cameraViewDirection, cameraRight, cameraUp;
    static float yawFree, pitchFree, rollFree;
    static float yaw, pitch, roll;
    static float yawOffset, pitchOffset, rollOffset;

    static glm::mat4 u_view;
    static glm::mat4 u_projection;
    static glm::mat4 u_camXY;

    static void update();
    static void reset();
    static void setCamDirection(glm::vec3 rotation);
    static void genViewMatrix(glm::vec3 position);
    static void genProjectionMatrix();
    static bool cameraMoved;
    static bool freeCam;

    static glm::vec3 getPosition();
    static glm::vec3 getRotation();
};

#endif