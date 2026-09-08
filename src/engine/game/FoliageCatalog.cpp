#include "engine/game/FoliageCatalog.h"

#include <algorithm>
#include <cmath>

#include "engine/game/PlaceableTransform.h"
#include "engine/scene/AquariumScene.h"
#include "Utils/SpatialHash.h"
#include "engine/voxel/VoxelBuilder.h"

namespace
{
constexpr uint32_t kFoliagePrototypeVersion = 1;
const glm::ivec3 kHeroFoliageDims{16, 18, 16};
const glm::vec3 kHeroFoliageScale{0.20f, 0.30f, 0.20f};

enum class HeroFoliageStyle
{
    Eelgrass,
    ForkedSprig,
    RibbonKelp,
    BudCluster,
    RedLudwigia,
    GoldenCrypt,
    TealFanwort,
};

enum class PlantTone
{
    DeepStem,
    MidLeaf,
    MintTip,
    BlueGreen,
    Shadow,
    CopperStem,
    RoseLeaf,
    GoldenLeaf,
    TealLeaf,
};

uint8_t plantBandIdForTone(int x, int y, int z, int baseY, int height, uint32_t seed,
                           PlantTone tone)
{
    const float t = std::clamp(
        static_cast<float>(y - baseY) / static_cast<float>(std::max(1, height)), 0.0f,
        1.0f);
    const int noise =
        static_cast<int>(SpatialHash::hash3D(x + static_cast<int>(seed & 0xffu), y,
                                             z + static_cast<int>((seed >> 8u) & 0xffu)) %
                         2u);

    uint8_t base = AquariumMaterial::PlantBand::Base;
    uint8_t count = AquariumMaterial::PlantBand::Count;
    int index = 0;
    switch (tone)
    {
    case PlantTone::DeepStem:
        index = std::clamp(static_cast<int>(t * 4.0f) + noise, 0, 5);
        break;
    case PlantTone::MidLeaf:
        index = std::clamp(2 + static_cast<int>(t * 5.0f) + noise, 2, 8);
        break;
    case PlantTone::MintTip:
        index = std::clamp(5 + static_cast<int>(t * 5.0f) + noise, 5, 10);
        break;
    case PlantTone::BlueGreen:
        index = (t > 0.62f && (noise != 0))
                    ? 11
                    : std::clamp(1 + static_cast<int>(t * 5.0f), 1, 7);
        break;
    case PlantTone::Shadow:
        index = std::clamp(static_cast<int>(t * 3.0f) + noise, 0, 4);
        break;
    case PlantTone::CopperStem:
        base = AquariumMaterial::FoliageAccentBand::Base;
        count = AquariumMaterial::FoliageAccentBand::Count;
        index = std::clamp(static_cast<int>(t * 5.0f) + noise, 0, 7);
        break;
    case PlantTone::RoseLeaf:
        base = AquariumMaterial::FoliageAccentBand::Base;
        count = AquariumMaterial::FoliageAccentBand::Count;
        index = std::clamp(4 + static_cast<int>(t * 6.0f) + noise, 4, 11);
        break;
    case PlantTone::GoldenLeaf:
        base = AquariumMaterial::GrassBand::Base;
        count = AquariumMaterial::GrassBand::Count;
        index = std::clamp(3 + static_cast<int>(t * 6.0f) + noise, 3, 11);
        break;
    case PlantTone::TealLeaf:
        base = AquariumMaterial::AlgaeBand::Base;
        count = AquariumMaterial::AlgaeBand::Count;
        index = std::clamp(4 + static_cast<int>(t * 6.0f) + noise, 4, 11);
        break;
    }

    return static_cast<uint8_t>(base + std::clamp(index, 0, static_cast<int>(count) - 1));
}

std::vector<uint8_t> buildHeroFoliageVolume(const glm::ivec3& dims, uint32_t variation,
                                            HeroFoliageStyle style)
{
    engine::VoxelBuilder builder(dims);

    auto setPlantVoxel = [&](int x, int y, int z, int baseY, int height, uint32_t seed,
                             PlantTone tone) {
        if (x >= 0 && x < dims.x && y >= 0 && y < dims.y && z >= 0 && z < dims.z)
        {
            builder.setVoxel(x, y, z,
                             plantBandIdForTone(x, y, z, baseY, height, seed, tone));
        }
    };

    auto setConnectedPlantVoxel = [&](int fromX, int fromZ, int x, int y, int z, int baseY,
                                      int height, uint32_t seed, PlantTone tone) {
        int cx = fromX;
        int cz = fromZ;
        setPlantVoxel(cx, y, cz, baseY, height, seed, tone);
        while (cx != x)
        {
            cx += (x > cx) ? 1 : -1;
            setPlantVoxel(cx, y, cz, baseY, height, seed, tone);
        }
        while (cz != z)
        {
            cz += (z > cz) ? 1 : -1;
            setPlantVoxel(cx, y, cz, baseY, height, seed, tone);
        }
    };

    struct StemPoint
    {
        int x;
        int y;
        int z;
    };

    auto stemPointAt = [&](int baseX, int baseZ, int dy, int leanX, int leanZ,
                           uint32_t seed) {
        const int bend = (dy >= 3 ? 1 : 0) + (dy >= 7 ? 1 : 0);
        const int smallWiggle =
            dy >= 4 ? static_cast<int>((SpatialHash::hash3D(baseX + dy, baseZ,
                                                             static_cast<int>(seed)) >>
                                        3u) %
                                       3u) -
                          1
                    : 0;
        return StemPoint{baseX + leanX * bend + (leanZ != 0 ? smallWiggle : 0), dy,
                         baseZ + leanZ * bend + (leanX != 0 ? smallWiggle : 0)};
    };

    auto addStem = [&](int baseX, int baseZ, int height, int leanX, int leanZ, uint32_t seed,
                       PlantTone stemTone, PlantTone leafTone, int branchEvery) {
        const int stemHeight = std::clamp(height, 4, dims.y - 1);
        const uint32_t stemHash = SpatialHash::hash3D(static_cast<int>(seed), baseX, baseZ);
        StemPoint previous{baseX, 0, baseZ};
        bool hasPrevious = false;
        for (int dy = 0; dy < stemHeight; ++dy)
        {
            const StemPoint point = stemPointAt(baseX, baseZ, dy, leanX, leanZ, seed);
            const PlantTone tone = dy >= stemHeight - 1 ? leafTone : stemTone;
            if (hasPrevious)
            {
                setConnectedPlantVoxel(previous.x, previous.z, point.x, point.y, point.z, 0,
                                       stemHeight, seed, tone);
            }
            else
            {
                setPlantVoxel(point.x, point.y, point.z, 0, stemHeight, seed, tone);
            }

            if (branchEvery > 0 && dy >= 3 && dy <= stemHeight - 2 &&
                ((dy + static_cast<int>(stemHash)) % branchEvery) == 0)
            {
                const int side = ((stemHash >> (dy & 7)) & 1u) != 0u ? 1 : -1;
                const bool branchOnX = std::abs(leanZ) >= std::abs(leanX);
                setPlantVoxel(point.x + (branchOnX ? side : 0), point.y,
                              point.z + (branchOnX ? 0 : side), 0, stemHeight,
                              seed + static_cast<uint32_t>(dy) * 11u, leafTone);
            }

            previous = point;
            hasPrevious = true;
        }
        return previous;
    };

    auto addRootSprouts = [&](uint32_t seed, int count, PlantTone tone) {
        for (int sprout = 0; sprout < count; ++sprout)
        {
            const uint32_t sproutHash =
                SpatialHash::hash3D(static_cast<int>(seed), sprout * 19, 7);
            const int x = static_cast<int>((sproutHash >> 2u) % 4u);
            const int z = static_cast<int>((sproutHash >> 6u) % 4u);
            const int height = 2 + static_cast<int>((sproutHash >> 10u) % 3u);
            const int leanX = static_cast<int>((sproutHash >> 14u) % 2u);
            const int leanZ = static_cast<int>((sproutHash >> 17u) % 2u);
            addStem(x, z, height, leanX, leanZ, seed + static_cast<uint32_t>(sprout) * 23u,
                    tone, PlantTone::MidLeaf, 0);
        }
    };

    auto addEelgrass = [&](uint32_t seed) {
        const int bladeCount = 3 + static_cast<int>(variation % 2u);
        for (int blade = 0; blade < bladeCount; ++blade)
        {
            const uint32_t bladeHash =
                SpatialHash::hash3D(static_cast<int>(seed), blade * 31, 11);
            const int baseX = static_cast<int>((bladeHash >> 2u) % 3u);
            const int baseZ = static_cast<int>((bladeHash >> 6u) % 3u);
            const int height = 10 + static_cast<int>((bladeHash >> 10u) % 5u);
            const int leanX = 1 + static_cast<int>((bladeHash >> 14u) % 2u);
            const int leanZ = static_cast<int>((bladeHash >> 17u) % 3u) - 1;
            addStem(baseX, baseZ, height, leanX, leanZ,
                    seed + static_cast<uint32_t>(blade) * 43u, PlantTone::BlueGreen,
                    PlantTone::MintTip, 0);
        }
        addRootSprouts(seed + 97u, 2, PlantTone::Shadow);
    };

    auto addForkedSprig = [&](uint32_t seed) {
        addRootSprouts(seed + 17u, 3 + static_cast<int>(variation % 2u),
                       PlantTone::DeepStem);

        const int mainHeight = 8 + static_cast<int>((variation * 3u) % 4u);
        addStem(0, 0, mainHeight, 1, 0, seed + 31u, PlantTone::DeepStem,
                PlantTone::MintTip, 4);
        addStem(1, 1, std::max(5, mainHeight - 2), 0, 1, seed + 47u,
                PlantTone::MidLeaf, PlantTone::MintTip, 3);
        if ((variation & 1u) == 0u)
        {
            addStem(0, 2, std::max(5, mainHeight - 3), 1, 1, seed + 59u,
                    PlantTone::DeepStem, PlantTone::MidLeaf, 4);
        }
    };

    auto addRibbonKelp = [&](uint32_t seed) {
        const uint32_t kelpHash = SpatialHash::hash3D(static_cast<int>(seed), 3, 7);
        const int height = 9 + static_cast<int>((kelpHash >> 5u) % 4u);
        const int leanX = 1 + static_cast<int>((kelpHash >> 9u) % 2u);
        const int leanZ = static_cast<int>((kelpHash >> 13u) % 3u) - 1;
        addStem(2, 0, height, leanX, leanZ, seed + 31u, PlantTone::DeepStem,
                PlantTone::BlueGreen, 0);

        for (int dy = 3; dy < height - 1; dy += 2)
        {
            const uint32_t leafHash = SpatialHash::hash3D(static_cast<int>(seed), dy, height);
            const int side = ((leafHash & 1u) != 0u) ? 1 : -1;
            const int reach = 1 + static_cast<int>((leafHash >> 3u) % 2u);
            const StemPoint stem = stemPointAt(2, 0, dy, leanX, leanZ, seed + 31u);
            const bool leafOnX = std::abs(leanZ) >= std::abs(leanX);
            for (int step = 1; step <= reach; ++step)
            {
                setPlantVoxel(stem.x + (leafOnX ? side * step : 0), stem.y,
                              stem.z + (leafOnX ? 0 : side * step), 0, height,
                              seed + static_cast<uint32_t>(dy * 17 + step),
                              step == reach ? PlantTone::MintTip : PlantTone::BlueGreen);
            }
        }

        addRootSprouts(seed + 83u, 2, PlantTone::Shadow);
    };

    auto addBudCluster = [&](uint32_t seed) {
        addRootSprouts(seed + 29u, 4, PlantTone::DeepStem);
        const int stemCount = 4 + static_cast<int>((variation + 1u) % 2u);
        for (int stem = 0; stem < stemCount; ++stem)
        {
            const uint32_t stemHash =
                SpatialHash::hash3D(static_cast<int>(seed), stem * 29, 5);
            const int baseX = static_cast<int>((stemHash >> 2u) % 4u);
            const int baseZ = static_cast<int>((stemHash >> 6u) % 4u);
            const int height = 4 + static_cast<int>((stemHash >> 10u) % 4u);
            const int leanX = static_cast<int>((stemHash >> 14u) % 2u);
            const int leanZ = static_cast<int>((stemHash >> 17u) % 2u);
            const uint32_t stemSeed = seed + static_cast<uint32_t>(stem) * 53u;
            const StemPoint end =
                addStem(baseX, baseZ, height, leanX, leanZ, stemSeed, PlantTone::MidLeaf,
                        PlantTone::MintTip, 0);

            setPlantVoxel(end.x + 1, end.y, end.z, 0, height, stemSeed + 7u,
                          PlantTone::MintTip);
            if ((stemHash & 3u) == 0u)
            {
                setPlantVoxel(end.x, end.y, end.z + 1, 0, height, stemSeed + 13u,
                              PlantTone::BlueGreen);
            }
        }
    };

    auto addRedLudwigia = [&](uint32_t seed) {
        addRootSprouts(seed + 11u, 3 + static_cast<int>(variation % 2u),
                       PlantTone::CopperStem);
        const int stemCount = 3 + static_cast<int>((variation + 1u) % 2u);
        for (int stem = 0; stem < stemCount; ++stem)
        {
            const uint32_t stemHash =
                SpatialHash::hash3D(static_cast<int>(seed), stem * 41, 19);
            const int baseX = static_cast<int>((stemHash >> 2u) % 4u);
            const int baseZ = static_cast<int>((stemHash >> 7u) % 4u);
            const int height = 7 + static_cast<int>((stemHash >> 12u) % 5u);
            const int leanX = static_cast<int>((stemHash >> 17u) % 3u) - 1;
            const int leanZ = 1 + static_cast<int>((stemHash >> 21u) % 2u);
            const StemPoint end =
                addStem(baseX, baseZ, height, leanX, leanZ,
                        seed + static_cast<uint32_t>(stem) * 61u,
                        PlantTone::CopperStem, PlantTone::RoseLeaf, 3);

            setPlantVoxel(end.x + 1, end.y, end.z, 0, height, seed + stem * 67u,
                          PlantTone::RoseLeaf);
            setPlantVoxel(end.x, end.y, end.z - 1, 0, height, seed + stem * 71u,
                          PlantTone::RoseLeaf);
        }
    };

    auto addGoldenCrypt = [&](uint32_t seed) {
        addRootSprouts(seed + 23u, 4, PlantTone::GoldenLeaf);
        const int leafCount = 5 + static_cast<int>(variation % 2u);
        for (int leaf = 0; leaf < leafCount; ++leaf)
        {
            const uint32_t leafHash =
                SpatialHash::hash3D(static_cast<int>(seed), leaf * 37, 31);
            const int baseX = 1 + static_cast<int>((leafHash >> 2u) % 3u);
            const int baseZ = 1 + static_cast<int>((leafHash >> 6u) % 3u);
            const int height = 4 + static_cast<int>((leafHash >> 10u) % 3u);
            const int leanX = static_cast<int>((leafHash >> 14u) % 3u) - 1;
            const int leanZ = static_cast<int>((leafHash >> 18u) % 3u) - 1;
            const StemPoint end =
                addStem(baseX, baseZ, height, leanX, leanZ,
                        seed + static_cast<uint32_t>(leaf) * 47u,
                        PlantTone::DeepStem, PlantTone::GoldenLeaf, 0);
            setPlantVoxel(end.x + leanZ, end.y, end.z + leanX, 0, height,
                          seed + static_cast<uint32_t>(leaf) * 53u,
                          PlantTone::GoldenLeaf);
        }
    };

    auto addTealFanwort = [&](uint32_t seed) {
        addRootSprouts(seed + 13u, 3, PlantTone::TealLeaf);
        const int stemCount = 3 + static_cast<int>(variation % 2u);
        for (int stem = 0; stem < stemCount; ++stem)
        {
            const uint32_t stemHash =
                SpatialHash::hash3D(static_cast<int>(seed), stem * 43, 29);
            const int baseX = static_cast<int>((stemHash >> 2u) % 4u);
            const int baseZ = static_cast<int>((stemHash >> 6u) % 4u);
            const int height = 6 + static_cast<int>((stemHash >> 10u) % 5u);
            const int leanX = static_cast<int>((stemHash >> 15u) % 3u) - 1;
            const int leanZ = static_cast<int>((stemHash >> 19u) % 3u) - 1;
            addStem(baseX, baseZ, height, leanX, leanZ,
                    seed + static_cast<uint32_t>(stem) * 59u,
                    PlantTone::BlueGreen, PlantTone::TealLeaf, 2);
        }
    };

    const uint32_t seed = 901u + variation * 83u;
    switch (style)
    {
    case HeroFoliageStyle::Eelgrass:
        addEelgrass(seed);
        break;
    case HeroFoliageStyle::ForkedSprig:
        addForkedSprig(seed);
        break;
    case HeroFoliageStyle::RibbonKelp:
        addRibbonKelp(seed);
        break;
    case HeroFoliageStyle::BudCluster:
        addBudCluster(seed);
        break;
    case HeroFoliageStyle::RedLudwigia:
        addRedLudwigia(seed);
        break;
    case HeroFoliageStyle::GoldenCrypt:
        addGoldenCrypt(seed);
        break;
    case HeroFoliageStyle::TealFanwort:
        addTealFanwort(seed);
        break;
    }

    return std::move(builder.data());
}

std::vector<uint8_t> buildEelgrassVolume(const glm::ivec3& dims, uint32_t seed)
{
    return buildHeroFoliageVolume(dims, seed, HeroFoliageStyle::Eelgrass);
}

std::vector<uint8_t> buildForkedSprigVolume(const glm::ivec3& dims, uint32_t seed)
{
    return buildHeroFoliageVolume(dims, seed, HeroFoliageStyle::ForkedSprig);
}

std::vector<uint8_t> buildRibbonKelpVolume(const glm::ivec3& dims, uint32_t seed)
{
    return buildHeroFoliageVolume(dims, seed, HeroFoliageStyle::RibbonKelp);
}

std::vector<uint8_t> buildBudClusterVolume(const glm::ivec3& dims, uint32_t seed)
{
    return buildHeroFoliageVolume(dims, seed, HeroFoliageStyle::BudCluster);
}

std::vector<uint8_t> buildRedLudwigiaVolume(const glm::ivec3& dims, uint32_t seed)
{
    return buildHeroFoliageVolume(dims, seed, HeroFoliageStyle::RedLudwigia);
}

std::vector<uint8_t> buildGoldenCryptVolume(const glm::ivec3& dims, uint32_t seed)
{
    return buildHeroFoliageVolume(dims, seed, HeroFoliageStyle::GoldenCrypt);
}

std::vector<uint8_t> buildTealFanwortVolume(const glm::ivec3& dims, uint32_t seed)
{
    return buildHeroFoliageVolume(dims, seed, HeroFoliageStyle::TealFanwort);
}

PlaceablePrototype makeFoliagePlaceable(const char* slug, const glm::vec3& scaleMultiplier,
                                        PlaceableVoxelBuilder builder)
{
    PlaceablePrototype prototype{};
    prototype.slug = slug;
    prototype.version = kFoliagePrototypeVersion;
    prototype.capabilities.render = PlaceableRenderCapability::VoxelVolume;
    prototype.capabilities.behavior = PlaceableBehaviorCapability::AnimatedFoliage;
    prototype.capabilities.interaction =
        PlaceableInteractionCapability::Selectable | PlaceableInteractionCapability::Removable;
    prototype.placement.allowedMaterialCategories = PlaceableMaterialCategory::Substrate;
    prototype.placement.minWaterDepth = 0.05f;
    prototype.placement.maxSlopeDegrees = 35.0f;
    prototype.placement.footprintRadius = 1.55f;
    prototype.voxelDims = kHeroFoliageDims;
    prototype.voxelScale = kHeroFoliageScale * scaleMultiplier;
    prototype.buildVoxelVolume = builder;
    return prototype;
}

const std::vector<FoliagePrototype>& foliagePrototypes()
{
    static const std::vector<FoliagePrototype> prototypes = {
        {makeFoliagePlaceable("eelgrass", {0.88f, 1.18f, 0.88f}, buildEelgrassVolume),
         {0.052f, 0.32f, 0.42f}},
        {makeFoliagePlaceable("ribbon_kelp", {0.96f, 1.08f, 0.96f},
                              buildRibbonKelpVolume),
         {0.044f, 0.38f, 0.36f}},
        {makeFoliagePlaceable("bud_cluster", {1.06f, 0.86f, 1.06f}, buildBudClusterVolume),
         {0.027f, 0.50f, 0.18f}},
        {makeFoliagePlaceable("forked_sprig", {1.0f, 1.0f, 1.0f},
                              buildForkedSprigVolume),
         {0.038f, 0.43f, 0.28f}},
        {makeFoliagePlaceable("red_ludwigia", {0.92f, 1.02f, 0.92f},
                              buildRedLudwigiaVolume),
         {0.035f, 0.46f, 0.26f}},
        {makeFoliagePlaceable("golden_crypt", {1.10f, 0.78f, 1.10f},
                              buildGoldenCryptVolume),
         {0.026f, 0.54f, 0.16f}},
        {makeFoliagePlaceable("teal_fanwort", {1.0f, 0.96f, 1.0f},
                              buildTealFanwortVolume),
         {0.042f, 0.48f, 0.32f}},
    };
    return prototypes;
}

PlaceableInstance makeHeroInstance(const char* uuid, const char* slug,
                                   const glm::vec3& rootWorld, uint32_t seed)
{
    PlaceableInstance instance{};
    instance.uuid = uuid;
    instance.prototypeSlug = slug;
    instance.prototypeVersion = kFoliagePrototypeVersion;
    instance.position = rootWorld;
    instance.seed = seed;
    return instance;
}
} // namespace

