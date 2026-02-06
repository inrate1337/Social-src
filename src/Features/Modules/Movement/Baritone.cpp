#include "Baritone.hpp"
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <SDK/Minecraft/Actor/Components/MoveInputComponent.hpp>
#include <SDK/Minecraft/Actor/Components/ActorRotationComponent.hpp>
#include <SDK/Minecraft/Actor/Components/ActorHeadRotationComponent.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
#include <SDK/Minecraft/World/BlockSource.hpp>
#include <SDK/Minecraft/World/BlockLegacy.hpp>
#include <Utils/GameUtils/ChatUtils.hpp>
#include <cmath>

#define PI 3.14159265359f
#define RAD_DEG (PI / 180.f)

void Baritone::onEnable() {
    if (!mActive) {
        ChatUtils::displayClientMessage("not target, use .goto x z");
        setEnabled(false);
        return;
    }
    mIsBypassing = false;
    mBypassSide = 0;
    mStuckTimer = 0;
    mBypassTimer = 0;
    mLastPosition = { 0,0,0 };

    gFeatureManager->mDispatcher->listen<BaseTickEvent, &Baritone::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &Baritone::onPacketOutEvent>(this);
}

void Baritone::onDisable() {
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &Baritone::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &Baritone::onPacketOutEvent>(this);

    auto player = ClientInstance::get()->getLocalPlayer();
    if (player) {
        auto moveInput = player->getMoveInputComponent();
        if (moveInput) {
            moveInput->mForward = false;
            moveInput->mMoveVector = { 0.f, 0.f };
            moveInput->mIsSprinting = false;
            moveInput->mIsJumping = false;
        }
    }
}

void Baritone::setTarget(const glm::vec3& pos) {
    mTargetPos = pos;
    mActive = true;
    setEnabled(true);
    std::string msg = "path to: " + std::to_string((int)pos.x) + ", " + std::to_string((int)pos.z);
    ChatUtils::displayClientMessage(msg);
}

float Baritone::getDistanceToTarget() {
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return 0.0f;
    float dx = mTargetPos.x - player->getPos()->x;
    float dz = mTargetPos.z - player->getPos()->z;
    return sqrtf(dx * dx + dz * dz);
}

void Baritone::calculateRotations() {
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    glm::vec3 currentPos = *player->getPos();
    float dx = mTargetPos.x - currentPos.x;
    float dz = mTargetPos.z - currentPos.z;

    mYaw = (atan2f(dz, dx) * 180.0f / PI) - 90.0f;
    mPitch = 0.f;
}

bool Baritone::isPathBlocked(float yaw, float distance, float widthOffset) {
    auto player = ClientInstance::get()->getLocalPlayer();
    auto region = ClientInstance::get()->getBlockSource();
    if (!player || !region) return true;

    glm::vec3 pos = *player->getPos();
    float calcYaw = (yaw + 90.0f) * RAD_DEG;
    float dirX = cos(calcYaw);
    float dirZ = sin(calcYaw);

    float offsets[] = { 0.0f, -widthOffset, widthOffset };

    for (float off : offsets) {
        float offX = pos.x + (dirX * distance) - (dirZ * off);
        float offZ = pos.z + (dirZ * distance) + (dirX * off);

        glm::ivec3 checkPos;
        checkPos.x = floor(offX);
        checkPos.z = floor(offZ);

        checkPos.y = floor(pos.y);
        if (region->getBlock(checkPos)->mLegacy->getBlockId() != 0) return true;

        checkPos.y = floor(pos.y + 1.0f);
        if (region->getBlock(checkPos)->mLegacy->getBlockId() != 0) return true;
    }

    return false;
}

bool Baritone::isVoid(float yaw, float distance) {
    auto player = ClientInstance::get()->getLocalPlayer();
    auto region = ClientInstance::get()->getBlockSource();
    if (!player || !region) return true;

    glm::vec3 pos = *player->getPos();
    float calcYaw = (yaw + 90.0f) * RAD_DEG;

    glm::ivec3 checkPos;
    checkPos.x = floor(pos.x + (cos(calcYaw) * distance));
    checkPos.z = floor(pos.z + (sin(calcYaw) * distance));

    checkPos.y = floor(pos.y - 1.0f);
    Block* groundBlock = region->getBlock(checkPos);

    checkPos.y = floor(pos.y - 2.0f);
    Block* belowGround = region->getBlock(checkPos);

    if (groundBlock->mLegacy->getBlockId() == 0 && belowGround->mLegacy->getBlockId() == 0) return true;

    return false;
}

bool Baritone::canJumpOver(float yaw) {
    auto player = ClientInstance::get()->getLocalPlayer();
    auto region = ClientInstance::get()->getBlockSource();
    if (!player || !region) return false;

    glm::vec3 pos = *player->getPos();
    float calcYaw = (yaw + 90.0f) * RAD_DEG;
    float dirX = cos(calcYaw);
    float dirZ = sin(calcYaw);

    glm::ivec3 blockPos;
    blockPos.x = floor(pos.x + (dirX * 0.8f));
    blockPos.z = floor(pos.z + (dirZ * 0.8f));
    blockPos.y = floor(pos.y);

    Block* obstacle = region->getBlock(blockPos);

    blockPos.y += 1;
    Block* aboveObstacle = region->getBlock(blockPos);

    blockPos.y += 1;
    Block* headSpace = region->getBlock(blockPos);

    if (obstacle->mLegacy->getBlockId() != 0 &&
        aboveObstacle->mLegacy->getBlockId() == 0 &&
        headSpace->mLegacy->getBlockId() == 0) return true;

    return false;
}

