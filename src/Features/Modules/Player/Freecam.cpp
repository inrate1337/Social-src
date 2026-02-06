#include "Freecam.hpp"

#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/KeyboardMouseSettings.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/Components/FlagComponent.hpp>
#include <SDK/Minecraft/Network/Packets/MovePlayerPacket.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
#include <SDK/Minecraft/Options.hpp>
#include <Utils/Keyboard.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>

void Freecam::onEnable()
{
    auto ci = ClientInstance::get();
    if (!ci)
    {
        setEnabled(false);
        return;
    }

    auto player = ci->getLocalPlayer();
    if (!player)
    {
        setEnabled(false);
        return;
    }

    auto rot = player->getActorRotationComponent();
    if (rot)
    {
        mAnchorRot = { rot->mPitch, rot->mYaw };
    }

    auto pos = player->getPos();
    if (!pos)
    {
        setEnabled(false);
        return;
    }
    mAnchorPos = *pos;
    mInitialized = false;

    if (auto options = ci->getOptions(); options && options->mThirdPerson)
    {
        mPrevThirdPerson = options->mThirdPerson->value;
        options->mThirdPerson->value = 0;
    }

    mPrevRenderCameraFlag = player->getFlag<RenderCameraComponent>();
    mPrevRenderPlayerModelFlag = player->getFlag<CameraRenderPlayerModelComponent>();

    player->setFlag<RenderCameraComponent>(true);
    player->setFlag<CameraRenderPlayerModelComponent>(mShowBody.mValue);

    gFeatureManager->mDispatcher->listen<BaseTickEvent, &Freecam::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<LookInputEvent, &Freecam::onLookInputEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &Freecam::onPacketOutEvent>(this);
}

void Freecam::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &Freecam::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<LookInputEvent, &Freecam::onLookInputEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &Freecam::onPacketOutEvent>(this);

    auto ci = ClientInstance::get();
    auto player = ci ? ci->getLocalPlayer() : nullptr;
    if (player)
    {
        if (auto options = ci->getOptions(); options && options->mThirdPerson)
        {
            options->mThirdPerson->value = mPrevThirdPerson;
        }

        player->setFlag<RenderCameraComponent>(mPrevRenderCameraFlag);
        player->setFlag<CameraRenderPlayerModelComponent>(mPrevRenderPlayerModelFlag);

        player->setPosition(mAnchorPos);
        if (auto stateVec = player->getStateVectorComponent())
        {
            stateVec->mPos = mAnchorPos;
            stateVec->mPosOld = mAnchorPos;
            stateVec->mVelocity = {};
        }

        if (mFreezeRotation.mValue)
        {
            if (auto rot = player->getActorRotationComponent())
            {
                rot->mPitch = mAnchorRot.x;
                rot->mOldPitch = mAnchorRot.x;
                rot->mYaw = mAnchorRot.y;
                rot->mOldYaw = mAnchorRot.y;
            }
        }
    }

    mInitialized = false;
}

void Freecam::onBaseTickEvent(BaseTickEvent& event)
{
    auto player = event.mActor;
    if (!player)
    {
        setEnabled(false);
        return;
    }

    auto ci = ClientInstance::get();
    if (!ci) return;

    if (auto options = ci->getOptions(); options && options->mThirdPerson)
    {
        options->mThirdPerson->value = 0;
    }

    player->setFlag<RenderCameraComponent>(true);
    player->setFlag<CameraRenderPlayerModelComponent>(mShowBody.mValue);

    if (auto stateVec = player->getStateVectorComponent())
    {
        stateVec->mPos = mAnchorPos;
        stateVec->mPosOld = mAnchorPos;
        stateVec->mVelocity = {};
    }
    player->setPosition(mAnchorPos);
}

