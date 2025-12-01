#!/bin/bash
#
# Upgrade script for PU850 ESP8266 firmware dependencies
# Checks for and updates library and platform versions in sketch.yaml
#
# Usage: ./upgrade.sh [options]
#
# Options:
#   --check    Only check for updates, don't modify files
#   --help     Display this help message
#

set -e

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

SKETCH_YAML="sketch.yaml"
CHECK_ONLY=false

# Temporary files (set early so cleanup trap can use them)
UPDATES_FILE=""
LIBRARY_INDEX_CACHE=""

# Cleanup function
cleanup() {
    rm -f "$UPDATES_FILE" "$LIBRARY_INDEX_CACHE" 2>/dev/null || true
}

# Set trap to cleanup on exit (normal or error)
trap cleanup EXIT

# Colors for output
RED='\033[91;1m'
GREEN='\033[92;1m'
YELLOW='\033[93;1m'
CYAN='\033[96;1m'
RESET='\033[0m'

displayInfo() {
    echo -e "${CYAN}[INFO]${RESET} $1"
}

displaySuccess() {
    echo -e "${GREEN}[SUCCESS]${RESET} $1"
}

displayWarning() {
    echo -e "${YELLOW}[WARNING]${RESET} $1"
}

displayError() {
    echo -e "${RED}[ERROR]${RESET} $1"
}

# Parse arguments
while [ $# -gt 0 ]; do
    case "$1" in
        --check)
            CHECK_ONLY=true
            shift
            ;;
        --help)
            echo ""
            echo -e "${YELLOW}Usage: $(basename "$0") [options]${RESET}"
            echo ""
            echo -e "${YELLOW}Options:${RESET}"
            echo -e "  ${GREEN}--check${RESET}    Only check for updates, don't modify files"
            echo -e "  ${GREEN}--help${RESET}     Display this help message"
            echo ""
            exit 0
            ;;
        *)
            displayWarning "Unknown argument: $1"
            shift
            ;;
    esac
done

# Check required commands
for cmd in curl python3 sed; do
    if ! command -v "$cmd" &> /dev/null; then
        displayError "Required command '$cmd' not found!"
        exit 1
    fi
done

# Check if sketch.yaml exists
if [ ! -f "$SKETCH_YAML" ]; then
    displayError "File '$SKETCH_YAML' not found!"
    exit 1
fi

displayInfo "Checking for library updates..."

# Temporary file for storing update info
UPDATES_FILE=$(mktemp)
LIBRARY_INDEX_CACHE=$(mktemp)
HAS_UPDATES=false

# Download and cache the library index once
displayInfo "Downloading Arduino library index..."
if ! curl -s "https://downloads.arduino.cc/libraries/library_index.json.gz" 2>/dev/null | gunzip > "$LIBRARY_INDEX_CACHE" 2>/dev/null; then
    displayError "Failed to download library index"
    rm -f "$UPDATES_FILE" "$LIBRARY_INDEX_CACHE"
    exit 1
fi

# Function to get latest library version from cached library index
get_latest_library_version() {
    local lib_name="$1"
    python3 -c "
import json, sys
with open('$LIBRARY_INDEX_CACHE', 'r') as f:
    data = json.load(f)
libs = data.get('libraries', [])
latest = None
search_name = '''$lib_name'''
for lib in libs:
    name = lib.get('name', '')
    version = lib.get('version', '')
    if name == search_name:
        if latest is None or version > latest:
            latest = version
print(latest if latest else '')
" 2>/dev/null
}

# Function to get latest ESP8266 platform version
get_latest_platform_version() {
    curl -s "https://arduino.esp8266.com/stable/package_esp8266com_index.json" 2>/dev/null | python3 -c "
import json, sys
data = json.load(sys.stdin)
packages = data.get('packages', [])
for pkg in packages:
    if pkg.get('name') == 'esp8266':
        platforms = pkg.get('platforms', [])
        versions = [p.get('version', '0') for p in platforms]
        if versions:
            print(max(versions))
        break
" 2>/dev/null
}

# Extract current library versions from sketch.yaml
extract_libraries() {
    grep -E '^\s+-\s+[^:]+\([^)]+\)' "$SKETCH_YAML" | grep -v 'platform:' | while read -r line; do
        lib_name=$(echo "$line" | sed -E 's/^\s*-\s*(.+)\s*\([^)]+\)\s*$/\1/' | sed 's/[[:space:]]*$//')
        lib_version=$(echo "$line" | sed -E 's/.*\(([^)]+)\).*/\1/')
        echo "$lib_name|$lib_version"
    done
}

