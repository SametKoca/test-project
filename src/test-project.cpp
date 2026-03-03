//============================================================================
// Name        : XmlGenerator.cpp
// Author      : Samet Koca
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================

// XmlGenerator.cpp
// C++14 rewrite of the given Python script using only Boost.PropertyTree for:
// - XML parsing/writing
// - JSON recipe parsing
//
// Run:
// ./XmlGenerator -i old.xml -r recipe.json -o out.xml

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace pt = boost::property_tree;

struct Args {
    std::string inputXml;
    std::string recipeJson;
    std::string outputXml;
};

static Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];

        auto need = [&](std::string& out) {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for argument: " + k);
            }
            out = argv[++i];
        };

        if (k == "-i" || k == "--input") {
            need(a.inputXml);
        } else if (k == "-r" || k == "--recipe") {
            need(a.recipeJson);
        } else if (k == "-o" || k == "--output") {
            need(a.outputXml);
        } else if (k == "-h" || k == "--help") {
            std::cout
                << "Usage: xmlgen -i old.xml -r recipe.json -o out.xml\n"
                << "Options:\n"
                << "  -i, --input   Old XML file\n"
                << "  -r, --recipe  Recipe JSON file\n"
                << "  -o, --output  Output XML file\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + k);
        }
    }

    if (a.inputXml.empty() || a.recipeJson.empty() || a.outputXml.empty()) {
        throw std::runtime_error("Required args: -i <old.xml> -r <recipe.json> -o <out.xml>");
    }

    return a;
}

static std::vector<std::string> splitp(const std::string& p) {
    std::string s = p;

    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    while (!s.empty() && s.back() == '/') s.pop_back();

    std::vector<std::string> out;
    std::string cur;

    for (char ch : s) {
        if (ch == '/') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) out.push_back(cur);

    return out;
}

static pt::ptree* find_child(pt::ptree& parent, const std::string& tag) {
    for (auto& kv : parent) {
        if (kv.first == tag) return &kv.second;
    }
    return nullptr;
}

static const pt::ptree* find_child_const(const pt::ptree& parent, const std::string& tag) {
    for (const auto& kv : parent) {
        if (kv.first == tag) return &kv.second;
    }
    return nullptr;
}

static pt::ptree* get_elem(pt::ptree& root, const std::string& rootTag, const std::string& path) {
    const auto parts = splitp(path);
    if (parts.empty() || parts.front() != rootTag) return nullptr;

    pt::ptree* cur = &root;
    for (size_t i = 1; i < parts.size(); ++i) {
        cur = find_child(*cur, parts[i]);
        if (!cur) return nullptr;
    }
    return cur;
}

static pt::ptree& ensure_elem(pt::ptree& root, const std::string& rootTag, const std::string& path) {
    const auto parts = splitp(path);
    if (parts.empty() || parts.front() != rootTag) {
        throw std::runtime_error("Bad path root: " + path);
    }

    pt::ptree* cur = &root;
    for (size_t i = 1; i < parts.size(); ++i) {
        pt::ptree* nxt = find_child(*cur, parts[i]);
        if (!nxt) {
            cur->push_back(std::make_pair(parts[i], pt::ptree{}));
            nxt = &cur->back().second;
        }
        cur = nxt;
    }
    return *cur;
}

static void apply_entry(pt::ptree& elem, const pt::ptree& entry) {
    // Clear all attributes by removing <xmlattr>.
    elem.erase("<xmlattr>");
    pt::ptree& attrs = elem.put_child("<xmlattr>", pt::ptree{});

    auto put_attr_if = [&](const char* k) {
        if (entry.get_optional<std::string>(k)) {
            attrs.put(k, entry.get<std::string>(k));
        }
    };

    put_attr_if("Name");
    put_attr_if("Type");
    put_attr_if("Desc");
    put_attr_if("Min");
    put_attr_if("Max");

    // Option -> Options, or Options directly.
    if (entry.get_optional<std::string>("Option")) {
        attrs.put("Options", entry.get<std::string>("Option"));
    } else if (entry.get_optional<std::string>("Options")) {
        attrs.put("Options", entry.get<std::string>("Options"));
    }

    // Text value.
    if (entry.get_optional<std::string>("value")) {
        elem.data() = entry.get<std::string>("value");
    } else {
        elem.data().clear();
    }
}

static void remove_path(pt::ptree& root, const std::string& rootTag, const std::string& path) {
    const auto parts = splitp(path);
    if (parts.size() < 2 || parts.front() != rootTag) return;

    // Build parent path.
    std::string parentPath;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        if (i) parentPath.push_back('/');
        parentPath += parts[i];
    }

    pt::ptree* parent = get_elem(root, rootTag, parentPath);
    if (!parent) return;

    const std::string& childTag = parts.back();
    for (auto it = parent->begin(); it != parent->end(); ++it) {
        if (it->first == childTag) {
            parent->erase(it);
            return;
        }
    }
}

static bool is_whitespace_only(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
    return s.empty();
}

static bool is_empty_node(const pt::ptree& node) {
    if (!node.empty()) return false;
    return is_whitespace_only(node.data());
}