void Freecam::onLookInputEvent(LookInputEvent& event)
{
    auto ci = ClientInstance::get();
    if (!ci) return;

    auto player = ci->getLocalPlayer();
    if (!player) return;

    if (auto options = ci->getOptions(); options && options->mThirdPerson)
    {
        options->mThirdPerson->value = 0;
    }

    if (!event.mFirstPersonCamera) return;

    if (!mInitialized)
    {
        mCamPos = event.mFirstPersonCamera->mOrigin;
        mTargetCamPos = mCamPos;
        mInitialized = true;
    }

    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f) dt = 1.0f / 120.0f;

    auto* binds = ci->getKeyboardSettings();
    if (!binds) return;
    const bool jump = Keyboard::mPressedKeys[(*binds)["key.jump"]];
    const bool sneak = Keyboard::mPressedKeys[(*binds)["key.sneak"]];

    glm::vec2 move = MathUtils::getMovement();
    if (move != glm::vec2(0.0f, 0.0f) || jump || sneak)
    {
        float yaw = 0.0f;
        if (auto rot = player->getActorRotationComponent())
        {
            yaw = rot->mYaw;
        }

        const float speed = mSpeed.mValue;
        const float baseYaw = glm::radians(yaw + 90.0f);
        const glm::vec2 forward = { std::cos(baseYaw), std::sin(baseYaw) };
        const glm::vec2 left = { std::cos(baseYaw + glm::half_pi<float>()), std::sin(baseYaw + glm::half_pi<float>()) };

        glm::vec3 delta = {};
        delta.x = (forward.x * move.y + left.x * move.x) * speed * dt;
        delta.z = (forward.y * move.y + left.y * move.x) * speed * dt;
        if (jump) delta.y += speed * dt;
        if (sneak) delta.y -= speed * dt;

        mTargetCamPos += delta;
    }

    float lerpAmt = MathUtils::clamp(dt * 18.0f, 0.0f, 1.0f);
    mCamPos = glm::mix(mCamPos, mTargetCamPos, lerpAmt);
    event.mFirstPersonCamera->mOrigin = mCamPos;
}

void Freecam::onPacketOutEvent(PacketOutEvent& event)
{
    if (!mCancelMovePackets.mValue) return;

    auto ci = ClientInstance::get();
    if (!ci) return;

    auto player = ci->getLocalPlayer();
    if (!player) return;
    if (!event.mPacket) return;

    if (event.mPacket->getId() == PacketID::MovePlayer)
    {
        auto pkt = event.getPacket<MovePlayerPacket>();
        if (pkt && pkt->mPlayerID == player->getRuntimeID())
        {
            event.cancel();
        }
        return;
    }

    if (event.mPacket->getId() != PacketID::PlayerAuthInput) return;

    auto pkt = event.getPacket<PlayerAuthInputPacket>();
    if (!pkt) return;
    pkt->mPos = mAnchorPos;
    pkt->mPosDelta = {};
    pkt->mMove = {};

    pkt->mInputData &= ~AuthInputAction::UP;
    pkt->mInputData &= ~AuthInputAction::DOWN;
    pkt->mInputData &= ~AuthInputAction::LEFT;
    pkt->mInputData &= ~AuthInputAction::RIGHT;
    pkt->mInputData &= ~AuthInputAction::UP_LEFT;
    pkt->mInputData &= ~AuthInputAction::UP_RIGHT;
    pkt->mInputData &= ~AuthInputAction::DOWN_LEFT;
    pkt->mInputData &= ~AuthInputAction::DOWN_RIGHT;
    pkt->mInputData &= ~AuthInputAction::JUMP_DOWN;
    pkt->mInputData &= ~AuthInputAction::SNEAK_DOWN;
    pkt->mInputData &= ~AuthInputAction::SPRINTING;
    pkt->mInputData &= ~AuthInputAction::START_SPRINTING;
    pkt->mInputData &= ~AuthInputAction::STOP_SPRINTING;
    pkt->mInputData &= ~AuthInputAction::START_JUMPING;

    if (mFreezeRotation.mValue)
    {
        pkt->mRot = mAnchorRot;
        pkt->mYHeadRot = mAnchorRot.y;
    }
}
