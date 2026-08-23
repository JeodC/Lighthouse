extern "C" {
#include <libultra/exception.h>
}

#include <stdlib.h>
#include <string.h>

#include "App.h"
#include "O2rImport.h"

#include <spdlog/spdlog.h>

#include "port/Enhancements/Events/Hooks/Events.h"

extern "C" {
#include "core2/animmtxlist.h"
#include "core2/timedfunc.h"
#include "port/DevTools/ThreadWatchdog.h"
#include "core2/modelRender.h"
#include "functions.h"
#include "libultraship/libultra/gbi.h"
#include <libultra/rcp.h>
#include <ultra64.h>

#define DEFAULT_FRAMEBUFFER_WIDTH 292
#define DEFAULT_FRAMEBUFFER_HEIGHT 216

// Animation scratch
static f32 sFrameDelta = 0.0f;
static AnimMtxList* sEditorBones = NULL;
static s32 sEditorBonesCap = 0;

// The bounds a setup's cubes are read against
static s32 sCubeMin[3] = { 0, 0, 0 };
static s32 sCubeMax[3] = { 0, 0, 0 };

// Where the music thinks the listener is
static s32 sListenerPos[3] = { 0, 0, 0 };
static s32 sListenerMap = 0;
static s32 sListenerUnderwater = 0;

s32 gFramebufferWidth = DEFAULT_FRAMEBUFFER_WIDTH;
s32 gFramebufferHeight = DEFAULT_FRAMEBUFFER_HEIGHT;
alignas(0x40) u8 D_8000E800[DEFAULT_FRAMEBUFFER_WIDTH * DEFAULT_FRAMEBUFFER_HEIGHT * sizeof(u16)];
u16 gFramebuffers[2][DEFAULT_FRAMEBUFFER_WIDTH * DEFAULT_FRAMEBUFFER_HEIGHT];

void BK_LOG_WARN(const char* fmt, ...) {
}
void BK_LOG_DEBUG(const char* fmt, ...) {
}
int port_audioHeld(void) {
    return 0;
}
void func_8033A5B8(BoneTransformList* self, s32 bone_id, f32 arg2[4], f32 scale[3], f32 arg4[3]) {
}
bool vec4f_isZero(f32 arg0[4]) {
    return 1;
}
void func_80345274(f32 arg0[4], f32 arg1[3][3]) {
}
void assetCache_free(void* arg0) {
}
char* ResourceMgr_LoadByAssetId(u32 assetId);
void* assetcache_get(enum asset_e assetId) {
    return ResourceMgr_LoadByAssetId(assetId);
}
void bk_free(void* ptr) {
    free(ptr);
}
void* bk_malloc(size_t size) {
    return malloc(size);
}
void* bk_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}
f32 core2_C3A40_getIntensity(f32 curve_values[4], f32 position) {
    return 0;
}
void* defrag(void* ptr) {
    return ptr;
}
bool func_802559A0(void) {
    return 0;
}
bool func_802E4A08(void) {
    return 0;
}
s32 func_802F9AA8(enum sfx_e uid) {
    (void)uid;
    return 0;
}
void func_802F9DB8(s32 a, f32 b, f32 c, f32 d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}
void func_802F9F80(s32 a, f32 b, f32 c, f32 d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}
void func_802FA028(s32 arg0, s32 arg1) {
}
void func_802FA060(s32 a, s32 b, s32 c, f32 d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}
f32 func_8030E200(u8 a) {
    (void)a;
    return 0;
}
void func_80349AD0(void) {
}
void gcsfx_playWithPitch(enum sfx_e uid, f32 arg1, s32 arg2) {
}
void points_to_boundingBoxWithMargin(f32 a[3], f32 b[3], f32 m, f32 mn[3], f32 mx[3]) {
    (void)a;
    (void)b;
    (void)m;
    (void)mn;
    (void)mx;
}
void port_applyModelDrawDistanceCull(int* fadeFlag, float* cullMult, float* cullDist) {
    *fadeFlag = 0;
    *cullMult = 1.0e12f;
    *cullDist = 3.0e38f;
}
int port_geoCullDraw(int type, const void* cmd, const void* modelBin, int drawnVanilla, const unsigned char* areaIds,
                     int areaCount, int detail0, int detail1) {
    return 1;
}
int port_mirror_bakeCounterScale(void) {
    return 0;
}
int port_shouldDisableLOD(void) {
    return 1;
}
f32 randf(void) {
    return 0;
}
f32 randf2(f32 min, f32 max) {
    return 0;
}
s32 randi2(s32 min, s32 max) {
    return 0;
}
void sfxSource_func_8030E2C4(u8 a) {
    (void)a;
}
s32 sfxSource_getSampleRate(u8 indx) {
    return 0;
}
void sfxSource_setunk43_7ByIndex(u8 a, int b) {
    (void)a;
    (void)b;
}
u8 sfxsource_createSfxsourceAndReturnIndex(void) {
    return 0;
}
void sfxsource_freeSfxsourceByIndex(u8 a) {
    (void)a;
}
void sfxsource_playSfxAtVolume(u8 a, f32 b) {
    (void)a;
    (void)b;
}
void sfxsource_setSampleRate(u8 a, s32 b) {
    (void)a;
    (void)b;
}
void sfxsource_setSfxId(u8 indx, enum sfx_e uid) {
}
f32 time_getDelta(void) {
    return sFrameDelta;
}
void lh_setFrameDelta(f32 dt) {
    sFrameDelta = dt;
}
void lh_setEditorBones(const f32* mats, s32 count) {
    s32 i;
    if (mats == NULL || count <= 0)
        return;
    if (sEditorBonesCap < count) {
        sEditorBones = (AnimMtxList*)bk_realloc(sEditorBones, sizeof(AnimMtxList) + count * sizeof(MtxF));
        sEditorBonesCap = count;
    }
    sEditorBones->size = count;
    sEditorBones->capacity = count;
    mlMtxIdent();
    mlMtxGet(&sEditorBones->default_matrix);
    for (i = 0; i < count; i++) {
        memcpy(&sEditorBones->data[i], mats + i * 16, sizeof(MtxF));
    }
    modelRender_func_8033A444(sEditorBones);
}
void vec3fArray_set_vec3f(Vec3fArray* self, s32 index, f32 src[3]) {
}
void port_camera_applyWsYawFix(float rotation[3]) {
    (void)rotation;
}
float port_drawDistanceMul(void) {
    return 1.0f;
}
void port_viewport_applyMirror(Gfx** gfx, Mtx** mtx) {
    (void)gfx;
    (void)mtx;
}
void FrameInterpolation_RecordCameraPosition(const float pos[3]) {
    (void)pos;
}
void FrameInterpolation_RecordCameraProjectionRotation(void* rollMtx, float rollDeg, void* pitchMtx, float pitchDeg,
                                                       void* yawMtx, float yawDeg) {
    (void)rollMtx;
    (void)rollDeg;
    (void)pitchMtx;
    (void)pitchDeg;
    (void)yawMtx;
    (void)yawDeg;
}

