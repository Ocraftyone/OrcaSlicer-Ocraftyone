#include <slic3r/GUI/GUI_App.hpp>
#include <slic3r/GUI/MainFrame.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <utility>
#include <vector>
#include <slic3r/GUI/CreatePresetsDialog.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/property_tree/json_parser.hpp>
#include "Spoolman.hpp"
#include "Http.hpp"

namespace Slic3r {

namespace {
template<class Type> Type get_opt(pt::ptree& data, string path) { return data.get_optional<Type>(path).value_or(Type()); }

static constexpr const char* MOONRAKER_DEFAULT_PORT = "7125";

struct ServerAddress
{
    std::string scheme{"http"};
    std::string host{};
    std::string port{};
    bool        has_port{false};
};

static ServerAddress parse_server_address(std::string address)
{
    boost::algorithm::trim(address);
    ServerAddress result;

    if (address.empty())
        return result;

    auto scheme_pos = address.find("://");
    if (scheme_pos != std::string::npos) {
        result.scheme = address.substr(0, scheme_pos);
        address       = address.substr(scheme_pos + 3);
    }

    while (!address.empty() && address.back() == '/')
        address.pop_back();

    if (address.empty())
        return result;

    // Very small IPv6 handling: if the host starts with '[' assume the port is after ']'.
    if (!address.empty() && address.front() == '[') {
        auto closing = address.find(']');
        if (closing != std::string::npos) {
            result.host = address.substr(0, closing + 1);
            if (closing + 1 < address.size() && address[closing + 1] == ':') {
                result.port     = address.substr(closing + 2);
                result.has_port = true;
            }
            return result;
        }
    }

    auto colon_pos = address.find_last_of(':');
    if (colon_pos != std::string::npos && colon_pos + 1 < address.size() && address.find(':', colon_pos + 1) == std::string::npos) {
        result.host     = address.substr(0, colon_pos);
        result.port     = address.substr(colon_pos + 1);
        result.has_port = true;
    } else {
        result.host = address;
    }

    return result;
}

static std::string build_query_body(const std::map<std::string, std::vector<std::string>>& objects)
{
    pt::ptree request;
    pt::ptree objects_node;

    for (const auto& [name, fields] : objects) {
        pt::ptree field_array;
        for (const auto& field : fields) {
            pt::ptree value;
            value.put("", field);
            field_array.push_back({"", value});
        }
        objects_node.add_child(name, field_array);
    }

    request.add_child("objects", objects_node);

    std::ostringstream stream;
    pt::write_json(stream, request, false);
    return stream.str();
}
} // namespace

// Max timout in seconds for Spoolman HTTP requests
static constexpr long MAX_TIMEOUT = 5;

//---------------------------------
// Spoolman
//---------------------------------

static std::string get_spoolman_api_url()
{
    std::string spoolman_host = wxGetApp().app_config->get("spoolman", "host");
    std::string spoolman_port = Spoolman::DEFAULT_PORT;

    // Remove http(s) designator from the string as it interferes with the next step
    spoolman_host = boost::regex_replace(spoolman_host, boost::regex("https?://"), "");

    // If the host contains a port, use that rather than the default
    if (spoolman_host.find_last_of(':') != string::npos) {
        static const boost::regex pattern(R"((?<host>[a-z0-9.\-_]+):(?<port>[0-9]+))", boost::regex_constants::icase);
        boost::smatch result;
        if (boost::regex_match(spoolman_host, result, pattern)) {
            spoolman_port = result["port"]; // get port value first since it is overwritten when setting the host value in the next line
            spoolman_host = result["host"];
        } else {
            BOOST_LOG_TRIVIAL(error) << "Failed to parse host string. Host: " << spoolman_host << ", Port: " << spoolman_port;
        }
    }

    return spoolman_host + ":" + spoolman_port + "/api/v1/";
}

pt::ptree Spoolman::get_spoolman_json(const string& api_endpoint)
{
    auto url  = get_spoolman_api_url() + api_endpoint;
    auto http = Http::get(url);

    bool        res;
    std::string res_body;

    http.on_error([&](const std::string& body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "Failed to get data from the Spoolman server. Make sure that the port is correct and the server is running." << boost::format(" HTTP Error: %1%, HTTP status code: %2%") % error % status;
            res = false;
        })
        .on_complete([&](std::string body, unsigned) {
            res_body = std::move(body);
            res      = true;
        })
        .timeout_max(MAX_TIMEOUT)
        .perform_sync();

    if (!res)
        return {};

    if (res_body.empty()) {
        BOOST_LOG_TRIVIAL(info) << "Spoolman request returned an empty string";
        return {};
    }

    pt::ptree tree;
    try {
        stringstream ss(res_body);
        pt::read_json(ss, tree);
    } catch (std::exception& exception) {
        BOOST_LOG_TRIVIAL(error) << "Failed to read json into property tree. Exception: " << exception.what();
        return {};
    }

    return tree;
}

pt::ptree Spoolman::put_spoolman_json(const string& api_endpoint, const pt::ptree& data)
{
    auto url  = get_spoolman_api_url() + api_endpoint;
    auto http = Http::put2(url);

    bool        res;
    std::string res_body;

    stringstream ss;
    pt::write_json(ss, data);

    http.header("Content-Type", "application/json")
        .set_post_body(ss.str())
        .on_error([&](const std::string& body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "Failed to put data to the Spoolman server. Make sure that the port is correct and the server is running." << boost::format(" HTTP Error: %1%, HTTP status code: %2%, Response body: %3%") % error % status % body;
            res = false;
        })
        .on_complete([&](std::string body, unsigned) {
            res_body = std::move(body);
            res      = true;
        })
        .timeout_max(MAX_TIMEOUT)
        .perform_sync();

    if (!res)
        return {};

    if (res_body.empty()) {
        BOOST_LOG_TRIVIAL(info) << "Spoolman request returned an empty string";
        return {};
    }

    pt::ptree tree;
    try {
        ss = stringstream(res_body);
        pt::read_json(ss, tree);
    } catch (std::exception& exception) {
        BOOST_LOG_TRIVIAL(error) << "Failed to read json into property tree. Exception: " << exception.what();
        return {};
    }

    return tree;
}

std::vector<std::string> Spoolman::get_moonraker_candidate_urls()
{
    std::vector<std::string> urls;

    std::string spoolman_host = wxGetApp().app_config->get("spoolman", "host");
    auto        address       = parse_server_address(spoolman_host);

    if (address.host.empty())
        return urls;

    std::set<std::string> seen;
    auto add_url = [&](const std::string& scheme, const std::string& host, const std::string& port) {
        std::string url = scheme + "://" + host;
        if (!port.empty())
            url += ":" + port;
        url += "/";
        if (seen.insert(url).second)
            urls.push_back(std::move(url));
    };

    if (address.has_port)
        add_url(address.scheme, address.host, address.port);

    add_url(address.scheme, address.host, MOONRAKER_DEFAULT_PORT);

    if (!address.has_port || (address.port != "80" && address.port != "443"))
        add_url(address.scheme, address.host, "");

    return urls;
}

bool Spoolman::moonraker_query(const std::string& request_body, pt::ptree& response)
{
    const auto urls = get_moonraker_candidate_urls();
    if (urls.empty())
        return false;

    for (const auto& base : urls) {
        bool        success{false};
        std::string res_body;

        auto http = Http::post(base + "printer/objects/query");
        http.header("Content-Type", "application/json")
            .timeout_connect(MAX_TIMEOUT)
            .set_post_body(request_body)
            .timeout_max(MAX_TIMEOUT)
            .on_complete([&](std::string body, unsigned) {
                res_body = std::move(body);
                success  = true;
            })
            .on_error([&](const std::string&, std::string error, unsigned status) {
                BOOST_LOG_TRIVIAL(error) << "Failed to query Moonraker at " << base
                                         << "printer/objects/query. Error: " << error << ", HTTP status: " << status;
            })
            .perform_sync();

        if (!success || res_body.empty())
            continue;

        try {
            std::stringstream ss(res_body);
            pt::read_json(ss, response);
            return true;
        } catch (const std::exception& exception) {
            BOOST_LOG_TRIVIAL(error) << "Failed to read Moonraker json into property tree. Exception: " << exception.what();
        }
    }

    return false;
}

bool Spoolman::update_moonraker_lane_cache()
{
    m_moonraker_lane_cache.clear();

    const auto lane_query = build_query_body({{"AFC", {"lanes"}}});

    pt::ptree lane_response;
    if (!moonraker_query(lane_query, lane_response))
        return false;

    auto lanes_node_opt = lane_response.get_child_optional("result.status.AFC.lanes");
    if (!lanes_node_opt)
        return true;

    std::set<std::string> lane_name_set;
    std::vector<const pt::ptree*> stack{&lanes_node_opt.get()};

    auto add_lane_name = [&](const std::string& value) {
        auto trimmed = boost::algorithm::trim_copy(value);
        if (!trimmed.empty())
            lane_name_set.insert(std::move(trimmed));
    };

    while (!stack.empty()) {
        const pt::ptree* node = stack.back();
        stack.pop_back();

        for (const auto& child : *node) {
            if (!child.first.empty())
                add_lane_name(child.first);

            if (auto value = child.second.get_value_optional<std::string>())
                add_lane_name(*value);

            if (!child.second.empty())
                stack.push_back(&child.second);
        }
    }

    std::vector<std::string> lane_names{lane_name_set.begin(), lane_name_set.end()};
    if (lane_names.empty())
        return true;

    std::map<std::string, std::vector<std::string>> lane_object_requests;
    const std::vector<std::string>                  lane_fields{
        "name",
        "lane",
        "spool_id",
        "loaded_spool_id",
        "spool",
        "spoolman",
        "spoolman_spool_id",
        "metadata",
    };
    for (const auto& lane_name : lane_names) {
        lane_object_requests["AFC_stepper " + lane_name] = lane_fields;
        lane_object_requests["AFC_lane " + lane_name]    = lane_fields;
    }

    const auto lane_objects_query = build_query_body(lane_object_requests);

    pt::ptree lane_objects_response;
    if (!moonraker_query(lane_objects_query, lane_objects_response))
        return false;

    auto status_node_opt = lane_objects_response.get_child_optional("result.status");
    if (!status_node_opt)
        return true;

    const auto& status_node = status_node_opt.get();

    std::set<unsigned int> used_lane_indices;
    unsigned int           next_lane_index = 0;

    auto allocate_lane_index = [&]() {
        while (used_lane_indices.count(next_lane_index) != 0)
            ++next_lane_index;

        const unsigned int allocated = next_lane_index;
        used_lane_indices.insert(allocated);
        ++next_lane_index;
        return allocated;
    };

    auto parse_lane_integer = [](const std::string& value) -> std::optional<unsigned int> {
        std::string trimmed = boost::algorithm::trim_copy(value);
        if (trimmed.empty())
            return std::nullopt;

        try {
            size_t parsed_chars = 0;
            auto   parsed       = std::stoul(trimmed, &parsed_chars);
            if (parsed_chars == trimmed.size())
                return static_cast<unsigned int>(parsed);
        } catch (...) {
        }

        std::string digits;
        std::copy_if(trimmed.begin(), trimmed.end(), std::back_inserter(digits), [](char ch) {
            return std::isdigit(static_cast<unsigned char>(ch));
        });
        if (!digits.empty()) {
            try {
                return static_cast<unsigned int>(std::stoul(digits));
            } catch (...) {
            }
        }

        return std::nullopt;
    };

    auto parse_unsigned_string = [&](const std::string& value) -> std::optional<unsigned int> {
        auto trimmed = boost::algorithm::trim_copy(value);
        if (trimmed.empty())
            return std::nullopt;

        try {
            size_t parsed_chars = 0;
            auto   parsed       = std::stoul(trimmed, &parsed_chars, 10);
            if (parsed_chars == trimmed.size())
                return static_cast<unsigned int>(parsed);
        } catch (...) {
        }

        std::string digits;
        std::copy_if(trimmed.begin(), trimmed.end(), std::back_inserter(digits), [](char ch) {
            return std::isdigit(static_cast<unsigned char>(ch));
        });
        if (!digits.empty()) {
            try {
                return static_cast<unsigned int>(std::stoul(digits));
            } catch (...) {
            }
        }

        return std::nullopt;
    };

    auto parse_node_value = [&](const pt::ptree& node) -> std::optional<unsigned int> {
        if (auto unsigned_value = node.get_value_optional<unsigned int>()) {
            if (*unsigned_value > 0)
                return *unsigned_value;
        }

        if (auto signed_value = node.get_value_optional<int>()) {
            if (*signed_value > 0)
                return static_cast<unsigned int>(*signed_value);
        }

        if (auto string_value = node.get_value_optional<std::string>())
            return parse_unsigned_string(*string_value);

        return std::nullopt;
    };

    auto extract_spool_id = [&](const pt::ptree& root) -> std::optional<unsigned int> {
        struct NodeEntry {
            const pt::ptree* node{nullptr};
            bool             spool_related{false};
        };

        std::vector<NodeEntry> stack{{NodeEntry{&root, false}}};

        while (!stack.empty()) {
            NodeEntry entry = stack.back();
            stack.pop_back();

            for (const auto& child : *entry.node) {
                const auto& key = child.first;
                auto        key_lower = boost::algorithm::to_lower_copy(key);
                bool        child_spool_related = entry.spool_related || key_lower.find("spool") != std::string::npos;

                bool key_contains_id = key_lower.find("id") != std::string::npos;
                bool looks_like_spool_id = key_lower.find("spool_id") != std::string::npos ||
                                           key_lower.find("spoolman_id") != std::string::npos ||
                                           (child_spool_related && key_contains_id);

                if (looks_like_spool_id) {
                    if (auto parsed = parse_node_value(child.second))
                        return parsed;
                }

                stack.push_back(NodeEntry{&child.second, child_spool_related});
            }
        }

        return std::nullopt;
    };

    auto extract_lane_index = [&](const std::string& lane_name, const std::array<const pt::ptree*, 2>& nodes) {
        for (const auto* node : nodes) {
            if (!node)
                continue;

            if (auto value = node->get_optional<unsigned int>("lane")) {
                if (*value > 0)
                    return *value;
            }

            if (auto signed_value = node->get_optional<int>("lane")) {
                if (*signed_value > 0)
                    return static_cast<unsigned int>(*signed_value);
            }

            if (auto lane_string = node->get_optional<std::string>("lane")) {
                if (auto parsed = parse_lane_integer(*lane_string))
                    return parsed;
            }

            if (auto name_value = node->get_optional<std::string>("name")) {
                if (auto parsed = parse_lane_integer(*name_value))
                    return parsed;
            }
        }

        if (auto parsed = parse_lane_integer(lane_name))
            return parsed;

        return std::optional<unsigned int>{};
    };

    auto extract_lane_label = [&](const std::string& lane_name, unsigned int lane_index,
                                  const std::array<const pt::ptree*, 2>& nodes) {
        for (const auto* node : nodes) {
            if (!node)
                continue;

            auto label = node->get("name", "");
            boost::algorithm::trim(label);
            if (!label.empty())
                return label;
        }

        std::string label = lane_name;
        boost::algorithm::trim(label);
        if (!label.empty())
            return label;

        return std::string("Lane ") + std::to_string(lane_index);
    };

    for (const auto& lane_name : lane_names) {
        const auto stepper_key = "AFC_stepper " + lane_name;
        const auto lane_key    = "AFC_lane " + lane_name;

        const pt::ptree* stepper_node = nullptr;
        const pt::ptree* lane_node    = nullptr;

        if (auto node_opt = status_node.get_child_optional(stepper_key))
            stepper_node = &node_opt.get();
        if (auto node_opt = status_node.get_child_optional(lane_key))
            lane_node = &node_opt.get();

        if (!stepper_node && !lane_node)
            continue;

        const std::array<const pt::ptree*, 2> nodes{{stepper_node, lane_node}};

        std::optional<unsigned int> spool_id;
        for (const auto* node : nodes) {
            if (!node)
                continue;
            if ((spool_id = extract_spool_id(*node)))
                break;
        }

        if (!spool_id) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << ": Failed to resolve spool id for lane '" << lane_name << "'";
            continue;
        }

        auto lane_index_opt = extract_lane_index(lane_name, nodes);
        unsigned int lane_index;
        if (lane_index_opt && used_lane_indices.insert(*lane_index_opt).second) {
            lane_index = *lane_index_opt;
            if (*lane_index_opt >= next_lane_index)
                next_lane_index = *lane_index_opt + 1;
        } else {
            lane_index = allocate_lane_index();
        }

        auto lane_label = extract_lane_label(lane_name, lane_index, nodes);

        LaneInfo info;
        info.lane_index = lane_index;
        info.lane_label = std::move(lane_label);

        auto [cache_it, inserted] = m_moonraker_lane_cache.emplace(*spool_id, std::move(info));
        if (!inserted) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": Spool " << *spool_id
                                       << " is assigned to multiple Moonraker lanes.";
        }
    }

    return true;
}

