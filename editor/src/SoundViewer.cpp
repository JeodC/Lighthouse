#include "App.h"
#include "O2rImport.h"
#include "UiCommon.h"

#include "imgui.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>


namespace {

SDL_AudioDeviceID sDevice = 0;
bool sAudioFailed = false;

bool EnsureAudio() {
    if (sDevice != 0) {
        return true;
    }
    if (sAudioFailed) {
        return false;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        sAudioFailed = true;
        return false;
    }
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = Lightbulb::kSoundBankRate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    sDevice = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if (sDevice == 0) {
        sAudioFailed = true;
        return false;
    }
    SDL_PauseAudioDevice(sDevice, 0);
    return true;
}

void Play(const std::vector<int16_t>& pcm, float ratio) {
    if (!EnsureAudio() || pcm.empty()) {
        return;
    }
    SDL_ClearQueuedAudio(sDevice);

    if (ratio <= 0.0f || std::fabs(ratio - 1.0f) < 0.001f) {
        SDL_QueueAudio(sDevice, pcm.data(), static_cast<Uint32>(pcm.size() * sizeof(int16_t)));
        return;
    }
    const size_t count = static_cast<size_t>(static_cast<float>(pcm.size()) / ratio);
    std::vector<int16_t> shifted(count);
    for (size_t i = 0; i < count; i++) {
        const float at = static_cast<float>(i) * ratio;
        const size_t idx = static_cast<size_t>(at);
        const float frac = at - static_cast<float>(idx);
        const int16_t a = pcm[std::min(idx, pcm.size() - 1)];
        const int16_t b = pcm[std::min(idx + 1, pcm.size() - 1)];
        shifted[i] = static_cast<int16_t>(a + (b - a) * frac);
    }
    SDL_QueueAudio(sDevice, shifted.data(), static_cast<Uint32>(shifted.size() * sizeof(int16_t)));
}

void Stop() {
    if (sDevice != 0) {
        SDL_ClearQueuedAudio(sDevice);
    }
}

bool WriteWav(const std::string& path, const std::vector<int16_t>& pcm, int rate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        return false;
    }
    const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    const uint32_t byteRate = static_cast<uint32_t>(rate) * 2;
    // RIFF is little-endian regardless of host.
    auto put32 = [&](uint32_t v) {
        const uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
        std::fwrite(b, 1, 4, f);
    };
    auto put16 = [&](uint16_t v) {
        const uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
        std::fwrite(b, 1, 2, f);
    };

    std::fwrite("RIFF", 1, 4, f);
    put32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    put32(16);
    put16(1); // PCM
    put16(1); // mono
    put32(static_cast<uint32_t>(rate));
    put32(byteRate);
    put16(2);  // block align
    put16(16); // bits
    std::fwrite("data", 1, 4, f);
    put32(dataBytes);
    for (int16_t sample : pcm) {
        put16(static_cast<uint16_t>(sample));
    }
    std::fclose(f);
    return true;
}

const char* ShortName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
}

int SfxIdFromName(const char* name) {
    int value = 0;
    int digits = 0;
    for (const char* at = name; *at != 0 && *at != '_'; ++at, ++digits) {
        const char c = *at;
        int digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else {
            return -1;
        }
        value = value * 16 + digit;
    }
    return digits > 0 ? value : -1;
}

const char* FindSfxById(const std::vector<std::string>& paths, int id) {
    char prefix[8];
    std::snprintf(prefix, sizeof(prefix), "%03X_", id);
    const size_t len = std::strlen(prefix);
    for (const std::string& path : paths) {
        const char* name = ShortName(path);
        if (std::strncmp(name, prefix, len) == 0) {
            return name;
        }
    }
    return nullptr;
}

void Field(const char* label, const char* fmt, ...) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}

} // namespace