void FrameInterpolation_RecordOpenChild(const void* key, uintptr_t id) {
    (void)key;
    (void)id;
}
void FrameInterpolation_RecordOpenChildHash3(const char* key, u64 a, u64 b, u64 c) {
    (void)key;
    (void)a;
    (void)b;
    (void)c;
}
void FrameInterpolation_RecordCloseChild(void) {
}
void FrameInterpolation_RecordMatrixToMtx(void* dst, const f32 src[4][4]) {
    (void)dst;
    (void)src;
}

bool EventSystem_Should(VBehaviorID id, uint32_t result, ...) {
    (void)id;
    return (bool)result;
}

void port_modelRender_snapshotAnimVertices(Gfx** gfx, void* vertices, s32 count) {
    (void)gfx;
    (void)vertices;
    (void)count;
}
void port_modelRenderResetTLUT(Gfx** gfx) {
    (void)gfx;
}

void port_spriteAltRegisterChunk(const void* chunkAddr, const char* path) {
    (void)chunkAddr;
    (void)path;
}
void port_spriteAltUnregisterChunk(const void* chunkAddr) {
    (void)chunkAddr;
}

void assetcache_release(void* p) {
    (void)p;
}
s32 D_80371F78 = 0;
bool func_8033B388(BKSprite** sprite_ptr, BKSpriteDisplayData** arg1) {
    (void)sprite_ptr;
    (void)arg1;
    return 0;
}
void func_80334448(NodeProp* arg0, ActorMarker* arg1) {
    (void)arg0;
    (void)arg1;
}
Actor* marker_getActor(ActorMarker* m) {
    (void)m;
    return 0;
}
void player_getPosition(f32 dst[3]) {
    dst[0] = dst[1] = dst[2] = 0.0f;
}
enum level_e level_get(void) {
    return (enum level_e)0;
}

