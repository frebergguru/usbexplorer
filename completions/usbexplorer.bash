# bash completion for usbexplorer            -*- shell-script -*-
_usbexplorer()
{
    local cur prev opts
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    opts="--list --tree --json --xml --tui --gui --watch \
          --device --serial --interfaces --compact \
          --udev-rule --dmesg --speed-test --diff \
          --history --history-csv \
          --reset-device --restart-device --port-cycle \
          --autosuspend --wakeup \
          --help --usage --version"

    # Options whose argument we cannot usefully complete: just wait for input.
    case "$prev" in
        --device|--serial|--udev-rule|--dmesg|--speed-test|--diff|\
        --reset-device|--restart-device|--port-cycle|--autosuspend|--wakeup)
            return 0
            ;;
    esac

    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
        return 0
    fi
}
complete -F _usbexplorer usbexplorer
