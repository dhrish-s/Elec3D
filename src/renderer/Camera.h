#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    /// Orbit: called on left-mouse drag.
    /// dx and dy are delta pixels from the last frame.
    void onMouseDrag(float dx, float dy)
    {
        constexpr float MOUSE_SENSITIVITY = 0.1f;
        yaw += dx * MOUSE_SENSITIVITY;
        pitch += dy * MOUSE_SENSITIVITY;
        pitch = std::clamp(pitch, -89.0f, 89.0f);
    }

    /// Zoom: called on scroll wheel.
    /// delta is positive = zoom in, negative = zoom out.
    void onScroll(float delta)
    {
        fov -= delta;
        fov = std::clamp(fov, 10.0f, 90.0f);
    }

    /// Pan: called on middle-mouse-button drag.
    /// Translates the target point in the camera's local XY plane.
    void pan(float dx, float dy)
    {
        constexpr float PAN_SPEED = 0.005f;
        glm::mat4 view = getView();
        glm::vec3 right(view[0][0], view[1][0], view[2][0]);
        glm::vec3 up(view[0][1], view[1][1], view[2][1]);
        target += right * (-dx * PAN_SPEED);
        target += up * (dy * PAN_SPEED);
    }

    /// Returns the view matrix for this frame.
    glm::mat4 getView() const
    {
        return glm::lookAt(getPosition(), target, worldUp);
    }

    /// Returns the projection matrix for this frame.
    /// aspectRatio = windowWidth / windowHeight.
    glm::mat4 getProjection(float aspectRatio) const
    {
        return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    }

    glm::vec3 getPosition() const
    {
        float camX = radius * std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch));
        float camY = radius * std::sin(glm::radians(pitch));
        float camZ = radius * std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch));
        return target + glm::vec3(camX, camY, camZ);
    }

    glm::vec3 screenPosToWorldRay(float mouseX, float mouseY, float screenWidth, float screenHeight) const
    {
        float x = (2.0f * mouseX) / screenWidth - 1.0f;
        float y = 1.0f - (2.0f * mouseY) / screenHeight;
        glm::vec4 rayClip = glm::vec4(x, y, 1.0f, 1.0f);

        glm::vec4 rayEye = glm::inverse(getProjection(screenWidth / screenHeight)) * rayClip;
        rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);

        glm::vec3 rayWorld = glm::vec3(glm::inverse(getView()) * rayEye);
        return glm::normalize(rayWorld);
    }

    float getYaw() const
    {
        return yaw;
    }

    float getPitch() const
    {
        return pitch;
    }

private:
    float yaw = -90.0f;
    float pitch = 0.0f;
    float radius = 10.0f;
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
};