bool Spoolman::pull_spoolman_spools()
{
    pt::ptree tree;

    this->clear();

    // Vendor
    tree = get_spoolman_json("vendor");
    if (tree.empty())
        return false;
    for (const auto& item : tree)
        m_vendors.emplace(item.second.get<int>("id"), make_shared<SpoolmanVendor>(SpoolmanVendor(item.second)));

    // Filament
    tree = get_spoolman_json("filament");
    if (tree.empty())
        return false;
    for (const auto& item : tree)
        m_filaments.emplace(item.second.get<int>("id"), make_shared<SpoolmanFilament>(SpoolmanFilament(item.second)));

    // Spool
    tree = get_spoolman_json("spool");
    if (tree.empty())
        return false;
    for (const auto& item : tree)
        m_spools.emplace(item.second.get<int>("id"), make_shared<SpoolmanSpool>(SpoolmanSpool(item.second)));

    return true;
}

bool Spoolman::use_spoolman_spool(const unsigned int& spool_id, const double& usage, const std::string& usage_type)
{
    pt::ptree tree;
    tree.put("use_" + usage_type, usage);

    std::string endpoint = (boost::format("spool/%1%/use") % spool_id).str();
    tree = put_spoolman_json(endpoint, tree);
    if (tree.empty())
        return false;

    get_spoolman_spool_by_id(spool_id)->update_from_json(tree);
    return true;
}

