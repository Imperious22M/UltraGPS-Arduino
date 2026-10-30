#!/usr/bin/env bash
#
# udev-setup.sh — give the UltraGPS Arduinos stable device names.
#
#   ./udev-setup.sh base           Wait for the base station, add its rule
#   ./udev-setup.sh transmitter    Wait for the transmitter station, add its rule
#   ./udev-setup.sh all            Do base, then transmitter
#   ./udev-setup.sh                Ask which one
#   ./udev-setup.sh remove [base|transmitter|all]
#                                  Remove rules (all if no station given)
#
# The script waits for a new /dev/ttyUSB* or /dev/ttyACM* device to appear, reads
# its USB attributes from udev, and writes a rule to $RULES_FILE that creates the
# symlink below. Re-running for a role replaces that role's previous rule.
#
# Boards are matched on idVendor + idProduct + serial. A board with no USB serial
# number (common on CH340 clones) can't be told apart from an identical one; for
# those the script offers a rule on idVendor + idProduct + product string
# instead, which will match EVERY identical clone, and asks before writing it.
#
# Every rule sets ENV{ID_MM_DEVICE_IGNORE}="1" so ModemManager leaves the port
# alone (it otherwise probes new ttys and can upset the board after a reset).
#
# Options:
#   -n, --dry-run    Print the rule instead of installing it (no sudo needed)

set -euo pipefail

RULES_FILE="/etc/udev/rules.d/99-ultragps.rules"
BASE_LINK="ultragps-base"
TX_LINK="ultragps-transmitter"

DRY_RUN=false

