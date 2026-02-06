#include "Freelook.hpp"

#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Options.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/Components/CameraComponent.hpp>
#include <SDK/Minecraft/Network/Packets/MovePlayerPacket.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>

void Freelook::onEnable()
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player)
    {
        setEnabled(false);
        return;
    }

    gFeatureManager->mDispatcher->listen<BaseTickEvent, &Freelook::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &Freelook::onPacketOutEvent>(this);

    auto gock = player->getActorHeadRotationComponent();
    if (!gock)
    {
        setEnabled(false);
        return;
    }

    mHeadYaw = { gock->mHeadRot, gock->mOldHeadRot };

    std::vector<EntityId> cameras;

    for (auto&& [id, cameraComponent] : player->mContext.mRegistry->view<CameraComponent>().each())
    {
        cameras.push_back(id);
    }

    for (auto id : cameras)
    {
        if (!player->mContext.mRegistry->valid(id)) continue;

        if (!player->mContext.mRegistry->any_of<CameraComponent>(id)) continue;
        auto& cameraComponent = player->mContext.mRegistry->get<CameraComponent>(id);

        player->mContext.mRegistry->set_flag<CameraAlignWithTargetForwardComponent>(id, false);

        auto storage = player->mContext.mRegistry->assure_t<UpdatePlayerFromCameraComponent>();
        if (storage && storage->contains(id))
        {
            mCameras[id] = storage->get(id).mUpdateMode;
            storage->remove(id);
        }

        if (cameraComponent.getMode() == CameraMode::FirstPerson)
        {
            auto* gaming = player->mContext.mRegistry->try_get<CameraDirectLookComponent>(id);
            if (gaming)
            {
                mOriginalRotRads[cameraComponent.getMode()] = gaming->mRotRads;
            }
        }
        else if (cameraComponent.getMode() == CameraMode::ThirdPerson || cameraComponent.getMode() == CameraMode::ThirdPersonFront)
        {
            auto* gaming = player->mContext.mRegistry->try_get<CameraOrbitComponent>(id);
            if (gaming)
            {
                mOriginalRotRads[cameraComponent.getMode()] = gaming->mRotRads;
            }
        }
    }

    auto options = ClientInstance::get()->getOptions();
    if (options && options->mThirdPerson)
    {
        mLastCameraState = options->mThirdPerson->value;
        options->mThirdPerson->value = 1;
    }
}

void Freelook::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &Freelook::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &Freelook::onPacketOutEvent>(this);

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player)
    {
        mResetRot = false;
        mCameras.clear();
        mOriginalRotRads.clear();
        return;
    }

    auto options = ClientInstance::get()->getOptions();
    if (options && options->mThirdPerson)
    {
        options->mThirdPerson->value = mLastCameraState;
    }

    std::vector<EntityId> cameras;
    for (auto&& [id, cameraComponent] : player->mContext.mRegistry->view<CameraComponent>().each())
    {
        cameras.push_back(id);
    }

    for (auto id : cameras)
    {
        if (!player->mContext.mRegistry->valid(id)) continue;
        if (!player->mContext.mRegistry->any_of<CameraComponent>(id)) continue;

        auto& cameraComponent = player->mContext.mRegistry->get<CameraComponent>(id);

        player->mContext.mRegistry->set_flag<CameraAlignWithTargetForwardComponent>(id, true);

        auto storage = player->mContext.mRegistry->assure_t<UpdatePlayerFromCameraComponent>();
        if (storage && !storage->contains(id))
        {
            int modeToRestore = 0;
            if (mCameras.find(id) != mCameras.end()) {
                modeToRestore = mCameras[id];
            }

            storage->emplace(id, UpdatePlayerFromCameraComponent(modeToRestore));
        }

        if (cameraComponent.getMode() == CameraMode::FirstPerson)
        {
            auto* gaming = player->mContext.mRegistry->try_get<CameraDirectLookComponent>(id);
            if (gaming && mOriginalRotRads.count(cameraComponent.getMode()))
            {
                gaming->mRotRads = mOriginalRotRads[cameraComponent.getMode()];
            }
        }
        else if (cameraComponent.getMode() == CameraMode::ThirdPerson || cameraComponent.getMode() == CameraMode::ThirdPersonFront)
        {
            auto* gaming = player->mContext.mRegistry->try_get<CameraOrbitComponent>(id);
            if (gaming && mOriginalRotRads.count(cameraComponent.getMode()))
            {
                gaming->mRotRads = mOriginalRotRads[cameraComponent.getMode()];
            }
        }
    }

    mCameras.clear();
    mOriginalRotRads.clear();
    mResetRot = false;
}

void Freelook::onBaseTickEvent(BaseTickEvent& event)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player)
    {
        setEnabled(false);
        return;
    }

    auto gock = player->getActorHeadRotationComponent();
    if (!gock)
    {
        setEnabled(false);
        return;
    }

    gock->mHeadRot = mHeadYaw.x;
    gock->mOldHeadRot = mHeadYaw.y;
}

void Freelook::onPacketOutEvent(PacketOutEvent& event)
{
    if (event.mPacket->getId() == PacketID::PlayerAuthInput)
    {
        auto paip = event.getPacket<PlayerAuthInputPacket>();
        paip->mYHeadRot = mHeadYaw.x;
    }
    else if (event.mPacket->getId() == PacketID::MovePlayer)
    {
        auto mpp = event.getPacket<MovePlayerPacket>();
        mpp->mYHeadRot = mHeadYaw.x;
    }
}

void Freelook::onLookInputEvent(LookInputEvent& event)
{
    if (!mResetRot) return;

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player)
    {
        mResetRot = false;
        return;
    }

    std::vector<EntityId> cameras;
    for (auto&& [id, cameraComponent] : player->mContext.mRegistry->view<CameraComponent>().each())
    {
        cameras.push_back(id);
    }

    for (auto id : cameras)
    {
        if (!player->mContext.mRegistry->valid(id)) continue;
        if (!player->mContext.mRegistry->any_of<CameraComponent>(id)) continue;

        auto& cameraComponent = player->mContext.mRegistry->get<CameraComponent>(id);

        player->mContext.mRegistry->set_flag<CameraAlignWithTargetForwardComponent>(id, true);

        auto storage = player->mContext.mRegistry->assure_t<UpdatePlayerFromCameraComponent>();
        if (storage && !storage->contains(id))
        {
            int modeToRestore = 0;
            if (mCameras.find(id) != mCameras.end()) {
                modeToRestore = mCameras[id];
            }

            storage->emplace(id, UpdatePlayerFromCameraComponent(modeToRestore));
        }

        if (cameraComponent.getMode() == CameraMode::FirstPerson)
        {
            auto* gaming = player->mContext.mRegistry->try_get<CameraDirectLookComponent>(id);
            if (gaming && mOriginalRotRads.count(cameraComponent.getMode()))
            {
                gaming->mRotRads = mOriginalRotRads[cameraComponent.getMode()];
            }
        }
        else if (cameraComponent.getMode() == CameraMode::ThirdPerson || cameraComponent.getMode() == CameraMode::ThirdPersonFront)
        {
            auto* gaming = player->mContext.mRegistry->try_get<CameraOrbitComponent>(id);
            if (gaming && mOriginalRotRads.count(cameraComponent.getMode()))
            {
                gaming->mRotRads = mOriginalRotRads[cameraComponent.getMode()];
            }
        }
    }

    mCameras.clear();
    mOriginalRotRads.clear();

    mResetRot = false;
}