bool Spoolman::use_spoolman_spools(const std::map<unsigned int, double>& data, const std::string& usage_type)
{
    if (!(usage_type == "length" || usage_type == "weight"))
        return false;

    std::vector<unsigned int> spool_ids;

    for (auto& [spool_id, usage] : data) {
        if (!use_spoolman_spool(spool_id, usage, usage_type))
            return false;
        spool_ids.emplace_back(spool_id);
    }

    update_specific_spool_statistics(spool_ids);

    m_use_undo_buffer = data;
    m_last_usage_type = usage_type;
    return true;
}

bool Spoolman::undo_use_spoolman_spools()
{
    if (m_use_undo_buffer.empty() || m_last_usage_type.empty())
        return false;

    std::vector<unsigned int> spool_ids;

    for (auto& [spool_id, usage] : m_use_undo_buffer) {
        if (!use_spoolman_spool(spool_id, usage * -1, m_last_usage_type))
            return false;
        spool_ids.emplace_back(spool_id);
    }

    update_specific_spool_statistics(spool_ids);

    m_use_undo_buffer.clear();
    m_last_usage_type.clear();
    return true;
}

SpoolmanLaneMap Spoolman::get_spools_by_loaded_lane(bool update)
{
    SpoolmanLaneMap lanes;
    const auto& spools = get_spoolman_spools(update);

    for (const auto& [id, spool] : spools) {
        if (!spool)
            continue;
        spool->loaded_lane_index.reset();
        spool->loaded_lane_label.clear();
    }

    if (!update_moonraker_lane_cache())
        return lanes;

    for (const auto& [spool_id, lane_info] : m_moonraker_lane_cache) {
        auto it = spools.find(spool_id);
        if (it == spools.end())
            continue;

        auto spool = it->second;
        if (!spool)
            continue;

        spool->loaded_lane_index = lane_info.lane_index;
        spool->loaded_lane_label = lane_info.lane_label;

        auto [lane_it, inserted] = lanes.emplace(lane_info.lane_index, spool);
        if (!inserted) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": Multiple spools are assigned to lane "
                                       << lane_info.lane_index << ". Ignoring spool " << spool_id;
        }
    }

    return lanes;
}

