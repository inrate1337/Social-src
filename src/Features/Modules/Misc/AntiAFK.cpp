#include "AntiAFK.hpp"
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
#include <SDK/Minecraft/Actor/Components/MoveInputComponent.hpp>
#include <SDK/Minecraft/Actor/Components/ActorRotationComponent.hpp>
#include <SDK/Minecraft/Actor/Components/ActorHeadRotationComponent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <cmath>

void AntiAFK::onEnable()
{
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &AntiAFK::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &AntiAFK::onPacketOutEvent>(this);

    auto now = std::chrono::steady_clock::now();
    mInitialized = false;
    mAngle = 0.0f;
    mPacketCounter = 0;

    mLastJumpAt = now;
    mLastRandomAt = now;
    mLastSneakAt = now;

    mSneakActive = false;
    mSneakTicksLeft = 0;
    mSneakSendStop = false;

    mRandomMoveX = 0.0f;
    mRandomMoveY = 0.0f;
    mRandomYaw = 0.0f;
    mRng.seed((uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
}

void AntiAFK::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &AntiAFK::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &AntiAFK::onPacketOutEvent>(this);
}

void AntiAFK::onBaseTickEvent(BaseTickEvent& event)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    auto mode = mMode.mValue;

    if (!mInitialized) {
        auto rotComp = player->getActorRotationComponent();
        if (rotComp) {
            mAngle = rotComp->mYaw;
            mRandomYaw = rotComp->mYaw;
        }
        mInitialized = true;
    }

    if (mode == Mode::Circle || mode == Mode::Spin || mode == Mode::RandomMove) {
        auto rotComp = player->getActorRotationComponent();
        auto headRotComp = player->getActorHeadRotationComponent();

        if (rotComp) {
            rotComp->mYaw = (mode == Mode::RandomMove) ? mRandomYaw : mAngle;
            rotComp->mPitch = 0.0f;
        }
        if (headRotComp) headRotComp->mHeadRot = (mode == Mode::RandomMove) ? mRandomYaw : mAngle;
    }

    if (mode == Mode::Circle) {
        auto moveInput = player->getMoveInputComponent();
        if (moveInput) {
            if (std::abs(moveInput->mMoveVector.x) < 0.1f && std::abs(moveInput->mMoveVector.y) < 0.1f) {
                moveInput->mForward = true;
                moveInput->mMoveVector = { 0.0f, 1.0f };
            }
        }
    }
}

