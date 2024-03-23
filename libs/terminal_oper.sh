#!/usr/bin/bash


# Get size (lines, columns) in chars
get_tty_cellsize() {
# TODO
#    IFS=x read -r cols lines <<< $(tmux display -p -t 0 '#{pane_width}x#{pane_height}')
#    echo $lines $cols
    char_width=8
    char_height=16
    echo $char_width $char_height
}

# Get left-top offset of current terminal (pane).
# Usually 0 0 but in tmux it should be adjusted for pane.
get_tty_offset() {
    if [ -n "${TMUX}" ] && [ "${TERM_PROGRAM}" == "tmux" ]; then
        # inside tmux
        tmux display -p "#{pane_left} #{pane_top}" 
    else
        # somewhere else, hopefully without multiplex
        echo 0 0
    fi
}

# Get size in pixels
get_screen_size() {
    total_size=/sys/class/graphics/fb0/virtual_size
    IFS=, read -r width height <<< $(cat $total_size)
    echo $width $height
}

# Get cursor position relative to pane
get_cursor() {
    oldstty=$(stty -g)
    stty raw -echo min 0
    printf '\033[6n' > /dev/tty
    IFS='[;' read -d R -rs _ y x _
    stty $oldstty
    #offset=($(get_tty_offset))
    #x=$((x + offset[0]))
    #y=$((y + offset[1]))

    printf '%s\n' "$x $y"
}

# Sets cursor to <$1> column and <$2> line index
# relative to pane (e.g. tmux starts from (0, 0) for each pane)
set_cursor() {
    natnum="^[0-9]+$"
    error=false
    if [[ $# -ne 2 ]]; then
        echo "Required 2 arguments, got $#" >&2
        error=true
    elif ! [[ $1 =~ $natnum ]] || ! [[ $2 =~ $natnum ]]; then
        echo "Invalid arguments: $1 $2" >&2
        error=true
    fi
    if $error; then
        echo "Usage: terminal_oper set_cursor <col> <line>" >&2
        echo "Set cursor position to <col> column and <line> line index." >&2
        exit 1
    fi

    col=$1
    line=$2
    
    tput cup $line $col
}

if declare -f "$1" > /dev/null
then
    "$@"
else
    echo "Usage:" >&2
    echo "  terminal_oper (get_tty_cellsize | get_tty_offset | get_screen_size | get_cursor)" >&2
    echo "  terminal_oper set_cursor <col> <line>" >&2
    echo "Error: '$1' command is not found"
fi
