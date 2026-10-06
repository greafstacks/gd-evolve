// GD Evolve: editor button -> pick image -> evolve real GD objects -> paste.
#include <Geode/Geode.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <chrono>
#include <fstream>
#include <memory>
#include "evolve.hpp"

using namespace geode::prelude;

static std::vector<int> parseRanges(std::string const& s) {
    std::vector<int> ids;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        std::string part = s.substr(i, j - i);
        i = j + 1;
        int a = 0, b = 0;
        if (std::sscanf(part.c_str(), "%d-%d", &a, &b) == 2) {
            for (int k = a; k <= b && k < 20000; k++) ids.push_back(k);
        } else if (std::sscanf(part.c_str(), "%d", &a) == 1) {
            ids.push_back(a);
        }
    }
    return ids;
}

// Draws each object's real in-game sprite into a small texture and keeps
// its alpha + grayscale, so the engine knows what every ID looks like.
static std::vector<gdevo::Sprite> buildCatalog(std::vector<int> const& ids, int N) {
    std::vector<gdevo::Sprite> out;
    auto toolbox = ObjectToolbox::sharedState();
    auto cache = CCSpriteFrameCache::sharedSpriteFrameCache();
    for (int id : ids) {
        const char* frame = toolbox->intKeyToFrame(id);
        if (!frame || !*frame) continue;
        if (std::string(frame).rfind("edit_", 0) == 0) continue;  // editor-only icons
        auto sf = cache->spriteFrameByName(frame);
        if (!sf) continue;
        auto spr = CCSprite::createWithSpriteFrame(sf);
        if (!spr) continue;
        auto size = spr->getContentSize();
        float m = std::max(size.width, size.height);
        if (m < 1.f) continue;
        auto rt = CCRenderTexture::create(N, N);
        if (!rt) continue;
        spr->setScale(N / m);
        spr->setPosition({N / 2.f, N / 2.f});
        rt->beginWithClear(0, 0, 0, 0);
        spr->visit();
        rt->end();
        CCImage* img = rt->newCCImage(true);
        if (!img) continue;
        int w = img->getWidth(), h = img->getHeight();
        auto data = img->getData();
        if (w != h || !data) { img->release(); continue; }

        gdevo::Sprite s;
        s.id = id; s.n = w; s.box = m;
        s.alpha.resize((size_t)w * h); s.gray.resize((size_t)w * h);
        double sumA = 0, sumG = 0, sumS = 0;
        for (int i = 0; i < w * h; i++) {
            float a = data[i * 4 + 3] / 255.f;
            float r = data[i * 4], g = data[i * 4 + 1], b = data[i * 4 + 2];
            // render target is premultiplied: undo it
            float inv = a > 0.02f ? 1.f / a : 0.f;
            r = std::min(255.f, r * inv); g = std::min(255.f, g * inv); b = std::min(255.f, b * inv);
            float gr = (r + g + b) / 3.f;
            s.alpha[i] = (uint8_t)std::lround(a * 255.f);
            s.gray[i] = (uint8_t)std::lround(gr);
            if (a > 0.5f) {
                float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
                sumA += 1; sumG += gr / 255.f; sumS += mx > 0 ? (mx - mn) / mx : 0;
            }
        }
        img->release();
        if (sumA < 8) continue;                 // basically empty
        if (sumG / sumA < 0.35) continue;       // too dark to be recolored
        if (sumS / sumA > 0.25) continue;       // baked-in color, HSV would not control it
        out.push_back(std::move(s));
    }
    return out;
}

class EvoNode : public CCNode {
public:
    static EvoNode* create(LevelEditorLayer* lel, std::vector<gdevo::Sprite> cat, int w, int h,
                           std::vector<float> target, float unitPx, int count, float ox, float oy) {
        auto ret = new EvoNode();
        ret->m_lel = lel;
        ret->m_cat = std::move(cat);
        ret->m_count = count; ret->m_ox = ox; ret->m_oy = oy;
        ret->m_ev = std::make_unique<gdevo::Evolver>(ret->m_cat, w, h, std::move(target), unitPx, 1337);
        if (ret->init()) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCNode::init()) return false;
        m_note = Notification::create("Evolving...", NotificationIcon::Loading, 0.f);
        m_note->show();
        m_ev->addBackground();
        this->scheduleUpdate();
        return true;
    }

    void update(float) override {
        auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() < 10.0) {
            if ((int)m_ev->shapes.size() >= m_count || m_fails > m_count * 4) { finish(); return; }
            float p = m_ev->shapes.size() / (float)m_count;
            if (!m_ev->addShape(40, 80, p)) m_fails++;
        }
        m_note->setString(fmt::format("Evolving {}/{}", m_ev->shapes.size(), m_count));
    }

    void finish() {
        this->unscheduleUpdate();
        auto str = m_ev->toObjectString(m_ox, m_oy);
        auto path = Mod::get()->getSaveDir() / "last_evolve_objects.txt";
        std::ofstream(path) << str;
        m_lel->m_editorUI->pasteObjects(str, true, true);
        m_note->setString(fmt::format("Done: {} objects", m_ev->shapes.size()));
        m_note->setIcon(NotificationIcon::Success);
        m_note->hide();
        this->removeFromParent();
    }

