#pragma once

#include <QString>

namespace PoldenDefaults {
// Seeds only empty profile/scene directories. Existing configuration is never replaced.
bool install(const QString &resources, const QString &profiles, const QString &scenes);
} // namespace PoldenDefaults
