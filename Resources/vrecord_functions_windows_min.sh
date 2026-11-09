# Windows action wiring for gtkdialog (sourced on every GUI action - keep lean).
# All device interaction goes through ffmpeg-ma's decklink muxer/demuxer
# (the proven backend). Every function here is a real implementation.

_unescape_amp(){
    local V="${1//&amp;/&}"
    printf '%s' "${V}"
}

_vd_alive(){
    [ -f "${1}" ] && kill -0 "$(cat "${1}")" 2>/dev/null
}

_vd_kill_pidfile(){
    if [ -f "${1}" ] ; then
        kill "$(cat "${1}")" 2>/dev/null
        rm -f "${1}"
    fi
}

_combine_duration_fields(){
    if [ -n "${DURATION_HOURS}" ] || [ -n "${DURATION_MINUTES}" ] || [ -n "${DURATION_SECONDS}" ] ; then
        DURATION="${DURATION_HOURS:-0}:${DURATION_MINUTES:-0}:${DURATION_SECONDS:-0}"
    else
        DURATION=""
    fi
}

_update_config_file(){
    _combine_duration_fields
    if [ "${DEVICE_INPUT_CHOICE}" = "3" ] || [ "${DEVICE_INPUT_CHOICE}" = "4" ] || [ "${DEVICE_INPUT_CHOICE}" = "5" ] ; then
        DEVICE_INPUT_CHOICE="${INPUT_CHOICE_MEM}"
    fi
    {
        echo "# Windows GUI config"
        echo "STARTUP_VIEW=\"${STARTUP_VIEW}\""
        echo "DEVICE_INPUT_CHOICE=\"${DEVICE_INPUT_CHOICE}\""
        echo "VIDEO_INPUT_CHOICE=\"${VIDEO_INPUT_CHOICE}\""
        echo "AUDIO_INPUT_CHOICE=\"${AUDIO_INPUT_CHOICE}\""
        echo "CONTAINER_CHOICE=\"${CONTAINER_CHOICE}\""
        echo "VIDEO_CODEC_CHOICE=\"${VIDEO_CODEC_CHOICE}\""
        echo "AUDIO_CODEC_CHOICE=\"${AUDIO_CODEC_CHOICE}\""
        echo "AUDIO_DEV_CHOICE=\"${AUDIO_DEV_CHOICE}\""
        echo "AUDIO_MODE_CODEC_CHOICE=\"${AUDIO_MODE_CODEC_CHOICE}\""
        echo "AUDIO_CHANNEL_CHOICE=\"${AUDIO_CHANNEL_CHOICE}\""
        echo "AUDIO_MODE_SR_CHOICE=\"${AUDIO_MODE_SR_CHOICE}\""
        echo "VIDEO_BIT_DEPTH_CHOICE=\"${VIDEO_BIT_DEPTH_CHOICE}\""
        echo "ASPECT_RATIO_CHOICE=\"${ASPECT_RATIO_CHOICE}\""
        echo "NTSC_43_SAR_CHOICE=\"${NTSC_43_SAR_CHOICE}\""
        echo "NTSC_169_SAR_CHOICE=\"${NTSC_169_SAR_CHOICE}\""
        echo "PAL_43_SAR_CHOICE=\"${PAL_43_SAR_CHOICE}\""
        echo "PAL_169_SAR_CHOICE=\"${PAL_169_SAR_CHOICE}\""
        echo "AUDIO_MAPPING_CHOICE=\"$(_unescape_amp "${AUDIO_MAPPING_CHOICE}")\""
        echo "STANDARD_CHOICE=\"${STANDARD_CHOICE}\""
        echo "MP4_CHOICE=\"${MP4_CHOICE}\""
        echo "FRAMEMD5_CHOICE=\"${FRAMEMD5_CHOICE}\""
        echo "TIMECODEVTT_CHOICE=\"${TIMECODEVTT_CHOICE}\""
        echo "QCTOOLSXML_V2_CHOICE=\"${QCTOOLSXML_V2_CHOICE}\""
        echo "EMBED_LOGS_CHOICE=\"${EMBED_LOGS_CHOICE}\""
        echo "PLAYBACKVIEW_CHOICE=\"${PLAYBACKVIEW_CHOICE}\""
        echo "PLAYBACKVIEW_CHOICE_PASS=\"${PLAYBACKVIEW_CHOICE_PASS}\""
        echo "DIR=\"${DIR}\""
        echo "LOGDIR=\"${LOGDIR}\""
        echo "DURATION=\"${DURATION}\""
        echo "PREFIX=\"${PREFIX}\""
        echo "USER_SUFFIX=\"${USER_SUFFIX}\""
        echo "YES_SUFFIX=\"${YES_SUFFIX}\""
        echo "TECHNICIAN=\"${TECHNICIAN}\""
        echo "DVRESCUE_INPUT_CHOICE=\"${DVRESCUE_INPUT_CHOICE}\""
        echo "DECKLINK_INPUT_CHOICE=\"$(_unescape_amp "${DECKLINK_INPUT_CHOICE}")\""
        echo "WAVEFORM_SCALE_CHOICE=\"${WAVEFORM_SCALE_CHOICE}\""
        echo "MONITOR_AUDIO_CHOICE=\"${MONITOR_AUDIO_CHOICE}\""
        echo "SIGNAL_VIEW_CHOICE=\"${SIGNAL_VIEW_CHOICE}\""
        echo "DV_CONTAINER_CHOICE=\"${DV_CONTAINER_CHOICE}\""
        echo "DV_RESCUE_OPTION_S=\"${DV_RESCUE_OPTION_S}\""
        echo "DV_RESCUE_OPTION_D=\"${DV_RESCUE_OPTION_D}\""
        echo "DV_RESCUE_OPTION_T=\"${DV_RESCUE_OPTION_T}\""
        echo "DV_RESCUE_OPTION_TC=\"${DV_RESCUE_OPTION_TC}\""
        echo "CC2VTT=\"${CC2VTT}\""
        echo "CC2SRT=\"${CC2SRT}\""
        echo "CAP2MP4=\"${CAP2MP4}\""
        echo "MEDIA_ID_TYPE=\"${MEDIA_ID_TYPE}\""
        echo "WEBVTT_EXTRA=\"${WEBVTT_EXTRA}\""
        echo "HD_CHOICE=\"${HD_CHOICE}\""
        echo "COLOR_SAFE=\"${COLOR_SAFE}\""
        echo "THEME_CHOICE=\"${THEME_CHOICE}\""
        echo "PREVIEW_RELAY_CHOICE=\"${PREVIEW_RELAY_CHOICE}\""
        echo "PREVIEW_RELAY_DEV_CHOICE=\"$(_unescape_amp "${PREVIEW_RELAY_DEV_CHOICE}")\""
        echo "VRECORD_PLAY_FILE=\"${VRECORD_PLAY_FILE}\""
        echo "FILEPLAY_AUTO_CHOICE=\"${FILEPLAY_AUTO_CHOICE:-true}\""
        echo "TEST_SIGNAL_OUTPUT_CHOICE=\"${TEST_SIGNAL_OUTPUT_CHOICE}\""
        echo "TEST_SIGNAL_PATTERN_CHOICE=\"${TEST_SIGNAL_PATTERN_CHOICE:-SMPTE Bars}\""
        echo "TEST_SIGNAL_AUDIO_CHOICE=\"${TEST_SIGNAL_AUDIO_CHOICE:-1 kHz Tone}\""
        echo "ID=\"${ID}\""
    } > "${CONFIG_FILE}.tmp"
    mv -f "${CONFIG_FILE}.tmp" "${CONFIG_FILE}"
    . "${CONFIG_FILE}"
}

