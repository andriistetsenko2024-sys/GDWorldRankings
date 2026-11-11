#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <algorithm>
#include <array>
#include <ctime>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace gwr {
    struct Player {
        std::string name;
        int rp;
    };

    struct TierEntry {
        int id;
        char tier;
        std::string name;
    };

    static const char TIERS[] = { 'S', 'A', 'B', 'C', 'D' };

    // ---------- time-based NPC simulation ----------

    static int64_t simEpoch() {
        auto mod = Mod::get();
        int64_t e = mod->getSavedValue<int64_t>("sim-epoch", 0);
        if (e <= 0) {
            e = static_cast<int64_t>(std::time(nullptr));
            mod->setSavedValue<int64_t>("sim-epoch", e);
        }
        return e;
    }

    static int hoursElapsed() {
        int64_t d = static_cast<int64_t>(std::time(nullptr)) - simEpoch();
        return d < 0 ? 0 : static_cast<int>(d / 3600);
    }

    static std::vector<Player> makePlayers() {
        static const std::array<const char*, 30> names = {
            "Xender", "Nexori", "DarkWave", "Zenth", "Kiro", "Vortex",
            "Astrix", "Neqro", "SilentX", "Ravex", "Cryzen", "Miro",
            "Hexa", "Nerix", "Voidly", "Kryptex", "Aero", "Zyro",
            "Flare", "Noxium", "Rexon", "Qubix", "Aven", "Drift",
            "Nyther", "Sparx", "Velox", "Orion", "Tenzu", "Lunex"
        };

        int hours = hoursElapsed();
        std::vector<Player> players;
        players.reserve(100);
        for (int i = 0; i < 100; ++i) {
            int tier = 100 - i;
            int base = 50000 + tier * 480 + ((i * 137) % 311);
            int perHour = 2 + (i * 13) % 9;
            players.push_back({ names[static_cast<size_t>(i) % names.size()], base + hours * perHour });
        }
        std::sort(players.begin(), players.end(), [](const Player& a, const Player& b) {
            return a.rp > b.rp;
        });
        return players;
    }

    // ---------- player RP / rank ----------

    static int getPlayerRP() {
        return Mod::get()->getSavedValue<int>("player-rp", 0);
    }

    static void setPlayerRP(int value) {
        Mod::get()->setSavedValue<int>("player-rp", std::max(0, value));
    }

    static int getPlayerRank() {
        auto players = makePlayers();
        int rp = getPlayerRP();
        int minRP = players.back().rp;
        if (rp >= minRP) {
            int rank = 1;
            for (auto const& p : players) {
                if (p.rp > rp) ++rank;
            }
            return rank;
        }
        double t = static_cast<double>(rp) / static_cast<double>(std::max(1, minRP));
        return std::max(101, static_cast<int>(150000.0 - t * 149899.0));
    }

    static int rpForLevel(GJGameLevel* level) {
        int stars = level->m_stars.value();
        int rp = stars > 0 ? stars * 100 : 20;
        if (level->m_demon.value() > 0) rp += 500;
        return rp;
    }

    static std::vector<int> getCompleted() {
        return Mod::get()->getSavedValue<std::vector<int>>("completed-levels", std::vector<int>{});
    }

    static int awardFirstCompletion(GJGameLevel* level) {
        int id = level->m_levelID.value();
        if (id <= 0) return 0;

        auto done = getCompleted();
        if (std::find(done.begin(), done.end(), id) != done.end()) return 0;

        done.push_back(id);
        Mod::get()->setSavedValue<std::vector<int>>("completed-levels", done);

        int gain = rpForLevel(level);
        setPlayerRP(getPlayerRP() + gain);
        return gain;
    }

    // ---------- personal tier list ----------

    static std::vector<TierEntry> getTierList() {
        std::vector<TierEntry> out;
        auto raw = Mod::get()->getSavedValue<std::vector<std::string>>("tier-levels", std::vector<std::string>{});
        for (auto const& s : raw) {
            auto p1 = s.find('|');
            if (p1 == std::string::npos) continue;
            auto p2 = s.find('|', p1 + 1);
            if (p2 == std::string::npos || p2 != p1 + 2) continue;
            TierEntry e;
            e.id = std::atoi(s.substr(0, p1).c_str());
            e.tier = s[p1 + 1];
            e.name = s.substr(p2 + 1);
            out.push_back(e);
        }
        return out;
    }

    static void saveTierList(std::vector<TierEntry> const& list) {
        std::vector<std::string> raw;
        for (auto const& e : list) {
            raw.push_back(std::to_string(e.id) + "|" + std::string(1, e.tier) + "|" + e.name);
        }
        Mod::get()->setSavedValue<std::vector<std::string>>("tier-levels", raw);
    }

    static char cycleTier(int id, std::string name) {
        for (auto& c : name) {
            if (c == '|') c = ' ';
        }
        auto list = getTierList();
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].id != id) continue;
            size_t t = 0;
            while (t < sizeof(TIERS) && TIERS[t] != list[i].tier) ++t;
            if (t + 1 >= sizeof(TIERS)) {
                list.erase(list.begin() + static_cast<long>(i));
                saveTierList(list);
                return 0;
            }
            list[i].tier = TIERS[t + 1];
            saveTierList(list);
            return list[i].tier;
        }
        list.push_back({ id, 'S', name });
        saveTierList(list);
        return 'S';
    }

    // ---------- screens ----------

    static void showAlert(char const* title, std::string const& body) {
        FLAlertLayer::create(nullptr, title, body, "Close", nullptr, 400.f, true, 280.f, 1.f)->show();
    }

    static void showPlayers() {
        auto players = makePlayers();
        std::string body = "YOUR POSITION: #" + std::to_string(getPlayerRank()) +
            "\nYOUR RP: " + std::to_string(getPlayerRP()) + "\n\n";
        for (size_t i = 0; i < players.size(); ++i) {
            body += std::to_string(i + 1) + ". " + players[i].name + "  -  " +
                std::to_string(players[i].rp) + " RP\n";
        }
        showAlert("TOP 100 (fictional NPCs)", body);
    }

    static void showLevels() {
        auto list = getTierList();
        if (list.empty()) {
            showAlert("Your tier list",
                "No levels yet.\n\nOpen any level page and press the TIER button:\n"
                "each press moves it S > A > B > C > D > removed.");
            return;
        }
        std::string body;
        for (char tier : TIERS) {
            std::string section;
            int n = 0;
            for (auto const& e : list) {
                if (e.tier != tier) continue;
                section += "  " + std::to_string(++n) + ". " + e.name + " (ID " + std::to_string(e.id) + ")\n";
            }
            if (!section.empty()) {
                body += std::string(1, tier) + " TIER\n" + section + "\n";
            }
        }
        showAlert("Your tier list", body);
    }

    static void showRank() {
        auto done = getCompleted();
        std::string body =
            "PLAYER: ANDRII\n"
            "WORLD RANK: #" + std::to_string(getPlayerRank()) + "\n"
            "RATING: " + std::to_string(getPlayerRP()) + " RP\n"
            "LEVELS COUNTED: " + std::to_string(done.size()) + "\n\n"
            "RP is given once per level for a normal (non-practice, non-testmode) completion:\n"
            "stars x 100, +500 for demons, 20 for unrated levels.\n"
            "NPCs keep gaining RP every real hour, even when the game is closed.";
        showAlert("GD World Rankings", body);
    }
}

