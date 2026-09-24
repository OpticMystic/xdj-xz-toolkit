#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# XDJ-XZ volatile loader and hardware diagnostic probe (firmware 1.26).

export PATH="/usr/sbin:/usr/bin:/sbin:/bin:/root/pdj:$PATH"

USB="$1"
export XZ_MODS_USB="$USB"
OUT="$USB/XZ_RUNTIME"
LOG="$OUT/session.txt"
RBP="/root/pdj/rbp"
RBP_NEXT="/root/pdj/rbp.xzmod"
SOURCE_RBP="/mnt/iso/rbp.patched"
EXPECTED_MD5_FILE="/mnt/iso/rbp.patched.md5"
SOURCE_GUI="/mnt/iso/gui/imagedata.dat"
EXPECTED_GUI_MD5_FILE="/mnt/iso/gui/imagedata.dat.md5"
GUI_RAM="/dev/shm/xz_imagedata.dat"
GUI_TARGET="/root/gui/pset/imagedata/imagedata.dat"
SOURCE_FB_TOOL="/mnt/iso/tools/xz-fb-overlay"
FB_TOOL_RAM="/dev/shm/xz-fb-overlay"
SOURCE_HOOK="/mnt/iso/tools/libxz-directfb-hook.so"
EXPECTED_HOOK_MD5_FILE="/mnt/iso/tools/libxz-directfb-hook.so.md5"
HOOK_RAM="/dev/shm/libxz-directfb-hook.so"
SOURCE_MODS="/mnt/iso/tools/libxz-mods.so"
MODS_RAM="/dev/shm/libxz-mods.so"
MODS_MODE=""
SOURCE_GUI_IP_PATCH="/mnt/iso/tools/xz-gui-ip-patch"
EXPECTED_GUI_IP_PATCH_MD5_FILE="/mnt/iso/tools/xz-gui-ip-patch.md5"
GUI_IP_PATCH_RAM="/dev/shm/xz-gui-ip-patch"
APPLYING_FLAG="/tmp/xz_mod_applying"
APPLIED_FLAG="/tmp/xz_mod_applied"

mkdir -p "$OUT" 2>/dev/null

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" >> "$LOG" 2>/dev/null
}

log_command() {
    title="$1"
    shift
    log "--- $title ---"
    "$@" >> "$LOG" 2>&1 || log "$title unavailable or failed (status $?)."
}

network_snapshot() {
    log_command "ifconfig -a" ifconfig -a
    log_command "route -n" route -n
    log_command "netstat listeners" netstat -lnt
    if [ -r /proc/net/arp ]; then
        log "--- /proc/net/arp ---"
        cat /proc/net/arp >> "$LOG" 2>&1
    fi
}

configure_vj_network() {
    CURRENT_IP=$(ifconfig eth0 2>/dev/null | sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p' | head -1)
    if [ -n "$CURRENT_IP" ]; then
        echo "$CURRENT_IP" > /tmp/xz_vj_ip
    else
        CURRENT_IP=$(cat /tmp/xz_vj_ip 2>/dev/null)
        [ -n "$CURRENT_IP" ] || CURRENT_IP="169.254.168.59"
        if ! ifconfig eth0 "$CURRENT_IP" netmask 255.255.0.0 up >> "$LOG" 2>&1; then
            log "FAILED: Could not restore VJ.Tools network address $CURRENT_IP."
            return 1
        fi
    fi
    case "$CURRENT_IP" in
        169.254.*) route add -net 169.254.0.0 netmask 255.255.0.0 dev eth0 >> "$LOG" 2>&1 || true ;;
    esac
    log "VJ_NETWORK_READY: interface=eth0 address=$CURRENT_IP receiver=tcp50005 source=detected-or-restored"
}