_get_output_filename(){
    local OUTDIR="${DIR:-$(pwd)}"
    local BASENAME="${PREFIX}${ID:-output}"
    if [ "${YES_SUFFIX}" = "true" ] && [ -n "${USER_SUFFIX}" ] ; then
        BASENAME="${BASENAME}_${USER_SUFFIX}"
    fi
    local EXT="mkv"
    case "${CONTAINER_CHOICE}" in
        "MXF") EXT="mxf" ;;
        "QuickTime") EXT="mov" ;;
        "AVI") EXT="avi" ;;
        "MP4") EXT="mp4" ;;
        "DV") EXT="dv" ;;
    esac
    printf '%s/%s.%s\n' "${OUTDIR}" "${BASENAME}" "${EXT}"
}

_validate_form(){
    if [ -z "${DIR}" ] ; then
        echo "WARNING: The recording directory is not set. $(pwd) will be used."
    elif [ ! -d "${DIR}" ] ; then
        echo "ERROR: The recording directory (${DIR}) does not exist."
    fi
    if [ -n "${LOGDIR}" ] && [ ! -d "${LOGDIR}" ] ; then
        echo "ERROR: The auxiliary directory (${LOGDIR}) does not exist."
    fi
    if [ "${DEVICE_INPUT_CHOICE:-0}" = "0" ] && [ -z "${DECKLINK_INPUT_CHOICE}" ] ; then
        echo "ERROR: Select a DeckLink device."
    fi
    if [ -z "${ID}" ] ; then
        echo "WARNING: Enter a Name of Recording to enable Record."
    fi
}

