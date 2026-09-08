// shared air-atmosphere math for cross-composite haze.
// mirrors the cpu reference in src/engine/render/SceneAtmosphere.cpp exactly;
// keep the two implementations in lockstep. lighting, water, glass, and the final
// composite consume it; density <= 0 must remain a mathematical no-op.
//
// parameter block (push constants or ubo members, stage-specific):
//   atmoDensity        extinction at atmoBaseHeight, per meter of air path
//   atmoHeightFalloff  exponential falloff with height, per meter
//   atmoBaseHeight     world height of nominal density
//   atmoSunPhaseStrength  0..1 authority of the sun-facing tint
//   atmoSunPhaseExponent  sharpness of the sun-facing lobe (>= 1)

const float kAtmoVerticalEpsilon = 1e-5;

float sceneAtmosphereOpticalDepth(float density, float heightFalloff, float baseHeight,
                                  float originHeight, float directionY,
                                  float lengthMeters)
{
    if (density <= 0.0 || lengthMeters <= 0.0)
    {
        return 0.0;
    }

    float densityAtOrigin = density * exp(-heightFalloff * (originHeight - baseHeight));
    float verticalRate = heightFalloff * directionY;
    if (abs(verticalRate) < kAtmoVerticalEpsilon)
    {
        return densityAtOrigin * lengthMeters;
    }
    return densityAtOrigin * (1.0 - exp(-verticalRate * lengthMeters)) / verticalRate;
}

vec3 sceneAtmosphereInscatterTint(float sunPhaseStrength, float sunPhaseExponent,
                                  vec3 viewDirection, vec3 sunDirection, vec3 skyTint,
                                  vec3 sunTint)
{
    float cosine = dot(normalize(viewDirection), normalize(sunDirection));
    float lobe = pow(clamp((cosine + 1.0) * 0.5, 0.0, 1.0), max(sunPhaseExponent, 1.0));
    float blend = clamp(sunPhaseStrength, 0.0, 1.0) * lobe;
    return mix(skyTint, sunTint, blend);
}

float sceneAtmosphereSkyOpticalDepth(float density, float heightFalloff, float baseHeight,
                                     float originHeight, float directionY)
{
    if (density <= 0.0)
    {
        return 0.0;
    }

    float verticalRate = heightFalloff * directionY;
    if (verticalRate <= kAtmoVerticalEpsilon)
    {
        // a large finite value avoids infinity arithmetic while producing exact
        // zero transmittance under exp(-opticalDepth).
        return 1.0e20;
    }
    float densityAtOrigin = density * exp(-heightFalloff * (originHeight - baseHeight));
    return densityAtOrigin / verticalRate;
}

void evaluateSceneAtmosphereFromOpticalDepth(
    float density, float sunPhaseStrength, float sunPhaseExponent,
    float opticalDepth, vec3 viewDirection, vec3 sunDirection, vec3 skyTint,
    vec3 sunTint, out float transmittance, out vec3 inscatter)
{
    transmittance = 1.0;
    inscatter = vec3(0.0);
    if (density <= 0.0 || opticalDepth <= 0.0)
    {
        return;
    }

    transmittance = exp(-opticalDepth);
    vec3 tint = sceneAtmosphereInscatterTint(sunPhaseStrength, sunPhaseExponent,
                                             viewDirection, sunDirection, skyTint,
                                             sunTint);
    inscatter = tint * (1.0 - transmittance);
}

// one in-air segment. the caller owns path-length policy: air extinction applies
// only to the camera-to-surface distance that is in air; water absorption owns the
// underwater segment; glass/transmission receives atmosphere once per path stage.
// compose as: surfaceColor * transmittance + inscatter.
void evaluateSceneAtmosphere(float density, float heightFalloff, float baseHeight,
                             float sunPhaseStrength, float sunPhaseExponent,
                             vec3 segmentOrigin, vec3 viewDirection, float lengthMeters,
                             vec3 sunDirection, vec3 skyTint, vec3 sunTint,
                             out float transmittance, out vec3 inscatter)
{
    transmittance = 1.0;
    inscatter = vec3(0.0);
    if (density <= 0.0 || lengthMeters <= 0.0)
    {
        return;
    }

    vec3 direction = normalize(viewDirection);
    float opticalDepth = sceneAtmosphereOpticalDepth(density, heightFalloff, baseHeight,
                                                     segmentOrigin.y, direction.y,
                                                     lengthMeters);
    evaluateSceneAtmosphereFromOpticalDepth(
        density, sunPhaseStrength, sunPhaseExponent, opticalDepth, direction,
        sunDirection, skyTint, sunTint, transmittance, inscatter);
}

// Sky/horizon variant: the length -> infinity limit, so far geometry and the sky
// background converge on the same haze. horizontal and downward rays fully
// converge to the inscatter tint.
void evaluateSceneAtmosphereSky(float density, float heightFalloff, float baseHeight,
                                float sunPhaseStrength, float sunPhaseExponent,
                                vec3 cameraPosition, vec3 viewDirection,
                                vec3 sunDirection, vec3 skyTint, vec3 sunTint,
                                out float transmittance, out vec3 inscatter)
{
    transmittance = 1.0;
    inscatter = vec3(0.0);
    if (density <= 0.0)
    {
        return;
    }

    vec3 direction = normalize(viewDirection);
    float opticalDepth = sceneAtmosphereSkyOpticalDepth(
        density, heightFalloff, baseHeight, cameraPosition.y, direction.y);
    evaluateSceneAtmosphereFromOpticalDepth(
        density, sunPhaseStrength, sunPhaseExponent, opticalDepth, direction,
        sunDirection, skyTint, sunTint, transmittance, inscatter);
}

// the transparent water pass receives a refraction base already atmosphered by
// lighting. keep that base fixed and attenuate only the surface-added delta.
// no inscatter is added here because it is already present in the base path.
vec3 attenuateSceneAtmosphereSurfaceContribution(
    float density, float opticalDepth, vec3 alreadyAtmospheredBase,
    vec3 surfaceCompositedColor)
{
    if (density <= 0.0 || opticalDepth <= 0.0)
    {
        return surfaceCompositedColor;
    }

    float transmittance = exp(-opticalDepth);
    return alreadyAtmospheredBase +
           (surfaceCompositedColor - alreadyAtmospheredBase) * transmittance;
}

// procedural sky emission is layered over an already-atmosphered sky. apply only
// extinction along the resolved air path; adding inscatter here would double the
// sky term that lighting already owns.
vec3 attenuateSceneAtmosphereSkyEmission(
    float density, float opticalDepth, vec3 emission)
{
    if (density <= 0.0 || opticalDepth <= 0.0)
    {
        return emission;
    }

    return emission * exp(-opticalDepth);
}