const Preset* Spoolman::find_preset_for_spool(int spool_id) const
{
    auto* preset_bundle = GUI::wxGetApp().preset_bundle;
    if (!preset_bundle)
        return nullptr;

    auto& filaments = preset_bundle->filaments;
    for (auto it = filaments.begin(); it != filaments.end(); ++it) {
        const Preset& preset = *it;
        if (!preset.is_user())
            continue;
        if (preset.config.opt_int("spoolman_spool_id", 0) == spool_id)
            return &preset;
    }

    return nullptr;
}

SpoolmanResult Spoolman::create_filament_preset_from_spool(const SpoolmanSpoolShrPtr& spool,
                                                           const Preset*              base_preset,
                                                           bool                       detach,
                                                           bool                       force)
{
    PresetCollection& filaments = wxGetApp().preset_bundle->filaments;
    SpoolmanResult    result;

    if (!base_preset)
        base_preset = &filaments.get_edited_preset();

    std::string filament_preset_name = spool->get_preset_name();

    // Bring over the printer name from the base preset or add one for the current printer
    if (const auto idx = base_preset->name.rfind(" @"); idx != std::string::npos)
        filament_preset_name += base_preset->name.substr(idx);
    else
        filament_preset_name += " @" + wxGetApp().preset_bundle->printers.get_selected_preset_name();

    if (const auto idx = filament_preset_name.rfind(" - Copy"); idx != std::string::npos)
        filament_preset_name.erase(idx);

    Preset* preset = filaments.find_preset(filament_preset_name);

    if (force) {
        if (preset && !preset->is_user())
            result.messages.emplace_back(_u8L("A system preset exists with the same name and cannot be overwritten"));
    } else {
        // Check if a preset with the same name already exists
        if (preset) {
            if (preset->is_user())
                result.messages.emplace_back(_u8L("Preset already exists with the same name"));
            else
                result.messages.emplace_back(_u8L("A system preset exists with the same name and cannot be overwritten"));
        }

        // Check for presets with the same spool ID
        int compatible(0);
        for (const auto item : filaments.get_compatible()) { // count num of visible and invisible
            if (item->is_user() && item->config.opt_int("spoolman_spool_id", 0) == spool->id) {
                compatible++;
                if (compatible > 1)
                    break;
            }
        }
        // if there were any, build the message
        if (compatible) {
            if (compatible > 1)
                result.messages.emplace_back(_u8L("Multiple compatible presets share the same spool ID"));
            else
                result.messages.emplace_back(_u8L("A compatible preset shares the same spool ID"));
        }

        // Check if the material types match between the base preset and the spool
        if (base_preset->config.opt_string("filament_type", 0) != spool->m_filament_ptr->material) {
            result.messages.emplace_back(_u8L("The materials of the base preset and the Spoolman spool do not match"));
        }
    }

    if (result.has_failed())
        return result;

    // get the first preset that is a system preset or base user preset in the inheritance hierarchy
    std::string inherits;
    if (!detach) {
        if (const auto base = filaments.get_preset_base(*base_preset))
            inherits = base->name;
        else // fallback if the above operation fails
            inherits = base_preset->name;
    }

    preset = new Preset(Preset::TYPE_FILAMENT, filament_preset_name);
    preset->config.apply(base_preset->config);
    preset->config.set_key_value("filament_settings_id", new ConfigOptionStrings({filament_preset_name}));
    preset->config.set("inherits", inherits, true);
    spool->apply_to_preset(preset);
    preset->filament_id = get_filament_id(filament_preset_name);
    preset->version     = base_preset->version;
    preset->loaded      = true;
    filaments.save_current_preset(filament_preset_name, detach, false, preset);

    return result;
}