class $modify(GWRMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto rankButton = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("MY RANK", "goldFont.fnt", "GJ_button_01.png", 0.55f),
            this, menu_selector(GWRMenuLayer::onOpenRank)
        );
        auto playersButton = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("TOP PLAYERS", "goldFont.fnt", "GJ_button_01.png", 0.55f),
            this, menu_selector(GWRMenuLayer::onOpenPlayers)
        );
        auto levelsButton = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("TOP LEVELS", "goldFont.fnt", "GJ_button_01.png", 0.55f),
            this, menu_selector(GWRMenuLayer::onOpenLevels)
        );

        auto menu = CCMenu::create();
        menu->addChild(rankButton);
        menu->addChild(playersButton);
        menu->addChild(levelsButton);
        menu->setPosition({ 0.f, 0.f });
        rankButton->setPosition({ winSize.width - 66.f, 112.f });
        playersButton->setPosition({ winSize.width - 66.f, 75.f });
        levelsButton->setPosition({ winSize.width - 66.f, 38.f });
        this->addChild(menu, 1000);
        return true;
    }

    void onOpenRank(CCObject*) { gwr::showRank(); }
    void onOpenPlayers(CCObject*) { gwr::showPlayers(); }
    void onOpenLevels(CCObject*) { gwr::showLevels(); }
};

class $modify(GWRPlayLayer, PlayLayer) {
    void levelComplete() {
        bool counts = !m_isPracticeMode && !m_isTestMode;
        PlayLayer::levelComplete();
        if (!counts || !m_level) return;

        int gain = gwr::awardFirstCompletion(m_level);
        if (gain > 0) {
            Notification::create(
                "+" + std::to_string(gain) + " RP  (rank #" + std::to_string(gwr::getPlayerRank()) + ")",
                NotificationIcon::Success, 3.f
            )->show();
        }
    }
};

class $modify(GWRLevelInfo, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto button = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("TIER", "goldFont.fnt", "GJ_button_01.png", 0.55f),
            this, menu_selector(GWRLevelInfo::onTier)
        );
        auto menu = CCMenu::create();
        menu->addChild(button);
        menu->setPosition({ 0.f, 0.f });
        button->setPosition({ winSize.width - 40.f, 100.f });
        this->addChild(menu, 100);
        return true;
    }

    void onTier(CCObject*) {
        if (!m_level) return;
        int id = m_level->m_levelID.value();
        if (id <= 0) {
            Notification::create("Only online levels can be added", NotificationIcon::Error, 2.f)->show();
            return;
        }
        char tier = gwr::cycleTier(id, static_cast<std::string>(m_level->m_levelName));
        if (tier == 0) {
            Notification::create("Removed from your tier list", NotificationIcon::Info, 2.f)->show();
        } else {
            Notification::create(std::string("Tier ") + tier, NotificationIcon::Success, 2.f)->show();
        }
    }
};

$on_mod(Loaded) {
    gwr::simEpoch();
    log::info("GD World Rankings loaded. Player RP: {}", gwr::getPlayerRP());
}
