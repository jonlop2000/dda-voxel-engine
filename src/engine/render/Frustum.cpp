#include "engine/render/Frustum.h"

#include <cmath>

namespace
{
glm::vec4 normalizePlane(const glm::vec4& p)
{
    const glm::vec3 n(p.x, p.y, p.z);
    const float len = std::sqrt(glm::dot(n, n));
    if (len <= 0.0f)
    {
        return p;
    }
    return p / len;
}
} // namespace

Frustum makeFrustum(const glm::mat4& viewProj)
{
    Frustum out{};

    // glm is column-major, so viewProj[col][row]
    // extract rows for Gribb/Hartmann plane extraction
    const glm::vec4 row0(viewProj[0][0], viewProj[1][0], viewProj[2][0], viewProj[3][0]);
    const glm::vec4 row1(viewProj[0][1], viewProj[1][1], viewProj[2][1], viewProj[3][1]);
    const glm::vec4 row2(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);
    const glm::vec4 row3(viewProj[0][3], viewProj[1][3], viewProj[2][3], viewProj[3][3]);

    out.planes[0] = normalizePlane(row3 + row0); // left
    out.planes[1] = normalizePlane(row3 - row0); // right
    out.planes[2] = normalizePlane(row3 + row1); // bottom
    out.planes[3] = normalizePlane(row3 - row1); // top
    out.planes[4] = normalizePlane(row3 + row2); // near
    out.planes[5] = normalizePlane(row3 - row2); // far

    return out;
}

bool aabbInFrustum(const Frustum& frustum, const glm::vec3& min, const glm::vec3& max)
{
    for (const auto& plane : frustum.planes)
    {
        glm::vec3 p = min;
        if (plane.x >= 0.0f)
        {
            p.x = max.x;
        }
        if (plane.y >= 0.0f)
        {
            p.y = max.y;
        }
        if (plane.z >= 0.0f)
        {
            p.z = max.z;
        }

        const float d = glm::dot(glm::vec3(plane), p) + plane.w;
        if (d < 0.0f)
        {
            return false;
        }
    }

    return true;
}
