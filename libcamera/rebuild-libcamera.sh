#!/bin/bash
# Rebuild Fedora's libcamera package with the SP7 IPU4 patch.
#
# Keeping the distro version (0.5.2) means pipewire-plugin-libcamera
# stays ABI-compatible and needs no rebuild. Fedora's package already
# carries the software-ISP backports the IPU4 needs; the one patch here
# adds the intel-ipu4-isys simple-pipeline entry and tolerates the fw's
# fourcc adjustment (backport of ruslanbay/ipu4-next libcamera hacks).
#
# NB: this is an external, distro-specific helper, not a repository package
# build. A distro libcamera update will overwrite this build.
set -e
cd "$(dirname "$0")"
RPMBUILD_DIR=${RPMBUILD_DIR:-"$PWD/.rpmbuild"}
mkdir -p "$RPMBUILD_DIR"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

sudo dnf -y install rpm-build rpmdevtools
dnf download --source --destdir "$RPMBUILD_DIR" libcamera
sudo dnf -y builddep "$RPMBUILD_DIR"/libcamera-*.src.rpm
rpm --define "_topdir $RPMBUILD_DIR" -i "$RPMBUILD_DIR"/libcamera-*.src.rpm

cp 2001-pipeline-simple-Intel-IPU4-support.patch "$RPMBUILD_DIR/SOURCES/"
sed -i -e 's/^\(Release: *[0-9]*\)%{?dist}/\1.ipu4.1%{?dist}/' \
    -e '/^Patch16:/a Patch17: 2001-pipeline-simple-Intel-IPU4-support.patch' \
    "$RPMBUILD_DIR/SPECS/libcamera.spec"

rpmbuild --define "_topdir $RPMBUILD_DIR" -ba "$RPMBUILD_DIR/SPECS/libcamera.spec"

sudo dnf -y upgrade \
    "$RPMBUILD_DIR"/RPMS/x86_64/libcamera-0*.rpm \
    "$RPMBUILD_DIR"/RPMS/x86_64/libcamera-ipa-0*.rpm \
    "$RPMBUILD_DIR"/RPMS/x86_64/libcamera-tools-0*.rpm
