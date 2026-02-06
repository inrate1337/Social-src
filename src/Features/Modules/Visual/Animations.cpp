//
// Created by vastrakai on 7/18/2024.
//

#include "Animations.hpp"

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/BobHurtEvent.hpp>
#include <Features/Events/BoneRenderEvent.hpp>
#include <Features/Events/SwingDurationEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/MinecraftSim.hpp>
#include <SDK/Minecraft/Actor/ActorPartModel.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <algorithm>
#include <array>
#include <cmath>

std::vector<unsigned char> gNoSwitchAnimation = {
    0x0F, 0x57, 0xC0, // xorps xmm0, xmm0
    0x90, 0x90, 0x90, 0x90, 0x90 // nop
};

DEFINE_PATCH_FUNC(patchNoSwitchAnimation, SigManager::ItemInHandRenderer_render_bytepatch, gNoSwitchAnimation);
DEFINE_NOP_PATCH_FUNC(patchFluxSwing, SigManager::FluxSwing, 0x5);
DEFINE_NOP_PATCH_FUNC(patchDefaultSwing, SigManager::ItemInHandRenderer_renderItem_bytepatch, 0x8);
DEFINE_NOP_PATCH_FUNC(patchDefaultSwing2, SigManager::ItemInHandRenderer_renderItem_bytepatch + 11, 0x8);

static float cubicBezier1D(float t, float p1, float p2)
{
    float c = 3.0f * p1;
    float b = 3.0f * (p2 - p1) - c;
    float a = 1.0f - c - b;
    return ((a * t + b) * t + c) * t;
}

static float cubicBezier1DDerivative(float t, float p1, float p2)
{
    float c = 3.0f * p1;
    float b = 3.0f * (p2 - p1) - c;
    float a = 1.0f - c - b;
    return (3.0f * a * t + 2.0f * b) * t + c;
}

static float cubicBezierEase(float x, float p1x, float p1y, float p2x, float p2y)
{
    x = std::clamp(x, 0.0f, 1.0f);
    p1x = std::clamp(std::isfinite(p1x) ? p1x : 0.25f, 0.0f, 1.0f);
    p2x = std::clamp(std::isfinite(p2x) ? p2x : 0.25f, 0.0f, 1.0f);
    p1y = std::clamp(std::isfinite(p1y) ? p1y : 0.10f, 0.0f, 1.0f);
    p2y = std::clamp(std::isfinite(p2y) ? p2y : 1.00f, 0.0f, 1.0f);

    static constexpr int kSplineTableSize = 11;
    static constexpr float kSampleStep = 1.0f / float(kSplineTableSize - 1);

    std::array<float, kSplineTableSize> sampleX{};
    for (int i = 0; i < kSplineTableSize; i++)
    {
        float t = float(i) * kSampleStep;
        sampleX[i] = cubicBezier1D(t, p1x, p2x);
    }

    int sampleIdx = 0;
    while (sampleIdx < kSplineTableSize - 2 && sampleX[sampleIdx + 1] <= x) sampleIdx++;

    float t0 = float(sampleIdx) * kSampleStep;
    float t1 = float(sampleIdx + 1) * kSampleStep;
    float x0 = sampleX[sampleIdx];
    float x1 = sampleX[sampleIdx + 1];

    float t = (x1 - x0) > 1e-6f ? (t0 + (x - x0) * (t1 - t0) / (x1 - x0)) : t0;
    t = std::clamp(t, 0.0f, 1.0f);

    for (int i = 0; i < 8; i++)
    {
        float xEst = cubicBezier1D(t, p1x, p2x) - x;
        float d = cubicBezier1DDerivative(t, p1x, p2x);
        if (std::abs(xEst) < 1e-6f) break;
        if (std::abs(d) < 1e-6f) break;
        t = std::clamp(t - xEst / d, 0.0f, 1.0f);
    }

    float y = cubicBezier1D(t, p1y, p2y);
    return std::clamp(y, 0.0f, 1.0f);
}