static void prune_empty(pt::ptree& root) {
    bool changed = true;
    while (changed) {
        changed = false;

        std::function<void(pt::ptree&)> rec = [&](pt::ptree& n) {
            for (auto it = n.begin(); it != n.end(); ) {
                rec(it->second);

                // Remove element nodes with no children and empty text.
                if (it->first != "<xmlattr>" && is_empty_node(it->second)) {
                    it = n.erase(it);
                    changed = true;
                } else {
                    ++it;
                }
            }
        };

        rec(root);
    }
}

static void clone_element_content(const pt::ptree& src, pt::ptree& dst) {
    // Copy attributes.
    dst.erase("<xmlattr>");
    if (const pt::ptree* a = find_child_const(src, "<xmlattr>")) {
        dst.put_child("<xmlattr>", *a);
    }

    // Copy text.
    dst.data() = src.data();
}

static void move_path_with_value(pt::ptree& root,
                                 const std::string& rootTag,
                                 const std::string& old_path,
                                 const std::string& new_path) {
    pt::ptree* oldElem = get_elem(root, rootTag, old_path);
    if (!oldElem) return;

    pt::ptree& newElem = ensure_elem(root, rootTag, new_path);
    clone_element_content(*oldElem, newElem);

    remove_path(root, rootTag, old_path);
}

static void set_value_by_path(pt::ptree& root,
                              const std::string& rootTag,
                              const std::string& path,
                              const std::string& value) {
    // Do not create if missing, only update existing.
    pt::ptree* elem = get_elem(root, rootTag, path);
    if (!elem) return;
    elem->data() = value;
}

int main(int argc, char** argv) {
    try {
        const Args args = parse_args(argc, argv);

        // Read XML into a document tree that contains the root tag as first child.
        pt::ptree doc;
        pt::read_xml(args.inputXml, doc, pt::xml_parser::trim_whitespace);

        if (doc.empty()) {
            throw std::runtime_error("XML document is empty");
        }

        const std::string rootTag = doc.begin()->first;
        pt::ptree& root = doc.begin()->second;

        // Read recipe JSON using property_tree.
        pt::ptree recipe;
        pt::read_json(args.recipeJson, recipe);

        // In property_tree, a JSON array is represented as children with empty keys.

        // 1) MOVE (old_value -> new_value).
        for (const auto& item : recipe) {
            const pt::ptree& op = item.second;

            auto oldOpt = op.get_child_optional("old_value");
            auto newOpt = op.get_child_optional("new_value");
            if (!oldOpt || !newOpt) continue;

            if (!oldOpt->get_optional<std::string>("path") || !newOpt->get_optional<std::string>("path")) continue;

            const std::string oldPath = oldOpt->get<std::string>("path");
            const std::string newPath = newOpt->get<std::string>("path");
            move_path_with_value(root, rootTag, oldPath, newPath);
        }
        prune_empty(root);

        // 2) REMOVE.
        for (const auto& item : recipe) {
            const pt::ptree& op = item.second;

            auto remOpt = op.get_child_optional("remove");
            if (!remOpt) continue;

            if (!remOpt->get_optional<std::string>("path")) continue;
            remove_path(root, rootTag, remOpt->get<std::string>("path"));
        }
        prune_empty(root);

        // 3) ADD.
        for (const auto& item : recipe) {
            const pt::ptree& op = item.second;

            auto addOpt = op.get_child_optional("add");
            if (!addOpt) continue;

            if (!addOpt->get_optional<std::string>("path")) continue;
            pt::ptree& elem = ensure_elem(root, rootTag, addOpt->get<std::string>("path"));
            apply_entry(elem, *addOpt);
        }

        // 4) UPDATE (old/new).
        for (const auto& item : recipe) {
            const pt::ptree& op = item.second;

            auto oldOpt = op.get_child_optional("old");
            auto newOpt = op.get_child_optional("new");
            if (!oldOpt || !newOpt) continue;

            if (!newOpt->get_optional<std::string>("path")) continue;
            pt::ptree& elem = ensure_elem(root, rootTag, newOpt->get<std::string>("path"));
            apply_entry(elem, *newOpt);
        }

        // 4.5) VALUE_UPDATE (only text/value, only if exists).
        for (const auto& item : recipe) {
            const pt::ptree& op = item.second;

            auto vuOpt = op.get_child_optional("value_update");
            if (!vuOpt) continue;

            if (!vuOpt->get_optional<std::string>("path") || !vuOpt->get_optional<std::string>("value")) continue;

            set_value_by_path(root, rootTag, vuOpt->get<std::string>("path"), vuOpt->get<std::string>("value"));
        }

        prune_empty(root);

        // Write XML to memory first, then size-check, then file.
        std::ostringstream oss;

        // Indent with tab, 1 per level.
        pt::xml_parser::xml_writer_settings<std::string> settings('\t', 1);
        pt::write_xml(oss, doc, settings);

        const std::string xmlText = oss.str();

        if (xmlText.size() < 20) {
            throw std::runtime_error("XML output is unexpectedly small, aborting write");
        }

        std::ofstream out(args.outputXml, std::ios::binary);
        if (!out) {
            throw std::runtime_error("Cannot open output file: " + args.outputXml);
        }

        out.write(xmlText.data(), static_cast<std::streamsize>(xmlText.size()));
        out.flush();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
