if [ "$1" = start ]; then
    export UI_GPU_ENABLED=0
    UI_GPU_REQUESTED=$(/usr/local/bin/extended-config.py get /oem/printer_data/config/extended/extended2.cfg components mali_gpu false)
    if [ "$UI_GPU_REQUESTED" = true ] && [ -c /dev/mali0 ] &&
       [ "$(cat /sys/class/devfreq/ff320000.gpu/max_freq 2>/dev/null)" = 400000000 ]; then
        export UI_GPU_ENABLED=1
    fi
    unset UI_GPU_REQUESTED
fi