usb_gadget_snapshot() {
    log "--- USB gadget/UDC read-only inventory ---"
    log_command "kernel" uname -a
    log_command "loaded USB modules" sh -c "lsmod | grep -Ei 'usb|gadget|udc|arcotg|pmulti|function'"
    log_command "UDC sysfs" sh -c "for u in /sys/class/udc/*; do [ -e \"\$u\" ] || continue; echo UDC=\$u; for f in state current_speed maximum_speed is_otg function; do [ -r \"\$u/\$f\" ] && echo \"\$f=\$(cat \"\$u/\$f\")\"; done; [ -r \"\$u/device/modalias\" ] && echo \"modalias=\$(cat \"\$u/device/modalias\")\"; done"
    log_command "UDC endpoint capabilities" sh -c "for e in /sys/class/udc/*/device/gadget/ep* /sys/kernel/debug/usb/*/ep*; do [ -e \"\$e\" ] || continue; echo ENDPOINT=\$e; [ -r \"\$e/maxpacket\" ] && echo \"maxpacket=\$(cat \"\$e/maxpacket\")\"; [ -r \"\$e/type\" ] && echo \"type=\$(cat \"\$e/type\")\"; done"
    [ -r /sys/kernel/debug/usb/devices ] && log_command "USB topology descriptors" cat /sys/kernel/debug/usb/devices
    [ -r /proc/bus/usb/devices ] && log_command "legacy USB topology descriptors" cat /proc/bus/usb/devices
    log_command "USB sysfs descriptors" sh -c "for d in /sys/bus/usb/devices/*; do [ -r \"\$d/idVendor\" ] || continue; echo DEVICE=\$d; for f in idVendor idProduct bcdUSB speed busnum devnum devpath manufacturer product serial bNumInterfaces; do [ -r \"\$d/\$f\" ] && echo \"\$f=\$(cat \"\$d/\$f\")\"; done; done"
    log_command "gadget modules available" sh -c "find /lib/modules/\$(uname -r) -type f 2>/dev/null | grep -Ei '/(g_|usb_f_|libcomposite|function)'"
    log "USB_GADGET_PROBE_COMPLETE: read_only=1 udc_rebound=0 modules_loaded=0"
}

file_md5() {
    md5sum "$1" 2>/dev/null | awk '{print $1}'
}

start_telnet() {
    if [ ! -f /mnt/iso/enable_telnet ]; then
        log "Telnet disabled by payload."
        return
    fi

    if netstat -lnt 2>/dev/null | grep -q ':2323 '; then
        log "Telnet already listening on TCP 2323."
        return
    fi

    # decrypt_autoexec.sh must unmount /mnt/iso after this script exits. The
    # daemon must not inherit /mnt/iso as its cwd or the unmount stays busy.
    (
        cd /
        exec telnetd -p 2323 -l /bin/sh
    ) >/tmp/xz_telnetd.log 2>&1 &
    TELNET_PID=$!
    sleep 1
    if kill -0 "$TELNET_PID" 2>/dev/null; then
        log "Telnet daemon alive (PID $TELNET_PID, TCP 2323, bind all interfaces)."
    else
        log "FAILED: telnetd exited; output follows."
        [ -f /tmp/xz_telnetd.log ] && cat /tmp/xz_telnetd.log >> "$LOG" 2>&1
    fi
}

