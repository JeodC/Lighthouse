#include "O2rImport.h"

#include <cmath>
#include <cstring>
#include <map>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/type/Blob.h>

namespace Lightbulb {
namespace {

std::map<std::string, O2rSound> sCache;

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : mData(data), mSize(size) {
    }
    bool Ok() const {
        return !mFailed;
    }
    uint8_t Read8() {
        if (mPos + 1 > mSize) {
            mFailed = true;
            return 0;
        }
        return mData[mPos++];
    }
    int8_t ReadS8() {
        return static_cast<int8_t>(Read8());
    }
    int16_t ReadS16() {
        const uint16_t lo = Read8();
        const uint16_t hi = Read8();
        return static_cast<int16_t>(lo | (hi << 8));
    }
    uint32_t Read32() {
        const uint32_t a = Read8(), b = Read8(), c = Read8(), d = Read8();
        return a | (b << 8) | (c << 16) | (d << 24);
    }
    int32_t ReadS32() {
        return static_cast<int32_t>(Read32());
    }
    const uint8_t* Take(size_t n) {
        if (mFailed || mPos + n > mSize) {
            mFailed = true;
            return nullptr;
        }
        const uint8_t* at = mData + mPos;
        mPos += n;
        return at;
    }
private:
    const uint8_t* mData;
    size_t mSize;
    size_t mPos = 0;
    bool mFailed = false;
};

int16_t Clamp16(int32_t v) {
    return static_cast<int16_t>(v < -32768 ? -32768 : (v > 32767 ? 32767 : v));
}

void DecodeAdpcm(const uint8_t* in, size_t nbytes, const std::vector<int16_t>& book, int npredictors,
                 std::vector<int16_t>& out) {
    if (book.size() < static_cast<size_t>(npredictors) * 16) {
        return;
    }
    const size_t frames = nbytes / 9;
    out.assign(frames * 16 + 16, 0);
    int16_t* dst = out.data() + 16;

    for (size_t f = 0; f < frames; f++) {
        const int shift = *in >> 4;
        int predictor = *in++ & 0xF;
        if (predictor >= npredictors) {
            predictor = 0;
        }
        const int16_t* tbl0 = book.data() + predictor * 16;
        const int16_t* tbl1 = tbl0 + 8;

        for (int half = 0; half < 2; half++) {
            int16_t ins[8];
            const int16_t prev1 = dst[-1];
            const int16_t prev2 = dst[-2];
            for (int j = 0; j < 4; j++) {
                ins[j * 2] = static_cast<int16_t>((((*in >> 4) << 28) >> 28) << shift);
                ins[j * 2 + 1] = static_cast<int16_t>((((*in++ & 0xF) << 28) >> 28) << shift);
            }
            for (int j = 0; j < 8; j++) {
                int32_t acc = tbl0[j] * prev2 + tbl1[j] * prev1 + (ins[j] << 11);
                for (int k = 0; k < j; k++) {
                    acc += tbl1[(j - k) - 1] * ins[k];
                }
                *dst++ = Clamp16(acc >> 11);
            }
        }
    }
    out.erase(out.begin(), out.begin() + 16);
}

} // namespace

float SoundPitchRatio(const O2rSound& s) {
    if (!s.hasKeyMap) {
        return 1.0f;
    }
    const float cents = static_cast<float>(s.keyBase) * 100.0f + static_cast<float>(s.detune) - 6000.0f;
    return std::pow(2.0f, cents / 1200.0f);
}

std::vector<std::string> ListO2rSoundPaths(const std::string& dir) {
    return ListO2rResourcePaths(dir);
}

void ResetSoundCache() {
    sCache.clear();
}

bool LoadO2rSound(const std::string& path, O2rSound& out) {
    const auto cached = sCache.find(path);
    if (cached != sCache.end()) {
        out = cached->second;
        return true;
    }

    auto resources = Ship::Context::GetRawInstance()->GetResourceManager();
    if (!resources) {
        return false;
    }
    auto blob = std::static_pointer_cast<Ship::Blob>(resources->LoadResource(path));
    if (blob == nullptr || blob->Data.empty()) {
        return false;
    }

    O2rSound s;
    s.path = path;
    Reader r(blob->Data.data(), blob->Data.size());

    s.samplePan = r.Read8();
    s.sampleVolume = r.Read8();
    s.flags = r.Read8();

    s.hasEnvelope = r.Read8() != 0;
    if (s.hasEnvelope) {
        s.attackTime = r.ReadS32();
        s.decayTime = r.ReadS32();
        s.releaseTime = r.ReadS32();
        s.attackVolume = r.Read8();
        s.decayVolume = r.Read8();
    }

    s.hasKeyMap = r.Read8() != 0;
    if (s.hasKeyMap) {
        s.velocityMin = r.Read8();
        s.velocityMax = r.Read8();
        s.keyMin = r.Read8();
        s.keyMax = r.Read8();
        s.keyBase = r.Read8();
        s.detune = r.ReadS8();
    }

    s.hasWave = r.Read8() != 0;
    if (s.hasWave) {
        s.waveType = r.Read8();
        s.waveFlags = r.Read8();

        s.hasLoop = r.Read8() != 0;
        if (s.hasLoop) {
            s.loopStart = r.Read32();
            s.loopEnd = r.Read32();
            s.loopCount = r.Read32();
            const uint32_t stateCount = r.Read32();
            if (r.Take(static_cast<size_t>(stateCount) * 2) == nullptr) {
                return false;
            }
        }

        std::vector<int16_t> book;
        s.hasBook = r.Read8() != 0;
        if (s.hasBook) {
            s.bookOrder = r.ReadS32();
            s.bookNpredictors = r.ReadS32();
            const uint32_t count = r.Read32();
            book.resize(count);
            for (uint32_t i = 0; i < count; i++) {
                book[i] = r.ReadS16();
            }
        }

        s.encodedBytes = r.Read32();
        const uint8_t* data = r.Take(s.encodedBytes);
        if (data == nullptr) {
            return false;
        }
        if (s.hasBook) {
            DecodeAdpcm(data, s.encodedBytes, book, s.bookNpredictors, s.pcm);
        }
    }

    s.userCount = r.Read32();

    if (!r.Ok()) {
        return false;
    }

    sCache[path] = s;
    out = s;
    return true;
}
} // namespace Lightbulb
