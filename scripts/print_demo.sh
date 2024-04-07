#!/bin/bash

log_message() {
    local level="$1"    local message="$2"
    local timestamp=$(date +"%Y-%m-%d %H:%M:%S")
    case "$level" in
        "INFO")
            color="2"    # Green
            ;;
        "WARNING")
            color="3"   # Yellow
            ;;
        "ERROR")
            color="1"   # Red
            ;;
        *)
            color="0"   # White (default color)
            ;;
    esac

    # Print the log message with the specified color and timestamp
    printf "\033[%sm%-8s \033[0m%s\n" "38;5;${color}" "[${timestamp}] [${level}]" "$message"
}

# Example usage:
log_message "INFO" "This is an informational message."
log_message "WARNING" "This is a warning message."
log_message "ERROR" "This is an error message."