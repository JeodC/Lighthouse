#include "O2rImport.h"

extern "C" {
#include "core2/camera.h"
#include "functions.h"
#include "prop.h"
#include "ultra64.h"
}

#include <cstring>
#include <memory>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/type/Blob.h>
#include <string>

extern "C" {
enum {
    SETUP_TAG_END = 0,
    SETUP_TAG_CUBES = 1,
    SETUP_TAG_UNUSED = 2,
    SETUP_TAG_CAMERAS = 3,
    SETUP_TAG_LIGHTING = 4,
};

s32 __ncCameraNodeList_capacity(void);
void lh_setCubeBounds(const s32 min[3], const s32 max[3]);
void cubeList_free(void);
void cubeList_init(void);
s32 ncCameraNodeList_getNodeType(int camera_node_index);
s32 ncCameraNodeList_nodeIsValid(int camera_node_index);
void ncCameraNodeList_init(void);
void lightingVectorList_fromFile(File* file_ptr);
void cubeList_fromFile(File* file_ptr);
void ncCameraNodeList_fromFile(File* file_ptr);
}

namespace Lightbulb {
namespace {
SetupScene* sFillScene = nullptr;

// A waypoint reuses the 20-byte node slot as a float and four big-endian words.
// Rebuild the words, then split them per Struct_glspline_t1.
void decodeScriptLeg(const NodeProp* node, SetupNode& out) {
    const uint32_t word0 = ((uint32_t)(uint16_t)node->x << 16) | (uint16_t)node->y;
    const uint32_t word1 = ((uint32_t)(uint16_t)node->z << 16) | ((uint32_t)(node->radius & 0x1FF) << 7) |
                           ((uint32_t)(node->bit6 & 0x3F) << 1) | (node->bit0 & 1);
    const uint32_t word2 = ((uint32_t)node->unk8 << 16) | ((uint32_t)node->unkA << 8) | node->padB;
    const uint32_t word3 = ((uint32_t)(node->yaw & 0x1FF) << 23) | (node->scale & 0x7FFFFF);

    std::memcpy(&out.legFraction, &word0, sizeof(out.legFraction));

    out.legLinkUid = (uint16_t)((word1 >> 20) & 0xFFF);
    out.legModeBits = (uint8_t)((word1 >> 18) & 3);
    out.legBlend = (uint8_t)((word1 >> 16) & 3);
    out.legPitch = (uint16_t)((word1 >> 7) & 0x1FF);
    out.legHeadingMode = (uint8_t)((word1 >> 4) & 7);
    out.legAnimMode = (uint8_t)((word1 >> 1) & 7);

    out.legAnim = (uint16_t)((word2 >> 22) & 0x3FF);
    out.legAnimDuration = (uint16_t)((word2 >> 11) & 0x7FF);
    out.legApply = (uint8_t)((word2 >> 8) & 7);
    // word2 bits 0..7 are scratch the path builder writes into at load; nothing authored.

    out.legYaw = (uint16_t)((word3 >> 23) & 0x1FF);
    out.legSpeed = (uint16_t)((word3 >> 12) & 0x7FF);
    out.legPause = (uint16_t)((word3 >> 1) & 0x7FF);
    out.legNoHeadingLookup = (uint8_t)(word3 & 1);

    out.legSmoothTurn = (uint8_t)(node->unk10_6 & 1);
    out.legPauseIsAlt = (uint8_t)((node->pad10_5 >> 3) & 1);
}

bool visitNode(NodeProp* node) {
    SetupNode outNode;
    outNode.pos[0] = node->x;
    outNode.pos[1] = node->y;
    outNode.pos[2] = node->z;
    outNode.radius = node->radius;
    outNode.category = node->bit6;
    outNode.script = (uint8_t)node->bit0;
    if (outNode.script) {
        decodeScriptLeg(node, outNode);
    }
    outNode.id = node->unk8;
    outNode.yawRaw = node->yaw;
    outNode.scaleRaw = node->scale;
    outNode.pathUid = (uint8_t)(node->unk10_31 & 0xFF);
    outNode.pathNext = (uint8_t)(node->unk10_19 & 0xFF);
    sFillScene->nodes.push_back(outNode);
    return true;
}

bool visitProp(Prop* prop) {
    SetupProp outProp;
    if (prop->markerFlag) {
        outProp.type = 1;
        outProp.pos[0] = prop->actorProp.x;
        outProp.pos[1] = prop->actorProp.y;
        outProp.pos[2] = prop->actorProp.z;
    } else if (prop->unk8_1) {
        outProp.type = 2;
        outProp.id = prop->modelProp.modelId;
        outProp.pos[0] = prop->modelProp.unk4[0];
        outProp.pos[1] = prop->modelProp.unk4[1];
        outProp.pos[2] = prop->modelProp.unk4[2];
        outProp.yaw = prop->modelProp.yaw;
        outProp.roll = prop->modelProp.roll;
        outProp.scale = prop->modelProp.scale;
    } else {
        outProp.type = 0;
        outProp.id = prop->spriteProp.spriteId;
        outProp.pos[0] = prop->spriteProp.unk4[0];
        outProp.pos[1] = prop->spriteProp.unk4[1];
        outProp.pos[2] = prop->spriteProp.unk4[2];
        outProp.scale = static_cast<uint8_t>(prop->spriteProp.scale);
        outProp.spritePhase = static_cast<uint8_t>(prop->spriteProp.unk8_10);
    }
    sFillScene->props.push_back(outProp);
    return true;
}

} // namespace

bool LoadO2rSetup(const std::string& path, SetupScene& out) {
    out = SetupScene{};
    out.path = path;

    const size_t slash = path.find_last_of('/');
    const char* name = path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
    if (std::strncmp(name, "ASSET_", 6) != 0) {
        return false;
    }
    const uint32_t assetId = static_cast<uint32_t>(std::strtoul(name + 6, nullptr, 16));

    {
        auto resources = Ship::Context::GetRawInstance()->GetResourceManager();
        auto blob = std::static_pointer_cast<Ship::Blob>(resources->LoadResource(path));
        if (!blob || blob->Data.size() < 26 || blob->Data[0] != 0x01 || blob->Data[1] != 0x01) {
            return false;
        }
        s32 boundsMin[3], boundsMax[3];
        std::memcpy(boundsMin, blob->Data.data() + 2, 12);
        std::memcpy(boundsMax, blob->Data.data() + 14, 12);
        lh_setCubeBounds(boundsMin, boundsMax);
        std::memcpy(out.boundsMin, boundsMin, 12);
        std::memcpy(out.boundsMax, boundsMax, 12);
    }
    cubeList_free();
    cubeList_init();

    // The decomp's readers fill the cube, camera and lighting lists as a side effect,
    // then func_80305290 walks the props and nodes back out through our visitors.
    File* file = file_open(static_cast<enum asset_e>(assetId));
    if (file == nullptr) {
        return false;
    }

    ncCameraNodeList_init();
    while (file_isNextByteExpected(file, SETUP_TAG_END) == 0) {
        if (file_isNextByteExpected(file, SETUP_TAG_UNUSED)) {
        } else if (file_isNextByteExpected(file, SETUP_TAG_CUBES)) {
            cubeList_fromFile(file);
        } else if (file_isNextByteExpected(file, SETUP_TAG_CAMERAS)) {
            ncCameraNodeList_fromFile(file);
        } else if (file_isNextByteExpected(file, SETUP_TAG_LIGHTING)) {
            lightingVectorList_fromFile(file);
        } else {
            break;
        }
    }
    file_close(file);

    sFillScene = &out;
    func_80305290(visitNode, visitProp);
    sFillScene = nullptr;

    for (int nodeIndex = 0; nodeIndex < __ncCameraNodeList_capacity(); nodeIndex++) {
        if (!ncCameraNodeList_nodeIsValid(nodeIndex)) {
            continue;
        }
        SetupCamera camera;
        camera.index = static_cast<int16_t>(nodeIndex);
        camera.type = static_cast<uint8_t>(ncCameraNodeList_getNodeType(nodeIndex));
        switch (camera.type) {
            case 1: {
                PivotCameraNode* cameraNode = ncCameraNodeList_getPivotCameraNode(nodeIndex);
                cameraNodeType1_getPosition(cameraNode, camera.pos);
                break;
            }
            case 2: {
                StaticCameraNode* cameraNode = ncCameraNodeList_getStaticCameraNode(nodeIndex);
                cameraNodeType2_getPosition(cameraNode, camera.pos);
                cameraNodeType2_getPitchYawRoll(cameraNode, camera.pitchYawRoll);
                break;
            }
            case 3: {
                ZoomCameraNode* cameraNode = ncCameraNodeList_getZoomCameraNode(nodeIndex);
                cameraNodeType3_getPosition(cameraNode, camera.pos);
                break;
            }
            default:
                break;
        }
        out.cameras.push_back(camera);
    }

    out.loaded = true;
    return true;
}

} // namespace Lightbulb
