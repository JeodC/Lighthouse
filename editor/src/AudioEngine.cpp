#include "O2rImport.h"

#include <ship/Context.h>

extern "C" {
#include "core2/timedfunc.h"
#include "core1/core1.h"
#include "model.h"
#include "core1/music.h"
#include "functions.h"
#include "ultra64.h"

void audioManagerThread_entry(void* arg);
void audioManager_init(void);
OSMesgQueue* audioManager_getFrameMesgQueue(void);
OSMesgQueue* audioManager_getReplyMesgQueue(void);
void coMusicPlayer_init(void);
void musicSlot_stopAll(void);
void musicTrack_releaseAll(void);
void coMusicPlayer_update(void);
void timedFuncQueue_init(void);
void timedFuncQueue_update(void);
void OS_EnableThreadEntry(void* entry);
void OS_SetQueueBlocking(OSMesgQueue* queue, int blocking);
void port_noteMainLoopAlive(void);
void core1_ce60_func_8024AF48(void);
void core1_ce60_func_8024BD40(s32 arg0, s32 arg1);
void lh_setAudioListener(const s32 pos[3], s32 mapId);
s32 lh_getListenerMap(void);
void lh_setListenerMap(s32 mapId);
void lh_setListenerUnderwater(s32 underwater);
void core1_ce60_func_8024BD40(s32 arg0, s32 arg1);
void func_8032278C(s32 arg0, s32 arg1);
s32 func_80322714(enum map_e map_id);
extern s32 D_802762C0;
}

namespace Lightbulb {
namespace {

// A triangle's collision type sits in the second byte of its flags; 3 and 4 are the two
// water surfaces.
constexpr s32 kCollisionTypeWater = 3;
constexpr s32 kCollisionTypeWater2 = 4;

bool sStarted = false;

} // namespace

bool AudioEngineRunning() {
    return sStarted;
}

bool StartAudioEngine() {
    if (sStarted) {
        return true;
    }
    auto* context = Ship::Context::GetRawInstance();
    if (context == nullptr) {
        return false;
    }
    Ship::AudioSettings settings;
    settings.SampleRate = 22000;
    settings.SampleLength = 736;
    settings.DesiredBuffered = 2208;
    context->InitAudio(settings);
    OS_EnableThreadEntry((void*)audioManagerThread_entry);
    OS_SetQueueBlocking(audioManager_getFrameMesgQueue(), 1);
    OS_SetQueueBlocking(audioManager_getReplyMesgQueue(), 1);

    timedFuncQueue_init();
    audioManager_init();
    coMusicPlayer_init();
    sStarted = true;
    return true;
}

void PumpAudioEngine() {
    if (!sStarted) {
        return;
    }
    port_noteMainLoopAlive();
    OSMesg tick;
    tick.ptr = NULL;
    osSendMesg(audioManager_getFrameMesgQueue(), tick, OS_MESG_NOBLOCK);
    core1_ce60_func_8024AF48();
    timedFuncQueue_update();
    coMusicPlayer_update();
}

void PlayMusicTrack(int trackId) {
    if (!sStarted) {
        return;
    }
    const enum comusic_e track = (enum comusic_e)trackId;
    func_8025A104(track, gcMusic_getDefaultVolumeForTrack(track));
}

void StopMusic() {
    if (sStarted) {
        func_8025A9D4();
    }
}

void ReleaseMusicTracks() {
    if (sStarted) {
        func_8025A9D4();
        musicTrack_releaseAll();
    }
}

namespace {

bool SurfaceAboveIsWater(BKModelBin* model, const float pos[3]) {
    if (model == nullptr || model->collision_list_offset == 0 || model->vtx_list_offset == 0) {
        return false;
    }
    f32 from[3] = { pos[0], pos[1] - 100.0f, pos[2] };
    f32 to[3] = { pos[0], pos[1] + 7000.0f, pos[2] };
    f32 normal[3] = { 0.0f, 0.0f, 0.0f };
    BKCollisionTriangle* tri =
        func_802E76B0(modelbin_getCollisionList(model), modelbin_getVtxList(model), from, to, normal, 0xF800FF0F);
    if (tri == nullptr) {
        return false;
    }
    const s32 type = (tri->flags >> 16) & 0xFF;
    return type == kCollisionTypeWater || type == kCollisionTypeWater2;
}

} // namespace

void SetAudioListener(const float pos[3], uint16_t mapId, BKModelBin* opaque, BKModelBin* translucent) {
    const s32 at[3] = { (s32)pos[0], (s32)pos[1], (s32)pos[2] };
    lh_setAudioListener(at, mapId);
    lh_setListenerUnderwater(SurfaceAboveIsWater(translucent, pos) || SurfaceAboveIsWater(opaque, pos));
}

int MusicChannelMask() {
    return sStarted ? D_802762C0 : 0;
}

int MusicListenerMap() {
    return lh_getListenerMap();
}

bool MusicSlotIdle() {
    return !sStarted || musicSlot_hasStopped(0);
}

void StopLevelMusic() {
    if (!sStarted) {
        return;
    }
    core1_ce60_func_8024BD40(2, 1);
    func_8025A9D4();
}

void StartLevelMusic(uint16_t mapId) {
    if (!sStarted) {
        return;
    }
    func_8025A9D4();
    lh_setListenerMap(mapId);
    core1_ce60_func_8024BD40(2, 2);
    func_8032278C(2, 2);
}

int LevelMusicTrack(uint16_t mapId) {
    return core2_9B650_getMusicTrackFromMap((enum map_e)mapId);
}

int LevelMusicTrack2(uint16_t mapId) {
    return func_80322714((enum map_e)mapId);
}

} // namespace Lightbulb