void AntiAFK::onPacketOutEvent(PacketOutEvent& event)
{
    if (event.mPacket->getId() != PacketID::PlayerAuthInput) return;
    auto packet = event.getPacket<PlayerAuthInputPacket>();
    if (!packet) return;

    mPacketCounter++;
    auto now = std::chrono::steady_clock::now();

    if (!mInitialized) {
        mAngle = packet->mRot.y;
        mRandomYaw = packet->mRot.y;
        mInitialized = true;
    }

    auto moveNeutral = [&]() -> bool {
        auto ax = std::abs(packet->mMove.x);
        auto ay = std::abs(packet->mMove.y);
        auto bx = std::abs(packet->mAnalogMoveVector.x);
        auto by = std::abs(packet->mAnalogMoveVector.y);
        return (ax < 0.1f && ay < 0.1f && bx < 0.1f && by < 0.1f);
    };

    auto applyMove = [&](float strafe, float forward, AuthInputAction flags) {
        packet->mInputData &= ~(AuthInputAction::UP | AuthInputAction::DOWN | AuthInputAction::LEFT | AuthInputAction::RIGHT | AuthInputAction::UP_LEFT | AuthInputAction::UP_RIGHT | AuthInputAction::DOWN_LEFT | AuthInputAction::DOWN_RIGHT);
        packet->mMove = { strafe, forward };
        packet->mAnalogMoveVector = packet->mMove;
        packet->mInputData |= flags;
    };

    auto mode = mMode.mValue;

    if (mode == Mode::Circle || mode == Mode::Spin) {
        float step = (mode == Mode::Circle) ? 10.0f : 14.0f;
        mAngle += step;
        if (mAngle >= 360.0f) mAngle -= 360.0f;

        packet->mRot = { 0.0f, mAngle };
        packet->mYHeadRot = mAngle;
    }

    if (mode == Mode::JumpEveryMinute) {
        if (now - mLastJumpAt >= std::chrono::seconds(60)) {
            packet->mInputData |= AuthInputAction::JUMPING | AuthInputAction::WANT_UP | AuthInputAction::JUMP_DOWN | AuthInputAction::START_JUMPING;
            mLastJumpAt = now;
        }
        return;
    }

    if (mode == Mode::SneakPulse) {
        if (!mSneakActive && !packet->hasInputData(AuthInputAction::SNEAKING) && (now - mLastSneakAt >= std::chrono::seconds(30))) {
            mSneakActive = true;
            mSneakTicksLeft = 10;
            mSneakSendStop = true;
            mLastSneakAt = now;
        }

        if (mSneakActive) {
            packet->mInputData |= AuthInputAction::SNEAK_DOWN | AuthInputAction::SNEAKING | AuthInputAction::START_SNEAKING;
            mSneakTicksLeft--;
            if (mSneakTicksLeft <= 0) mSneakActive = false;
        }
        else if (mSneakSendStop) {
            packet->mInputData |= AuthInputAction::STOP_SNEAKING;
            mSneakSendStop = false;
        }
        return;
    }

    if (!moveNeutral()) return;

    if (mode == Mode::ForwardBackward) {
        bool forward = ((mPacketCounter / 20) % 2) == 0;
        applyMove(0.0f, forward ? 1.0f : -1.0f, forward ? AuthInputAction::UP : AuthInputAction::DOWN);
        return;
    }

    if (mode == Mode::LeftRight) {
        bool right = ((mPacketCounter / 20) % 2) == 0;
        applyMove(right ? 1.0f : -1.0f, 0.0f, right ? AuthInputAction::RIGHT : AuthInputAction::LEFT);
        return;
    }

    if (mode == Mode::Circle) {
        applyMove(0.0f, 1.0f, AuthInputAction::UP);
        return;
    }

    if (mode == Mode::RandomMove) {
        if (now - mLastRandomAt >= std::chrono::seconds(2)) {
            std::uniform_int_distribution<int> dirDist(0, 7);
            int dir = dirDist(mRng);
            float sx = 0.0f;
            float fy = 0.0f;
            switch (dir) {
            case 0: sx = 0.0f; fy = 1.0f; break;
            case 1: sx = 0.0f; fy = -1.0f; break;
            case 2: sx = 1.0f; fy = 0.0f; break;
            case 3: sx = -1.0f; fy = 0.0f; break;
            case 4: sx = 1.0f; fy = 1.0f; break;
            case 5: sx = -1.0f; fy = 1.0f; break;
            case 6: sx = 1.0f; fy = -1.0f; break;
            default: sx = -1.0f; fy = -1.0f; break;
            }

            std::uniform_real_distribution<float> yawJitter(-60.0f, 60.0f);
            mRandomYaw += yawJitter(mRng);
            if (mRandomYaw >= 360.0f) mRandomYaw -= 360.0f;
            if (mRandomYaw < 0.0f) mRandomYaw += 360.0f;

            mRandomMoveX = sx;
            mRandomMoveY = fy;
            mLastRandomAt = now;
        }

        packet->mRot = { 0.0f, mRandomYaw };
        packet->mYHeadRot = mRandomYaw;

        AuthInputAction flags = AuthInputAction::NONE;
        if (mRandomMoveY > 0.1f) flags |= AuthInputAction::UP;
        else if (mRandomMoveY < -0.1f) flags |= AuthInputAction::DOWN;
        if (mRandomMoveX > 0.1f) flags |= AuthInputAction::RIGHT;
        else if (mRandomMoveX < -0.1f) flags |= AuthInputAction::LEFT;

        if ((flags & (AuthInputAction::UP | AuthInputAction::RIGHT)) == (AuthInputAction::UP | AuthInputAction::RIGHT)) flags = AuthInputAction::UP_RIGHT;
        else if ((flags & (AuthInputAction::UP | AuthInputAction::LEFT)) == (AuthInputAction::UP | AuthInputAction::LEFT)) flags = AuthInputAction::UP_LEFT;
        else if ((flags & (AuthInputAction::DOWN | AuthInputAction::RIGHT)) == (AuthInputAction::DOWN | AuthInputAction::RIGHT)) flags = AuthInputAction::DOWN_RIGHT;
        else if ((flags & (AuthInputAction::DOWN | AuthInputAction::LEFT)) == (AuthInputAction::DOWN | AuthInputAction::LEFT)) flags = AuthInputAction::DOWN_LEFT;

        if (flags != AuthInputAction::NONE) applyMove(mRandomMoveX, mRandomMoveY, flags);
        return;
    }
}