SpoolmanResult Spoolman::update_filament_preset_from_spool(Preset* filament_preset, bool update_from_server, bool only_update_statistics)
{
    DynamicConfig  config;
    SpoolmanResult result;
    if (filament_preset->type != Preset::TYPE_FILAMENT) {
        result.messages.emplace_back("Preset is not a filament preset");
        return result;
    }
    const int&     spool_id = filament_preset->config.opt_int("spoolman_spool_id", 0);
    if (spool_id < 1) {
        result.messages.emplace_back(
            "Preset provided does not have a valid Spoolman spool ID"); // IDs below 1 are not used by spoolman and should be ignored
        return result;
    }
    SpoolmanSpoolShrPtr spool = get_instance()->get_spoolman_spool_by_id(spool_id);
    if (!spool) {
        result.messages.emplace_back("The spool ID does not exist in the local spool cache");
        return result;
    }
    if (update_from_server)
        spool->update_from_server(!only_update_statistics);
    spool->apply_to_preset(filament_preset, only_update_statistics);
    return result;
}

void Spoolman::update_visible_spool_statistics(bool clear_cache)
{
    PresetBundle* preset_bundle = GUI::wxGetApp().preset_bundle;
    PresetCollection& filaments    = preset_bundle->filaments;

    // Clear the cache so that it can be repopulated with the correct info
    if (clear_cache) get_instance()->clear();
    if (is_server_valid()) {
        for (const auto item : filaments.get_compatible()) {
            if (item->is_user() && item->spoolman_enabled()) {
                if (auto res = update_filament_preset_from_spool(item, true, true); res.has_failed())
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": Failed to update spoolman statistics with the following error: "
                                             << res.build_single_line_message() << "Spool ID: " << item->config.opt_int("spoolman_spool_id", 0);
            }
        }
    }
}

