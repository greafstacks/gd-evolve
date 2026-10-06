#include <Geode/modify/PauseLayer.hpp>
#include <Geode/utils/file.hpp>
#include "MacroManager.hpp"
#include "popups/MacroInfoPopup.hpp"

using namespace geode::prelude;

class $modify(PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto macroBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Macro", "bigFont.fnt", "GJ_button_04.png", 0.8f),
            this,
            menu_selector(PauseLayer::onFWCMacro)
        );

        auto recorderBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Recorder", "bigFont.fnt", "GJ_button_04.png", 0.8f),
            this,
            menu_selector(PauseLayer::onFWCRecorder)
        );

        auto addMacroBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Add Macro", "bigFont.fnt", "GJ_button_04.png", 0.8f),
            this,
            menu_selector(PauseLayer::onFWCAddMacro)
        );

        m_buttonMenu->addChild(macroBtn);
        m_buttonMenu->addChild(recorderBtn);
        m_buttonMenu->addChild(addMacroBtn);
        m_buttonMenu->updateLayout();
    }

    void onFWCMacro(CCObject*) {
        MacroInfoPopup::create()->show();
    }

    void onFWCRecorder(CCObject*) {
        // Real FFmpeg-API-backed recording UI lands in the next phase.
        FLAlertLayer::create("FFmpeg Recorder", "Coming in the next update.", "OK")->show();
    }

    void onFWCAddMacro(CCObject*) {
        // NOTE: file::pick's exact option struct fields/enum names here
        // are a best-effort guess at Geode's real file-picker utility --
        // verify against <Geode/utils/file.hpp> if this doesn't compile.
        file::pick(file::PickMode::OpenFile, file::FilePickOptions{
            .filters = { { .description = "Macro files", .files = { "*.gdr2" } } }
        }).listen([](Result<std::filesystem::path>* result) {
            if (!result || result->isErr()) return;

            auto path = result->unwrap();
            auto err = MacroManager::get().loadFromFile(path);
            if (err) {
                FLAlertLayer::create("Error", err->c_str(), "OK")->show();
            } else {
                MacroInfoPopup::create()->show();
            }
        });
    }
};

$on_mod(Loaded) {
    log::info("Frame Window Counter loaded");
}
