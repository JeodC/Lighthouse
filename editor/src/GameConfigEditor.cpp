#include "App.h"
#include "LevelView.h"
#include "O2rImport.h"
#include "UiCommon.h"

#include "imgui.h"

// The game's own map names. Entrances stay numbered: the only catalog of entrance names covers
// vanilla, and a romhack is free to make entrance 8 somewhere else entirely.
#include "port/UI/DeveloperTools/MapNames.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
using Lightbulb::GameConfig;

void hint(const char* text) {
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text);
    }
}

bool namedMap(int mapId) {
    if (mapId <= 0 || mapId >= MAP_NUM_MAPS) {
        return false;
    }
    const char* name = mapNames[mapId].displayName;
    return std::strcmp(name, "Unused") != 0 && std::strcmp(name, "Unknown") != 0;
}

// Every map the game lists, then any the hack touches that the game doesn't.
template <typename Overrides> std::vector<int> mapRows(const Overrides& overrides) {
    std::vector<int> ids;
    for (int i = 0; i < Lightbulb::kBKLevelCount; ++i) {
        ids.push_back(Lightbulb::kBKLevels[i].mapId);
    }
    for (const auto& entry : overrides) {
        if (!Lightbulb::FindBKLevel((uint16_t)entry.first)) {
            ids.push_back(entry.first);
        }
    }
    return ids;
}

const char* mapLabel(int mapId, char* fallback, size_t size) {
    if (mapId < 0) {
        return "(none)";
    }
    if (namedMap(mapId)) {
        return mapNames[mapId].displayName;
    }
    std::snprintf(fallback, size, "map 0x%X", mapId);
    return fallback;
}

const char* levelLabel(int level, char* fallback, size_t size) {
    if (level < 0) {
        return "(none)";
    }
    if (const char* name = Lightbulb::LevelEnumName(level)) {
        return name;
    }
    std::snprintf(fallback, size, "level 0x%X", level);
    return fallback;
}