verify_running_rbp() {
    VERIFY_PID="$1"
    VERIFY_EXPECTED="$2"

    if [ -z "$VERIFY_PID" ] || ! kill -0 "$VERIFY_PID" 2>/dev/null; then
        log "FAILED: Patched rbp did not survive the verification delay."
        log_command "rbp launch output" cat /tmp/xz_rbp.log
        log_command "recent kernel messages" dmesg
        return 1
    fi

    EXE_PATH=$(readlink "/proc/$VERIFY_PID/exe" 2>/dev/null)
    EXE_MD5=$(file_md5 "/proc/$VERIFY_PID/exe")
    log "RUNTIME_PROOF: rbp PID=$VERIFY_PID exe=$EXE_PATH md5=$EXE_MD5 expected=$VERIFY_EXPECTED"
    if [ -n "$VERIFY_EXPECTED" ] && [ "$EXE_MD5" != "$VERIFY_EXPECTED" ]; then
        log "FAILED: Running rbp hash does not match the payload."
        return 1
    fi

    log "SUCCESS: Patched rbp survived and its executable hash matches the payload."
    if [ -f "$HOOK_RAM" ]; then
        if grep -F "$HOOK_RAM" "/proc/$VERIFY_PID/maps" >/dev/null 2>&1; then
            log "HOOK_RUNTIME_PROOF: PID=$VERIFY_PID loaded=$HOOK_RAM tcp=50005"
        else
            log "FAILED: VJ.Tools DirectFB hook is staged but not mapped into rbp."
            return 1
        fi
    fi
    if [ -n "$MODS_MODE" ] && ! grep -F "$MODS_RAM" "/proc/$VERIFY_PID/maps" >/dev/null 2>&1; then
        log "FAILED: Requested standalone mod runtime is not mapped."
        return 1
    fi
    return 0
}

collect_rbp_options() {
    ti_tsc=$(fw_printenv -n ti_tsc 2>/dev/null)
    tsc_option=""
    [ "$ti_tsc" = "1" ] && tsc_option="-t"

    joglcd=$(fw_printenv -n joglcd 2>/dev/null)
    joglcd_option=""
    [ "$joglcd" = "rt" ] && joglcd_option="-r"

    rootfs=$(fw_printenv -n rootfs 2>/dev/null)
    nfs_options=""
    if [ "$rootfs" = "nfs" ]; then
        host_pc_port=$(fw_printenv -n lanhub_host_port 2>/dev/null)
        nfs_options="-a -h$host_pc_port"
    fi
}

launch_runtime_rbp() {
    collect_rbp_options
    cd /root/pdj || return 1
    if [ -f "$HOOK_RAM" ]; then
        if [ -n "$MODS_MODE" ]; then
            MODS_OBSERVE=0
            MODS_UI=1
            [ "$MODS_MODE" = observer ] && MODS_OBSERVE=1 && MODS_UI=0
            LD_PRELOAD="$HOOK_RAM:$MODS_RAM" XZ_MODS_ENABLE=1 XZ_MODS_OBSERVER="$MODS_OBSERVE" XZ_MODS_UI="$MODS_UI" XZ_MODS_STEMS=1 \
                ./rbp $tsc_option $joglcd_option $nfs_options >/tmp/xz_rbp.log 2>&1 &
        else
            LD_PRELOAD="$HOOK_RAM" ./rbp $tsc_option $joglcd_option $nfs_options >/tmp/xz_rbp.log 2>&1 &
        fi
        RBP_LAUNCH_PID=$!
        log "Launched hooked rbp PID=$RBP_LAUNCH_PID flags=[$tsc_option $joglcd_option $nfs_options] receiver=tcp50005."
    else
        ./rbp $tsc_option $joglcd_option $nfs_options >/tmp/xz_rbp.log 2>&1 &
        RBP_LAUNCH_PID=$!
        log "Launched candidate rbp PID=$RBP_LAUNCH_PID flags=[$tsc_option $joglcd_option $nfs_options]."
    fi
    return 0
}

