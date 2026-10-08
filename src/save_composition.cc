#include "save_composition.h"

namespace fallout {

namespace {

    struct Component {
        std::string name;
        std::string fingerprint;
        char kind;
    };

    std::vector<Component> decode(const std::string& encoded)
    {
        std::vector<Component> components;
        size_t start = 0;
        while (start < encoded.size()) {
            size_t end = encoded.find(';', start);
            if (end == std::string::npos) {
                end = encoded.size();
            }
            std::string item = encoded.substr(start, end - start);
            size_t equals = item.rfind('=');
            size_t colon = item.rfind(':');
            if (equals != std::string::npos && colon != std::string::npos && colon > equals && colon + 1 < item.size()) {
                components.push_back({ item.substr(0, equals), item.substr(equals + 1, colon - equals - 1), item[colon + 1] });
            }
            start = end + 1;
        }
        return components;
    }

    const Component* find(const std::vector<Component>& components, const std::string& name)
    {
        for (const Component& component : components) {
            if (component.name == name) {
                return &component;
            }
        }
        return nullptr;
    }

} // namespace

SaveCompatibility saveCompositionCompare(const std::string& made, const std::string& now, std::vector<SaveCompatibilityChange>* changes)
{
    if (changes != nullptr) {
        changes->clear();
    }
    if (made.empty()) {
        return SaveCompatibility::kUnknown;
    }

    std::vector<Component> was = decode(made);
    std::vector<Component> is = decode(now);
    SaveCompatibility result = SaveCompatibility::kSame;
    auto note = [&](SaveCompatibility level, SaveCompatibilityChange::Kind kind, const std::string& name) {
        if (static_cast<int>(level) > static_cast<int>(result)) {
            result = level;
        }
        if (changes != nullptr) {
            changes->push_back({ kind, name });
        }
    };

    for (const Component& before : was) {
        const Component* after = find(is, before.name);
        if (before.kind == 'G') {
            if (after == nullptr || after->fingerprint != before.fingerprint) {
                note(SaveCompatibility::kUnlikely, SaveCompatibilityChange::Kind::kGameChanged, before.name);
            }
        } else if (before.kind == 'L') {
            if (after == nullptr) {
                note(SaveCompatibility::kUnlikely, SaveCompatibilityChange::Kind::kModRemoved, before.name);
            } else if (after->fingerprint != before.fingerprint) {
                note(SaveCompatibility::kLikely, SaveCompatibilityChange::Kind::kModUpdated, before.name);
            }
        }
    }
    for (const Component& after : is) {
        if (after.kind == 'L' && find(was, after.name) == nullptr) {
            note(SaveCompatibility::kLikely, SaveCompatibilityChange::Kind::kModAdded, after.name);
        }
    }
    return result;
}

} // namespace fallout