void Spoolman::update_specific_spool_statistics(const std::vector<unsigned int>& spool_ids)
{
    PresetBundle* preset_bundle = GUI::wxGetApp().preset_bundle;
    PresetCollection& filaments    = preset_bundle->filaments;

    std::set spool_ids_set(spool_ids.begin(), spool_ids.end());
    // make sure '0' is not a value
    spool_ids_set.erase(0);

    if (is_server_valid()) {
        for (const auto item : filaments.get_compatible()) {
            if (item->is_user() && spool_ids_set.count(item->config.opt_int("spoolman_spool_id", 0)) > 0) {
                if (auto res = update_filament_preset_from_spool(item, true, true); res.has_failed())
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": Failed to update spoolman statistics with the following error: "
                                             << res.build_single_line_message() << "Spool ID: " << item->config.opt_int("spoolman_spool_id", 0);
            }
        }
    }
}


bool Spoolman::is_server_valid()
{
    bool res = false;
    if (!is_enabled())
        return res;

    Http::get(get_spoolman_api_url() + "info").on_complete([&res](std::string, unsigned http_status) {
        if (http_status == 200)
            res = true;
    })
    .timeout_max(MAX_TIMEOUT)
    .perform_sync();
    return res;
}

bool Spoolman::is_enabled() { return GUI::wxGetApp().app_config->get_bool("spoolman", "enabled"); }

