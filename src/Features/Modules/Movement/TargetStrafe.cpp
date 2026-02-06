//
// Created by dark on 9/19/2024.
//

#include "TargetStrafe.hpp"
#include "Speed.hpp"

#include <Features/Modules/Combat/Aura.hpp>
#include <Utils/GameUtils/ActorUtils.hpp>

#include <Features/FeatureManager.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/KeyboardMouseSettings.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>

#include <cmath>

void TargetStrafe::onEnable()
{
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &TargetStrafe::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<RenderEvent, &TargetStrafe::onRenderEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &TargetStrafe::onPacketOutEvent>(this);
}

void TargetStrafe::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &TargetStrafe::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<RenderEvent, &TargetStrafe::onRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &TargetStrafe::onPacketOutEvent>(this);

    mShouldStrafe = false;
    mWasStrafeColliding = false;
    mStrafeDir = 1.0f;
    mStrafeMoveX = 0.0f;
    mStrafeMoveY = 0.0f;

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    handleKeyInput(false, false, false, false);
}

void TargetStrafe::onBaseTickEvent(BaseTickEvent& event)
{
    auto player = event.mActor;
    if (!player) return;

    glm::vec3 playerPos = *player->getPos();
    auto moveInputComponent = player->getMoveInputComponent();
    static auto speed = gFeatureManager->mModuleManager->getModule<Speed>();
    auto& binds = *ClientInstance::get()->getKeyboardSettings();
    bool isJumping = Keyboard::mPressedKeys[binds["key.jump"]];

    Actor* auraTarget = nullptr;
    if (Aura::sTargetRuntimeID != 0) auraTarget = ActorUtils::getActorFromRuntimeID(Aura::sTargetRuntimeID);
    if (!auraTarget) auraTarget = Aura::sTarget;
    if (!Aura::sHasTarget || !auraTarget || !auraTarget->isValid() || auraTarget->isDead() || auraTarget->getHealth() <= 0.0f || !auraTarget->getActorTypeComponent() || (mJumpOnly.mValue && !isJumping) || (mSpeedOnly.mValue && (speed == nullptr || !speed->mEnabled)))
    {
        mWasStrafing = mShouldStrafe;
        mShouldStrafe = false;
        mCurrentTarget = nullptr;
        mMoveForwardValue = 0.f;
        mMoveStrafeValue = 0.f;
        mWasStrafeColliding = false;
        mStrafeDir = 1.0f;
        mStrafeMoveX = 0.0f;
        mStrafeMoveY = 0.0f;

        // restore original movement keys
        if (mWasStrafing)  handleKeyInput(Keyboard::mPressedKeys[binds["key.forward"]], Keyboard::mPressedKeys[binds["key.left"]],
            Keyboard::mPressedKeys[binds["key.back"]], Keyboard::mPressedKeys[binds["key.right"]]);

        return;
    }

    if (mSpeedOnly.mValue && mJumpOnly.mValue) moveInputComponent->mIsJumping = false;

    mCurrentTarget = auraTarget;
    glm::vec3 targetPos = *mCurrentTarget->getPos();
    float dx = targetPos.x - playerPos.x;
    float dz = targetPos.z - playerPos.z;
    float dist = std::sqrt((dx * dx) + (dz * dz));

    bool colliding = player->isCollidingHorizontal();
    if (mWallCheck.mValue && colliding && !mWasStrafeColliding) {
        mMoveRight = !mMoveRight;
    }
    mWasStrafeColliding = mWallCheck.mValue ? colliding : false;

    if (moveInputComponent) {
        if (moveInputComponent->mLeft) mMoveRight = false;
        else if (moveInputComponent->mRight) mMoveRight = true;
    }

    float desiredRadius = MathUtils::clamp(mDistance.mValue, 0.02f, 5.0f);
    float radiusErr = dist - desiredRadius;

    float orbitBlendDenom = MathUtils::clamp(desiredRadius * 0.25f, 0.18f, 1.20f);
    float orbitBlend = MathUtils::clamp((dist - desiredRadius) / orbitBlendDenom, 0.0f, 1.0f);

    float forwardScale = 1.75f / MathUtils::clamp(desiredRadius, 0.30f, 5.0f);
    float desiredForward = MathUtils::clamp(radiusErr * forwardScale, -0.55f, 1.00f);
    desiredForward *= (0.25f + 0.75f * orbitBlend);

    float strafeDirTarget = (mMoveRight ? 1.0f : -1.0f);
    mStrafeDir += (strafeDirTarget - mStrafeDir) * 0.18f;

    float desiredStrafe = (0.90f - 0.18f * orbitBlend) * mStrafeDir;
    if (dist > (desiredRadius * 0.60f)) {
        float desiredAbs = std::min(0.85f, std::max(std::abs(desiredStrafe), 0.35f));
        desiredStrafe = (desiredStrafe >= 0.0f) ? desiredAbs : -desiredAbs;
    }

    if (!player->isOnGround())
    {
        float distExcess = std::max(0.0f, dist - (desiredRadius + 0.08f));
        float distExcessT = MathUtils::clamp(distExcess / 0.60f, 0.0f, 1.0f);
        desiredForward = std::max(desiredForward, 0.55f + 0.35f * distExcessT);
        desiredStrafe *= (1.0f - 0.35f * distExcessT);
    }

    float smooth = 0.12f;
    mStrafeMoveX += (desiredStrafe - mStrafeMoveX) * smooth;
    mStrafeMoveY += (desiredForward - mStrafeMoveY) * smooth;

    mMoveStrafeValue = mStrafeMoveX;
    mMoveForwardValue = mStrafeMoveY;

    mForward = mMoveForwardValue > 0.1f;
    mBackward = mMoveForwardValue < -0.1f;

    auto yaw = MathUtils::getRots(*player->getPos(), mCurrentTarget->getAABB()).y;
    auto rotationComponent = player->getActorRotationComponent();
    if (rotationComponent) {
        rotationComponent->mYaw = yaw;
        rotationComponent->mOldYaw = yaw;
    }

    bool left = mMoveStrafeValue < -0.1f;
    bool right = mMoveStrafeValue > 0.1f;
    handleKeyInput(mForward, left, mBackward, right);

    mWasStrafing = mShouldStrafe;
    mShouldStrafe = true;
}