stage_optional_assets() {
    if [ -f "$SOURCE_FB_TOOL" ]; then
        if cp "$SOURCE_FB_TOOL" "$FB_TOOL_RAM" 2>>"$LOG"; then
            chmod 755 "$FB_TOOL_RAM"
            log "Staged volatile framebuffer helper: $FB_TOOL_RAM md5=$(file_md5 "$FB_TOOL_RAM")"
        else
            log "FAILED: Could not stage framebuffer helper."
            return 1
        fi
    fi

    if [ -f "$SOURCE_HOOK" ]; then
        HOOK_SOURCE_MD5=$(file_md5 "$SOURCE_HOOK")
        HOOK_EXPECTED_MD5=$(awk 'NR == 1 {print $1}' "$EXPECTED_HOOK_MD5_FILE" 2>/dev/null)
        log "DirectFB hook payload: bytes=$(wc -c < "$SOURCE_HOOK" 2>/dev/null) md5=$HOOK_SOURCE_MD5 expected=$HOOK_EXPECTED_MD5"
        if [ -n "$HOOK_EXPECTED_MD5" ] && [ "$HOOK_SOURCE_MD5" != "$HOOK_EXPECTED_MD5" ]; then
            log "FAILED: DirectFB hook hash does not match its manifest."
            return 1
        fi
        if ! cp "$SOURCE_HOOK" "$HOOK_RAM.next" 2>>"$LOG"; then
            log "FAILED: Could not stage DirectFB hook in volatile RAM."
            return 1
        fi
        chmod 755 "$HOOK_RAM.next"
        if [ "$(file_md5 "$HOOK_RAM.next")" != "$HOOK_SOURCE_MD5" ]; then
            log "FAILED: Staged DirectFB hook hash mismatch."
            return 1
        fi
        mv -f "$HOOK_RAM.next" "$HOOK_RAM" || return 1
        log "SUCCESS: VJ.Tools filmstrip receiver staged in volatile RAM."
    fi

    if [ -f "$SOURCE_MODS" ]; then
        MODS_EXPECTED=$(awk 'NR == 1 {print $1}' /mnt/iso/tools/libxz-mods.so.md5 2>/dev/null)
        [ -n "$MODS_EXPECTED" ] && [ "$(file_md5 "$SOURCE_MODS")" = "$MODS_EXPECTED" ] || return 1
        MODS_MODE=$(cat /mnt/iso/mods-mode 2>/dev/null)
        case "$MODS_MODE" in observer|experimental) ;; *) return 1;; esac
        [ -f "$HOOK_RAM" ] || return 1
        cp "$SOURCE_MODS" "$MODS_RAM.next" || return 1
        [ "$(file_md5 "$MODS_RAM.next")" = "$MODS_EXPECTED" ] || return 1
        chmod 755 "$MODS_RAM.next" || return 1
        mv -f "$MODS_RAM.next" "$MODS_RAM" || return 1
        log "Standalone runtime staged in RAM: mode=$MODS_MODE md5=$MODS_EXPECTED"
    fi

    if [ ! -f "$SOURCE_GUI" ]; then
        log "No custom GUI pack in this payload."
        return 0
    fi

    GUI_SOURCE_MD5=$(file_md5 "$SOURCE_GUI")
    GUI_EXPECTED_MD5=$(awk 'NR == 1 {print $1}' "$EXPECTED_GUI_MD5_FILE" 2>/dev/null)
    log "GUI payload: bytes=$(wc -c < "$SOURCE_GUI" 2>/dev/null) md5=$GUI_SOURCE_MD5 expected=$GUI_EXPECTED_MD5"
    if [ -n "$GUI_EXPECTED_MD5" ] && [ "$GUI_SOURCE_MD5" != "$GUI_EXPECTED_MD5" ]; then
        log "FAILED: Mounted GUI payload hash does not match its manifest."
        return 1
    fi
    if ! cp "$SOURCE_GUI" "$GUI_RAM" 2>>"$LOG"; then
        log "FAILED: Could not stage custom GUI in /dev/shm."
        return 1
    fi
    if [ "$(file_md5 "$GUI_RAM")" != "$GUI_SOURCE_MD5" ]; then
        log "FAILED: Staged GUI hash mismatch."
        return 1
    fi

    GUI_RUNTIME_MD5="$GUI_SOURCE_MD5"
    if [ -f "$SOURCE_GUI_IP_PATCH" ]; then
        PATCH_SOURCE_MD5=$(file_md5 "$SOURCE_GUI_IP_PATCH")
        PATCH_EXPECTED_MD5=$(awk 'NR == 1 {print $1}' "$EXPECTED_GUI_IP_PATCH_MD5_FILE" 2>/dev/null)
        if [ -n "$PATCH_EXPECTED_MD5" ] && [ "$PATCH_SOURCE_MD5" != "$PATCH_EXPECTED_MD5" ]; then
            log "FAILED: GUI IP patcher hash mismatch."
            return 1
        fi
        cp "$SOURCE_GUI_IP_PATCH" "$GUI_IP_PATCH_RAM" 2>>"$LOG" || return 1
        chmod 755 "$GUI_IP_PATCH_RAM"
        DEVICE_IP=$(ifconfig eth0 2>/dev/null | sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p' | head -1)
        [ -n "$DEVICE_IP" ] || DEVICE_IP="NO-IP"
        if ! "$GUI_IP_PATCH_RAM" "$GUI_RAM" "$DEVICE_IP" >>"$LOG" 2>&1; then
            log "FAILED: Could not render runtime IP into GUI pack."
            return 1
        fi
        GUI_RUNTIME_MD5=$(file_md5 "$GUI_RAM")
        log "GUI_IP_RUNTIME_PROOF: address=$DEVICE_IP md5=$GUI_RUNTIME_MD5"
    fi

    # On an early USB mount apl_start may still be mounting the UBIFS GUI
    # partition. Wait for that target, then overlay only this file from RAM.
    WAIT_COUNT=0
    while [ ! -f "$GUI_TARGET" ] && [ "$WAIT_COUNT" -lt 45 ]; do
        sleep 1
        WAIT_COUNT=$((WAIT_COUNT + 1))
    done
    if [ ! -f "$GUI_TARGET" ]; then
        log "FAILED: GUI target did not appear after ${WAIT_COUNT}s."
        return 1
    fi

    if mount | grep -F " on $GUI_TARGET " >/dev/null 2>&1; then
        umount "$GUI_TARGET" 2>>"$LOG" || return 1
    fi
    if ! mount --bind "$GUI_RAM" "$GUI_TARGET" 2>>"$LOG"; then
        log "FAILED: RAM GUI bind mount failed."
        return 1
    fi
    GUI_BOUND_MD5=$(file_md5 "$GUI_TARGET")
    log "GUI_RUNTIME_PROOF: target=$GUI_TARGET md5=$GUI_BOUND_MD5 expected=$GUI_RUNTIME_MD5"
    if [ "$GUI_BOUND_MD5" != "$GUI_RUNTIME_MD5" ]; then
        log "FAILED: Bound GUI hash does not match the payload."
        return 1
    fi
    log "SUCCESS: Custom GUI is bound from volatile RAM."
    return 0
}