//---------------------------------
// SpoolmanVendor
//---------------------------------

void SpoolmanVendor::update_from_server() { update_from_json(Spoolman::get_spoolman_json("vendor/" + std::to_string(id))); }

void SpoolmanVendor::update_from_json(pt::ptree json_data)
{
    id   = json_data.get<int>("id");
    name = get_opt<string>(json_data, "name");
}

void SpoolmanVendor::apply_to_config(Slic3r::DynamicConfig& config) const
{
    config.set_key_value("filament_vendor", new ConfigOptionStrings({name}));
}

//---------------------------------
// SpoolmanFilament
//---------------------------------

void SpoolmanFilament::update_from_server(bool recursive)
{
    const boost::property_tree::ptree& json_data = Spoolman::get_spoolman_json("filament/" + std::to_string(id));
    update_from_json(json_data);
    if (recursive)
        m_vendor_ptr->update_from_json(json_data.get_child("vendor"));
}

void SpoolmanFilament::update_from_json(pt::ptree json_data)
{
    if (int vendor_id = json_data.get<int>("vendor.id"); m_vendor_ptr && m_vendor_ptr->id != vendor_id) {
        if (!m_spoolman->m_vendors.count(vendor_id))
            m_spoolman->m_vendors.emplace(vendor_id, make_shared<SpoolmanVendor>(SpoolmanVendor(json_data.get_child("vendor"))));
        m_vendor_ptr = m_spoolman->m_vendors[vendor_id];
    }
    id             = json_data.get<int>("id");
    name           = get_opt<string>(json_data, "name");
    material       = get_opt<string>(json_data, "material");
    price          = get_opt<float>(json_data, "price");
    density        = get_opt<float>(json_data, "density");
    diameter       = get_opt<float>(json_data, "diameter");
    article_number = get_opt<string>(json_data, "article_number");
    extruder_temp  = get_opt<int>(json_data, "settings_extruder_temp");
    bed_temp       = get_opt<int>(json_data, "settings_bed_temp");
    color          = "#" + get_opt<string>(json_data, "color_hex");
}