namespace FoliageCatalog
{
const FoliagePrototype* findPrototype(std::string_view slug, uint32_t version)
{
    const std::vector<FoliagePrototype>& prototypes = foliagePrototypes();
    for (const FoliagePrototype& prototype : prototypes)
    {
        if (prototype.placeable.slug == slug && prototype.placeable.version == version)
        {
            return &prototype;
        }
    }
    return nullptr;
}

std::span<const FoliagePrototype> prototypes()
{
    return foliagePrototypes();
}

const std::vector<PlaceableInstance>& defaultAquariumHeroFoliageInstances()
{
    static const std::vector<PlaceableInstance> instances = {
        makeHeroInstance("00000000-0000-4000-8000-000000000001", "eelgrass",
                         {-20.2f, 5.03f, 8.2f}, 0u),
        makeHeroInstance("00000000-0000-4000-8000-000000000002", "bud_cluster",
                         {-18.0f, 5.03f, -6.8f}, 1u),
        makeHeroInstance("00000000-0000-4000-8000-000000000003", "ribbon_kelp",
                         {-14.4f, 5.03f, 2.2f}, 2u),
        makeHeroInstance("00000000-0000-4000-8000-000000000004", "forked_sprig",
                         {-11.5f, 5.03f, 10.5f}, 3u),
        makeHeroInstance("00000000-0000-4000-8000-000000000005", "ribbon_kelp",
                         {-8.0f, 5.03f, -8.6f}, 4u),
        makeHeroInstance("00000000-0000-4000-8000-000000000006", "eelgrass",
                         {-4.2f, 5.03f, 4.0f}, 5u),
        makeHeroInstance("00000000-0000-4000-8000-000000000007", "forked_sprig",
                         {-2.2f, 5.03f, -11.0f}, 6u),
        makeHeroInstance("00000000-0000-4000-8000-000000000008", "bud_cluster",
                         {-0.8f, 5.03f, 11.0f}, 7u),
        makeHeroInstance("00000000-0000-4000-8000-000000000009", "eelgrass",
                         {2.8f, 5.03f, -5.5f}, 8u),
        makeHeroInstance("00000000-0000-4000-8000-00000000000a", "ribbon_kelp",
                         {5.8f, 5.03f, 8.8f}, 9u),
        makeHeroInstance("00000000-0000-4000-8000-00000000000b", "bud_cluster",
                         {8.8f, 5.03f, 0.2f}, 10u),
        makeHeroInstance("00000000-0000-4000-8000-00000000000c", "forked_sprig",
                         {11.6f, 5.03f, 6.4f}, 11u),
        makeHeroInstance("00000000-0000-4000-8000-00000000000d", "ribbon_kelp",
                         {14.7f, 5.03f, -6.4f}, 12u),
        makeHeroInstance("00000000-0000-4000-8000-00000000000e", "bud_cluster",
                         {17.2f, 5.03f, -9.2f}, 13u),
        makeHeroInstance("00000000-0000-4000-8000-00000000000f", "eelgrass",
                         {18.8f, 5.03f, 2.8f}, 14u),
        makeHeroInstance("00000000-0000-4000-8000-000000000010", "forked_sprig",
                         {20.2f, 5.03f, 10.0f}, 15u),
        makeHeroInstance("00000000-0000-4000-8000-000000000011", "red_ludwigia",
                         {-21.6f, 5.03f, -11.6f}, 16u),
        makeHeroInstance("00000000-0000-4000-8000-000000000012", "golden_crypt",
                         {-15.8f, 5.03f, 12.4f}, 17u),
        makeHeroInstance("00000000-0000-4000-8000-000000000013", "teal_fanwort",
                         {-9.2f, 5.03f, 11.8f}, 18u),
        makeHeroInstance("00000000-0000-4000-8000-000000000014", "red_ludwigia",
                         {-5.4f, 5.03f, -12.4f}, 19u),
        makeHeroInstance("00000000-0000-4000-8000-000000000015", "golden_crypt",
                         {1.8f, 5.03f, 12.7f}, 20u),
        makeHeroInstance("00000000-0000-4000-8000-000000000016", "teal_fanwort",
                         {7.8f, 5.03f, -11.8f}, 21u),
        makeHeroInstance("00000000-0000-4000-8000-000000000017", "red_ludwigia",
                         {13.3f, 5.03f, 11.6f}, 22u),
        makeHeroInstance("00000000-0000-4000-8000-000000000018", "golden_crypt",
                         {19.8f, 5.03f, -2.4f}, 23u),
    };
    return instances;
}

std::vector<PlaceableInstance> aquariumHeroFoliageInstances(
    const std::vector<PlaceableInstance>* scenePlaceables, bool useDefaultPlaceables)
{
    std::vector<PlaceableInstance> instances{};
    if (scenePlaceables != nullptr)
    {
        instances.reserve(scenePlaceables->size());
        for (const PlaceableInstance& instance : *scenePlaceables)
        {
            if (findPrototype(instance.prototypeSlug, instance.prototypeVersion) == nullptr)
            {
                continue;
            }

            const engine::game::PlaceableTransformValidation transform =
                engine::game::validatePlaceableTransform(instance);
            if (transform.valid())
            {
                instances.push_back(transform.canonical);
            }
        }
    }

    if (instances.empty() && useDefaultPlaceables)
    {
        instances = defaultAquariumHeroFoliageInstances();
    }
    return instances;
}

std::string heroFoliageVolumeName(size_t index)
{
    return "HeroFoliage" + std::to_string(index);
}
} // namespace FoliageCatalog
