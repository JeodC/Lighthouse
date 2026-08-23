#include "O2rImport.h"
#include "PreviewScene.h"

#define INIT_EVENT_IDS
#include "port/Enhancements/Events/Hooks/Events.h"

#include "port/Resource/Importers/AnimFactory.h"
#include "port/Resource/Importers/MapFactory.h"
#include "port/Resource/Importers/ModelFactory.h"
#include "port/Resource/Importers/MusicFactory.h"
#include "port/Resource/Importers/SoundFactory.h"
#include "port/Resource/Importers/SpriteFactory.h"
#include <algorithm>
#include <fast/resource/ResourceType.h>
#include <fast/resource/factory/DisplayListFactory.h>
#include <fast/resource/factory/TextureFactory.h>
#include <fast/resource/factory/VertexFactory.h>
#include <set>
#include <ship/Context.h>
#include <ship/resource/ResourceLoader.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/ResourceType.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/resource/factory/BlobFactory.h>
#include <string>

extern void ResourceHelpers_ClearRefCache();

namespace Lightbulb {

void RegisterBKFactories() {
    auto resources = Ship::Context::GetRawInstance()->GetResourceManager();
    if (!resources) {
        return;
    }
    auto loader = resources->GetResourceLoader();
    if (!loader) {
        return;
    }

    loader->RegisterResourceFactory(std::make_shared<Factories::ResourceFactoryBinaryModelV0>(), RESOURCE_FORMAT_BINARY,
                                    "Model", 0x424B4D4Fu, 0);
    loader->RegisterResourceFactory(std::make_shared<Factories::ResourceFactoryBinarySpriteV0>(),
                                    RESOURCE_FORMAT_BINARY, "Sprite", 0x424B5350u, 0);
    loader->RegisterResourceFactory(std::make_shared<Factories::ResourceFactoryBinaryBKAnimationV0>(),
                                    RESOURCE_FORMAT_BINARY, "BKAnimation", 0x424B414Eu, 0);
    loader->RegisterResourceFactory(std::make_shared<Factories::ResourceFactoryBinaryBKMapV0>(), RESOURCE_FORMAT_BINARY,
                                    "BKMap", 0x424B4D50u, 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryVertexV0>(), RESOURCE_FORMAT_BINARY,
                                    "Vertex", static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV0>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV1>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 1);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryDisplayListV0>(),
                                    RESOURCE_FORMAT_BINARY, "DisplayList",
                                    static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);
    loader->RegisterResourceFactory(std::make_shared<Ship::ResourceFactoryBinaryBlobV0>(), RESOURCE_FORMAT_BINARY,
                                    "Blob", static_cast<uint32_t>(Ship::ResourceType::Blob), 0);
    loader->RegisterResourceFactory(std::make_shared<Factories::ResourceFactoryBinaryBKSoundV0>(),
                                    RESOURCE_FORMAT_BINARY, "BKSound", Factories::kBKSoundResourceType, 0);
    loader->RegisterResourceFactory(std::make_shared<Factories::ResourceFactoryBinaryBKSoundBankV0>(),
                                    RESOURCE_FORMAT_BINARY, "BKSoundBank", Factories::kBKSoundBankResourceType, 0);
    loader->RegisterResourceFactory(std::make_shared<Factories::ResourceFactoryBinaryBKMusicV0>(),
                                    RESOURCE_FORMAT_BINARY, "BKMusic", Factories::kBKMusicResourceType, 0);
}

namespace {
std::string sMountedBase;
std::string sMountedRomhack;

std::shared_ptr<Ship::ArchiveManager> archiveManager() {
    auto resources = Ship::Context::GetRawInstance()->GetResourceManager();
    return resources ? resources->GetArchiveManager() : nullptr;
}

std::vector<std::string> listSorted(const std::string& glob) {
    std::vector<std::string> out;
    auto archives = archiveManager();
    if (!archives) {
        return out;
    }
    if (auto files = archives->ListFiles(glob)) {
        out.assign(files->begin(), files->end());
        std::sort(out.begin(), out.end());
    }
    return out;
}

// Every cache here holds pointers into archive memory, including the decomp's map
// transform list, which walks its entries each frame.
void dropCaches() {
    ResetModelCache();
    ResetSpriteCache();
    ResetAnimCache();
    ResetSoundCache();
    ResetMapXforms();
    ResourceHelpers_ClearRefCache();
    if (auto resources = Ship::Context::GetRawInstance()->GetResourceManager()) {
        resources->UnloadResources("assets/*");
    }
}

} // namespace

std::vector<std::string> ListO2rModelPaths(const std::string& dir) {
    const std::vector<std::string> files = listSorted(dir + "/*");
    const std::set<std::string> all(files.begin(), files.end());
    std::vector<std::string> models;
    for (const std::string& path : files) {
        if (all.count(path + "_VTX")) {
            models.push_back(path);
        }
    }
    return models;
}

std::vector<std::string> ListO2rResourcePaths(const std::string& dir) {
    return listSorted("assets/" + dir + "/*");
}

bool BaseO2rMounted() {
    return !sMountedBase.empty();
}

// A base archive replaces everything: the asset-id table it carries is parsed once
// per process, so a romhack loaded without one would poison it permanently.
MountResult MountO2r(const std::string& path) {
    auto archives = archiveManager();
    if (!archives || !archives->AddArchive(path)) {
        return MountResult::Failed;
    }
    if (!archives->HasFile("assets/aBKAssetTable")) {
        archives->RemoveArchive(path);
        return MountResult::Failed;
    }
    dropCaches();
    if (!sMountedRomhack.empty()) {
        archives->RemoveArchive(sMountedRomhack);
        sMountedRomhack.clear();
    }
    if (!sMountedBase.empty()) {
        archives->RemoveArchive(sMountedBase);
    }
    sMountedBase = path;
    return MountResult::Base;
}

// Romhacks layer over the base game rather than replacing it, overriding only the
// assets they ship.
MountResult MountRomhackO2r(const std::string& path) {
    if (sMountedBase.empty()) {
        return MountResult::NeedsBase;
    }
    auto archives = archiveManager();
    if (!archives || !archives->AddArchive(path)) {
        return MountResult::Failed;
    }
    dropCaches();
    if (!sMountedRomhack.empty() && sMountedRomhack != path) {
        archives->RemoveArchive(sMountedRomhack);
    }
    sMountedRomhack = path;
    return MountResult::Romhack;
}

} // namespace Lightbulb