void SpoolmanFilament::apply_to_config(Slic3r::DynamicConfig& config) const
{
    config.set_key_value("filament_type", new ConfigOptionStrings({material}));
    config.set_key_value("filament_cost", new ConfigOptionFloats({price}));
    config.set_key_value("filament_density", new ConfigOptionFloats({density}));
    config.set_key_value("filament_diameter", new ConfigOptionFloats({diameter}));
    config.set_key_value("nozzle_temperature_initial_layer", new ConfigOptionInts({extruder_temp + 5}));
    config.set_key_value("nozzle_temperature", new ConfigOptionInts({extruder_temp}));
    config.set_key_value("hot_plate_temp_initial_layer", new ConfigOptionInts({bed_temp + 5}));
    config.set_key_value("hot_plate_temp", new ConfigOptionInts({bed_temp}));
    config.set_key_value("default_filament_colour", new ConfigOptionStrings{color});
    m_vendor_ptr->apply_to_config(config);
}

//---------------------------------
// SpoolmanSpool
//---------------------------------

void SpoolmanSpool::update_from_server(bool recursive)
{
    const boost::property_tree::ptree& json_data = Spoolman::get_spoolman_json("spool/" + std::to_string(id));
    update_from_json(json_data);
    if (recursive) {
        m_filament_ptr->update_from_json(json_data.get_child("filament"));
        getVendor()->update_from_json(json_data.get_child("filament.vendor"));
    }
}

std::string SpoolmanSpool::get_preset_name()
{
    auto name = getVendor()->name;

    if (!m_filament_ptr->name.empty())
        name += " " + m_filament_ptr->name;
    if (!m_filament_ptr->material.empty())
        name += " " + m_filament_ptr->material;

    if (id > 0)
        name += " (Spool #" + std::to_string(id) + ")";

    return remove_special_key(name);
}

void SpoolmanSpool::apply_to_config(Slic3r::DynamicConfig& config) const
{
    config.set_key_value("spoolman_spool_id", new ConfigOptionInts({id}));
    m_filament_ptr->apply_to_config(config);
}

void SpoolmanSpool::apply_to_preset(Preset* preset, bool only_update_statistics) const
{
    auto spoolman_stats = preset->spoolman_statistics;
    spoolman_stats->remaining_weight = remaining_weight;
    spoolman_stats->used_weight = used_weight;
    spoolman_stats->remaining_length = remaining_length;
    spoolman_stats->used_length = used_length;
    spoolman_stats->archived = archived;
    if (only_update_statistics)
        return;
    this->apply_to_config(preset->config);
}

void SpoolmanSpool::update_from_json(pt::ptree json_data)
{
    if (int filament_id = json_data.get<int>("filament.id"); m_filament_ptr && m_filament_ptr->id != filament_id) {
        if (!m_spoolman->m_filaments.count(filament_id))
            m_spoolman->m_filaments.emplace(filament_id, make_shared<SpoolmanFilament>(SpoolmanFilament(json_data.get_child("filament"))));
        m_filament_ptr = m_spoolman->m_filaments.at(filament_id);
    }
    id               = json_data.get<int>("id");
    remaining_weight = get_opt<float>(json_data, "remaining_weight");
    used_weight      = get_opt<float>(json_data, "used_weight");
    remaining_length = get_opt<float>(json_data, "remaining_length");
    used_length      = get_opt<float>(json_data, "used_length");
    archived         = get_opt<bool>(json_data, "archived");

    loaded_lane_index.reset();
    loaded_lane_label.clear();
}

} // namespace Slic3r