void TargetStrafe::onRenderEvent(RenderEvent& event) 
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    if (mShouldStrafe)
    {
        if (!mCurrentTarget || !mCurrentTarget->isValid() || mCurrentTarget->isDead() || mCurrentTarget->getHealth() <= 0.0f || !mCurrentTarget->getActorTypeComponent()) return;
        float yaw = MathUtils::getRots(*player->getPos(), mCurrentTarget->getAABB()).y;
        auto rotationComponent = player->getActorRotationComponent();
        if (rotationComponent) {
            rotationComponent->mYaw = yaw;
            rotationComponent->mOldYaw = yaw;
        }
    }
}

void TargetStrafe::onPacketOutEvent(PacketOutEvent& event)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    if (event.mPacket->getId() == PacketID::PlayerAuthInput)
    {
        auto pkt = event.getPacket<PlayerAuthInputPacket>();
        if (!mShouldStrafe) return;

        pkt->mMove = { mMoveStrafeValue, mMoveForwardValue };
        pkt->mAnalogMoveVector = pkt->mMove;

        glm::vec2 desiredMoveVec = pkt->mMove;
        glm::vec2 xzVel = { pkt->mPosDelta.x, pkt->mPosDelta.z };
        if ((desiredMoveVec.x != 0.0f || desiredMoveVec.y != 0.0f) && (xzVel.x != 0.0f || xzVel.y != 0.0f))
        {
            float yaw = -pkt->mRot.y;
            float movementYaw = atan2(xzVel.x, xzVel.y);
            float movementYawDegrees = movementYaw * (180.0f / M_PI);
            float yawDiff = movementYawDegrees - yaw;

            float newMoveVecX = sin(glm::radians(yawDiff));
            float newMoveVecY = cos(glm::radians(yawDiff));
            glm::vec2 newMoveVec = { newMoveVecX, newMoveVecY };

            if (std::abs(newMoveVec.x) < 0.001f) newMoveVec.x = 0.0f;
            if (std::abs(newMoveVec.y) < 0.001f) newMoveVec.y = 0.0f;
            if (desiredMoveVec.x == 0.0f && desiredMoveVec.y == 0.0f) newMoveVec = { 0.0f, 0.0f };

            pkt->mMove = newMoveVec;
            pkt->mAnalogMoveVector = pkt->mMove;
            pkt->mVehicleRotation = newMoveVec;
            pkt->mInputMode = InputMode::MotionController;
        }

        pkt->mInputData &= ~(AuthInputAction::UP | AuthInputAction::DOWN | AuthInputAction::LEFT | AuthInputAction::RIGHT |
                             AuthInputAction::UP_LEFT | AuthInputAction::UP_RIGHT | AuthInputAction::DOWN_LEFT | AuthInputAction::DOWN_RIGHT);

        auto moveVec = pkt->mMove;
        if (moveVec.y > 0.1f) pkt->mInputData |= AuthInputAction::UP;
        else if (moveVec.y < -0.1f) pkt->mInputData |= AuthInputAction::DOWN;

        if (moveVec.x > 0.1f) pkt->mInputData |= AuthInputAction::RIGHT;
        else if (moveVec.x < -0.1f) pkt->mInputData |= AuthInputAction::LEFT;

        if (mAlwaysSprint.mValue) {
            pkt->mInputData |= AuthInputAction::SPRINT_DOWN | AuthInputAction::SPRINTING | AuthInputAction::START_SPRINTING;
            pkt->mInputData &= ~AuthInputAction::STOP_SPRINTING;
        }
    }
}

void TargetStrafe::handleKeyInput(bool pressingW, bool pressingA, bool pressingS, bool pressingD)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    auto moveInputComponent = player->getMoveInputComponent();

    moveInputComponent->mForward = pressingW;
    moveInputComponent->mLeft = pressingA;
    moveInputComponent->mBackward = pressingS;
    moveInputComponent->mRight = pressingD;
}
