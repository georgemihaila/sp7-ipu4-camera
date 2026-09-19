#!/usr/bin/env bash
# Read-only SELinux evidence collection. It never creates policy or changes mode.
set -euo pipefail

printf 'getenforce='; getenforce
printf '%s\n' 'recent Howdy/PAM AVC denials:'
ausearch -m avc -ts recent 2>/dev/null | grep -Ei 'howdy|pam_howdy|sp7-camera' || \
	printf '%s\n' 'none observed or ausearch unavailable'
