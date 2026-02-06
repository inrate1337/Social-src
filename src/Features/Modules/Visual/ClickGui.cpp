//
// Created by vastrakai on 6/29/2024.
//

#include "ClickGui.hpp"

#include <Features/Events/MouseEvent.hpp>
#include <Features/Events/KeyEvent.hpp>
#include <Features/GUI/ModernDropdown.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <Windows.h>

static bool lastMouseState = false;
static bool isPressingShift = false;
static ModernGui modernGui = ModernGui();
static uint64_t lastToggleMs = 0;


void ClickGui::onEnable()
{
    auto ci = ClientInstance::get();
    if (ci) {
        lastMouseState = ci->getMouseGrabbed();
        ci->releaseMouse();
    }

    modernGui.openGui();

    gFeatureManager->mDispatcher->listen<MouseEvent, &ClickGui::onMouseEvent>(this);
    gFeatureManager->mDispatcher->listen<KeyEvent, &ClickGui::onKeyEvent, nes::event_priority::FIRST>(this);
}

void ClickGui::onDisable()
{
    gFeatureManager->mDispatcher->deafen<MouseEvent, &ClickGui::onMouseEvent>(this);
    gFeatureManager->mDispatcher->deafen<KeyEvent, &ClickGui::onKeyEvent>(this);
    modernGui.closeGui();
    isPressingShift = false;

    if (lastMouseState) {
        auto ci = ClientInstance::get();
        if (ci) ci->grabMouse();
    }
}

void ClickGui::onWindowResizeEvent(WindowResizeEvent& event)
{
    modernGui.onWindowResizeEvent(event); // are you okay in the head 😭
}


void ClickGui::onMouseEvent(MouseEvent& event)
{
    event.mCancelled = true;
}

void ClickGui::onKeyEvent(KeyEvent& event)
{
    {
        std::scoped_lock<std::mutex> lk(modernGui.uiMutex);
        if (modernGui.isBinding)
        {
            event.mCancelled = true;

            if (event.mPressed)
            {
                if (event.mKey == VK_ESCAPE)
                {
                    if (!modernGui.isBoolSettingBinding && modernGui.lastMod)
                    {
                        modernGui.lastMod->mKey = 0;
                    }
                }
                else if (event.mKey == VK_BACK || event.mKey == VK_DELETE)
                {
                    if (modernGui.isBoolSettingBinding && modernGui.lastBoolSetting)
                    {
                        modernGui.lastBoolSetting->mKey = -1;
                    }
                    else if (modernGui.lastMod)
                    {
                        modernGui.lastMod->mKey = 0;
                    }
                }
                else
                {
                    if (modernGui.isBoolSettingBinding && modernGui.lastBoolSetting)
                    {
                        modernGui.lastBoolSetting->mKey = event.mKey;
                    }
                    else if (modernGui.lastMod)
                    {
                        modernGui.lastMod->mKey = event.mKey;
                    }
                }
                modernGui.isBinding = false;
                modernGui.isBoolSettingBinding = false;
                modernGui.lastBoolSetting = nullptr;
                modernGui.lastMod = nullptr;
            }

            return;
        }
    }

    bool shouldClose = false;
    if (event.mKey == VK_ESCAPE && event.mPressed)
    {
        {
            std::scoped_lock<std::mutex> lk(modernGui.uiMutex);
            if (modernGui.isThemePickerOpen) {
                modernGui.isThemePickerOpen = false;
                event.mCancelled = true;
                return;
            }
            shouldClose = true;
        }
    }

    if (event.mKey == VK_ESCAPE)
    {
        event.mCancelled = true;
    }

    if (event.mKey == VK_SHIFT)
    {
        isPressingShift = event.mPressed;
        event.mCancelled = true;
    }

    if (shouldClose) {
        modernGui.requestClose();
    }
}

float ClickGui::getEaseAnim(EasingUtil ease, int mode) {
    switch (mode) {
    case 0: return ease.easeOutExpo(); break;
    case 1: return mEnabled ? ease.easeOutElastic() : ease.easeOutBack(); break;
    default: return ease.easeOutExpo(); break;
    }

}

void ClickGui::onRenderEvent(RenderEvent& event)
{
    if (!ImGui::GetCurrentContext()) return;
    if (!mEnabled) return;
    if (modernGui.consumeCloseRequest()) {
        uint64_t now = GetTickCount64();
        if (now - lastToggleMs >= 120) {
            lastToggleMs = now;
            this->toggle();
        }
        return;
    }
    auto ci = ClientInstance::get();
    if (mEnabled && ci) ci->releaseMouse();
    static int scrollDirection = 0;
    static char h[2] = { 0 };
    if (ImGui::GetIO().MouseWheel > 0) {
        scrollDirection = -1;
    }
    else if (ImGui::GetIO().MouseWheel < 0) {
        scrollDirection = 1;
    }
    else {
        scrollDirection = 0;
    }


    if (mStyle.mValue == ClickGuiStyle::Modern)
    {
        modernGui.render(1.0f, 1.0f, scrollDirection, h, mMidclickRounding.mValue, isPressingShift);
    }

}
