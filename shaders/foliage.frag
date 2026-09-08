#version 450

layout(location = 0) flat in vec3 vNormalWS;
layout(location = 1) in vec3 vWorldCurrent;
layout(location = 2) in vec3 vWorldPrevious;
layout(location = 3) flat in uint vMaterialId;
layout(location = 4) flat in uint vPrimitiveRole;
layout(location = 5) flat in float vMotionHeight;

layout(set = 0, binding = 0) uniform FrameUBO
{
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 viewProjUnjittered;
    mat4 prevViewProjUnjittered;
    mat4 prevViewProj;
    mat4 invViewProjUnjittered;
    vec4 cameraWorld;
    vec2 renderSize;
    vec2 invRenderSize;
} u;

struct PaletteEntry
{
    vec4 baseColor_alpha;
    vec4 pbr0;
    vec4 extra;
};

layout(std430, set = 1, binding = 1) readonly buffer PaletteBuffer
{
    PaletteEntry entries[];
} gPalette;

layout(push_constant) uniform FoliagePC
{
    vec4 timeWind;
    vec4 controls;
    vec4 secondaryWave;
    vec4 crossWave;
    vec4 appearance;
    vec4 surface;
    vec4 material;
    vec4 ambient;
} pc;

layout(location = 0) out vec4 oAlbedo;
layout(location = 1) out vec4 oNormal;
layout(location = 2) out vec4 oMaterial;
layout(location = 3) out vec4 oVelocity;
layout(location = 4) out vec2 oWaterMeta;

float palettePresentationStrength()
{
    float roleScale = 0.78;
    if (vPrimitiveRole == 2u) roleScale = 0.20;
    else if (vPrimitiveRole == 3u) roleScale = 0.55;
    else if (vPrimitiveRole == 0u) roleScale = 0.70;
    else if (vPrimitiveRole == 4u) roleScale = 0.48;
    else if (vPrimitiveRole == 5u) roleScale = 0.18;
    float meadow = clamp(pc.surface.w, 0.0, 1.0);
    if (meadow > 0.0 && vPrimitiveRole != 2u)
    {
        float meadowRoleScale = vPrimitiveRole == 3u ? 0.82 :
                                vPrimitiveRole == 0u ? 0.80 : 0.88;
        roleScale = mix(roleScale, meadowRoleScale, meadow);
    }
    return clamp(pc.appearance.x, 0.0, 1.0) * roleScale;
}

float paletteValueScale()
{
    if (vPrimitiveRole >= 4u) return vPrimitiveRole == 4u ? 0.96 : 1.04;
    if (vPrimitiveRole == 2u) return 1.0;
    float meadow = clamp(pc.surface.w, 0.0, 1.0);
    float valueScale = vPrimitiveRole == 3u ? mix(0.90, 0.82, meadow) :
        (vPrimitiveRole == 0u ? mix(0.86, 0.78, meadow) :
                               mix(0.88, 0.91, meadow));
    float pastel = clamp(pc.material.x, 0.0, 1.0);
    float pastelScale = vPrimitiveRole == 3u ? 0.96 :
                        vPrimitiveRole == 0u ? 0.92 : 0.98;
    return mix(valueScale, pastelScale, pastel);
}

float normalPresentationStrength()
{
    float roleScale = vPrimitiveRole == 2u ? 0.25 :
                      vPrimitiveRole == 3u ? 0.90 :
                      vPrimitiveRole == 4u ? 0.40 :
                      vPrimitiveRole == 5u ? 0.12 : 1.0;
    return clamp(pc.appearance.x, 0.0, 1.0) * roleScale;
}

float smoothUnit(float value)
{
    float clamped = clamp(value, 0.0, 1.0);
    return clamped * clamped * (3.0 - 2.0 * clamped);
}

vec3 botanicalVariantTint()
{
    uint variant = vMaterialId % 3u;
    if (variant == 0u) return vec3(0.84, 1.02, 0.98);
    if (variant == 1u) return vec3(0.91, 1.00, 0.87);
    return vec3(0.98, 1.03, 0.79);
}

vec3 meadowVariantTint()
{
    uint variant = vMaterialId % 3u;
    if (variant == 0u) return vec3(0.88, 1.04, 0.72);
    if (variant == 1u) return vec3(0.80, 1.01, 0.78);
    return vec3(0.94, 1.03, 0.68);
}

vec3 pastelVariantTint()
{
    // preserve all eight authored nature pond foliage materials as distinct
    // pastel families instead of folding them into the previous three colors.
    uint variant = vMaterialId % 8u;
    if (variant == 0u) return vec3(0.90, 1.03, 0.82); // sage
    if (variant == 1u) return vec3(0.82, 1.01, 0.94); // mint
    if (variant == 2u) return vec3(1.04, 1.07, 0.74); // yellow-green
    if (variant == 3u) return vec3(0.82, 0.94, 0.70); // moss
    if (variant == 4u) return vec3(0.92, 1.08, 0.78); // spring
    if (variant == 5u) return vec3(0.76, 0.98, 0.88); // cool fern
    if (variant == 6u) return vec3(0.98, 1.01, 0.70); // olive
    return vec3(0.80, 1.05, 0.98);                    // seafoam
}

float botanicalRoleValue()
{
    if (vPrimitiveRole == 3u) return 0.92;
    return vPrimitiveRole == 0u ? 0.86 : 1.0;
}