# Extract current platform version from sketch.yaml
extract_platform_version() {
    grep 'platform: esp8266:esp8266' "$SKETCH_YAML" | sed -E 's/.*\(([^)]+)\).*/\1/' | head -1
}

# Check platform version
displayInfo "Checking ESP8266 platform version..."
current_platform=$(extract_platform_version)
latest_platform=$(get_latest_platform_version)

if [ -n "$latest_platform" ] && [ "$current_platform" != "$latest_platform" ]; then
    echo "esp8266:esp8266|$current_platform|$latest_platform" >> "$UPDATES_FILE"
    displayWarning "Platform esp8266:esp8266: $current_platform -> $latest_platform"
    HAS_UPDATES=true
else
    displaySuccess "Platform esp8266:esp8266: $current_platform (up to date)"
fi

# Check if sort -V is available (GNU sort with version comparison)
SORT_V_AVAILABLE=false
if printf '1.2.3\n1.10.0' | sort -V >/dev/null 2>&1; then
    SORT_V_AVAILABLE=true
fi

# Function to compare semantic versions (returns 0 if v1 >= v2, 1 otherwise)
version_gte() {
    local v1="$1"
    local v2="$2"
    
    if [ "$SORT_V_AVAILABLE" = true ]; then
        # Use sort -V if available (GNU sort)
        if [ "$(printf '%s\n%s' "$v1" "$v2" | sort -V | head -1)" = "$v2" ]; then
            return 0  # v1 >= v2
        else
            return 1  # v1 < v2
        fi
    else
        # Fallback: use Python for version comparison
        python3 -c "
from packaging.version import Version
import sys
try:
    v1 = Version('$v1')
    v2 = Version('$v2')
    sys.exit(0 if v1 >= v2 else 1)
except:
    # Simple string comparison fallback
    sys.exit(0 if '$v1' >= '$v2' else 1)
" 2>/dev/null
        return $?
    fi
}

# Check library versions
displayInfo "Checking library versions..."
while IFS='|' read -r lib_name lib_version; do
    latest=$(get_latest_library_version "$lib_name")
    if [ -n "$latest" ] && [ "$lib_version" != "$latest" ]; then
        # Only upgrade, never downgrade (in case current version is from a different source/fork)
        if version_gte "$latest" "$lib_version"; then
            echo "$lib_name|$lib_version|$latest" >> "$UPDATES_FILE"
            displayWarning "Library $lib_name: $lib_version -> $latest"
        else
            displaySuccess "Library $lib_name: $lib_version (newer than registry: $latest)"
        fi
    else
        displaySuccess "Library $lib_name: $lib_version (up to date)"
    fi
done < <(extract_libraries)

# Check if there are updates
if [ -s "$UPDATES_FILE" ]; then
    HAS_UPDATES=true
fi

if [ "$HAS_UPDATES" = true ] && [ "$CHECK_ONLY" = false ]; then
    displayInfo "Applying updates to $SKETCH_YAML..."
    
    # Apply updates
    while IFS='|' read -r name old_version new_version; do
        if [ "$name" = "esp8266:esp8266" ]; then
            # Update platform version
            sed -i "s/platform: esp8266:esp8266 ($old_version)/platform: esp8266:esp8266 ($new_version)/g" "$SKETCH_YAML"
            displaySuccess "Updated platform $name: $old_version -> $new_version"
        else
            # Update library version
            # Handle library names with spaces
            escaped_name=$(echo "$name" | sed 's/[\/&]/\\&/g')
            sed -i "s/- $escaped_name ($old_version)/- $escaped_name ($new_version)/g" "$SKETCH_YAML"
            displaySuccess "Updated library $name: $old_version -> $new_version"
        fi
    done < "$UPDATES_FILE"
    
    displaySuccess "All updates applied to $SKETCH_YAML"
    
    # Output for CI use
    echo ""
    echo "UPDATES_APPLIED=true"
elif [ "$HAS_UPDATES" = true ]; then
    displayWarning "Updates available but not applied (--check mode)"
    echo ""
    echo "UPDATES_AVAILABLE=true"
else
    displaySuccess "All dependencies are up to date!"
    echo ""
    echo "UPDATES_AVAILABLE=false"
fi

# Cleanup is handled by the EXIT trap

exit 0
