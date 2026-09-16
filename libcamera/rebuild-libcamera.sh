#!/usr/bin/env bash
# Rebuild Fedora's libcamera package against the installed/current distro stack.
#
# Fedora 0.7.1 already contains the required Simple-pipeline support for this
# machine: the IPU4P media device presents itself as "intel-ipu6" and the
# entry enables SoftISP.  The old local patch added a nonexistent
# "intel-ipu4-isys" identity and relaxed the format check unnecessarily.
# This helper therefore validates the known source/spec layout and builds the
# unmodified Fedora source.  Unknown layouts fail closed.
set -Eeuo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
readonly ROOT

usage() {
	cat <<'EOF'
usage:
  rebuild-libcamera.sh --check SOURCE_TREE [SPEC]
  rebuild-libcamera.sh [--install]

--check validates an unpacked Fedora libcamera source tree and, when supplied,
its RPM spec. It performs no downloads or writes.

The build mode downloads the current Fedora libcamera SRPM, validates its
source/spec layout, runs dnf builddep, and builds into a fresh .rpmbuild
subdirectory. It does not install packages unless --install is supplied.
RPMBUILD_DIR may name the parent directory for build outputs.
EOF
}

die() {
	printf 'error: %s\n' "$*" >&2
	exit 2
}

need_command() {
	command -v "$1" >/dev/null 2>&1 ||
		die "required command '$1' is missing; install the Fedora RPM build prerequisites"
}

