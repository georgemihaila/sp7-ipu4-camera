# Shared live capture validation, also sourced by hardware-independent tests.

capture_is_valid() {
	cam=$1
	raw=$2
	case $cam in
		front) width=2592; height=1944 ;;
		rear) width=3264; height=2448 ;;
		*) return 1 ;;
	esac
	[ -s "$raw" ] || return 1
	bytes=$(wc -c < "$raw" | tr -d '[:space:]')
	case $bytes in ''|*[!0-9]*) return 1 ;; esac
	minimum=$((width * height * 2 * 3))
	[ "$bytes" -ge "$minimum" ] || return 1
	# The output is three consecutive raw frames. Allow per-frame stride padding,
	# but require three equal-sized frame segments and image data in each one.
	[ "$((bytes % 3))" -eq 0 ] || return 1
	frame_bytes=$((bytes / 3))
	[ "$frame_bytes" -ge "$((width * height * 2))" ] || return 1
	frame=0
	while [ "$frame" -lt 3 ]; do
		nonzero=$(dd if="$raw" bs="$frame_bytes" skip="$frame" count=1 2>/dev/null |
			LC_ALL=C tr -d '\000' | wc -c | tr -d '[:space:]')
		case $nonzero in ''|*[!0-9]*) return 1 ;; esac
		[ "$nonzero" -gt 0 ] || return 1
		frame=$((frame + 1))
	done
	return 0
}