log "=== XDJ-XZ Diagnostic Native Loader v3 (firmware 1.26) ==="
log "USB Mount Path: $USB"
configure_vj_network
network_snapshot
usb_gadget_snapshot
start_telnet

if [ -f "$APPLIED_FLAG" ]; then
    log "Payload already applied this boot: $(cat "$APPLIED_FLAG" 2>/dev/null)"
    CURRENT_PID=$(pidof rbp 2>/dev/null)
    verify_running_rbp "$CURRENT_PID" "$(cat "$APPLIED_FLAG" 2>/dev/null)"
    sync
    exit 0
fi

if [ -f "$APPLYING_FLAG" ]; then
    log "Loader re-entry blocked while another application attempt is active."
    sync
    exit 0
fi
touch "$APPLYING_FLAG"

if [ ! -f "$SOURCE_RBP" ]; then
    log "FAILED: $SOURCE_RBP is missing from the mounted payload."
    rm -f "$APPLYING_FLAG"
    sync
    exit 1
fi

SOURCE_MD5=$(file_md5 "$SOURCE_RBP")
EXPECTED_MD5=$(awk 'NR == 1 {print $1}' "$EXPECTED_MD5_FILE" 2>/dev/null)
log "Payload rbp: bytes=$(wc -c < "$SOURCE_RBP" 2>/dev/null) md5=$SOURCE_MD5 expected=$EXPECTED_MD5"