float meadowRoleValue()
{
    if (vPrimitiveRole == 3u) return 0.88;
    return vPrimitiveRole == 0u ? 0.80 : 1.0;
}

float botanicalFaceValue(vec3 faceNormal)
{
    vec3 normal = normalize(faceNormal);
    float top = max(normal.y, 0.0);
    float bottom = max(-normal.y, 0.0);
    float side = 1.0 - abs(normal.y);
    return 1.0 + clamp(pc.surface.z, 0.0, 1.0) *
        (0.04 * top - 0.10 * bottom - 0.06 * side);
}

vec3 softPaletteResponse(vec3 baseColor)
{
    float strength = palettePresentationStrength();
    if (strength <= 0.0)
    {
        return baseColor;
    }

    const vec3 lumaWeights = vec3(0.2126, 0.7152, 0.0722);
    const float pivot = 0.52;
    float luminance = dot(baseColor, lumaWeights);
    float compressedLuminance = pivot +
        (luminance - pivot) * clamp(pc.appearance.y, 0.0, 1.0);
    vec3 chroma = baseColor - vec3(luminance);
    vec3 softened = vec3(compressedLuminance) +
        chroma * clamp(pc.appearance.z, 0.0, 1.0);
    vec3 separated = softened * paletteValueScale();
    float identityStrength = vPrimitiveRole == 2u || vPrimitiveRole == 5u
        ? 0.0 : clamp(pc.surface.x, 0.0, 1.0);
    if (vPrimitiveRole == 4u) identityStrength *= 0.55;
    if (identityStrength > 0.0)
    {
        float meadow = clamp(pc.surface.w, 0.0, 1.0);
        float pastel = clamp(pc.material.x, 0.0, 1.0);
        float smoothHeight = smoothUnit(vMotionHeight);
        float heightValue = mix(
            mix(0.76, 1.04, smoothHeight),
            mix(0.68, 1.03, smoothHeight), meadow);
        heightValue = mix(
            heightValue, mix(0.88, 1.05, smoothHeight), pastel);
        vec3 variantTint = mix(
            botanicalVariantTint(), meadowVariantTint(), meadow);
        variantTint = mix(variantTint, pastelVariantTint(), pastel);
        float roleValue = mix(
            botanicalRoleValue(), meadowRoleValue(), meadow);
        vec3 offset = mix(vec3(-0.010, 0.0, 0.012),
                          vec3(0.006, 0.0, -0.012), meadow);
        vec3 botanical = separated * variantTint * roleValue * heightValue *
            botanicalFaceValue(vNormalWS) + offset;
        separated = mix(separated, botanical, identityStrength);
        separated += vec3(0.050, 0.070, 0.025) * pastel;
    }
    return clamp(mix(baseColor, separated, strength), 0.0, 1.0);
}

float botanicalRoughness(float baseRoughness)
{
    float identityStrength = clamp(pc.surface.x, 0.0, 1.0);
    if (identityStrength <= 0.0)
    {
        return baseRoughness;
    }
    float target = 0.74;
    if (vPrimitiveRole == 2u) target = 0.80;
    else if (vPrimitiveRole == 3u) target = 0.78;
    else if (vPrimitiveRole == 0u) target = 0.84;
    else if (vPrimitiveRole == 4u) target = 0.82;
    else if (vPrimitiveRole == 5u) target = 0.68;
    float blend = identityStrength * clamp(pc.surface.y, 0.0, 1.0);
    return clamp(mix(baseRoughness, target, blend), 0.04, 1.0);
}

vec3 softFaceNormal(vec3 faceNormal)
{
    vec3 normal = normalize(faceNormal);
    float strength = normalPresentationStrength();
    if (strength <= 0.0 || abs(normal.y) >= 0.5)
    {
        return normal;
    }
    normal.y += max(pc.appearance.w, 0.0) * strength;
    return normalize(normal);
}

void main()
{
    if (pc.controls.w > 0.5 && vWorldCurrent.y < pc.controls.z)
    {
        discard;
    }

    PaletteEntry material = gPalette.entries[vMaterialId];
    oAlbedo = vec4(softPaletteResponse(material.baseColor_alpha.rgb), 0.0);
    oNormal = vec4(softFaceNormal(vNormalWS) * 0.5 + 0.5, 1.0);
    oMaterial = vec4(botanicalRoughness(
                         clamp(material.pbr0.y, 0.04, 1.0)),
                     clamp(material.pbr0.x, 0.0, 1.0), 1.0,
                     clamp(material.pbr0.z, 0.0, 1.0));

    vec4 currentClip = u.viewProjUnjittered * vec4(vWorldCurrent, 1.0);
    vec4 previousClip = u.prevViewProjUnjittered * vec4(vWorldPrevious, 1.0);
    vec2 currentNdc = currentClip.xy / max(currentClip.w, 0.0001);
    vec2 previousNdc = previousClip.xy / max(previousClip.w, 0.0001);
    oVelocity = vec4((currentNdc - previousNdc) * 0.5, 0.02, 0.0);
    // the integer component remains the shared plant category. the bounded
    // fractional component opts instanced foliage into the V10 soft-lighting
    // response without adding a g-buffer attachment or affecting voxel plants.
    oWaterMeta = vec2(0.0, 3.0 + clamp(pc.material.x, 0.0, 1.0) * 0.25);
}