private:
    Ref<LevelEditorLayer> m_lel;
    std::vector<gdevo::Sprite> m_cat;  // must outlive the evolver
    std::unique_ptr<gdevo::Evolver> m_ev;
    Ref<Notification> m_note;
    int m_count = 0, m_fails = 0;
    float m_ox = 0, m_oy = 0;
};

#include <Geode/modify/EditorPauseLayer.hpp>
class $modify(EvoPauseLayer, EditorPauseLayer) {
    bool init(LevelEditorLayer* editor) {
        if (!EditorPauseLayer::init(editor)) return false;
        auto menu = CCMenu::create();
        auto btn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Evolve Image"), this, menu_selector(EvoPauseLayer::onEvolve));
        menu->addChild(btn);
        auto win = CCDirector::get()->getWinSize();
        menu->setPosition({win.width - 80.f, 30.f});
        this->addChild(menu);
        return true;
    }

    // Geode 5 replaced the old file picker API, so the image is read from a
    // fixed place instead: <mod save dir>/input.png (or input.jpg).
    void onEvolve(CCObject*) {
        auto dir = Mod::get()->getSaveDir();
        for (auto name : {"input.png", "input.jpg", "input.jpeg"}) {
            std::error_code ec;
            auto p = dir / name;
            if (std::filesystem::exists(p, ec)) {
                this->startEvolve(p);
                return;
            }
        }
        FLAlertLayer::create(
            "GD Evolve",
            fmt::format("Put your image here, named input.png or input.jpg:\n{}",
                        geode::utils::string::pathToString(dir)),
            "OK")->show();
    }

    void startEvolve(std::filesystem::path const& path) {
        auto* mod = Mod::get();
        int count = (int)mod->getSettingValue<int64_t>("count");
        int widthUnits = (int)mod->getSettingValue<int64_t>("width");
        int res = (int)mod->getSettingValue<int64_t>("res");
        auto ids = parseRanges(mod->getSettingValue<std::string>("ids"));
        float ox = (float)mod->getSettingValue<int64_t>("origin-x");
        float oy = (float)mod->getSettingValue<int64_t>("origin-y");

        auto* img = new CCImage();
        auto ext = path.extension().string();
        bool jpg = ext == ".jpg" || ext == ".jpeg" || ext == ".JPG" || ext == ".JPEG";
        if (!img->initWithImageFile(geode::utils::string::pathToString(path).c_str(),
                                    jpg ? CCImage::kFmtJpg : CCImage::kFmtPng)) {
            img->release();
            FLAlertLayer::create("GD Evolve", "Could not read that image.", "OK")->show();
            return;
        }
        int iw = img->getWidth(), ih = img->getHeight();
        auto* px = img->getData();
        bool hasAlpha = img->hasAlpha();
        int bpp = hasAlpha ? 4 : 3;

        // downscale (box filter) to the working resolution
        float sc = (float)res / std::max(iw, ih);
        int w = std::max(8, (int)std::lround(iw * sc)), h = std::max(8, (int)std::lround(ih * sc));
        std::vector<float> target((size_t)w * h * 3);
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int x0 = x * iw / w, x1 = std::max(x0 + 1, (x + 1) * iw / w);
            int y0 = y * ih / h, y1 = std::max(y0 + 1, (y + 1) * ih / h);
            double acc[3] = {0, 0, 0}; int n = 0;
            for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) {
                auto* p = px + ((size_t)yy * iw + xx) * bpp;
                for (int c = 0; c < 3; c++) acc[c] += p[c];
                n++;
            }
            for (int c = 0; c < 3; c++) target[((size_t)y * w + x) * 3 + c] = (float)(acc[c] / n / 255.0);
        }
        img->release();

        auto cat = buildCatalog(ids, 64);
        if (cat.size() < 3) {
            FLAlertLayer::create("GD Evolve", "Found too few usable object sprites. Check the Object IDs setting.", "OK")->show();
            return;
        }
        Notification::create(fmt::format("Catalog: {} objects", cat.size()), NotificationIcon::Info)->show();

        float unitPx = (float)w / (float)widthUnits;
        auto node = EvoNode::create(m_editorLayer, std::move(cat), w, h, std::move(target), unitPx, count, ox, oy);
        if (node) CCDirector::get()->getRunningScene()->addChild(node);
    }
};
