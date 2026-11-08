# Minimal Windows helpers for gtkdialog actions (keep this file small - sourced on every click)

_update_config_file(){
    : > "${CONFIG_FILE}.tmp"
    {
        echo "DIR=\"${DIR}\""
        echo "LOGDIR=\"${LOGDIR}\""
        echo "ID=\"${ID}\""
        echo "DEVICE_INPUT_CHOICE=\"${DEVICE_INPUT_CHOICE}\""
        echo "DECKLINK_INPUT_CHOICE=\"${DECKLINK_INPUT_CHOICE}\""
        echo "VIDEO_INPUT_CHOICE=\"${VIDEO_INPUT_CHOICE}\""
        echo "AUDIO_INPUT_CHOICE=\"${AUDIO_INPUT_CHOICE}\""
        echo "AUDIO_MAPPING_CHOICE=\"${AUDIO_MAPPING_CHOICE}\""
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
    if [ -z "${DECKLINK_INPUT_CHOICE}" ]; then
        echo "Select a DeckLink device"
    elif [ -z "${ID}" ]; then
        echo "Enter a Name of Recording to enable Record"
    else
        echo "OK"
    fi
}

_get_decklink_input_list(){ _list_decklink_sources; }
_list_decklink_sources(){
    ${FFMPEG_BIN} -nostdin -sources decklink 2>&1 | awk -F'[][]' '/\[/ {print $2}' | grep -v '^$' || true
}
_get_format_list(){ echo -e "NTSC\nPAL"; }
_get_audio_device_list(){ echo ""; }
_get_dvrescue_input_list(){ echo ""; }
_get_summary(){ echo "Windows portable vrecord"; }

# Windows preview: decklink -> one overwriting JPEG frame (image2 -update 1),
# displayed by the gtkdialog pixmap + its refresh-only timer (no bash per tick).
_start_video_preview(){
    _stop_video_preview
    echo "starting" > "${VRECORD_PREVIEW_STATUS_TMP}"
    "${FFMPEG_BIN}" -nostdin -loglevel error -f decklink -raw_format uyvy422 \
        -i "${DECKLINK_INPUT_CHOICE}" -an -vf fps=4 \
        -update 1 -q:v 5 -y "${VRECORD_PREVIEW_TMP}" \
        > "${VRECORD_PREVIEW_LOG_TMP}" 2>&1 &
    echo $! > "${VRECORD_PREVIEW_PID_TMP}"
    echo "live (${DECKLINK_INPUT_CHOICE})" > "${VRECORD_PREVIEW_STATUS_TMP}"
}
_stop_video_preview(){
    if [[ -f "${VRECORD_PREVIEW_PID_TMP}" ]] ; then
        kill "$(cat "${VRECORD_PREVIEW_PID_TMP}")" 2>/dev/null
        rm -f "${VRECORD_PREVIEW_PID_TMP}"
    fi
    echo "stopped" > "${VRECORD_PREVIEW_STATUS_TMP}"
}
_start_file_playback(){ :; }
_stop_file_playback(){ :; }
_pause_file_playback(){ :; }
_toggle_transport_play(){
    # swap the transport button icon (play <-> pause); the 500ms refresh-only
    # timer picks the new image up without any bash on the GUI thread
    if [[ -f "${VRECORD_FILEPLAY_ICON_TMP}" ]] && cmp -s "${VRECORD_FILEPLAY_ICON_TMP}" "${RESOURCE_PATH}/transport-play.png" 2>/dev/null ; then
        cp "${RESOURCE_PATH}/transport-pause.png" "${VRECORD_FILEPLAY_ICON_TMP}" 2>/dev/null
    else
        cp "${RESOURCE_PATH}/transport-play.png" "${VRECORD_FILEPLAY_ICON_TMP}" 2>/dev/null
    fi
}
_halt_file_playback(){ :; }
_apply_relay_state(){ :; }
_start_test_signal(){ :; }
_stop_test_signal(){ :; }
_apply_test_signal_state(){ :; }
_change_test_signal_source(){ :; }
_start_gui_recording(){ :; }
_stop_gui_recording(){ :; }
_toggle_gui_passthrough(){ :; }
_write_black_ppm(){ :; }
_snap_window(){ :; }
_ensure_preview_reader(){ :; }
