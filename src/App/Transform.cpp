#include "App/Transform.h"

#include <glm/gtc/matrix_transform.hpp>

glm::mat4 Transform::modelMatrix() const
{
    glm::mat4 model(1.0f);
    model = glm::translate(model, position);
    model = glm::rotate(model, rotationEuler.z, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, rotationEuler.y, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, rotationEuler.x, glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::scale(model, scale);
    return model;
}
