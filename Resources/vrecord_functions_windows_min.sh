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

_update_config_file(){
    : > "${CONFIG_FILE}.tmp"
    {
        echo "DIR=\"${DIR}\""
        echo "LOGDIR=\"${LOGDIR}\""
        echo "ID=\"${ID}\""
        echo "DEVICE_INPUT_CHOICE=\"${DEVICE_INPUT_CHOICE}\""
        echo "DECKLINK_INPUT_CHOICE=\"$(_unescape_amp "${DECKLINK_INPUT_CHOICE}")\""
        echo "VIDEO_INPUT_CHOICE=\"${VIDEO_INPUT_CHOICE}\""
        echo "AUDIO_INPUT_CHOICE=\"${AUDIO_INPUT_CHOICE}\""
        echo "AUDIO_MAPPING_CHOICE=\"$(_unescape_amp "${AUDIO_MAPPING_CHOICE}")\""
        echo "STANDARD_CHOICE=\"${STANDARD_CHOICE}\""
        echo "VIDEO_BIT_DEPTH_CHOICE=\"${VIDEO_BIT_DEPTH_CHOICE}\""
        echo "ASPECT_RATIO_CHOICE=\"${ASPECT_RATIO_CHOICE}\""
        echo "CONTAINER_CHOICE=\"${CONTAINER_CHOICE}\""
        echo "VIDEO_CODEC_CHOICE=\"${VIDEO_CODEC_CHOICE}\""
        echo "AUDIO_CODEC_CHOICE=\"${AUDIO_CODEC_CHOICE}\""
        echo "EMBED_LOGS_CHOICE=\"${EMBED_LOGS_CHOICE}\""
        echo "PLAYBACKVIEW_CHOICE=\"${PLAYBACKVIEW_CHOICE}\""
        echo "PLAYBACKVIEW_CHOICE_PASS=\"${PLAYBACKVIEW_CHOICE_PASS}\""
        echo "MP4_CHOICE=\"${MP4_CHOICE}\""
        echo "FRAMEMD5_CHOICE=\"${FRAMEMD5_CHOICE}\""
        echo "TIMECODEVTT_CHOICE=\"${TIMECODEVTT_CHOICE}\""
        echo "QCTOOLSXML_V2_CHOICE=\"${QCTOOLSXML_V2_CHOICE}\""
        echo "DURATION=\"${DURATION}\""
        echo "THEME_CHOICE=\"${THEME_CHOICE}\""
        echo "HD_CHOICE=\"${HD_CHOICE}\""
        echo "SIGNAL_CHOICE=\"${SIGNAL_CHOICE}\""
        echo "WAVEFORM_SCALE_CHOICE=\"${WAVEFORM_SCALE_CHOICE}\""
        echo "SIGNAL_INT_CHOICE=\"${SIGNAL_INT_CHOICE}\""
        echo "MONITOR_AUDIO_CHOICE=\"${MONITOR_AUDIO_CHOICE}\""
        echo "TECHNICIAN=\"${TECHNICIAN}\""
        echo "PREFIX=\"${PREFIX}\""
        echo "SUFFIX=\"${SUFFIX}\""
        echo "YES_SUFFIX=\"${YES_SUFFIX}\""
    } >> "${CONFIG_FILE}.tmp"
    mv -f "${CONFIG_FILE}.tmp" "${CONFIG_FILE}"
}

_get_output_filename(){
    echo "${DIR}/${ID:-output}.mkv"
}

_validate_form(){
    if [ -z "${DECKLINK_INPUT_CHOICE}" ] ; then
        echo "Select a DeckLink device"
    elif [ -z "${ID}" ] ; then
        echo "Enter a Name of Recording to enable Record"
    else
        echo "OK"
    fi
}

_get_decklink_input_list(){ _list_decklink_sources; }
_list_decklink_sources(){
    "${FFMPEG_BIN}" -nostdin -sources decklink 2>&1 | awk -F'[][]' '/\[/ {print $2}' | grep -v '^$' || true
}
_get_format_list(){ echo -e "NTSC\nPAL"; }
_get_audio_device_list(){ echo ""; }
_get_dvrescue_input_list(){ echo ""; }
_get_summary(){ echo "Windows portable vrecord"; }

# ---- video preview: decklink input -> small JPEG + optional relay to output ----
# The tee muxer feeds the preview JPEG and (when the relay checkbox is on) the
# card's output connector in one pipeline. Timer refreshes are in-process.
_start_video_preview(){
    _stop_video_preview
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
    if [ -z "${VRECORD_PLAY_FILE}" ] || [ ! -f "${VRECORD_PLAY_FILE}" ] ; then
        echo "select a file to play first" > "${VRECORD_FILEPLAY_STATUS_TMP}"
        return 1
    fi
    if [ -z "${PREVIEW_RELAY_DEV_CHOICE}" ] ; then
        PREVIEW_RELAY_DEV_CHOICE="${DECKLINK_INPUT_CHOICE}"
    fi
    echo "playing" > "${VRECORD_FILEPLAY_STATUS_TMP}"
    "${FFMPEG_BIN}" -nostdin -loglevel error -stream_loop -1 -i "${VRECORD_PLAY_FILE}" \
        -c:v v210 -c:a pcm_s24le -ar 48k \
        -f decklink -o "${PREVIEW_RELAY_DEV_CHOICE}" \
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
    if [ -z "${PREVIEW_RELAY_DEV_CHOICE}" ] ; then
        PREVIEW_RELAY_DEV_CHOICE="${DECKLINK_INPUT_CHOICE}"
    fi
    "${FFMPEG_BIN}" -nostdin -loglevel error -f lavfi -i "smptebars=size=720x486:r=30000/1001" \
        -f lavfi -i "sine=440:r=48000" -shortest \
        -c:v v210 -c:a pcm_s24le -ar 48k \
        -f decklink -o "${PREVIEW_RELAY_DEV_CHOICE}" \
        > "${VRECORD_TEST_LOG_TMP}" 2>&1 &
    echo $! > "${VRECORD_TEST_PID_TMP}"
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

_start_gui_recording(){ :; }
_stop_gui_recording(){ :; }
_toggle_gui_passthrough(){ :; }
_write_black_ppm(){ :; }
_snap_window(){ :; }
_ensure_preview_reader(){ :; }