void Animations::onEnable()
{
    gFeatureManager->mDispatcher->listen<SwingDurationEvent, &Animations::onSwingDurationEvent>(this);
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &Animations::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<BoneRenderEvent, &Animations::onBoneRenderEvent>(this);
    gFeatureManager->mDispatcher->listen<BobHurtEvent, &Animations::onBobHurtEvent>(this);

    if (!mSwingAngle)
    {
        mSwingAngle = reinterpret_cast<float*>(SigManager::TapSwingAnim);
        MemUtils::setProtection(reinterpret_cast<uintptr_t>(mSwingAngle), sizeof(float), PAGE_READWRITE);
    }

    patchNoSwitchAnimation(mNoSwitchAnimation.mValue);
    patchFluxSwing(mFluxSwing.mValue);
    patchDefaultSwing(mAnimation.mValue == Animation::Test);
    patchDefaultSwing2(mAnimation.mValue == Animation::Test);
}

void Animations::onDisable()
{
    gFeatureManager->mDispatcher->deafen<SwingDurationEvent, &Animations::onSwingDurationEvent>(this);
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &Animations::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<BoneRenderEvent, &Animations::onBoneRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<BobHurtEvent, &Animations::onBobHurtEvent>(this);
    patchNoSwitchAnimation(false);
    patchFluxSwing(false);

    if (mSwingAngle) *mSwingAngle = glm::radians(-80.f);

    patchDefaultSwing(false);
    patchDefaultSwing2(false);
}


void Animations::onBaseTickEvent(BaseTickEvent& event)
{
    auto player = event.mActor;

    patchNoSwitchAnimation(mNoSwitchAnimation.mValue);
    patchFluxSwing(mFluxSwing.mValue);
    patchDefaultSwing(mAnimation.mValue == Animation::Test);
    patchDefaultSwing2(mAnimation.mValue == Animation::Test);

    if (mOnlyOnBlock.mValue && mSwingAngle)
    {
        if (mShouldBlock)
        {
            *mSwingAngle = glm::radians(mCustomSwingAngle.mValue ? mSwingAngleSetting.as<float>() : -80.f);
        }
        else
        {
            *mSwingAngle = glm::radians(-80.f);
        }
    }
    else if (mSwingAngle)
    {
        *mSwingAngle = glm::radians(mCustomSwingAngle.mValue ? mSwingAngleSetting.as<float>() : -80.f);
    }


    mOldSwingDuration = (float)player->getOldSwingProgress();
    mSwingDuration = (float)player->getSwingProgress();

}

void Animations::onSwingDurationEvent(SwingDurationEvent& event)
{
    int original = std::max(1, event.mSwingDuration);
    float mul = mSwingSpeed.as<float>();
    mul = std::isfinite(mul) ? mul : 1.0f;
    mul = std::clamp(mul, 0.20f, 3.00f);

    int adjusted = std::max(1, (int)std::round((float)original / mul));
    event.mSwingDuration = adjusted;
    mSwingDurationMax = adjusted;
}

void Animations::onBoneRenderEvent(BoneRenderEvent& event)
{
    auto ent = event.mActor;
    auto player = ClientInstance::get()->getLocalPlayer();
    auto bone = event.mBone;
    auto partModel = event.mPartModel;

    if (ent != ClientInstance::get()->getLocalPlayer()) return;

    if (bone->mBoneStr == "rightarm" && mThirdPersonBlock.mValue)
    {
        auto heldItem = player->getSupplies()->getContainer()->getItem(player->getSupplies()->mInHandSlot);
        bool isHoldingSword = heldItem && heldItem->mItem && heldItem->getItem()->isSword();
        if ((!ClientInstance::get()->getMouseGrabbed() && ImGui::IsMouseDown(ImGuiMouseButton_Right) && isHoldingSword || event.mDoBlockAnimation && isHoldingSword) && mAnimation.mValue != Animation::Default)
        {
            float xRot = mXRot.mValue;
            float yRot = mYRot.mValue;
            float zRot = mZRot.mValue;

            Actor* player = ClientInstance::get()->getLocalPlayer();

            if (player->getSwingProgress() > 0) {
                xRot = MathUtils::animate(-30, xRot, ImRenderUtils::getDeltaTime() * 3.f);
                yRot = MathUtils::animate(0, yRot, ImRenderUtils::getDeltaTime() * 3.f);
            }
            else {
                xRot = MathUtils::animate(-65, xRot, ImRenderUtils::getDeltaTime() * 3.f);
                yRot = MathUtils::animate(-17, yRot, ImRenderUtils::getDeltaTime() * 3.f);
            }

            partModel->mRot.x = xRot;
            partModel->mRot.y = yRot;
            partModel->mRot.z = zRot;
            partModel->mSize.z = 0.9177761;
        }
    }
}

