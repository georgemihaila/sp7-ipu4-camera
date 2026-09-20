#!/usr/bin/env bash
# Boot-time, non-mutating pre-login readiness check. Failure means fallback.
set -euo pipefail

HELPER=/usr/local/libexec/sp7-camera-auth-capture
PYTHON=/usr/local/libexec/sp7-camera-howdy/python/bin/python
CONFIG=/etc/howdy/config.ini
PAM=/usr/lib64/security/pam_howdy.so
ILLUMINATOR_PARAM=/sys/module/ov7251/parameters/experimental_strobe_output

fallback() {
	printf 'howdy_preflight result=fallback detail=%s\n' "$*" >&2
	exit 0
}

[ -x "$HELPER" ] || fallback 'capture helper unavailable'
[ -x "$PYTHON" ] || fallback 'protected Python runtime unavailable'
[ -r "$CONFIG" ] || fallback 'Howdy configuration unavailable'
[ -r "$PAM" ] || fallback 'pam_howdy.so unavailable'
[ "$(stat -c '%u:%a' "$HELPER")" = '0:755' ] || fallback 'helper ownership or mode is unsafe'
PYTHON_TARGET=$(readlink -f "$PYTHON" 2>/dev/null || :)
[ -n "$PYTHON_TARGET" ] || fallback 'Python interpreter target is unavailable'
[ "$(stat -c '%u:%a' "$PYTHON_TARGET")" = '0:755' ] || fallback 'Python ownership or mode is unsafe'
[ "$(stat -c '%u:%a' "$PAM")" = '0:755' ] || fallback 'PAM module ownership or mode is unsafe'
[ -r "$ILLUMINATOR_PARAM" ] || fallback 'OV7251 illuminator driver control unavailable'
case "$(cat "$ILLUMINATOR_PARAM" 2>/dev/null || :)" in
	Y|y|1|true) ;;
	*) fallback 'OV7251 illuminator is not enabled' ;;
esac

# Do not open the camera, change modules, change media links, or restart RGB.
# The helper performs the final route/ownership check during authentication.
"$HELPER" --help >/dev/null 2>&1 || fallback 'capture helper is not executable'
printf 'howdy_preflight result=ready hardware=deferred\n'