if [ -n "$EXPECTED_MD5" ] && [ "$SOURCE_MD5" != "$EXPECTED_MD5" ]; then
    log "FAILED: Mounted payload hash does not match its build manifest."
    rm -f "$APPLYING_FLAG"
    sync
    exit 1
fi

# Copy to a new inode and atomically rename it over the live executable. This
# avoids ETXTBSY behavior when replacing a currently mapped ELF.
rm -f "$RBP_NEXT"
if ! cp "$SOURCE_RBP" "$RBP_NEXT" 2>>"$LOG"; then
    log "FAILED: Could not stage patched rbp in tmpfs."
    rm -f "$APPLYING_FLAG"
    sync
    exit 1
fi
chmod 755 "$RBP_NEXT"

STAGED_MD5=$(file_md5 "$RBP_NEXT")
if [ "$STAGED_MD5" != "$SOURCE_MD5" ]; then
    log "FAILED: Staged rbp hash mismatch ($STAGED_MD5 != $SOURCE_MD5)."
    rm -f "$RBP_NEXT" "$APPLYING_FLAG"
    sync
    exit 1
fi

if ! mv -f "$RBP_NEXT" "$RBP" 2>>"$LOG"; then
    log "FAILED: Atomic rbp replacement failed."
    rm -f "$RBP_NEXT" "$APPLYING_FLAG"
    sync
    exit 1
fi

INSTALLED_MD5=$(file_md5 "$RBP")
log "Installed rbp in tmpfs: md5=$INSTALLED_MD5"
if [ "$INSTALLED_MD5" != "$SOURCE_MD5" ]; then
    log "FAILED: Installed rbp hash mismatch."
    rm -f "$APPLYING_FLAG"
    sync
    exit 1
fi

if ! stage_optional_assets; then
    rm -f "$APPLYING_FLAG"
    sync
    exit 1
fi

OLD_PID=$(pidof rbp 2>/dev/null)
if [ -z "$OLD_PID" ]; then
    log "Early boot: patched rbp installed before apl_start. A delayed verifier is armed."
    echo "$SOURCE_MD5" > "$APPLIED_FLAG"
    rm -f "$APPLYING_FLAG"

    (
        cd /
        sleep 25
        DELAYED_PID=$(pidof rbp 2>/dev/null)
        log "Delayed boot probe found rbp PID(s): $DELAYED_PID"
        if [ -f "$HOOK_RAM" ]; then
            log "Restarting early-boot rbp once so the RAM-only DirectFB hook is loaded."
            [ -n "$DELAYED_PID" ] && killall -9 rbp 2>/dev/null
            sleep 1
    if launch_runtime_rbp; then
        DELAYED_PID="$RBP_LAUNCH_PID"
        sleep 5
        configure_vj_network
            else
                DELAYED_PID=""
            fi
        fi
        verify_running_rbp "$DELAYED_PID" "$SOURCE_MD5"
        network_snapshot
        sync
    ) &

    sync
    exit 0
fi

log "Live insertion: replacing running rbp PID(s) $OLD_PID."
killall -9 rbp 2>/dev/null
sleep 1

if ! launch_runtime_rbp; then
    log "FAILED: Could not launch patched rbp."
    rm -f "$APPLYING_FLAG"
    sync
    exit 1
fi
NEW_PID="$RBP_LAUNCH_PID"

sleep 5
configure_vj_network
if verify_running_rbp "$NEW_PID" "$SOURCE_MD5"; then
    echo "$SOURCE_MD5" > "$APPLIED_FLAG"
    STATUS=0
else
    STATUS=1
fi
rm -f "$APPLYING_FLAG"
network_snapshot
log "=== XDJ-XZ Diagnostic Native Loader Complete (status $STATUS) ==="
sync
exit "$STATUS"
