#!/usr/bin/env python3
"""
gen_log_config.py — Generate LogConfig.hpp from log_config.conf.

Reads  config/log_config.conf (key=value + feature whitelist)
       src/utils/LogTypes.hpp   (LogFeature enum values)
Writes src/generated/LogConfig.hpp

Usage:
  python3 scripts/gen_log_config.py           # normal generation
  python3 scripts/gen_log_config.py --restore  # restore config to factory default
"""

import os
import re
import sys


# ===== Factory Default Config (Golden Master) =====
# Embedded in the script so the config file can be fully restored
# even if deleted or corrupted, with zero external dependencies.

FACTORY_CONFIG = """# ===== Log Output Settings =====
# log_dir:      output directory (relative to project root, auto-created)
# max_size_kb:  max log file size in KB (1024 = 1 MB)
log_dir     = logs
max_size_kb = 1024

# ===== Runtime Log Level =====
# Logs below this level are suppressed at runtime.
# Severity order: ERR > WRN > INFO > DBG  (ERR is most severe)
# Allowed values: ERR | WRN | INFO | DBG
# Default: INFO
log_level = INFO

# ===== Enabled Features (whitelist) =====
# List feature names to enable. Unlisted features default to OFF.
# Feature names must match enum values in LogTypes.hpp.
# Valid features are auto-detected from LogTypes.hpp at generation time.
"""


# ===== Paths (relative to project root) =====

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG_PATH = os.path.join(PROJECT_ROOT, "config", "log_config.conf")
LOG_TYPES_PATH = os.path.join(PROJECT_ROOT, "src", "utils", "LogTypes.hpp")
OUTPUT_PATH = os.path.join(PROJECT_ROOT, "src", "generated", "LogConfig.hpp")

VALID_LOG_LEVELS = {"ERR", "WRN", "INFO", "DBG"}
KNOWN_SETTINGS = {"log_dir", "max_size_kb", "log_level"}


# ===== Core Functions =====

def restore_config():
    """Write the factory default config file."""
    os.makedirs(os.path.dirname(CONFIG_PATH), exist_ok=True)
    with open(CONFIG_PATH, "w") as f:
        f.write(FACTORY_CONFIG)
    print("[gen_log_config] Restored config/log_config.conf to factory default.")


def parse_log_features():
    """Parse LogTypes.hpp and return the list of LogFeature enum value names."""
    if not os.path.exists(LOG_TYPES_PATH):
        die("LogTypes.hpp not found at: {}".format(LOG_TYPES_PATH))

    with open(LOG_TYPES_PATH, "r") as f:
        content = f.read()

    # Match: enum class LogFeature : uint8_t { ... };
    # Extract identifier names before '=' or ','
    match = re.search(r"enum\s+class\s+LogFeature\s*.*?\{(.*?)\};", content, re.DOTALL)
    if not match:
        die("Could not find 'enum class LogFeature' in LogTypes.hpp")

    body = match.group(1)
    features = []
    for line in body.split("\n"):
        line = line.strip()
        if not line or line.startswith("//") or line.startswith("/*"):
            continue
        # Match: AICHAT = 0,  or  AICHAT    = 0,
        m = re.match(r"(\w+)\s*=", line)
        if m:
            features.append(m.group(1))

    return features


def parse_config():
    """
    Parse config/log_config.conf.
    Returns (settings_dict, whitelist_features, errors).
    settings_dict: { "log_dir": "logs", "max_size_kb": "1024", "log_level": "INFO" }
    whitelist_features: ["AICHAT", "MIFAMILY"]
    errors: [(line_num, message), ...]
    """
    settings = {}
    whitelist = []
    errors = []

    if not os.path.exists(CONFIG_PATH):
        return settings, whitelist, [(0, "config/log_config.conf does not exist")]

    with open(CONFIG_PATH, "r") as f:
        lines = f.readlines()

    for i, raw_line in enumerate(lines):
        line_num = i + 1
        line = raw_line.strip()

        # Skip empty lines and comments
        if not line or line.startswith("#"):
            continue

        # Try key = value
        eq_match = re.match(r"^(\w+)\s*=\s*(.*)$", line)
        if eq_match:
            key = eq_match.group(1)
            value = eq_match.group(2).strip()

            if key in KNOWN_SETTINGS:
                settings[key] = value
                # Validate
                err = validate_setting(line_num, key, value)
                if err:
                    errors.append((line_num, err))
            else:
                errors.append((
                    line_num,
                    'Unknown setting "{}". Allowed settings: {}'.format(
                        key, ", ".join(sorted(KNOWN_SETTINGS))
                    ),
                ))
            continue

        # Otherwise treat as feature name (whitelist entry)
        name_match = re.match(r"^(\w+)$", line)
        if name_match:
            whitelist.append(name_match.group(1))
        else:
            errors.append((
                line_num,
                'Cannot parse line: "{}". Use KEY = VALUE or a feature name.'.format(line),
            ))

    return settings, whitelist, errors


def validate_setting(line_num, key, value):
    """Validate a single setting. Returns error string or None."""
    if key == "log_dir":
        if not value:
            return 'log_dir is empty. Must be a non-empty path, e.g. "logs"'
        return None

    if key == "max_size_kb":
        try:
            v = int(value)
            if v <= 0:
                return 'max_size_kb = {} is invalid. Must be a positive integer, e.g. 1024'.format(value)
        except ValueError:
            return 'max_size_kb = "{}" is invalid. Must be a positive integer, e.g. 1024'.format(value)
        return None

    if key == "log_level":
        if value not in VALID_LOG_LEVELS:
            return 'log_level = "{}" is invalid. Allowed values: {}'.format(
                value, ", ".join(sorted(VALID_LOG_LEVELS))
            )
        return None

    return None


