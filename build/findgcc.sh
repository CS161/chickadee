#! /bin/sh

# Look for gcc-7 or gcc-6.

ver1 () {
    ( $1 -dumpversion 2>/dev/null || echo 0 ) | sed 's/[^0-9].*//'
}

prog="$1"
if "$prog" -v 2>&1 | grep '^gcc' >/dev/null; then
    base=`echo "$prog" | sed 's/^\([^g]\)/g\1/'`
    v=`ver1 "$prog"`
    if [ $v -lt 7 ] && [ `ver1 "$base-7"` -ge 7 ]; then
        echo "$base-7"; exit
    elif [ $v -lt 6 ] && [ `ver1 "$base-6"` -ge 6 ]; then
        echo "$base-6"; exit
    fi
fi
echo "$prog"