usage() {
    sed -n '3,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

# Current USB serial devices, one per line (empty if none)
list_ttys() {
    compgen -G '/dev/ttyUSB*' || true
    compgen -G '/dev/ttyACM*' || true
}

# Block until a tty that wasn't there before shows up; print its path
wait_for_new_tty() {
    local before now dev
    before="$(list_ttys | sort)"
    while true; do
        now="$(list_ttys | sort)"
        dev="$(comm -13 <(printf '%s\n' "$before") <(printf '%s\n' "$now") | head -n1)"
        if [[ -n "$dev" ]]; then
            udevadm settle 2>/dev/null || sleep 1
            printf '%s\n' "$dev"
            return
        fi
        # Unplugging something resets the baseline so replugging it counts
        before="$now"
        sleep 0.5
    done
}

# Value of one udev property for a device (empty if unset)
udev_prop() {
    udevadm info -q property -n "$1" | sed -n "s/^$2=//p" | head -n1
}

# USB product string of the device's USB parent (empty if it has none)
usb_product_attr() {
    udevadm info -a -n "$1" | sed -n 's/^ *ATTRS{product}=="\(.*\)"$/\1/p' | head -n1
}

# Ask a yes/no question on the terminal; default no
confirm() {
    local answer=""
    read -rp "$1 [y/N] " answer || true
    [[ "$answer" =~ ^[Yy]([Ee][Ss])?$ ]]
}

# Build the rule line for a device and symlink name
make_rule() {
    local dev="$1" link="$2" role="$3"
    local vendor product serial match

    vendor="$(udev_prop "$dev" ID_VENDOR_ID)"
    product="$(udev_prop "$dev" ID_MODEL_ID)"
    serial="$(udev_prop "$dev" ID_SERIAL_SHORT)"

    if [[ -z "$vendor" || -z "$product" ]]; then
        echo "error: $dev has no USB vendor/product id — is it a USB serial device?" >&2
        return 1
    fi

    if [[ -n "$serial" ]]; then
        echo "  $dev: vendor=$vendor product=$product serial=$serial" >&2
        match="SUBSYSTEM==\"tty\", ATTRS{idVendor}==\"$vendor\", ATTRS{idProduct}==\"$product\", ATTRS{serial}==\"$serial\""
    else
        # Clone without a serial number: fall back to vendor/product ids plus
        # the product string, but only if the user accepts that it is not unique
        local name
        name="$(usb_product_attr "$dev")"
        {
            echo "warning: $dev (vendor=$vendor product=$product${name:+ name=\"$name\"}) has no"
            echo "         USB serial number, so it can't be told apart from an identical board."
            echo "         A rule matching only its vendor/product will make ANY Arduino clone"
            echo "         with the same USB chip show up as /dev/$link."
            echo "         (If the other station is the same kind of clone, both symlinks"
            echo "         will point at whichever board is plugged in.)"
        } >&2
        if ! confirm "Add the $role rule anyway?" >&2; then
            {
                echo "No udev rule written. udevadm info for the device:"
                echo
                udevadm info -n "$dev"
            } >&2
            return 1
        fi
        match="SUBSYSTEM==\"tty\", ATTRS{idVendor}==\"$vendor\", ATTRS{idProduct}==\"$product\""
        if [[ -n "$name" ]]; then
            match+=", ATTRS{product}==\"$name\""
        fi
    fi

    # The marker comment above the rule lets a re-run find and replace it
    # (udev doesn't allow comments at the end of a rule line)
    printf '# ultragps:%s\n%s, SYMLINK+="%s", ENV{ID_MM_DEVICE_IGNORE}="1"\n' \
        "$role" "$match" "$link"
}

# Print $RULES_FILE without the role's marker line and the rule line after it
strip_role() {
    awk -v m="# ultragps:$1" '$0 == m { skip = 1; next } skip { skip = 0; next } { print }' \
        "$RULES_FILE"
}

# Re-run the rules on tty devices so symlinks are added or dropped
reload_udev() {
    sudo udevadm control --reload-rules
    sudo udevadm trigger --subsystem-match=tty --action=add
    udevadm settle 2>/dev/null || sleep 1
}

# Replace (or add) the marker + rule lines for a role in $RULES_FILE
install_rule() {
    local role="$1" rule="$2" tmp
    tmp="$(mktemp)"

    if [[ -f "$RULES_FILE" ]]; then
        strip_role "$role" > "$tmp"
    else
        echo "# UltraGPS Arduino symlinks — generated by udev-setup.sh" > "$tmp"
    fi
    printf '%s\n' "$rule" >> "$tmp"

    sudo install -m 0644 "$tmp" "$RULES_FILE"
    rm -f "$tmp"
    reload_udev
}

# Remove the rules for the given roles; delete the file once none are left
remove_rules() {
    local role tmp
    if [[ ! -f "$RULES_FILE" ]]; then
        echo "Nothing to remove: $RULES_FILE does not exist" >&2
        return
    fi

    tmp="$(mktemp)"
    cp "$RULES_FILE" "$tmp"
    for role in "$@"; do
        if grep -qxF "# ultragps:$role" "$tmp"; then
            RULES_FILE="$tmp" strip_role "$role" > "$tmp.new"
            mv "$tmp.new" "$tmp"
            echo "Removed $role rule" >&2
        else
            echo "No $role rule in $RULES_FILE" >&2
        fi
    done

    if ! grep -q '^# ultragps:' "$tmp"; then
        sudo rm -f "$RULES_FILE"
        echo "No rules left; deleted $RULES_FILE" >&2
    else
        sudo install -m 0644 "$tmp" "$RULES_FILE"
    fi
    rm -f "$tmp"
    reload_udev
}

setup_role() {
    local role="$1" link label
    case "$role" in
        base)        link="$BASE_LINK"; label="base station" ;;
        transmitter) link="$TX_LINK";   label="transmitter station" ;;
        *) echo "error: unknown station '$role'" >&2; return 1 ;;
    esac

    echo "Plug in the $label now (unplug and replug if it's already connected)..." >&2
    local dev rule
    dev="$(wait_for_new_tty)"
    echo "Found $dev" >&2
    rule="$(make_rule "$dev" "$link" "$role")"

    if $DRY_RUN; then
        printf '%s\n' "$rule"
        return
    fi

    install_rule "$role" "$rule"
    if [[ -e "/dev/$link" ]]; then
        echo "OK: /dev/$link -> $(readlink -f "/dev/$link")" >&2
    else
        echo "warning: rule installed in $RULES_FILE but /dev/$link did not appear;" \
             "try unplugging and replugging the board" >&2
    fi
}

main() {
    local role="" remove=false
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -n|--dry-run) DRY_RUN=true ;;
            -h|--help)    usage; exit 0 ;;
            remove)       remove=true ;;
            base|transmitter|all) role="$1" ;;
            *) echo "error: unknown argument '$1'" >&2; usage >&2; exit 1 ;;
        esac
        shift
    done

    if $remove; then
        case "${role:-all}" in
            all) remove_rules base transmitter ;;
            *)   remove_rules "$role" ;;
        esac
        return
    fi

    if [[ -z "$role" ]]; then
        read -rp "Which station are you plugging in? [base/transmitter/all] " role
    fi

    case "$role" in
        base|transmitter) setup_role "$role" ;;
        all)              setup_role base; setup_role transmitter ;;
        *) echo "error: expected base, transmitter or all" >&2; exit 1 ;;
    esac
}

main "$@"