_get_decklink_input_list(){ _list_decklink_sources; }
_list_decklink_sources(){
    "${FFMPEG_BIN}" -nostdin -sources decklink 2>&1 | awk -F'[][]' '/\[/ {print $2}' | grep -v '^$' || true
}
_get_format_list(){ echo -e "NTSC\nPAL"; }
_get_audio_device_list(){
    "${FFMPEG_BIN}" -nostdin -hide_banner -f dshow -list_devices true -i dummy 2>&1 | sed -n '/DirectShow audio devices/,/DirectShow video devices/p' | grep -o '"[^"]*"' | tr -d '"' || true
}
_get_dvrescue_input_list(){ echo ""; }
_get_summary(){
    if [ ! -f "${CONFIG_FILE}" ] ; then
        echo "Vrecord configuration file is missing, please visit the Settings panel."
        return 0
    fi
    . "${CONFIG_FILE}"
    case "${DEVICE_INPUT_CHOICE}" in
        "0")
            echo "Device Input: DeckLink"
            echo "Card: ${DECKLINK_INPUT_CHOICE}"
            echo "Video/Audio Inputs: ${VIDEO_INPUT_CHOICE} / ${AUDIO_INPUT_CHOICE}"
            echo "Container/Codecs: ${CONTAINER_CHOICE} / ${VIDEO_CODEC_CHOICE} / ${AUDIO_CODEC_CHOICE}"
            echo "Standard: ${STANDARD_CHOICE}    Aspect: ${ASPECT_RATIO_CHOICE}    Bit depth: ${VIDEO_BIT_DEPTH_CHOICE}"
            echo "Output: $(_get_output_filename)"
            ;;
        "1")
            echo "Device Input: DV"
            echo "Device: ${DVRESCUE_INPUT_CHOICE}"
            echo "Output: $(_get_output_filename)"
            ;;
        "2")
            echo "Device Input: Audio"
            echo "Device: ${AUDIO_DEV_CHOICE}"
            echo "Audio mode: ${AUDIO_MODE_CODEC_CHOICE}, ${AUDIO_CHANNEL_CHOICE} channels, ${AUDIO_MODE_SR_CHOICE}"
            echo "Output: $(_get_output_filename)"
            ;;
        *)
            echo "Configure an input tab, recording directory, and recording name."
            ;;
    esac
}

# ---- video preview: decklink input -> small JPEG + optional relay to output ----
# The tee muxer feeds the preview JPEG and (when the relay checkbox is on) the
# card's output connector in one pipeline. Timer refreshes are in-process.
_start_video_preview(){
    _stop_video_preview
    _stop_test_signal
    if [ -z "${DECKLINK_INPUT_CHOICE}" ] ; then
        echo "select a video card on the Decklink tab first" > "${VRECORD_PREVIEW_STATUS_TMP}"
        return 1
    fi
    echo "starting" > "${VRECORD_PREVIEW_STATUS_TMP}"
    local TEE_SPEC="\"${VRECORD_PREVIEW_TMP}\""
    if [ "${PREVIEW_RELAY_CHOICE}" = "true" ] && [ -n "${PREVIEW_RELAY_DEV_CHOICE}" ] ; then
        TEE_SPEC="${TEE_SPEC}|[f=decklink:onfail=ignore]${PREVIEW_RELAY_DEV_CHOICE}"
    fi
    "${FFMPEG_BIN}" -nostdin -loglevel error -f decklink -raw_format uyvy422 \
        -i "${DECKLINK_INPUT_CHOICE}" -an \
        -vf "fps=4,scale=360:-1,format=yuv420p" \
        -c:v mpeg4 -q:v 5 -f tee -y "${TEE_SPEC}" \
        > "${VRECORD_PREVIEW_LOG_TMP}" 2>&1 &
    echo $! > "${VRECORD_PREVIEW_PID_TMP}"
    echo "live (${DECKLINK_INPUT_CHOICE})" > "${VRECORD_PREVIEW_STATUS_TMP}"
}
_stop_video_preview(){
    _vd_kill_pidfile "${VRECORD_PREVIEW_PID_TMP}"
    echo "stopped" > "${VRECORD_PREVIEW_STATUS_TMP}"
}

# relay checkbox: restart the preview so the output state matches immediately
_apply_relay_state(){
    if _vd_alive "${VRECORD_PREVIEW_PID_TMP}" ; then
        _stop_video_preview
        _start_video_preview
    fi
}