bool mapCombo(const char* label, int& mapId, float width, bool allowNone = false) {
    char fallback[24];
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, mapLabel(mapId, fallback, sizeof(fallback)))) {
        if (allowNone && ImGui::Selectable("(none)", mapId < 0)) {
            mapId = -1;
            changed = true;
        }
        for (int candidate = 1; candidate < MAP_NUM_MAPS; ++candidate) {
            if (!namedMap(candidate)) {
                continue;
            }
            if (ImGui::Selectable(mapNames[candidate].displayName, candidate == mapId)) {
                mapId = candidate;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool levelCombo(const char* label, int& level, float width) {
    char fallback[24];
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, levelLabel(level, fallback, sizeof(fallback)))) {
        for (int id = 1; id <= 0xD; ++id) {
            if (ImGui::Selectable(Lightbulb::LevelEnumName(id), id == level)) {
                level = id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool intField(const char* label, int& value, int lo, int hi, float width = 110.0f) {
    ImGui::SetNextItemWidth(width);
    return ImGui::DragInt(label, &value, 0.2f, lo, hi);
}

bool floatField(const char* label, float& value, float width = 90.0f) {
    ImGui::SetNextItemWidth(width);
    return ImGui::DragFloat(label, &value, 0.01f, -1000.0f, 1000.0f, "%.3f");
}

// A warp target packs the map above the entry point. The two fixed warps carry a second key
// holding the map alone, cut from the same two bytes, so it moves with them.
bool warpField(int& dest, int* startLevel = nullptr) {
    int map = (dest >> 8) & 0xFF;
    int entry = dest & 0xFF;
    bool changed = mapCombo("##map", map, 220.0f);
    ImGui::SameLine();
    changed |= intField("entrance", entry, 0, 255, 70.0f);
    hint("Which entry point in that map Banjo arrives at.");
    if (changed) {
        dest = (map << 8) | entry;
        if (startLevel) {
            *startLevel = map;
        }
    }
    return changed;
}

bool textField(const char* label, std::string& value, const char* placeholder, float width) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s", value.c_str());
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputTextWithHint(label, placeholder, buf, sizeof(buf))) {
        value = buf;
        return true;
    }
    return false;
}

// A map table covers every map in the game, so only the rows on screen get built.
struct MapTable {
    std::vector<int> ids;
    ImGuiListClipper clipper;

    template <typename Overrides> explicit MapTable(const Overrides& overrides) : ids(mapRows(overrides)) {
        clipper.Begin((int)ids.size());
    }
    bool step() {
        return clipper.Step();
    }
};

// Rows the romhack changed stand out from the game's own values, and can be put back.
bool beginMapRow(int mapId, bool overridden) {
    ImGui::PushID(mapId);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    char fallback[24];
    const char* name = mapLabel(mapId, fallback, sizeof(fallback));
    if (overridden) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", name);
    } else {
        ImGui::TextUnformatted(name);
    }
    return overridden;
}

bool vanillaButton(bool overridden) {
    return overridden && ImGui::SmallButton("vanilla");
}

// The archive's own track list, so a romhack's added music shows up beside the game's.
const std::vector<std::pair<int, std::string>>& musicTracks() {
    static std::vector<std::pair<int, std::string>> tracks;
    if (tracks.empty()) {
        for (const std::string& path : Lightbulb::ListO2rResourcePaths("comusic")) {
            const size_t slash = path.find_last_of('/');
            const char* name = path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
            if (std::strncmp(name, "COMUSIC_", 8) != 0) {
                continue;
            }
            tracks.emplace_back((int)std::strtoul(name + 8, nullptr, 16), name);
        }
        std::sort(tracks.begin(), tracks.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    }
    return tracks;
}

bool skyCombo(const char* label, int& model, float width) {
    char preview[48];
    if (model == 0) {
        std::snprintf(preview, sizeof(preview), "(none)");
    } else if (const char* name = Lightbulb::SkyModelName((uint32_t)model)) {
        std::snprintf(preview, sizeof(preview), "%s", name);
    } else {
        std::snprintf(preview, sizeof(preview), "0x%X", model);
    }
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, preview)) {
        if (ImGui::Selectable("(none)", model == 0)) {
            model = 0;
            changed = true;
        }
        for (int i = 0; i < Lightbulb::SkyModelCount(); ++i) {
            const uint32_t id = Lightbulb::SkyModelId(i);
            ImGui::PushID((int)id);
            if (ImGui::Selectable(Lightbulb::SkyModelName(id), (uint32_t)model == id)) {
                model = (int)id;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool musicCombo(const char* label, int& track, float width, bool allowNone) {
    const std::vector<std::pair<int, std::string>>& tracks = musicTracks();
    const bool none = track < 0 || track == 0xFFFF;
    char preview[64];
    std::snprintf(preview, sizeof(preview), "0x%X", track);
    if (allowNone && none) {
        std::snprintf(preview, sizeof(preview), "(none)");
    } else {
        for (const auto& entry : tracks) {
            if (entry.first == track) {
                std::snprintf(preview, sizeof(preview), "%s", entry.second.c_str());
                break;
            }
        }
    }
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, preview)) {
        if (allowNone && ImGui::Selectable("(none)", none)) {
            track = 0xFFFF;
            changed = true;
        }
        for (const auto& entry : tracks) {
            ImGui::PushID(entry.first);
            if (ImGui::Selectable(entry.second.c_str(), entry.first == track)) {
                track = entry.first;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// D_803947F8 in jigsawpicture.c, by the progress flag each row records.
const char* kPuzzleNames[11] = {
    "Mumbo's Mountain", "Treasure Trove Cove", "Clanker's Cavern",    "Bubblegloop Swamp",
    "Freezeezy Peak",   "Gobi's Valley",       "Mad Monster Mansion", "Rusty Bucket Bay",
    "Click Clock Wood", "Grunty picture",      "Double health",
};

// The save keeps every picture's count in one run of bits, 0x5D to 0x82. Past the last flag the
// game names, 0x123, there are four spare bits before the file ends.
constexpr int kPuzzleFlagFirst = 0x5D;
constexpr int kPuzzleFlagBits = 0x25;
constexpr int kPuzzleFlagSpare = 0x124;

// Where a count can go without landing on save data that means something else: the slot each
// picture starts at, and the spare bits at the end.
const int kPuzzleSlots[12] = { 0x5D, 0x5E, 0x60, 0x63, 0x66, 0x6A,
                               0x6E, 0x72, 0x76, 0x7A, 0x7F, kPuzzleFlagSpare };

// A count has to reach the cost, so the cost decides how many bits the picture needs.
int bitsForCost(int cost) {
    int bits = 1;
    while (cost > (1 << bits) - 1 && bits < 15) {
        ++bits;
    }
    return bits;
}

constexpr ImGuiTableFlags kTableFlags =
    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
constexpr float kMapTableHeight = 260.0f;
} // namespace

// Reads the destination map's own setup to see which entry points it places. Loading a setup
// resets the decomp's cube and camera lists, so the open level is loaded again afterwards; the
// scene on screen keeps its own copy, edits and all.
const std::vector<uint32_t>& App::EntryActorsForMap(int mapId) {
    const auto cached = mMapEntryActors.find(mapId);
    if (cached != mMapEntryActors.end()) {
        return cached->second;
    }
    std::vector<uint32_t>& actors = mMapEntryActors[mapId];
    if (mSetupIndex.empty()) {
        mSetupIndex = Lightbulb::indexById(Lightbulb::ListO2rResourcePaths("setup"));
    }
    const auto setup = mSetupIndex.find((uint32_t)(mapId + 0x71C));
    if (setup == mSetupIndex.end()) {
        return actors;
    }
    Lightbulb::SetupScene probe;
    if (Lightbulb::LoadO2rSetup(setup->second, probe)) {
        for (const Lightbulb::SetupNode& nd : probe.nodes) {
            if (!nd.script && nd.category == 6 && Lightbulb::ActorIsEntryPoint(nd.id)) {
                actors.push_back(nd.id);
            }
        }
    }
    if (mLevelScene.sel >= 0 && mLevelScene.sel < (int)mLevelScene.entries.size()) {
        const std::string& open = mLevelScene.entries[mLevelScene.sel].setupPath;
        if (!open.empty()) {
            Lightbulb::SetupScene restore;
            Lightbulb::LoadO2rSetup(open, restore);
        }
    }
    return actors;
}

void App::DrawGameConfig() {
    if (!mShowGameConfig) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(720, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Game Config", &mShowGameConfig)) {
        ImGui::End();
        return;
    }
    if (!mO2rLoaded) {
        ImGui::TextWrapped("Open a bk.o2r to see its game config.");
        ImGui::End();
        return;
    }

    GameConfig& cfg = mGameConfig;
    int* c = cfg.constants;
    bool changed = false;

    if (mSetupIndex.empty()) {
        mSetupIndex = Lightbulb::indexById(Lightbulb::ListO2rResourcePaths("setup"));
    }

    // A warp lands on whichever node carries the entry-point actor its entrance names. Say so
    // when the destination can't answer: checked against the map already read, so editing a
    // destination checks it, and the button below checks the ones already in the list.
    auto warpTrouble = [&](int dest) -> const char* {
        const int map = (dest >> 8) & 0xFF;
        if (map == 0) {
            return "This warp has no destination map.";
        }
        if (!mSetupIndex.count((uint32_t)(map + 0x71C))) {
            return "That map has no setup in this archive, so the game ignores this warp.";
        }
        // Only entrances below 0x80 arrive at a node. Higher ones carry their own position, and
        // 0x63 and 0x65 are answered before the game goes looking, so leave all three alone.
        const int entrance = dest & 0xFF;
        if (entrance >= 0x80 || entrance == 0x63 || entrance == 0x65) {
            return nullptr;
        }
        const auto read = mMapEntryActors.find(map);
        if (read == mMapEntryActors.end()) {
            return nullptr;
        }
        const uint32_t actor = Lightbulb::EntryActorForExit((uint32_t)entrance);
        if (actor == 0) {
            return "No entry point answers to that entrance number.";
        }
        const auto has = [&](uint32_t id) {
            return std::find(read->second.begin(), read->second.end(), id) != read->second.end();
        };
        if (has(actor)) {
            return nullptr;
        }
        // Missing, so the game walks entrances 0 to 0x1D and takes the first the map does have.
        // Entry points outside that range can't rescue it.
        for (uint32_t probe = 0; probe < 0x1E; ++probe) {
            const uint32_t alternative = Lightbulb::EntryActorForExit(probe);
            if (alternative != 0 && has(alternative)) {
                return "That map has no matching entry point, so Banjo arrives at a different one.";
            }
        }
        return "That map has no entry point the game can fall back to, so Banjo arrives in the void.";
    };
    auto warnWarp = [&](int dest) {
        if (const char* trouble = warpTrouble(dest)) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "   %s", trouble);
        }
    };

    if (cfg.fromArchive) {
        ImGui::Text("From %s", cfg.romName.empty() ? "the loaded romhack" : cfg.romName.c_str());
    } else {
        ImGui::TextUnformatted("Vanilla settings; the loaded archives carry no aGameConfig.");
    }
    if (mGameConfigDirty) {
        ImGui::SameLine();
        ImGui::TextDisabled("(edited)");
    }
    ImGui::BeginDisabled(true);
    ImGui::Button("Save");
    ImGui::EndDisabled();
    hint("Writing the o2r isn't in yet; edits live until the archive is reopened.");
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        Lightbulb::LoadO2rGameConfig(cfg);
        mGameConfigDirty = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to vanilla")) {
        cfg = Lightbulb::VanillaGameConfig();
        mGameConfigDirty = true;
    }
    ImGui::Separator();

    ImGui::BeginChild("##gameconfig");

    if (ImGui::CollapsingHeader("Start", ImGuiTreeNodeFlags_DefaultOpen)) {
        // One start level in the editor a hack was built with; three map bytes in the config.
        const int newGame = c[Lightbulb::kNewGameMap];
        const int outOfHouse = (c[Lightbulb::kWarpExitBanjosHouse] >> 8) & 0xFF;
        const int intoLair = (c[Lightbulb::kWarpEnterLair] >> 8) & 0xFF;
        const bool together = newGame == outOfHouse && outOfHouse == intoLair;
        char fallback[24];
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::BeginCombo("Start level",
                              together ? mapLabel(newGame, fallback, sizeof(fallback)) : "(three separate maps)")) {
            for (int i = 0; i < Lightbulb::kBKLevelCount; ++i) {
                const Lightbulb::BKLevel& level = Lightbulb::kBKLevels[i];
                if (ImGui::Selectable(level.name, together && level.mapId == newGame)) {
                    c[Lightbulb::kNewGameMap] = level.mapId;
                    c[Lightbulb::kStartLevel1] = level.mapId;
                    c[Lightbulb::kStartLevel2] = level.mapId;
                    c[Lightbulb::kWarpExitBanjosHouse] =
                        (level.mapId << 8) | (c[Lightbulb::kWarpExitBanjosHouse] & 0xFF);
                    c[Lightbulb::kWarpEnterLair] = (level.mapId << 8) | (c[Lightbulb::kWarpEnterLair] & 0xFF);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        hint("Points a new file, the way out of Banjo's house and the lair entrance at the same "
             "map. Set them apart below and under Fixed warps.");

        changed |= mapCombo("New game map", c[Lightbulb::kNewGameMap], 240.0f);
        hint("Where a new file begins. Vanilla starts on a Spiral Mountain cutscene.");
        // Asking for it at all is the setting; the value is a leftover of the instruction patch.
        bool allMoves = cfg.constantSet[Lightbulb::kKnowAllMoves];
        if (ImGui::Checkbox("Start with every move learned", &allMoves)) {
            cfg.constantSet[Lightbulb::kKnowAllMoves] = allMoves;
            if (allMoves) {
                c[Lightbulb::kKnowAllMoves] = Lightbulb::kKnowAllMovesOn;
            }
            changed = true;
        }
        hint("Teaches Banjo every move on a new file.");
    }

    if (ImGui::CollapsingHeader("Mumbo Token Cost")) {
        changed |= intField("Termite", c[Lightbulb::kMumboCostTermite], 0, 255);
        changed |= intField("Crocodile", c[Lightbulb::kMumboCostCroc], 0, 255);
        changed |= intField("Walrus", c[Lightbulb::kMumboCostWalrus], 0, 255);
        changed |= intField("Pumpkin", c[Lightbulb::kMumboCostPumpkin], 0, 255);
        changed |= intField("Bee", c[Lightbulb::kMumboCostBee], 0, 255);
    }

    if (ImGui::CollapsingHeader("Item limits")) {
        changed |= intField("Eggs", c[Lightbulb::kEggsMax], 0, 999);
        ImGui::SameLine();
        changed |= intField("with Cheato##eggs", c[Lightbulb::kEggsCheatoMax], 0, 999);
        changed |= intField("Red feathers", c[Lightbulb::kRedFeathersMax], 0, 999);
        ImGui::SameLine();
        changed |= intField("with Cheato##red", c[Lightbulb::kRedFeathersCheatoMax], 0, 999);
        changed |= intField("Gold feathers", c[Lightbulb::kGoldFeathersMax], 0, 999);
        ImGui::SameLine();
        changed |= intField("with Cheato##gold", c[Lightbulb::kGoldFeathersCheatoMax], 0, 999);
        changed |= intField("Notes per world", c[Lightbulb::kNotesMax], 0, 999);
        changed |= intField("Jiggies per world", c[Lightbulb::kJiggiesPerWorld], 0, 99);
        changed |= intField("Honeycombs per world", c[Lightbulb::kHoneycombsPerWorld], 0, 99);
        changed |= intField("Honeycombs in the special level", c[Lightbulb::kExtraHoneycombStart], 0, 99);
        hint("The special level below counts this many instead.");
    }

    if (ImGui::CollapsingHeader("Special levels")) {
        changed |= levelCombo("Extra honeycomb level", c[Lightbulb::kSpecialLevel], 240.0f);
        hint("Counts the honeycombs above instead of the per-world number. Vanilla Spiral Mountain.");
        changed |= levelCombo("Page without notes or jiggies", c[Lightbulb::kHideJiggiesLevel], 240.0f);
        hint("Its pause-menu page drops the note and jiggy rows. Vanilla Spiral Mountain.");
        changed |= levelCombo("Page without notes or honeycombs", c[Lightbulb::kHideCollectiblesLevel], 240.0f);
        hint("Its pause-menu page drops the note and honeycomb rows. Vanilla Grunty's Lair.");
    }

    if (ImGui::CollapsingHeader("Fixed warps")) {
        Lightbulb::ui::TextDisabledWrapped("Two warps the game hardcodes instead of reading from a node. "
                                           "Their maps double as the start levels, so both move together.");
        ImGui::TextUnformatted("Leaving Banjo's house");
        ImGui::PushID("exit");
        if (warpField(c[Lightbulb::kWarpExitBanjosHouse], &c[Lightbulb::kStartLevel1])) {
            EntryActorsForMap((c[Lightbulb::kWarpExitBanjosHouse] >> 8) & 0xFF);
            changed = true;
        }
        warnWarp(c[Lightbulb::kWarpExitBanjosHouse]);
        ImGui::PopID();
        ImGui::TextUnformatted("Entering the lair from Spiral Mountain");
        ImGui::PushID("enter");
        if (warpField(c[Lightbulb::kWarpEnterLair], &c[Lightbulb::kStartLevel2])) {
            EntryActorsForMap((c[Lightbulb::kWarpEnterLair] >> 8) & 0xFF);
            changed = true;
        }
        warnWarp(c[Lightbulb::kWarpEnterLair]);
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Note doors")) {
        if (ImGui::BeginTable("##notedoors", 4, ImGuiTableFlags_SizingFixedFit)) {
            for (int row = 0; row < 6; ++row) {
                ImGui::TableNextRow();
                for (int column = 0; column < 2; ++column) {
                    const int door = column * 6 + row;
                    char label[24];
                    std::snprintf(label, sizeof(label), "Door %d", door + 1);
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(label);
                    ImGui::TableNextColumn();
                    ImGui::PushID(door);
                    changed |= intField("##notes", cfg.noteDoors[door], 0, 999);
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Jiggy puzzles")) {
        Lightbulb::ui::TextDisabledWrapped(
            "What each picture costs. The counter size follows from the cost, and the save slot is "
            "where that count lives in the save file.");

        int used = 0;
        for (int i = 0; i < 11; ++i) {
            cfg.puzzles[i].size = bitsForCost(cfg.puzzles[i].cost);
            used += cfg.puzzles[i].size;
        }

        int order[11];
        for (int i = 0; i < 11; ++i) {
            order[i] = i;
        }
        std::sort(order, order + 11, [&](int a, int b) { return cfg.puzzles[a].flag < cfg.puzzles[b].flag; });
        bool overlap = false;
        for (int i = 0; i + 1 < 11; ++i) {
            const GameConfig::JiggyPuzzle& lower = cfg.puzzles[order[i]];
            if (lower.flag + lower.size > cfg.puzzles[order[i + 1]].flag) {
                overlap = true;
            }
        }

        ImGui::Text("Bits used %d of %d", used, kPuzzleFlagBits);
        hint("The eleven counts share one run of room in the save, and a costlier picture needs "
             "more of it.");
        if (used > kPuzzleFlagBits) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "over budget");
            hint("These no longer fit the room the game set aside. Lower a cost, or move one to "
                 "the spare slot.");
        }

        if (ImGui::BeginTable("##puzzles", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Picture", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn("Jiggies");
            ImGui::TableSetupColumn("Counter");
            ImGui::TableSetupColumn("Save slot");
            ImGui::TableHeadersRow();
            for (int i = 0; i < 11; ++i) {
                GameConfig::JiggyPuzzle& puzzle = cfg.puzzles[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(kPuzzleNames[i]);
                ImGui::TableNextColumn();
                changed |= intField("##cost", puzzle.cost, 0, 255, 70.0f);
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%d bit%s", puzzle.size, puzzle.size == 1 ? "" : "s");
                hint("Worked out from the cost.");
                ImGui::TableNextColumn();
                char slotLabel[48];
                std::snprintf(slotLabel, sizeof(slotLabel), "0x%X", puzzle.flag);
                for (int slot = 0; slot < 12; ++slot) {
                    if (kPuzzleSlots[slot] == puzzle.flag) {
                        std::snprintf(slotLabel, sizeof(slotLabel), "0x%X  %s", kPuzzleSlots[slot],
                                      slot < 11 ? kPuzzleNames[slot] : "spare room");
                        break;
                    }
                }
                ImGui::SetNextItemWidth(190.0f);
                if (ImGui::BeginCombo("##flag", slotLabel)) {
                    for (int slot = 0; slot < 12; ++slot) {
                        char option[48];
                        std::snprintf(option, sizeof(option), "0x%X  %s", kPuzzleSlots[slot],
                                      slot < 11 ? kPuzzleNames[slot] : "spare room");
                        if (ImGui::Selectable(option, kPuzzleSlots[slot] == puzzle.flag)) {
                            puzzle.flag = kPuzzleSlots[slot];
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                hint("Where this count sits in the save. These are the only slots that don't "
                     "write over something else.");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (overlap) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "Two counts share save file room.");
            hint("One picture's count is writing over the next one's.");
            ImGui::SameLine();
        }
        if (ImGui::Button("Lay out save slots")) {
            int next = kPuzzleFlagFirst;
            for (int i = 0; i < 11; ++i) {
                cfg.puzzles[i].flag = next;
                next += cfg.puzzles[i].size;
            }
            changed = true;
        }
        hint("Packs them end to end from the slot the game starts at.");
    }

    if (ImGui::CollapsingHeader("Level names")) {
        Lightbulb::ui::TextDisabledWrapped("Pause-menu headings, in menu order rather than world order. "
                                           "Each row is named for the page it replaces.");
        const GameConfig vanilla = Lightbulb::VanillaGameConfig();
        for (int i = 0; i < 13; ++i) {
            char label[16];
            std::snprintf(label, sizeof(label), "##name%d", i);
            ImGui::PushID(i);
            changed |= textField(label, cfg.levelNames[i], vanilla.levelNames[i].c_str(), 260.0f);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", vanilla.levelNames[i].c_str());
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Music by map")) {
        if (ImGui::BeginTable("##music", 4, kTableFlags, ImVec2(0, kMapTableHeight))) {
            ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthFixed, 220.0f);
            ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthFixed, 210.0f);
            ImGui::TableSetupColumn("Alternate", ImGuiTableColumnFlags_WidthFixed, 210.0f);
            ImGui::TableSetupColumn("");
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            MapTable table(cfg.music);
            while (table.step()) {
                for (int i = table.clipper.DisplayStart; i < table.clipper.DisplayEnd; ++i) {
                    const int mapId = table.ids[i];
                    const auto found = cfg.music.find(mapId);
                    const bool over = beginMapRow(mapId, found != cfg.music.end());
                    GameConfig::MusicRow row;
                    if (over) {
                        row = found->second;
                    } else {
                        row.track1 = Lightbulb::LevelMusicTrack(mapId);
                        row.track2 = Lightbulb::LevelMusicTrack2(mapId);
                        if (row.track2 < 0) {
                            row.track2 = 0xFFFF;
                        }
                    }
                    ImGui::TableNextColumn();
                    bool edited = musicCombo("##t1", row.track1, 200.0f, false);
                    hint("The map's music. Tools > Music plays them.");
                    ImGui::TableNextColumn();
                    edited |= musicCombo("##t2", row.track2, 200.0f, true);
                    hint("A second track some maps crossfade to; Spiral Mountain's bridge has one. "
                         "What starts the fade is in the game's code, so setting one on a map the "
                         "code says nothing about does nothing.");
                    ImGui::TableNextColumn();
                    if (edited) {
                        cfg.music[mapId] = row;
                        changed = true;
                    }
                    if (vanillaButton(over)) {
                        cfg.music.erase(mapId);
                        changed = true;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Level by map")) {
        Lightbulb::ui::TextDisabledWrapped("Which world a map belongs to, for scores, names and the pause menu.");
        const std::vector<int> ids = mapRows(cfg.sceneRemap);
        constexpr int kAcross = 3;
        const int rows = ((int)ids.size() + kAcross - 1) / kAcross;
        if (ImGui::BeginTable("##remap", kAcross * 3, kTableFlags, ImVec2(0, kMapTableHeight))) {
            for (int column = 0; column < kAcross; ++column) {
                ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 175.0f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 46.0f);
            }
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(rows);
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    ImGui::TableNextRow();
                    for (int column = 0; column < kAcross; ++column) {
                        const int index = column * rows + row;
                        ImGui::TableNextColumn();
                        if (index >= (int)ids.size()) {
                            ImGui::TableNextColumn();
                            ImGui::TableNextColumn();
                            continue;
                        }
                        const int mapId = ids[index];
                        const auto found = cfg.sceneRemap.find(mapId);
                        const bool over = found != cfg.sceneRemap.end();
                        ImGui::PushID(mapId);
                        char fallback[24];
                        const char* name = mapLabel(mapId, fallback, sizeof(fallback));
                        if (over) {
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", name);
                        } else {
                            ImGui::TextUnformatted(name);
                        }
                        hint(name);
                        int value = over ? found->second : Lightbulb::VanillaMapLevel(mapId);
                        ImGui::TableNextColumn();
                        if (levelCombo("##level", value, 170.0f)) {
                            cfg.sceneRemap[mapId] = value;
                            changed = true;
                        }
                        ImGui::TableNextColumn();
                        if (vanillaButton(over)) {
                            cfg.sceneRemap.erase(mapId);
                            changed = true;
                        }
                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Skybox by map")) {
        if (ImGui::BeginTable("##sky", 11, kTableFlags | ImGuiTableFlags_ScrollX, ImVec2(0, kMapTableHeight))) {
            ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthFixed, 220.0f);
            for (int layer = 0; layer < 3; ++layer) {
                char model[16], scale[16], rot[16];
                std::snprintf(model, sizeof(model), "Sky %d", layer + 1);
                std::snprintf(scale, sizeof(scale), "Scale %d", layer + 1);
                std::snprintf(rot, sizeof(rot), "Spin %d", layer + 1);
                ImGui::TableSetupColumn(model, ImGuiTableColumnFlags_WidthFixed, 180.0f);
                ImGui::TableSetupColumn(scale);
                ImGui::TableSetupColumn(rot);
            }
            ImGui::TableSetupColumn("");
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            MapTable table(cfg.skybox);
            while (table.step()) {
                for (int i = table.clipper.DisplayStart; i < table.clipper.DisplayEnd; ++i) {
                    const int mapId = table.ids[i];
                    const auto found = cfg.skybox.find(mapId);
                    const bool over = beginMapRow(mapId, found != cfg.skybox.end());
                    GameConfig::SkyRow row;
                    if (over) {
                        row = found->second;
                    } else {
                        Lightbulb::SkyLayerInfo layers[3];
                        const int count = Lightbulb::SkyLayersForMap(mapId, layers);
                        for (int layer = 0; layer < count; ++layer) {
                            row.models[layer] = (int)layers[layer].modelId;
                            row.scales[layer] = layers[layer].scale;
                            row.rotations[layer] = layers[layer].rotSpeed;
                        }
                    }
                    bool edited = false;
                    for (int layer = 0; layer < 3; ++layer) {
                        ImGui::PushID(layer);
                        ImGui::TableNextColumn();
                        edited |= skyCombo("##model", row.models[layer], 170.0f);
                        ImGui::TableNextColumn();
                        edited |= floatField("##scale", row.scales[layer], 70.0f);
                        ImGui::TableNextColumn();
                        edited |= floatField("##rot", row.rotations[layer], 70.0f);
                        ImGui::PopID();
                    }
                    ImGui::TableNextColumn();
                    if (edited) {
                        cfg.skybox[mapId] = row;
                        changed = true;
                    }
                    if (vanillaButton(over)) {
                        cfg.skybox.erase(mapId);
                        changed = true;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Warps")) {
        Lightbulb::ui::TextDisabledWrapped("Where every warp in the game sends Banjo. A warp itself is a "
                                           "node in a level's setup; this only changes where it leads.");

        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputTextWithHint("##warpfilter", "Filter by name", mWarpFilter, sizeof(mWarpFilter));
        ImGui::SameLine();
        ImGui::Checkbox("Only the ones I've changed", &mWarpChangedOnly);

        // Bucket by world first: the game's own warp order interleaves them.
        struct WarpGroup {
            std::string world;
            std::vector<int> rows;
        };
        std::vector<WarpGroup> groups;
        for (int i = 0; i < Lightbulb::WarpCount(); ++i) {
            const char* name = Lightbulb::WarpName(i);
            const char* tail = name + 5;
            size_t prefix = 0;
            while (tail[prefix] >= 'a' && tail[prefix] <= 'z') {
                ++prefix;
            }
            std::string world(tail, prefix ? prefix : std::strlen(tail));
            for (char& ch : world) {
                ch = (char)std::toupper((unsigned char)ch);
            }
            auto group = std::find_if(groups.begin(), groups.end(),
                                      [&](const WarpGroup& g) { return g.world == world; });
            if (group == groups.end()) {
                groups.push_back({ world, {} });
                group = groups.end() - 1;
            }
            group->rows.push_back(i);
        }

        const bool filtering = mWarpFilter[0] != 0 || mWarpChangedOnly;
        if (ImGui::BeginTable("##warps", 4, kTableFlags, ImVec2(0, 320.0f))) {
            ImGui::TableSetupColumn("Warp", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 230.0f);
            ImGui::TableSetupColumn("Entry", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (const WarpGroup& group : groups) {
                std::vector<int> visible;
                for (int i : group.rows) {
                    const int warp = Lightbulb::WarpIdAt(i);
                    if (mWarpChangedOnly && !cfg.warpDests.count(warp)) {
                        continue;
                    }
                    if (mWarpFilter[0] != 0 &&
                        !Lightbulb::ui::ContainsNoCase(Lightbulb::WarpName(i), mWarpFilter)) {
                        continue;
                    }
                    visible.push_back(i);
                }
                if (visible.empty()) {
                    continue;
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(group.world.c_str());
                char header[64];
                std::snprintf(header, sizeof(header), "%s  (%d)", group.world.c_str(), (int)visible.size());
                if (filtering) {
                    ImGui::SetNextItemOpen(true);
                }
                const bool open = ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_SpanFullWidth);
                if (!open) {
                    ImGui::PopID();
                    continue;
                }

                for (int i : visible) {
                    const int warp = Lightbulb::WarpIdAt(i);
                    const char* name = Lightbulb::WarpName(i);
                    const char* label = name + 5 + group.world.size();
                    const auto found = cfg.warpDests.find(warp);
                    const bool over = found != cfg.warpDests.end();
                    const int vanilla = Lightbulb::VanillaWarpDest(warp);
                    const int dest = over ? found->second : vanilla;
                    int map = dest < 0 ? 0 : (dest >> 8) & 0xFF;
                    int entry = dest < 0 ? 0 : dest & 0xFF;

                    ImGui::PushID(warp);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    if (over) {
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", label);
                    } else {
                        ImGui::TextUnformatted(label);
                    }
                    hint(name);

                    ImGui::TableNextColumn();
                    bool edited = mapCombo("##map", map, 220.0f);
                    if (!over && vanilla < 0) {
                        hint("This warp works out its own destination as the game runs. Setting one "
                             "here overrides that.");
                    }
                    ImGui::TableNextColumn();
                    edited |= intField("##entry", entry, 0, 255, 70.0f);
                    hint("Which entry point in that map Banjo arrives at.");
                    if (edited) {
                        cfg.warpDests[warp] = (map << 8) | entry;
                        EntryActorsForMap(map);
                        changed = true;
                    }
                    ImGui::TableNextColumn();
                    if (vanillaButton(over)) {
                        cfg.warpDests.erase(warp);
                        changed = true;
                    }
                    if (over || vanilla >= 0) {
                        if (const char* trouble = warpTrouble((map << 8) | entry)) {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "%s", trouble);
                        }
                    } else {
                        ImGui::SameLine();
                        ImGui::TextDisabled("set by the game");
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (ImGui::Button("Check every destination")) {
            EntryActorsForMap((c[Lightbulb::kWarpExitBanjosHouse] >> 8) & 0xFF);
            EntryActorsForMap((c[Lightbulb::kWarpEnterLair] >> 8) & 0xFF);
            for (const auto& [warp, dest] : cfg.warpDests) {
                EntryActorsForMap((dest >> 8) & 0xFF);
            }
            mStatus = "Checked every warp destination in the config.";
        }
        hint("Reads each destination map's setup. Editing a row checks that one on its own.");
    }

    if (cfg.fromArchive && ImGui::CollapsingHeader("Identity")) {
        if (!cfg.romSha1.empty()) {
            ImGui::Text("ROM SHA-1     : %s", cfg.romSha1.c_str());
        }
        if (cfg.customCodeSha1.empty()) {
            ImGui::TextUnformatted("Custom code   : none");
        } else {
            static const char* kKinds[] = { "none", "BB globalization", "injected" };
            ImGui::Text("Custom code   : %s at 0x%08X",
                        cfg.customCodeKind >= 0 && cfg.customCodeKind < 3 ? kKinds[cfg.customCodeKind] : "?",
                        cfg.customCodeRamBase);
            ImGui::Text("Code SHA-1    : %s", cfg.customCodeSha1.c_str());
        }
    }

    ImGui::EndChild();
    ImGui::End();

    if (changed) {
        mGameConfigDirty = true;
    }
}