void lh_setAudioListener(const s32 pos[3], s32 mapId) {
    s32 i;
    for (i = 0; i < 3; i++) {
        sListenerPos[i] = pos[i];
    }
    sListenerMap = mapId;
}

enum map_e gsworld_getMap(void) {
    return (enum map_e)sListenerMap;
}

s32 lh_getListenerMap(void) {
    return sListenerMap;
}

void lh_setListenerMap(s32 mapId) {
    sListenerMap = mapId;
}

void lh_setListenerUnderwater(s32 underwater) {
    sListenerUnderwater = underwater;
}

void player_getPosition_s32(s32 dst[3]) {
    s32 i;
    for (i = 0; i < 3; i++) {
        dst[i] = sListenerPos[i];
    }
}
s32 gsworld_getUnk0(void) {
    return 0;
}
void lh_setCubeBounds(const s32 min[3], const s32 max[3]) {
    s32 i;
    for (i = 0; i < 3; i++) {
        sCubeMin[i] = min[i];
        sCubeMax[i] = max[i];
    }
}
void mapModel_getCubeBounds(s32 min[3], s32 max[3]) {
    s32 i;
    for (i = 0; i < 3; i++) {
        min[i] = sCubeMin[i];
        max[i] = sCubeMax[i];
    }
}
void spawnQueue_func_802C39D4(void) {
}
s32 globalTimer_getTime(void) {
    static s32 t;
    return ++t;
}
s32 volatileFlag_get(enum volatile_flags_e index) {
    (void)index;
    return 0;
}
void suSetSpriteScale(Actor* a, f32 scale) {
    (void)a;
    (void)scale;
}
BKSprite* codeB3A80_getSprite(enum asset_e sprite_id, BKSpriteDisplayData** arg1) {
    (void)sprite_id;
    (void)arg1;
    return 0;
}
bool codeB3A80_releaseSprite(void** sprite_ptr, BKSpriteDisplayData** arg1) {
    (void)sprite_ptr;
    (void)arg1;
    return 0;
}
s32 dummy_func_80320248(void) {
    return 0;
}
int gcparade_8031B4CC(void) {
    return 0;
}
int func_80254BC4(int arg0) {
    (void)arg0;
    return 0;
}
void* func_80254BD0(s32* size, u32 arg1) {
    (void)size;
    (void)arg1;
    return 0;
}
int func_80255B08(int arg0) {
    (void)arg0;
    return 0;
}
bool func_8028F280(void) {
    return 0;
}
void func_8028FAB0(f32 arg0[3]) {
    arg0[0] = arg0[1] = arg0[2] = 0.0f;
}
s32 func_8029453C(void) {
    return 0;
}
int func_802D6A38(enum map_e map_id) {
    (void)map_id;
    return 0;
}
void func_8031B5C4(s32 arg0) {
    (void)arg0;
}
void func_80320B24(void* arg0, void* arg1, void* arg2) {
    (void)arg0;
    (void)arg1;
    (void)arg2;
}
ActorProp* func_80320EB0(ActorMarker* m, f32 a, s32 b) {
    (void)m;
    (void)a;
    (void)b;
    return 0;
}
int func_80320ED8(ActorMarker* m, f32 a, s32 b) {
    (void)m;
    (void)a;
    (void)b;
    return 0;
}
f32 func_803243D0(struct56s* arg0, f32 arg1[3]) {
    (void)arg0;
    (void)arg1;
    return 0.0f;
}
void func_80326C24(s32 arg0) {
    (void)arg0;
}
void func_8032AB84(Actor* arg0) {
    (void)arg0;
}
void func_8032ACA8(Actor* arg0) {
    (void)arg0;
}
bool func_80340020(Struct83s* a, f32 b[3], f32 c[3], f32 d, f32* e, BKVertexList* f, f32 g[3], f32 h[3]) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)h;
    return 0;
}
void func_80340200(Struct83s* a, f32 b[3], f32 c[3], f32 d, f32 e[3], s16 f[3], BKVertexList* g, f32 h[3]) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)h;
}
s32 func_80341C78(s32 arg0[3]) {
    (void)arg0;
    return 0;
}
s32 func_80341D5C(s32 arg0[3], s32 arg1[3]) {
    (void)arg0;
    (void)arg1;
    return 0;
}
struct56s* func_80342038(s32 indx) {
    (void)indx;
    return 0;
}
BKModelBin* propModelList_getModel(s32 id) {
    (void)id;
    return 0;
}
BKModelBin* propModelList_getModelIfActive(s32 id) {
    (void)id;
    return 0;
}
BKSprite* propModelList_getSprite(s32 id) {
    (void)id;
    return 0;
}
f32 propModelList_getScale(Prop* p) {
    (void)p;
    return 1.0f;
}
void propModelList_setScale(Prop* p, f32 s) {
    (void)p;
    (void)s;
}
void propModelList_drawModel(Gfx** gfx, Mtx** mtx, Vtx** vtx, f32 pos[3], f32 rot[3], f32 scale, s32 a, Cube* cube) {
    (void)gfx;
    (void)mtx;
    (void)vtx;
    (void)pos;
    (void)rot;
    (void)scale;
    (void)a;
    (void)cube;
}
void propModelList_drawSprite(Gfx** gfx, Mtx** mtx, Vtx** vtx, f32 pos[3], f32 a, s32 b, Cube* cube, s32 c, s32 d,
                              s32 e, s32 f, s32 g) {
    (void)gfx;
    (void)mtx;
    (void)vtx;
    (void)pos;
    (void)a;
    (void)b;
    (void)cube;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
}
void port_warnNodePropSplit(s32 splitIndex, s32 nodeCnt) {
    (void)splitIndex;
    (void)nodeCnt;
}
void port_warnPropNotInCube(s32 index, s32 propCnt) {
    (void)index;
    (void)propCnt;
}
void port_spriteDisplayCache_clear(void) {
}