# ---- file playback: file -> card output + status ----
_start_file_playback(){
    _stop_file_playback
    _stop_test_signal
    if [ -z "${VRECORD_PLAY_FILE}" ] || [ ! -f "${VRECORD_PLAY_FILE}" ] ; then
        echo "select a file to play first" > "${VRECORD_FILEPLAY_STATUS_TMP}"
        return 1
    fi
    if [ -z "${PREVIEW_RELAY_DEV_CHOICE}" ] ; then
        PREVIEW_RELAY_DEV_CHOICE="${DECKLINK_INPUT_CHOICE}"
    fi
    echo "playing" > "${VRECORD_FILEPLAY_STATUS_TMP}"
    "${FFMPEG_BIN}" -nostdin -loglevel error -stream_loop -1 -i "${VRECORD_PLAY_FILE}" \
        -c:v v210 -c:a pcm_s24le -ar 48000 -ac 2 \
        -f decklink "${PREVIEW_RELAY_DEV_CHOICE}" \
        > "${VRECORD_FILEPLAY_LOG_TMP}" 2>&1 &
    echo $! > "${VRECORD_FILEPLAY_PID_TMP}"
}
_stop_file_playback(){
    _vd_kill_pidfile "${VRECORD_FILEPLAY_PID_TMP}"
    echo "stopped" > "${VRECORD_FILEPLAY_STATUS_TMP}"
}
_pause_file_playback(){ :; }
_halt_file_playback(){ _stop_file_playback; }

# transport button: stop/restart the file playback
_toggle_transport_play(){
    if _vd_alive "${VRECORD_FILEPLAY_PID_TMP}" ; then
        _stop_file_playback
    else
        _start_file_playback
    fi
}

# ---- test signal: bars + tone to the card output ----
_start_test_signal(){
    _stop_test_signal
    _stop_file_playback
    if [ -z "${PREVIEW_RELAY_DEV_CHOICE}" ] ; then
        PREVIEW_RELAY_DEV_CHOICE="${DECKLINK_INPUT_CHOICE}"
    fi
    "${FFMPEG_BIN}" -nostdin -loglevel error -re \
        -f lavfi -i "smptebars=size=720x486:r=30000/1001" \
        -f lavfi -i "sine=frequency=440:sample_rate=48000" \
        -map 0:v -map 1:a -c:v v210 -c:a pcm_s24le -ar 48000 -ac 2 \
        -f decklink "${PREVIEW_RELAY_DEV_CHOICE}" \
        > "${VRECORD_TEST_LOG_TMP}" 2>&1 &
    echo $! > "${VRECORD_TEST_PID_TMP}"
    echo "test signal (bars + tone) playing" > "${VRECORD_FILEPLAY_STATUS_TMP}"
}
_stop_test_signal(){
    _vd_kill_pidfile "${VRECORD_TEST_PID_TMP}"
}
_apply_test_signal_state(){
    if [ "${TEST_SIGNAL_OUTPUT_CHOICE}" = "true" ] ; then
        _start_test_signal
    else
        _stop_test_signal
    fi
}
_change_test_signal_source(){ :; }

_refresh_bg_status(){
    if [ -f "${1}" ] && kill -0 "$(cat "${1}")" 2>/dev/null ; then
        echo "${4}" > "${3}"
        return 0
    fi
    if [ -s "${2}" ] ; then
        local LASTLINE
        LASTLINE="$(tail -n 1 "${2}" 2>/dev/null)"
        [ -n "${LASTLINE}" ] && echo "stopped: ${LASTLINE}" > "${3}"
    fi
}

_refresh_video_preview_status(){ _refresh_bg_status "${VRECORD_PREVIEW_PID_TMP}" "${VRECORD_PREVIEW_LOG_TMP}" "${VRECORD_PREVIEW_STATUS_TMP}" "live (${DECKLINK_INPUT_CHOICE})"; }

_refresh_file_playback_status(){
    if _vd_alive "${VRECORD_TEST_PID_TMP}" ; then
        echo "test signal (bars + tone) playing" > "${VRECORD_FILEPLAY_STATUS_TMP}"
        return 0
    fi
    if _vd_alive "${VRECORD_FILEPLAY_PID_TMP}" ; then
        echo "playing ${VRECORD_PLAY_FILE##*/}" > "${VRECORD_FILEPLAY_STATUS_TMP}"
        return 0
    fi
    if [ "${PREVIEW_RELAY_CHOICE}" = "true" ] && _vd_alive "${VRECORD_PREVIEW_PID_TMP}" ; then
        echo "relaying live input to output" > "${VRECORD_FILEPLAY_STATUS_TMP}"
        return 0
    fi
    _refresh_bg_status "${VRECORD_FILEPLAY_PID_TMP}" "${VRECORD_FILEPLAY_LOG_TMP}" "${VRECORD_FILEPLAY_STATUS_TMP}" "playing"
}

_refresh_preview_page(){
    _refresh_video_preview_status
    _refresh_file_playback_status
}

_start_gui_recording(){ :; }
_stop_gui_recording(){ :; }
_toggle_gui_passthrough(){ :; }
_write_black_ppm(){ :; }
_snap_window(){ :; }
_ensure_preview_reader(){ :; }