void Animations::onBobHurtEvent(BobHurtEvent& event)
{
    auto matrix = event.mMatrix;
    if (mSmallItems.mValue) *matrix = glm::translate(*matrix, glm::vec3(0.5f, -0.2f, -0.6f));

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    auto heldItem = player->getSupplies()->getContainer()->getItem(player->getSupplies()->mInHandSlot);
    bool isHoldingSword = heldItem && heldItem->mItem && heldItem->getItem()->isSword();
    if ((!ClientInstance::get()->getMouseGrabbed() && ImGui::IsMouseDown(ImGuiMouseButton_Right) && isHoldingSword || event.mDoBlockAnimation && isHoldingSword) && mAnimation.mValue != Animation::Default)
    {
        mShouldBlock = true;

        *matrix = glm::translate(*matrix, glm::vec3(0.4, 0.0, -0.15));
        *matrix = glm::translate(*matrix, glm::vec3(-0.1f, 0.15f, -0.2f));
        *matrix = glm::translate(*matrix, glm::vec3(-0.24F, 0.25f, -0.20F));
        *matrix = glm::rotate(*matrix, -1.98F, glm::vec3(0.0F, 1.0F, 0.0F));
        *matrix = glm::rotate(*matrix, 1.30F, glm::vec3(4.0F, 0.0F, 0.0F));
        *matrix = glm::rotate(*matrix, 59.9F, glm::vec3(0.0F, 1.0F, 0.0F));
        *matrix = glm::translate(*matrix, glm::vec3(0.0f, -0.1f, 0.15f));
        *matrix = glm::translate(*matrix, glm::vec3(0.08f, 0.0f, 0.0f));
        *matrix = glm::scale(*matrix, glm::vec3(1.05f, 1.05f, 1.05f));
    }
    else
    {
        mShouldBlock = false;
    }


    if (mAnimation.mValue == Animation::Test) return;

    int swingTime = std::max(1, mSwingDurationMax);
    float swingProgress = mSwingDuration;
    float oldSwingProgress = mOldSwingDuration;
    float lerpedSwingProgress = MathUtils::lerp(oldSwingProgress, swingProgress, ClientInstance::get()->getMinecraftSim()->getGameSim()->mDeltaTime);
    float percent = std::clamp(lerpedSwingProgress / (float)swingTime, 0.0f, 1.0f);
    float eased = percent;
    if (mCustomInterpolation.mValue)
    {
        eased = cubicBezierEase(
            percent,
            mInterpP1X.as<float>(),
            mInterpP1Y.as<float>(),
            mInterpP2X.as<float>(),
            mInterpP2Y.as<float>()
        );
    }

    float swing = std::sin(eased * 3.1415927f);
    swing = swing * swing * (3.0f - 2.0f * swing);
    float swing2 = std::sin(eased * 1.5707963f);
    swing2 = swing2 * swing2 * (3.0f - 2.0f * swing2);

    glm::mat4 m = *matrix;
    m = glm::translate(m, glm::vec3(-0.48f * swing, 0.12f * swing2, -0.38f * swing));
    m = glm::rotate(m, -1.55f * swing, glm::vec3(1.0f, 0.0f, 0.0f));
    m = glm::rotate(m, 1.05f * swing, glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, 0.75f * swing2, glm::vec3(0.0f, 0.0f, 1.0f));
    *matrix = m;
};