void Baritone::onBaseTickEvent(BaseTickEvent& event) {
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player || !mActive) return;

    if (getDistanceToTarget() < 1.0f) {
        ChatUtils::displayClientMessage("Success");
        mActive = false;
        setEnabled(false);
        return;
    }

    glm::vec3 currentPos = *player->getPos();
    float dx = mTargetPos.x - currentPos.x;
    float dz = mTargetPos.z - currentPos.z;
    float idealYaw = (atan2f(dz, dx) * 180.0f / PI) - 90.0f;

    float moveYaw = idealYaw;
    bool shouldJump = false;

    if (glm::distance(currentPos, mLastPosition) < 0.05f) {
        mStuckTimer++;
    }
    else {
        mStuckTimer = 0;
    }
    mLastPosition = currentPos;

    if (mAvoidance.mValue) {

        if (mIsBypassing) {
            mBypassTimer++;

            if (mBypassTimer > 10 && !isPathBlocked(idealYaw, 3.0f, 0.5f) && !isVoid(idealYaw, 2.0f)) {
                mIsBypassing = false;
                mBypassSide = 0;
            }
            else {
                float angleOffset = (mBypassSide == 1) ? 75.0f : -75.0f;
                moveYaw = idealYaw + angleOffset;

                if (isPathBlocked(moveYaw, 1.2f, 0.5f)) {
                    angleOffset = (mBypassSide == 1) ? 90.0f : -90.0f;
                    moveYaw = idealYaw + angleOffset;

                    if (isPathBlocked(moveYaw, 1.0f, 0.4f)) {
                        angleOffset = (mBypassSide == 1) ? 135.0f : -135.0f;
                        moveYaw = idealYaw + angleOffset;
                    }
                }
            }
        }
        else {

            if (isPathBlocked(idealYaw, 1.5f, 0.5f) || isVoid(idealYaw, 1.5f)) {

                if (mJump.mValue && canJumpOver(idealYaw)) {
                    shouldJump = true;
                }
                else {
                    mIsBypassing = true;
                    mBypassTimer = 0;

                    bool leftClear = !isPathBlocked(idealYaw - 50.f, 2.0f, 0.4f);
                    bool rightClear = !isPathBlocked(idealYaw + 50.f, 2.0f, 0.4f);

                    if (rightClear) mBypassSide = 1;
                    else if (leftClear) mBypassSide = -1;
                    else mBypassSide = 1;

                    moveYaw = idealYaw + (mBypassSide == 1 ? 75.0f : -75.0f);
                }
            }
        }
    }

    if (mStuckTimer > 15) {
        shouldJump = true;
        mIsBypassing = true;
        mBypassSide = -mBypassSide;
        moveYaw = idealYaw + 180.0f;
        mStuckTimer = 0;
    }

    mYaw = moveYaw;
    mPitch = 0.f;

    auto rotComp = player->getActorRotationComponent();
    auto headRotComp = player->getActorHeadRotationComponent();
    if (rotComp) {
        rotComp->mYaw = mYaw;
        rotComp->mPitch = mPitch;
    }
    if (headRotComp) {
        headRotComp->mHeadRot = mYaw;
    }

    auto moveInput = player->getMoveInputComponent();
    if (moveInput) {
        moveInput->mForward = true;
        moveInput->mMoveVector = { 0.f, 1.f };
        moveInput->mIsSprinting = mAutoSprint.mValue;

        if (mJump.mValue) {
            if (shouldJump || (player->isCollidingHorizontal() && player->isOnGround())) {
                moveInput->mIsJumping = true;
            }
            else {
                moveInput->mIsJumping = false;
            }
        }
    }
}

void Baritone::onPacketOutEvent(PacketOutEvent& event) {
    if (!mActive) return;

    if (event.mPacket->getId() == PacketID::PlayerAuthInput) {
        auto pkt = event.getPacket<PlayerAuthInputPacket>();

        pkt->mRot = { mPitch, mYaw };
        pkt->mYHeadRot = mYaw;
        pkt->mMove = { 0.f, 1.f };

        auto inputFlags = pkt->mInputData | AuthInputAction::UP;

        if (mAutoSprint.mValue) inputFlags |= AuthInputAction::SPRINTING;

        auto localPlayer = ClientInstance::get()->getLocalPlayer();
        auto moveInput = localPlayer ? localPlayer->getMoveInputComponent() : nullptr;
        if (moveInput && moveInput->mIsJumping) {
            inputFlags |= AuthInputAction::JUMPING;
        }

        pkt->mInputData = inputFlags;
    }
}