void App::DrawSoundViewer() {
    if (!mShowSounds) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(880, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Sounds", &mShowSounds)) {
        ImGui::End();
        return;
    }
    if (!mO2rLoaded) {
        ImGui::TextWrapped("Open a bk.o2r to browse its sounds.");
        if (ImGui::Button("Open bk.o2r...")) {
            OpenO2r();
        }
        ImGui::End();
        return;
    }

    SoundView& view = mSoundView;
    if (view.paths.empty()) {
        view.paths = Lightbulb::ListO2rSoundPaths(view.showInstruments ? "instruments" : "sfx");
    }

    ImGui::BeginChild("##sndlist", ImVec2(view.listW, 0), true);
    {
        if (ImGui::RadioButton("Sound effects", !view.showInstruments) && view.showInstruments) {
            view.showInstruments = false;
            view.paths.clear();
            view.sel = -1;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Instruments", view.showInstruments) && !view.showInstruments) {
            view.showInstruments = true;
            view.paths.clear();
            view.sel = -1;
        }
        ImGui::Separator();
        const int clicked =
            Lightbulb::ui::AssetPicker("snd", view.paths, view.filter, sizeof(view.filter), view.sel, "sounds");
        if (clicked >= 0) {
            view.sel = clicked;
        }
    }
    ImGui::EndChild();

    Lightbulb::ui::VerticalSplitter("##sndsplit", view.listW);

    ImGui::BeginChild("##sndview", ImVec2(0, 0), false);
    {
        if (view.sel != view.curSel) {
            view.curSel = view.sel;
            view.curOk = view.sel >= 0 && view.sel < (int)view.paths.size() &&
                         Lightbulb::LoadO2rSound(view.paths[view.sel], view.cur);
            if (view.curOk && view.autoPlay) {
                Play(view.cur.pcm, view.pitched ? Lightbulb::SoundPitchRatio(view.cur) : 1.0f);
            }
        }
        const Lightbulb::O2rSound& sound = view.cur;
        if (!view.curOk) {
            ImGui::TextDisabled("(Select a sound)");
            ImGui::EndChild();
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted(ShortName(sound.path));
        const float ratio = Lightbulb::SoundPitchRatio(sound);
        const float seconds = sound.pcm.empty() ? 0.0f
                                                : static_cast<float>(sound.pcm.size()) /
                                                      (static_cast<float>(Lightbulb::kSoundBankRate) * ratio);

        if (ImGui::Button("Play")) {
            Play(sound.pcm, view.pitched ? ratio : 1.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            Stop();
        }
        ImGui::SameLine();
        ImGui::Checkbox("At its pitch", &view.pitched);
        ImGui::SameLine();
        ImGui::Checkbox("Play on select", &view.autoPlay);
        ImGui::SameLine();
        if (ImGui::Button("Export .wav...")) {
            std::string out;
            const std::string suggested = std::string(ShortName(sound.path)) + ".wav";
            if (Lightbulb::SaveFileDialog("Export sound", { { "WAV audio", { "*.wav" } } }, suggested, out)) {
                view.lastExportOk = WriteWav(out, sound.pcm, (int)(Lightbulb::kSoundBankRate * ratio));
                view.lastExport = out;
            }
        }
        if (!view.lastExport.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled(view.lastExportOk ? "saved" : "export failed");
        }

        if (sAudioFailed) {
            Lightbulb::ui::TextDisabledWrapped("(no audio device; preview unavailable)");
        }

        if (!sound.pcm.empty()) {
            const ImVec2 size(ImGui::GetContentRegionAvail().x, 90.0f);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                                ImGui::GetColorU32(ImGuiCol_FrameBg));
            const float mid = origin.y + size.y * 0.5f;
            draw->AddLine(ImVec2(origin.x, mid), ImVec2(origin.x + size.x, mid), ImGui::GetColorU32(ImGuiCol_Border));
            const int columns = std::max(1, (int)size.x);
            const size_t per = std::max<size_t>(1, sound.pcm.size() / (size_t)columns);
            for (int c = 0; c < columns; c++) {
                const size_t begin = (size_t)c * per;
                if (begin >= sound.pcm.size()) {
                    break;
                }
                const size_t end = std::min(begin + per, sound.pcm.size());
                int16_t lo = 0, hi = 0;
                for (size_t i = begin; i < end; i++) {
                    lo = std::min(lo, sound.pcm[i]);
                    hi = std::max(hi, sound.pcm[i]);
                }
                const float x = origin.x + (float)c;
                const float y0 = mid - (float)hi / 32768.0f * (size.y * 0.5f);
                const float y1 = mid - (float)lo / 32768.0f * (size.y * 0.5f);
                draw->AddLine(ImVec2(x, y0), ImVec2(x, y1), ImGui::GetColorU32(ImGuiCol_PlotLines));
            }
            ImGui::Dummy(size);
        }

        ImGui::Separator();

        if (ImGui::BeginTable("##sndspec", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            Field("Length", "%.2f s", seconds);
            Field("Plays at", "%d Hz, mono", (int)(Lightbulb::kSoundBankRate * ratio));
            Field("Loops", "%s", sound.hasLoop ? "yes" : "no");
            if (view.showInstruments) {
                // Real ranges here; only instrument 0 repurposes them for sfx.
                Field("Key range", "%d-%d", sound.keyMin, sound.keyMax);
                Field("Velocity", "%d-%d", sound.velocityMin, sound.velocityMax);
            } else if (const int chain = Lightbulb::SoundChainTarget(sound); chain >= 0) {
                // Chain index counts within instrument 0; show it as an sfx id.
                const int self = SfxIdFromName(ShortName(sound.path));
                const int target = (self >= 0x3E9 ? 0x3E9 : 0) + chain;
                const char* name = FindSfxById(view.paths, target);
                if (name != nullptr) {
                    Field("Chain", "plays %s after %d frames", name, Lightbulb::SoundChainDelayFrames(sound));
                } else {
                    Field("Chain", "plays 0x%03X after %d frames", target, Lightbulb::SoundChainDelayFrames(sound));
                }
            }
            if (!view.showInstruments) {
                if (sound.userCount == 0) {
                    Field("Played by", "nothing by name");
                } else if (sound.userCount == 1) {
                    Field("Played by", "1 actor");
                } else {
                    Field("Played by", "%u actors", sound.userCount);
                }
            }
            ImGui::EndTable();
        }

        if (!view.showInstruments && sound.userCount > 1) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.35f, 1.0f));
            ImGui::TextWrapped("Used by multiple actors.");
            ImGui::PopStyleColor();
        }

        if (ImGui::CollapsingHeader("Soundfont details")) {
            if (ImGui::BeginTable("##snddec", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                Field("Encoding", "%s, %u bytes, %d samples", sound.waveType == 0 ? "ADPCM" : "raw 16-bit",
                      sound.encodedBytes, (int)sound.pcm.size());
                Field("Pitch", "keyBase %d%+d cents -> %.3fx", sound.keyBase, sound.detune, ratio);
                if (!view.showInstruments) {
                    Field("Reverb send", "%d/15", Lightbulb::SoundReverbSend(sound));
                    Field("Volume group", "%d", Lightbulb::SoundVolumeGroup(sound));
                    Field("Positional", "%s", Lightbulb::SoundIsPositional(sound) ? "yes (decayTime -1)" : "no");
                }
                Field("Pan / volume", "%d / %d", sound.samplePan, sound.sampleVolume);
                if (sound.hasEnvelope) {
                    Field("Envelope", "attack %d us to %d, decay %d us to %d, release %d us", sound.attackTime,
                          sound.attackVolume, sound.decayTime, sound.decayVolume, sound.releaseTime);
                }
                if (sound.hasLoop) {
                    Field("Loop points", "%u..%u", sound.loopStart, sound.loopEnd);
                }
                if (sound.hasBook) {
                    Field("Codebook", "order %d, %d predictors", sound.bookOrder, sound.bookNpredictors);
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("velocityMin %u  velocityMax %u  keyMin 0x%02X  keyMax 0x%02X  flags 0x%02X",
                                sound.velocityMin, sound.velocityMax, sound.keyMin, sound.keyMax, sound.flags);
        }

        ImGui::Separator();
        Lightbulb::ui::TextDisabledWrapped("A mod .o2r carrying this path replaces just this sound. Importing your "
                                           "own audio needs the encoder, which is not written yet.");
    }
    ImGui::EndChild();

    ImGui::End();
}
