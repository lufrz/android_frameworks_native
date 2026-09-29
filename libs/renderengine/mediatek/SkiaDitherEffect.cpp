/*
 * Copyright 2026 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "SkiaDitherEffect.h"

#include <SkImageInfo.h>
#include <SkSamplingOptions.h>
#include <SkString.h>
#include <SkTileMode.h>
#include <log/log.h>

#include <cstdint>
#include <utility>

namespace android::renderengine::skia {

SkiaDitherEffect::SkiaDitherEffect() {
    if (!mDitherBmp.tryAllocPixels(SkImageInfo::MakeA8(8, 8))) {
        ALOGE("Could not allocate MediaTek UDFPS dither table");
        return;
    }

    // Ordered 8x8 dithering table, matching the upstream MediaTek/Skia algorithm.
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            const int m = (y & 1) << 5 | (x & 1) << 4 | (y & 2) << 2 | (x & 2) << 1 |
                    (y & 4) >> 1 | (x & 4) >> 2;
            const float value = static_cast<float>(m) / 64.f - 63.f / 128.f;
            *mDitherBmp.getAddr8(x, y) = static_cast<uint8_t>((value + 0.5f) * 255.f + 0.5f);
        }
    }
    mDitherBmp.setImmutable();
    mDitherTableShader = mDitherBmp.makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat,
                                               SkSamplingOptions(SkFilterMode::kNearest));
    if (!mDitherTableShader) {
        ALOGE("Could not create MediaTek UDFPS dither table shader");
        return;
    }

    const SkString ditherSource(R"(
        uniform shader DataInput;
        uniform shader table;
        uniform float ditherAlpha;
        half4 main(float2 xy) {
            float4 c = float4(DataInput.eval(xy));
            float dither = (table.eval(xy).a - 0.5) / 256.0;
            return float4(clamp(c.rgb * ditherAlpha + dither, 0.0, 1.0), c.a);
        }
    )");
    auto [effect, error] = SkRuntimeEffect::MakeForShader(ditherSource);
    if (!effect) {
        ALOGE("Could not compile MediaTek UDFPS dither shader: %s", error.c_str());
        return;
    }
    mDitherEffect = std::move(effect);
}

bool SkiaDitherEffect::isInitOK() const {
    return mDitherEffect != nullptr && mDitherTableShader != nullptr;
}

sk_sp<SkShader> SkiaDitherEffect::createDitherShader(sk_sp<SkShader> input,
                                                     float ditherAlpha) const {
    if (!isInitOK() || !input) {
        return nullptr;
    }
    SkRuntimeShaderBuilder builder(mDitherEffect);
    builder.child("DataInput") = std::move(input);
    builder.child("table") = mDitherTableShader;
    builder.uniform("ditherAlpha") = ditherAlpha;
    return builder.makeShader();
}

} // namespace android::renderengine::skia