def validate_features(whitelist, valid_features):
    """Validate that whitelisted features exist in LogFeature enum. Returns list of errors."""
    errors = []
    for feat in whitelist:
        if feat not in valid_features:
            errors.append(
                'Feature "{}" not found in LogTypes.hpp. Valid features: {}'.format(
                    feat, ", ".join(valid_features)
                )
            )
    return errors


def generate_header(settings, whitelist, valid_features):
    """Generate the content of LogConfig.hpp."""

    log_dir = settings.get("log_dir", "logs")
    max_size_kb = settings.get("max_size_kb", "1024")
    log_level = settings.get("log_level", "INFO")

    lines = []
    lines.append("// Auto-generated by scripts/gen_log_config.py — DO NOT EDIT.")
    lines.append("")
    lines.append("#pragma once")
    lines.append('#include "utils/LogTypes.hpp"')
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append("namespace utils")
    lines.append("{")
    lines.append("")
    lines.append("// ===== Log Output Settings =====")
    lines.append("")
    lines.append('constexpr const char *kLogDir   = "{}";'.format(log_dir))
    lines.append("constexpr uint32_t   kMaxSizeKb = {};".format(max_size_kb))
    lines.append("")
    lines.append("// ===== Runtime Minimum Level =====")
    lines.append("// Lower enum value = more severe.")
    lines.append("//   ERR(0) > WRN(1) > INFO(2) > DBG(3)")
    lines.append("")
    lines.append("#ifdef FLOWHUB_TEST_BUILD")
    lines.append("constexpr LogLevel kRuntimeMinLevel = LogLevel::DBG;")
    lines.append("#else")
    lines.append("constexpr LogLevel kRuntimeMinLevel = LogLevel::{};".format(log_level))
    lines.append("#endif")
    lines.append("")
    lines.append("// ===== Feature Switches (whitelist-based) =====")
    lines.append("")
    lines.append("#ifdef FLOWHUB_TEST_BUILD")
    for feat in valid_features:
        lines.append("constexpr bool kLogFeat{}Enabled = true;".format(feat))
    lines.append("#else")
    for feat in valid_features:
        enabled = "true" if feat in whitelist else "false"
        lines.append("constexpr bool kLogFeat{}Enabled = {};".format(feat, enabled))
    lines.append("#endif")
    lines.append("")
    lines.append("// ===== Feature Compile-time Query =====")
    lines.append("")
    lines.append("constexpr bool isLogFeatEnabled(LogFeature f) noexcept")
    lines.append("{")
    lines.append("  switch (f)")
    lines.append("  {")
    for feat in valid_features:
        lines.append("  case LogFeature::{}:  return kLogFeat{}Enabled;".format(feat, feat))
    lines.append("  }")
    lines.append("  return false;")
    lines.append("}")
    lines.append("")
    lines.append("} // namespace utils")
    lines.append("")

    return "\n".join(lines)


def die(msg):
    """Print error and exit."""
    print("[gen_log_config] ERROR: {}".format(msg), file=sys.stderr)
    sys.exit(1)


# ===== Main =====

def main():
    # Handle --restore
    if "--restore" in sys.argv:
        restore_config()
        print("[gen_log_config] Config restored. Now generating...")

    # Parse config
    settings, whitelist, errors = parse_config()

    # Config file missing: create and continue
    if errors and errors[0][0] == 0:
        print("[gen_log_config] config/log_config.conf not found. Creating factory default.")
        restore_config()
        # Re-parse the newly created config
        settings, whitelist, errors = parse_config()

    # Config parse errors: report, restore, exit
    if errors:
        print("[gen_log_config] Errors found in config/log_config.conf:", file=sys.stderr)
        for line_num, msg in errors:
            if line_num > 0:
                print("  line {}: {}".format(line_num, msg), file=sys.stderr)
            else:
                print("  {}".format(msg), file=sys.stderr)
        print("", file=sys.stderr)
        print("[gen_log_config] Restoring config/log_config.conf to factory default.", file=sys.stderr)
        restore_config()
        die("Please fix the errors above and re-run. Config has been reset to default.")

    # Parse LogFeature enum
    valid_features = parse_log_features()

    # Validate whitelist features
    feat_errors = validate_features(whitelist, valid_features)
    if feat_errors:
        print("[gen_log_config] Feature errors in config/log_config.conf:", file=sys.stderr)
        for msg in feat_errors:
            print("  {}".format(msg), file=sys.stderr)
        print("", file=sys.stderr)
        print("[gen_log_config] Restoring config/log_config.conf to factory default.", file=sys.stderr)
        restore_config()
        die("Please fix the errors above and re-run. Config has been reset to default.")

    # Generate header
    header_content = generate_header(settings, whitelist, valid_features)
    os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
    with open(OUTPUT_PATH, "w") as f:
        f.write(header_content)

    print("[gen_log_config] Generated: {}".format(
        os.path.relpath(OUTPUT_PATH, PROJECT_ROOT)))
    print("[gen_log_config]   log_level = {}".format(settings.get("log_level", "INFO")))
    print("[gen_log_config]   features  = {}".format(
        ", ".join(whitelist) if whitelist else "(none)"))
    print("[gen_log_config] Done.")


if __name__ == "__main__":
    main()
