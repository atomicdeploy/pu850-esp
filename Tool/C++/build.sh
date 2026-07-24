#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
architecture="all"
configuration="Release"
clean=0

usage() {
	cat <<'EOF'
Cross-build the native Windows firmware-transfer client with MinGW-w64.

Usage: ./build.sh [--architecture x64|x86|all] [--configuration Release|Debug] [--clean]

The resulting self-contained Windows executables are written below dist/.
Runtime loopback tests are performed by the native Windows build/CI job.
EOF
}

while (($#)); do
	case "$1" in
		--architecture)
			[[ $# -ge 2 ]] || { echo "missing --architecture value" >&2; exit 2; }
			architecture="$2"
			shift 2
			;;
		--configuration)
			[[ $# -ge 2 ]] || { echo "missing --configuration value" >&2; exit 2; }
			configuration="$2"
			shift 2
			;;
		--clean)
			clean=1
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "unknown option: $1" >&2
			usage >&2
			exit 2
			;;
	esac
done

case "$architecture" in
	x64|x86|all) ;;
	*) echo "architecture must be x64, x86, or all" >&2; exit 2 ;;
esac

case "$configuration" in
	Release|Debug) ;;
	*) echo "configuration must be Release or Debug" >&2; exit 2 ;;
esac

command -v cmake >/dev/null || { echo "cmake is required" >&2; exit 3; }
command -v ninja >/dev/null || { echo "ninja is required" >&2; exit 3; }

build_one() {
	local name="$1"
	local compiler_prefix toolchain
	case "$name" in
		x64)
			compiler_prefix="x86_64-w64-mingw32"
			toolchain="${script_dir}/cmake/mingw-w64-x64.cmake"
			;;
		x86)
			compiler_prefix="i686-w64-mingw32"
			toolchain="${script_dir}/cmake/mingw-w64-x86.cmake"
			;;
	esac

	command -v "${compiler_prefix}-g++" >/dev/null || {
		echo "${compiler_prefix}-g++ is required for the ${name} build" >&2
		exit 3
	}

	local build_dir="${script_dir}/build/mingw-${name}"
	local dist_dir="${script_dir}/dist/mingw-${name}"
	if ((clean)); then
		case "$build_dir" in
			"${script_dir}/build/"*) rm -rf -- "$build_dir" ;;
			*) echo "refusing to clean unexpected path: $build_dir" >&2; exit 9 ;;
		esac
	fi

	cmake \
		-S "$script_dir" \
		-B "$build_dir" \
		-G Ninja \
		-DCMAKE_TOOLCHAIN_FILE="$toolchain" \
		-DCMAKE_BUILD_TYPE="$configuration" \
		-DBUILD_TESTING=OFF
	cmake --build "$build_dir" --config "$configuration" --parallel

	mkdir -p -- "$dist_dir"
	cp -- "${build_dir}/FirmwareTransferCpp.exe" "${dist_dir}/FirmwareTransferCpp.exe"
	sha256sum "${dist_dir}/FirmwareTransferCpp.exe"
}

if [[ "$architecture" == "all" || "$architecture" == "x64" ]]; then
	build_one x64
fi
if [[ "$architecture" == "all" || "$architecture" == "x86" ]]; then
	build_one x86
fi
