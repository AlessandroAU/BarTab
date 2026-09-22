#pragma once
#include "core/preferences.hpp"
#include <optional>

namespace usage {
// Owns the rollback snapshot; applying values and saving to disk remain host responsibilities.
class SettingsEdit {
  public:
    void begin(const Preferences& current) {
        if (!original_)
            original_ = current;
    }
    Preferences preview(Preferences value) const {
        value.normalize();
        return value;
    }
    void commit() {
        original_.reset();
    }
    std::optional<Preferences> cancel() {
        auto original = original_;
        original_.reset();
        return original;
    }

  private:
    std::optional<Preferences> original_;
};
} // namespace usage
