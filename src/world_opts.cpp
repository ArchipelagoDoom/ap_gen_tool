#include <vector>
#include <string>

#include <onut/Log.h>
#include <onut/Json.h>

#include "data.h"
#include "python.hpp"
#include "message.hpp"

static std::string to_snake_case(const std::string& name)
{
    std::string ret;
    for (char c : name)
    {
        if (!(c >= '0' && c <= '9') && !(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z'))
        {
            if (ret.empty() || ret.back() != '_')
                ret += '_';
        }
        else
            ret += (char)tolower((unsigned char)c);
    }
    return ret;
}

static std::string to_title_case(const std::string& name)
{
    std::string ret;
    bool capitalize = true;

    for (char c : name)
    {
        if (capitalize)
            ret += (char)toupper((unsigned char)c);
        else
            ret += c;
        capitalize = (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z'));
    }
    return ret;
}

class WorldOption
{
public:
    static constexpr std::string_view name{"(unknown option)"};
    virtual std::string GetName(void) { return std::string(name.data(), name.size()); }

    virtual ~WorldOption(void) {}
    virtual void InsertWorldHook(game_t *game, const std::string &hook_type, std::vector<std::string>& hook) {};
    virtual void InsertPyOptions(game_t *game, std::vector<PyOption>& options) {};
};

// ============================================================================
// ============================================================================

/*****************************
** World Option: Difficulty **
*****************************/

struct default_difficulty_t {
    const char *name;
    const char *description;
};

class WO_Difficulty : public WorldOption
{
    const default_difficulty_t DIFFICULTIES_DOOM[5] = {
        {"Baby",      "Damage taken is halved. Ammo received from pickups is doubled."},
        {"Easy",      "Lesser number or strength of monsters, and more pickups."},
        {"Medium",    "The default skill. Balanced monsters and pickups."},
        {"Hard",      "Greater number or strength of monsters, and less pickups."},
        {"Nightmare", "Monsters are faster, more aggressive, and respawn."}
    };
    const default_difficulty_t DIFFICULTIES_HERETIC[5] = {
        {"Wet nurse",    "Damage taken is halved. Ammo received from pickups is doubled. Quartz Flasks and Mystic Urns are automatically used when the player nears death."},
        {"Easy",         "Lesser number or strength of monsters, and more pickups."},
        {"Medium",       "The default skill. Balanced monsters and pickups."},
        {"Hard",         "Greater number or strength of monsters, and less pickups."},
        {"Black plague", "Monsters are faster and more aggressive."}
    };

    int _preset;
    std::string _sk5_warning;
    std::vector<Json::Value> _opt_jsons; 

public:
    static constexpr std::string_view name{"Difficulty"};
    std::string GetName(void) override { return std::string(name.data(), name.size()); }

    WO_Difficulty(game_t *game, const Json::Value& json)
    {
        std::string preset_str = json.get("preset", "").asString();

        if (preset_str == "Doom")
            _preset = 1;
        else if (preset_str == "Heretic")
            _preset = 2;
        else
        {
            _preset = 0;
            _sk5_warning = json.get("skill_5_warning", "").asString();
            _opt_jsons.resize(5);
            if (json["list"].isArray())
            {
                for (int i = 0; i < 5; ++i)
                    _opt_jsons[i] = json["list"].get(i, Json::objectValue);
            }
        }
    }

    void InsertPyOptions(game_t *game, std::vector<PyOption>& options) override
    {
        if (_preset == 1)
        {
            PyOption& opt = options.emplace_back("difficulty", "DifficultyDoom", PyOptionType::InID1Common);
            opt.option_group = "Difficulty Options";
        }
        else if (_preset == 2)
        {
            PyOption& opt = options.emplace_back("difficulty", "DifficultyHeretic", PyOptionType::InID1Common);
            opt.option_group = "Difficulty Options";
        }
        else
        {
            const default_difficulty_t *diff_strings = (game->iwad_name == "HERETIC.WAD" ? DIFFICULTIES_HERETIC : DIFFICULTIES_DOOM);
            std::vector<std::string> choices;
            std::vector<std::string> aliases;

            PyOption& opt = options.emplace_back("difficulty", "Difficulty", PyOptionType::Choice);
            opt.option_group = "Difficulty Options";
            opt.docstring.push_back("Choose the game difficulty (skill level).");
            opt.docstring.push_back("");

            for (int i = 0; i < 5; ++i)
            {
                std::string full_name = _opt_jsons[i].get("full_name", "").asString();
                std::string opt_name = _opt_jsons[i].get("option_name", diff_strings[i].name).asString();
                std::string description = _opt_jsons[i].get("description", diff_strings[i].description).asString();
                if (full_name == "")
                    continue;

                opt.docstring.push_back("- **" + opt_name + "**: (" + full_name + ") - " + description);
                choices.push_back("option_" + to_snake_case(opt_name) + " = " + std::to_string(i));
                for (const auto& alias : _opt_jsons[i].get("aliases", Json::arrayValue))
                    aliases.push_back("alias_" + to_snake_case(alias.asString()) + " = " + std::to_string(i));
            }
            if (_sk5_warning != "")
                opt.option_list.push_back("skill_5_warning = " + Py_QuoteString(_sk5_warning));
            opt.option_list.insert(opt.option_list.end(), choices.begin(), choices.end());
            opt.option_list.insert(opt.option_list.end(), aliases.begin(), aliases.end());
            opt.default_int = 2;
        }
    }
};

/**********************************
** World Option: Start with Maps **
**********************************/

class WO_StartWithMaps : public WorldOption
{
    int _doom_type;
    std::string _plural_name;
    std::string _class_name;

public:
    static constexpr std::string_view name{"Start with Maps"};
    std::string GetName(void) override { return std::string(name.data(), name.size()); }

    WO_StartWithMaps(game_t *game, const Json::Value& json)
    {
        _doom_type = json.get("doom_type", (game->iwad_name == "HERETIC.WAD" ? 35 : 2026)).asInt();

        std::string singular = get_item_name(game, _doom_type);
        _plural_name = json.get("plural_name", singular + "s").asString();
        _class_name = "start_with_" + to_snake_case(_plural_name);
    }

    void InsertWorldHook(game_t *game, const std::string& hook_type, std::vector<std::string>& hook) override
    {
        if (hook_type != "create_items")
            return;

        hook.push_back("map_opt = self.options." + _class_name);
        hook.push_back("if map_opt.value:");
        hook.push_back("    map_items = [pop_from_pool(i.name) for i in self.matching_items(doom_type=map_opt.doom_type).values()]");
        hook.push_back("    [self.multiworld.push_precollected(self.create_item(n)) for n in map_items if n is not None]");
    }

    void InsertPyOptions(game_t *game, std::vector<PyOption>& options) override
    {
        // If defaults, use the common types
        if (_doom_type == 2026 && _plural_name == "Computer area maps")
        {
            PyOption& opt = options.emplace_back(_class_name, "StartWithComputerAreaMaps", PyOptionType::InID1Common);
            opt.option_group = "Randomizer Options";
        }
        else if (_doom_type == 35 && _plural_name == "Map Scrolls")
        {
            PyOption& opt = options.emplace_back(_class_name, "StartWithMapScrolls", PyOptionType::InID1Common);
            opt.option_group = "Randomizer Options";
        }
        else
        {
            std::string public_name = "Start With " + to_title_case(_plural_name);
            PyOption& opt = options.emplace_back(_class_name, public_name, PyOptionType::StartWithMaps);
            opt.option_group = "Randomizer Options";
            opt.docstring.push_back("If enabled, all " + _plural_name + " will be given to the player from the start.");
            opt.doom_type = _doom_type;
        }
    }
};

/********************************
** World Option: Invis as Trap **
********************************/

class WO_InvisAsTrap : public WorldOption
{
    int _doom_type;
    std::string _class_name;

public:
    static constexpr std::string_view name{"Invis as Trap"};
    std::string GetName(void) override { return std::string(name.data(), name.size()); }

    WO_InvisAsTrap(game_t *game, const Json::Value& json)
    {
        _doom_type = json.get("doom_type", 2024).asInt();
        _class_name = to_snake_case(get_item_name(game, _doom_type)) + "_as_trap";
    }

    void InsertWorldHook(game_t *game, const std::string& hook_type, std::vector<std::string>& hook) override
    {
        if (hook_type != "create_item")
            return;

        hook.push_back("invis_trap = self.options." + _class_name);
        hook.push_back("if invis_trap.value and item_data.doom_type == invis_trap.doom_type:");
        hook.push_back("    classification = AP.ItemClassification.trap");
    }

    void InsertPyOptions(game_t *game, std::vector<PyOption>& options) override
    {
        const std::string& invis_name = get_item_name(game, _doom_type);

        // If defaults, use the common type
        if (_doom_type == 2024 && invis_name == "Partial invisibility")
        {
            PyOption& opt = options.emplace_back(_class_name, PyOptionType::InvisibilityTrap);
            opt.option_group = "Randomizer Options";
        }
        else
        {
            std::string public_name = to_title_case(invis_name) + " as Trap";

            PyOption& opt = options.emplace_back(_class_name, public_name, PyOptionType::InvisibilityTrap);
            opt.option_group = "Randomizer Options";
            opt.docstring.push_back("If enabled, " + invis_name +" will be classified as a trap, rather than just filler.");
            opt.docstring.push_back("This does not change how the item behaves, only how Archipelago sees it.");
            opt.doom_type = _doom_type;        
        }
    }
};

/***************************************
** World Option: Custom Ammo Capacity **
***************************************/

struct capacitytype_t
{
    std::string name;
    std::string class_suffix;
    int capacity;
};

class WO_Capacity : public WorldOption
{
    std::vector<capacitytype_t> _ammotypes;

public:
    static constexpr std::string_view name{"Custom Ammo Capacity"};
    std::string GetName(void) override { return std::string(name.data(), name.size()); }

    WO_Capacity(game_t *game, const Json::Value& json)
    {
        (void)json; // not used
        const Json::Value& all_ammo = game->json_game_info.get("ammo", Json::arrayValue);
        for (const Json::Value &ammo : all_ammo)
        {
            capacitytype_t newcap;
            newcap.name = ammo.get("name", "(no name)").asString();
            newcap.class_suffix = to_snake_case(newcap.name);
            newcap.capacity = ammo.get("max", 0).asInt();
            _ammotypes.push_back(newcap);
        }
    }

    void InsertWorldHook(game_t *game, const std::string& hook_type, std::vector<std::string>& hook) override
    {
        if (hook_type == "generate_early")
        {
            bool first = true;
            hook.push_back("if (");
            for (const capacitytype_t& ammo_type : _ammotypes)
            {
                std::string condition = (first ? "    " : "    or ");
                condition += "self.options.max_ammo_" + ammo_type.class_suffix + ".value < ";
                condition += "self.options.max_ammo_" + ammo_type.class_suffix + ".default";
                hook.push_back(condition);
                first = false;
            }
            hook.push_back("):");
            hook.push_back("    self.warning(\"Some starting ammo capacity options are set below their default values.\\n\"");
            hook.push_back("                 \"This may make games significantly harder than intended; you have been warned.\")");
        }
        else if (hook_type == "fill_slot_data")
        {
            hook.push_back("slot_data[\"ammo_start\"] = [");
            for (const capacitytype_t& ammo_type : _ammotypes)
                hook.push_back("    self.options.max_ammo_" + ammo_type.class_suffix + ".value,");
            hook.push_back("]");
            hook.push_back("slot_data[\"ammo_add\"] = [");
            for (const capacitytype_t& ammo_type : _ammotypes)
                hook.push_back("    self.options.added_ammo_" + ammo_type.class_suffix + ".value,");
            hook.push_back("]");
        }
    }

    void InsertPyOptions(game_t *game, std::vector<PyOption>& options) override
    {
        for (const capacitytype_t& ammo_type : _ammotypes)
        {
            PyOption& max_opt = options.emplace_back(
                "max_ammo_" + ammo_type.class_suffix,
                "Max Ammo - " + ammo_type.name,
                PyOptionType::BoundedRandomRange
            );
            max_opt.docstring.push_back("Set the starting capacity for " + ammo_type.name + ".");
            max_opt.docstring.push_back("");
            max_opt.docstring.push_back("Setting this below the default of " + std::to_string(ammo_type.capacity) + " is allowed, but may be logically unsafe.");
            max_opt.option_group = "Ammo Capacity";
            max_opt.range_start = ammo_type.capacity / 10;
            max_opt.random_start = ammo_type.capacity;
            max_opt.range_end = 999;
            max_opt.default_int = ammo_type.capacity;
        }
        for (const capacitytype_t& ammo_type : _ammotypes)
        {
            PyOption& added_opt = options.emplace_back(
                "added_ammo_" + ammo_type.class_suffix,
                "Added Ammo - " + ammo_type.name,
                PyOptionType::Range
            );
            added_opt.docstring.push_back("Set how much capacity for " + ammo_type.name + " will be added when a capacity upgrade is obtained.");
            added_opt.option_group = "Ammo Capacity";
            added_opt.range_start = ammo_type.capacity / 10;
            added_opt.range_end = 999;
            added_opt.default_int = ammo_type.capacity;
        }
    }
};

/************************************
** World Option: Capacity Upgrades **
************************************/

class WO_CapacityUpgrades : public WorldOption
{
    int _doom_type;
    int _item_count;

    std::string _plural_name;
    std::string _split_class;
    std::string _count_class;

public:
    static constexpr std::string_view name{"Capacity Upgrades"};
    std::string GetName(void) override { return std::string(name.data(), name.size()); }

    WO_CapacityUpgrades(game_t *game, const Json::Value& json)
    {
        _doom_type = json.get("doom_type", 8).asInt();

        std::string singular = get_item_name(game, _doom_type);
        _plural_name = json.get("combined_plural_name", singular + "s").asString();

        _split_class = "split_" + to_snake_case(singular);
        _count_class = to_snake_case(singular) + "_count";

        _item_count = json.get("item_count", (game->iwad_name == "HERETIC.WAD" ? 6 : 4)).asInt();
    }

    void InsertWorldHook(game_t *game, const std::string& hook_type, std::vector<std::string>& hook) override
    {
        if (hook_type != "create_items")
            return;

        hook.push_back("split_opt = self.options." + _split_class);
        hook.push_back("split_items = list(self.matching_items(doom_type=split_opt.split_doom_types).values())");
        hook.push_back("combined_items = list(self.matching_items(doom_type=split_opt.doom_type).values())");
        hook.push_back("");
        hook.push_back("# Remove stray capacity upgrades of all types from the pool");
        hook.push_back("item_names = [i.name for i in split_items] + [i.name for i in combined_items]");
        hook.push_back("itempool = [n for n in itempool if n not in item_names]");
        hook.push_back("");
        hook.push_back("# Insert requested types and count of capacity upgrades");
        hook.push_back("if split_opt.value:");
        hook.push_back("    itempool += [i.name for i in split_items for _ in range(self.options." + _count_class + ".value)]");
        hook.push_back("else:");
        hook.push_back("    itempool += [i.name for i in combined_items for _ in range(self.options." + _count_class + ".value)]");
    }

    void InsertPyOptions(game_t *game, std::vector<PyOption>& options) override
    {
        std::string singular = get_item_name(game, _doom_type);

        // If defaults, use the common types
        if (_doom_type == 8 && singular == "Backpack" && _item_count == 4)
        {
            PyOption& split_opt = options.emplace_back(_split_class, "SplitBackpack", PyOptionType::InID1Common);
            split_opt.option_group = "Randomizer Options";
            PyOption& count_opt = options.emplace_back(_count_class, "BackpackCount", PyOptionType::InID1Common);
            count_opt.option_group = "Randomizer Options";
        }
        else if (_doom_type == 8 && singular == "Bag of Holding" && _item_count == 6)
        {
            PyOption& split_opt = options.emplace_back(_split_class, "SplitBagOfHolding", PyOptionType::InID1Common);
            split_opt.option_group = "Randomizer Options";
            PyOption& count_opt = options.emplace_back(_count_class, "BagOfHoldingCount", PyOptionType::InID1Common);
            count_opt.option_group = "Randomizer Options";
        }
        else
        {
            std::string public_split_name = "Split " + to_title_case(singular);
            std::string public_count_name = to_title_case(singular) + " Count";

            PyOption& split_opt = options.emplace_back(_split_class, public_split_name, PyOptionType::CapacitySplit);
            split_opt.option_group = "Randomizer Options";
            split_opt.docstring.push_back("Split the " + singular + " into " + std::to_string(_item_count) + " individual items, "
                "each one increasing ammo capacity for one type of weapon only.");
            split_opt.doom_type = _doom_type;
            split_opt.split_item_count = _item_count;

            PyOption& count_opt = options.emplace_back(_count_class, public_count_name, PyOptionType::CapacityCount);
            count_opt.option_group = "Randomizer Options";
            count_opt.docstring.push_back("How many " + _plural_name + " will be available.");
            count_opt.docstring.push_back("If " + public_split_name + " is set, this will be the number of each capacity upgrade available.");
        }
    }
};

/********************************
** World Option: Custom Option **
********************************/

class WO_CustomOption : public WorldOption
{
    PyOption* _opt = nullptr;

public:
    static constexpr std::string_view name{"Custom Option"};
    std::string GetName(void) override { return std::string(name.data(), name.size()); }

    WO_CustomOption(game_t *game, const Json::Value& json)
    {
        PyOptionType type = PyOptionType::Removed;
        if (json["type"].isString())
        {
            std::string type_str = json["type"].asString();
            if (type_str == "Toggle")                  type = PyOptionType::Toggle;
            else if (type_str == "Choice")             type = PyOptionType::Choice;
            else if (type_str == "Range")              type = PyOptionType::Range;
            else if (type_str == "BoundedRandomRange") type = PyOptionType::BoundedRandomRange;
            //else if (type_str == "OptionSet")          type = PyOptionType::OptionSet;
            else throw std::runtime_error("Custom option with unknown type '" + type_str + "'");
        }
        else
            throw std::runtime_error("Custom Option with no type (or non-string)");

        std::string public_name = json.get("display_name", "").asString();
        if (public_name.empty())
            throw std::runtime_error("Custom Option without a required display name");

        std::string private_name = json.get("option_name", "").asString();
        if (private_name.empty())
            private_name = to_snake_case(public_name);

        _opt = new PyOption(private_name, public_name, type);
        _opt->option_group = json.get("group", "Randomizer Options").asString();
        _opt->doom_type = json.get("doom_type", 0).asInt();

        stringarray_to_vector(_opt->docstring, json["description"]);

        switch (type)
        {
        case PyOptionType::Toggle:
            _opt->default_int = json.get("default", false).asBool();
            break;
        case PyOptionType::BoundedRandomRange:
        case PyOptionType::Range:
            _opt->range_start  = json.get("range_start",  0).asInt();
            _opt->range_end    = json.get("range_end",    100).asInt();
            _opt->default_int  = json.get("default",      _opt->range_start).asInt();
            _opt->random_start = json.get("random_start", -9999).asInt();
            _opt->random_end   = json.get("random_end",   -9999).asInt();
            break;
        case PyOptionType::Choice:
            if (json["options"].isArray())
            {
                int first_value = -9999;
                std::vector<std::string> choices;
                std::vector<std::string> aliases;
                for (const Json::Value& option_json : json["options"])
                {
                    if (!option_json["name"].isString() || !option_json["value"].isInt())
                        continue;
                    int value = option_json["value"].asInt();
                    if (first_value == -9999)
                        first_value = value;

                    choices.push_back("option_" + to_snake_case(option_json["name"].asString()) + " = " + std::to_string(value));
                    for (const auto& alias : option_json.get("aliases", Json::arrayValue))
                        aliases.push_back("alias_" + to_snake_case(alias.asString()) + " = " + std::to_string(value));
                }
                _opt->option_list.insert(_opt->option_list.end(), choices.begin(), choices.end());
                _opt->option_list.insert(_opt->option_list.end(), aliases.begin(), aliases.end());
            }
            _opt->default_int  = json.get("default", _opt->range_start).asInt();
            break;
        }
    }

    void InsertPyOptions(game_t *game, std::vector<PyOption>& options) override
    {
        options.push_back(*_opt);
    }

    ~WO_CustomOption(void)
    {
        delete _opt;
    }
};

// ============================================================================
// ============================================================================

static std::vector<WorldOption *> initialized_world_options;

int WorldOptions_Init(game_t *game)
{
    int errors = 0;

    for (const Json::Value& option : game->json_world_options)
    {
        WorldOption *newopt = nullptr;

        try
        {
            std::string option_name = option.get("name", "(no name)").asString();
            if (option_name == WO_Difficulty::name)
                newopt = new WO_Difficulty(game, option);
            else if (option_name == WO_StartWithMaps::name)
                newopt = new WO_StartWithMaps(game, option);
            else if (option_name == WO_InvisAsTrap::name)
                newopt = new WO_InvisAsTrap(game, option);
            else if (option_name == WO_Capacity::name)
                newopt = new WO_Capacity(game, option);
            else if (option_name == WO_CapacityUpgrades::name)
                newopt = new WO_CapacityUpgrades(game, option);
            else if (option_name == WO_CustomOption::name)
                newopt = new WO_CustomOption(game, option);

            if (!newopt)
                throw std::runtime_error("Unknown world option '" + option_name + "'");

            initialized_world_options.push_back(newopt);
        }
        catch (const std::runtime_error &e)
        {
            ++errors;
            std::string error_str = std::string("World option error: ") + e.what();
            OLogE(error_str);
            OnScreenMessages::AddError(error_str);
        }
    }
    return errors;
}

std::vector<std::string>& WorldOptions_GetAllHooks(game_t *game, const std::string& hook_type, int line_breaks)
{
    static std::vector<std::string> content;

    content.clear();

    // Warn on generation for missing / incomplete logic
    if (hook_type == "generate_early" &&
        (game->warnings.location_no_region || game->warnings.no_exit_connection))
    {
        content.push_back("######## World generation warnings begin here ########");
        content.push_back("self.warning(\"The logic for this game (" + game->ap_name + ") is likely incomplete.\\n\"");
        if (game->warnings.location_no_region)
            content.push_back("             \"- " + std::to_string(game->warnings.location_no_region) + " location(s) were not assigned to a region.\\n\"");
        if (game->warnings.no_exit_connection)
            content.push_back("             \"- " + std::to_string(game->warnings.no_exit_connection) + " level exit(s) were not reachable.\\n\"");
        content.push_back("             \"Use with caution.\")");
        content.push_back("######## World generation warnings end here ########");
        content.resize(content.size() + line_breaks, "");
    }

    // Mix in hooks from the game itself
    if (game->world_hooks.count(hook_type))
    {
        content.push_back("######## Custom code for this world begins here ########");
        content.insert(content.end(), game->world_hooks[hook_type].begin(), game->world_hooks[hook_type].end());
        content.push_back("######## Custom code for this world ends here ########");
        content.resize(content.size() + line_breaks, "");
    }

    // Mix in option hooks
    for (WorldOption *opt : initialized_world_options)
    {
        static std::vector<std::string> temp;

        temp.clear();
        opt->InsertWorldHook(game, hook_type, temp);
        if (temp.empty())
            continue;

        std::string opt_name = opt->GetName();
        content.push_back("######## Custom code for world option '" + opt_name + "' begins here ########");
        content.insert(content.end(), temp.begin(), temp.end());
        content.push_back("######## Custom code for world option '" + opt_name + "' ends here ########");
        content.resize(content.size() + line_breaks, "");
    }

    return content;
}

void WorldOptions_MixinPyOptions(game_t *game, std::vector<PyOption>& pyopts)
{
    for (WorldOption *opt : initialized_world_options)
        opt->InsertPyOptions(game, pyopts);
}

void WorldOptions_Deinit(void)
{
    for (WorldOption *opt : initialized_world_options)
        delete opt;
    initialized_world_options.clear();
}