void bkmemcpy64(void* dest, void* src, s32 size) {
    memcpy(dest, src, size);
}
void bkmemset64(void* dest, s32 value, s32 size) {
    memset(dest, value, size);
}

u32 func_8025C29C(u32* seed) {
    u32 result = seed[0] ^ seed[1];
    seed[0] = (seed[0] >> 1) ^ seed[1];
    seed[1] = (seed[1] << 1) ^ seed[0];
    return result;
}

int port_getRomhackSkyboxFull(int scene_id, int out_models[3], float out_scales[3], float out_rotations[3]) {
    (void)scene_id;
    (void)out_models;
    (void)out_scales;
    (void)out_rotations;
    return 0;
}
void drawRectangle2D(Gfx** gfx, s32 x, s32 y, s32 w, s32 h, s32 r, s32 g, s32 b) {
    (void)gfx;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)r;
    (void)g;
    (void)b;
}

void core1_15B30_addAudioTaskData(Acmd* start, Acmd* end, OSMesgQueue* replyQueue, OSMesg replyMesg) {
    (void)start;
    (void)end;
    osSendMesg(replyQueue, replyMesg, OS_MESG_NOBLOCK);
}

int gPortResetPending = 0;
u8 osTvType = 1;

void ThreadWatchdog_Beat(WatchdogThread id) {
    (void)id;
}
void func_8028F918(s32 arg0) {
    (void)arg0;
}
void func_802BE720(void) {
}
void func_802D6114(void) {
}
void func_8030E760(enum sfx_e uid, f32 arg1, s32 arg2) {
    (void)uid;
    (void)arg1;
    (void)arg2;
}
void func_8030E9C4(enum sfx_e uid, f32 arg1, u32 arg2, f32 arg3[3], f32 arg4, f32 arg5) {
    (void)uid;
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
}
bool gcdialog_showDialog(s32 text_id, s32 arg1, f32* pos, ActorMarker* marker,
                         void (*callback)(ActorMarker*, enum asset_e, s32),
                         void (*arg5)(ActorMarker*, enum asset_e, s32)) {
    (void)text_id;
    (void)arg1;
    (void)pos;
    (void)marker;
    (void)callback;
    (void)arg5;
    return FALSE;
}
void jiggy_spawn(enum jiggy_e jiggy_id, f32 pos[3]) {
    (void)jiggy_id;
    (void)pos;
}
void ncStaticCamera_exit(void) {
}
void ncStaticCamera_setToNode(s32 node) {
    (void)node;
}
void assetcache_func_8033B788(void) {
}
void func_80247F9C(s32 arg0) {
    (void)arg0;
}
void gcdebugText_showLargeValue(s32 arg0, s32 arg1) {
    (void)arg0;
    (void)arg1;
}
void gcdebugText_pauseThread(void) {
}
bool sns_get_item_state(enum StopNSwop_Item item, s32 set) {
    (void)item;
    (void)set;
    return FALSE;
}
int port_getRomhackMusic(int map_id, int* out_track1, int* out_track2) {
    (void)map_id;
    (void)out_track1;
    (void)out_track2;
    return 0;
}
// Not a variant selector like the jiggy and flag stubs below: Spiral Mountain gates its
// whole near-the-lair crossfade on this, so a fresh-save answer would mean the music never
// changes there. The editor shows the game taught.
int chmole_learnedAllSpiralMountainAbilities(void) {
    return 1;
}
int func_802D686C(void) {
    return 0;
}
void func_802F9FD0(s32 arg0, f32 arg1, f32 arg2, f32 arg3) {
    (void)arg0;
    (void)arg1;
    (void)arg2;
    (void)arg3;
}
bool func_80309D58(f32 arg0[3], s32 arg1) {
    (void)arg0;
    (void)arg1;
    return FALSE;
}
s32 gameSelect_getGameNumber(void) {
    return 0;
}
int gctransition_8030BDC0(void) {
    return 0;
}
s32 getGameMode(void) {
    return 0;
}
u32 jiggyscore_isCollected(enum jiggy_e jiggy_id) {
    (void)jiggy_id;
    return 0;
}
s32 levelSpecificFlags_get(s32 i) {
    (void)i;
    return 0;
}
bool player_isDead(void) {
    return FALSE;
}
enum bswatergroup_e player_getWaterState(void) {
    return sListenerUnderwater ? BSWATERGROUP_2_UNDERWATER : BSWATERGROUP_0_NONE;
}
bool player_is_present(void) {
    return FALSE;
}
}
