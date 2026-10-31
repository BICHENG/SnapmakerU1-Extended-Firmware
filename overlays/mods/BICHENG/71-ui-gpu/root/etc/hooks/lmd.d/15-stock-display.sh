if [ "$1" = start ]; then
    STOCK_DISPLAY_GUI=$(/usr/local/bin/extended-config.py get /oem/printer_data/config/extended/extended2.cfg components gui snapmaker)
    if [ "$STOCK_DISPLAY_GUI" = snapmaker ] && [ -f /usr/local/lib/libstock-display.so ]; then
        case ":${LD_PRELOAD:-}:" in
            *:/usr/local/lib/libstock-display.so:*) ;;
            *) export LD_PRELOAD="/usr/local/lib/libstock-display.so${LD_PRELOAD:+:$LD_PRELOAD}" ;;
        esac
    fi
    unset STOCK_DISPLAY_GUI
fi
