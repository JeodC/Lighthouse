#include "O2rImport.h"
#include <map>
#include <memory>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/type/Blob.h>
#include <spdlog/spdlog.h>
#include <string>

namespace Lightbulb {

const BKLevel kBKLevels[] = {
#include "LevelModels.inc"
};
const int kBKLevelCount = sizeof(kBKLevels) / sizeof(kBKLevels[0]);

namespace {
struct CachedModel {
    std::shared_ptr<Ship::Blob> blob;
    BKModelBin* model = nullptr;
};
std::map<std::string, CachedModel>& modelCache() {
    static std::map<std::string, CachedModel> cache;
    return cache;
}
} // namespace

void ResetModelCache() {
    modelCache().clear();
}

BKModelBin* LoadO2rModel(const std::string& path) {
    auto& cache = modelCache();
    const auto cached = cache.find(path);
    if (cached != cache.end()) {
        return cached->second.model;
    }
    auto resources = Ship::Context::GetRawInstance()->GetResourceManager();
    if (!resources) {
        SPDLOG_WARN("o2r: no ResourceManager");
        return nullptr;
    }
    auto resource = resources->LoadResource(path);
    if (!resource) {
        SPDLOG_WARN("o2r: LoadResource('{}') -> null (not mounted / factory failed?)", path);
        return nullptr;
    }
    auto blob = std::static_pointer_cast<Ship::Blob>(resource);
    if (!blob || blob->Data.empty()) {
        SPDLOG_WARN("o2r: '{}' -> empty/non-blob resource", path);
        return nullptr;
    }
    CachedModel& entry = cache[path];
    entry.blob = blob;
    entry.model = reinterpret_cast<BKModelBin*>(blob->Data.data());
    return entry.model;
}

} // namespace Lightbulb