source_version() {
	local meson_file=$1
	awk '
		/^[[:space:]]*version[[:space:]]*:/ {
			value = $0
			sub(/^[^'"'"']*'"'"'/, "", value)
			sub(/'"'"'.*$/, "", value)
			print value
			exit
		}
	' "$meson_file"
}

check_spec_layout() {
	local spec=$1
	[ -f "$spec" ] || die "RPM spec not found: $spec"

	grep -Eq '^Name:[[:space:]]*libcamera[[:space:]]*$' "$spec" ||
		die "unsupported RPM spec: expected Name: libcamera in $spec"
	grep -Eq '^Version:[[:space:]]*[0-9]+\.[0-9]+\.[0-9]+' "$spec" ||
		die "unsupported RPM spec: no numeric libcamera Version: in $spec"
	grep -Eq '^Release:[[:space:]]*[^#]+' "$spec" ||
		die "unsupported RPM spec: no Release: tag in $spec"
	grep -Eq '^%autosetup([[:space:]]|$)' "$spec" ||
		die "unsupported RPM spec: expected %autosetup in $spec"

	printf 'PASS: recognized Fedora libcamera RPM spec layout: %s\n' "$spec"
}

check_source_layout() {
	local source_dir=$1
	local expected_version=${2:-}
	local simple="$source_dir/src/libcamera/pipeline/simple/simple.cpp"
	local bayer="$source_dir/src/libcamera/bayer_format.cpp"
	local v4l2="$source_dir/src/libcamera/v4l2_pixelformat.cpp"
	local meson="$source_dir/meson.build"
	local actual_version

	[ -d "$source_dir" ] || die "libcamera source directory not found: $source_dir"
	for file in "$simple" "$bayer" "$v4l2" "$meson"; do
		[ -f "$file" ] ||
			die "unsupported libcamera source layout: missing $file"
	done

	actual_version=$(source_version "$meson")
	[ -n "$actual_version" ] ||
		die "unsupported libcamera source layout: could not read project version from $meson"
	if [ -n "$expected_version" ] && [ "$actual_version" != "$expected_version" ]; then
		die "source/spec version mismatch: source=$actual_version expected=$expected_version"
	fi

	# The IPU4P driver deliberately registers the current compatibility identity
	# used by libcamera. Keep this exact invariant visible and fail closed if it
	# moves or is removed upstream.
	grep -Eq '^[[:space:]]*\{[[:space:]]*"intel-ipu6",[[:space:]]*\{\},[[:space:]]*true[[:space:]]*\},' "$simple" ||
		die "unsupported libcamera layout: Simple pipeline lacks the intel-ipu6 + SoftISP entry"

	# The IPU4 processed BE/SOC nodes expose unpacked 10-bit Bayer (BG10,
	# V4L2_PIX_FMT_SBGGR10, 16 bits per sample). pBAA is packed RAW10 for the
	# direct-MIPI nodes and is not a substitute for this processed path.
	grep -Fq 'formats::SBGGR10, V4L2PixelFormat(V4L2_PIX_FMT_SBGGR10)' "$bayer" ||
		die "unsupported libcamera layout: unpacked BG10 Bayer mapping is missing"
	grep -Fq 'V4L2PixelFormat(V4L2_PIX_FMT_SBGGR10P)' "$bayer" ||
		die "unsupported libcamera layout: packed pBAA Bayer mapping is missing"
	grep -Fq 'V4L2_PIX_FMT_SBGGR10P' "$v4l2" ||
		die "unsupported libcamera layout: packed 10-bit Bayer description is missing"

	# No local fourcc relaxation is required. Keep the strict size/fourcc check
	# as a guard against accepting an unrelated or corrupted negotiated format.
	grep -Fq 'if (captureFormat.fourcc != videoFormat ||' "$simple" ||
		die "unsupported libcamera layout: capture fourcc/size guard is not in the known form"
	grep -Fq 'captureFormat.size != pipeConfig->captureSize' "$simple" ||
		die "unsupported libcamera layout: capture size guard is missing"

	printf 'PASS: libcamera %s already supports the IPU4P through intel-ipu6 + SoftISP; no local patch is necessary\n' "$actual_version"
	printf 'PASS: processed format is unpacked BG10 (V4L2_PIX_FMT_SBGGR10); direct format pBAA remains separate\n'
}

check_tree_from_archive() {
	local archive=$1
	local expected_version=$2
	local extract_dir=$3
	local source_dir

	mkdir -p "$extract_dir"
	tar -xf "$archive" -C "$extract_dir"
	source_dir=$(find "$extract_dir" -mindepth 1 -maxdepth 1 -type d -name 'libcamera-*' -print -quit)
	[ -n "$source_dir" ] || die "SRPM source archive did not contain a libcamera-* tree"
	check_source_layout "$source_dir" "$expected_version"
	printf '%s\n' "$source_dir"
}

check_mode() {
	local source_dir=${1:-}
	local spec=${2:-}
	[ -n "$source_dir" ] || { usage >&2; exit 2; }
	check_source_layout "$source_dir"
	if [ -n "$spec" ]; then
		check_spec_layout "$spec"
	fi
}

build_mode() {
	local install_packages=0
	if [ "${1:-}" = '--install' ]; then
		install_packages=1
	elif [ "${1:-}" != '' ]; then
		usage >&2
		exit 2
	fi

	for command in dnf rpm rpmbuild tar find awk grep; do
		need_command "$command"
	done

	local parent=${RPMBUILD_DIR:-"$ROOT/.rpmbuild"}
	mkdir -p "$parent"
	local download_dir
	download_dir=$(mktemp -d "$parent/download.XXXXXX")
	local build_dir
	build_dir=$(mktemp -d "$parent/build.XXXXXX")

	printf 'Downloading current Fedora libcamera SRPM into %s\n' "$download_dir"
	dnf download --source --destdir "$download_dir" libcamera

	local srpm_count
	srpm_count=$(find "$download_dir" -maxdepth 1 -type f -name 'libcamera-*.src.rpm' | wc -l)
	[ "$srpm_count" -eq 1 ] ||
		die "expected exactly one libcamera SRPM in $download_dir, found $srpm_count"
	local srpm
	srpm=$(find "$download_dir" -maxdepth 1 -type f -name 'libcamera-*.src.rpm' -print -quit)

	local package_version package_release
	package_version=$(rpm -qp --qf '%{VERSION}' "$srpm")
	package_release=$(rpm -qp --qf '%{RELEASE}' "$srpm")
	printf 'Using libcamera-%s-%s source package\n' "$package_version" "$package_release"

	local topdir="$build_dir/topdir"
	mkdir -p "$topdir"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
	rpm --define "_topdir $topdir" -i "$srpm"

	local spec
	spec=$(find "$topdir/SPECS" -maxdepth 1 -type f -name 'libcamera*.spec' -print -quit)
	[ -n "$spec" ] || die "SRPM did not install a libcamera*.spec under $topdir/SPECS"
	check_spec_layout "$spec"

	local archive
	archive=$(find "$topdir/SOURCES" -maxdepth 1 -type f -name 'libcamera-v*.tar.*' -print -quit)
	[ -n "$archive" ] || die "SRPM did not provide a versioned libcamera source archive"
	check_tree_from_archive "$archive" "$package_version" "$build_dir/source-check" >/dev/null

	printf 'Running Fedora build dependencies and rpmbuild\n'
	sudo dnf -y builddep "$srpm"
	rpmbuild --define "_topdir $topdir" --define 'dist .ipu4' -ba "$spec"

	printf 'PASS: build completed; packages are under %s/RPMS\n' "$topdir"
	if [ "$install_packages" -eq 1 ]; then
		local rpms=()
		while IFS= read -r -d '' rpm_file; do
			rpms+=("$rpm_file")
		done < <(find "$topdir/RPMS" -type f -name 'libcamera-*.rpm' -print0)
		[ "${#rpms[@]}" -gt 0 ] || die "build produced no libcamera RPMs"
		sudo dnf -y install "${rpms[@]}"
		printf 'PASS: installed only the rebuilt libcamera RPM set\n'
	fi

	printf 'Build directory retained for inspection: %s\n' "$build_dir"
}

case "${1:-}" in
	--help|-h)
		usage
		;;
	--check)
		shift
	check_mode "${1:-}" "${2:-}"
		;;
	--install)
		build_mode --install
		;;
'')
		build_mode
		;;
*)
		usage >&2
		exit 2
	;;
esac
