# Personal profile: retain the original screen and load its version-checked extension.
if [ "$1" = start ]; then
    STOCK_CFG=/oem/printer_data/config/extended/extended2.cfg
    STOCK_SELECTED=$(/usr/local/bin/extended-config.py get "$STOCK_CFG" components gui snapmaker)
    if [ "$STOCK_SELECTED" != snapmaker ]; then
        /usr/local/bin/extended-config.py add "$STOCK_CFG" components gui snapmaker
    fi
    STOCK_ENABLED=$(/usr/local/bin/extended-config.py get "$STOCK_CFG" components stock_preview true)
    if [ "$STOCK_ENABLED" = true ] && [ -e /dev/mali0 ]; then
        export LD_PRELOAD="/usr/local/lib/libstock-ui.so:$LD_PRELOAD"
    fi
    unset STOCK_CFG STOCK_SELECTED STOCK_ENABLED
fi
