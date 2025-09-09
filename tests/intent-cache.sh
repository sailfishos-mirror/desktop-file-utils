#!/bin/bash

##
## Basic setup
##

set -euo pipefail

[[ -z "${BUILD_ROOT}" ]] && echo "BUILD_ROOT is unset" && exit 1
update_desktop_database="${BUILD_ROOT}/src/update-desktop-database"

TEST_DATA_DIR=`mktemp -d /tmp/test-desktop-file-utils-XXXXXX`
cleanup () {
  rm -rf $TEST_DATA_DIR
}
trap cleanup EXIT

##
## Helpers (Stolen from Flatpak)
##

assert_has_file () {
    { { local BASH_XTRACEFD=3; } 2> /dev/null
    test -f "$1" || (echo 1>&2 "Couldn't find '$1' at $(basename ${BASH_SOURCE[1]}):${BASH_LINENO[0]}"; exit 1)
    } 3> /dev/null
}

assert_file_equals () {
    { { local BASH_XTRACEFD=3; } 2> /dev/null
    if ! [ "$(cat $1)" = "$2" ]; then
        echo 1>&2 "File '$1' doesn't match expected output at $(basename ${BASH_SOURCE[1]}):${BASH_LINENO[0]}"
        echo 1>&2 "$(cat $1)"
        exit 1
    fi
    } 3> /dev/null
}

##
## Test cases
##


###
# Check that it runs and produces some output
mkdir "${TEST_DATA_DIR}/empty"
$update_desktop_database "${TEST_DATA_DIR}/empty"
assert_has_file "${TEST_DATA_DIR}/empty/mimeinfo.cache"
assert_has_file "${TEST_DATA_DIR}/empty/intent.cache"


###
# Check that the correct Intent metadata got extracted to the cache
mkdir "${TEST_DATA_DIR}/intent1"
cat << EOF > "${TEST_DATA_DIR}/intent1/myapp1.desktop"
[Desktop Entry]
Encoding=UTF-8
Version=1.0
Type=Application
Exec=true %f
Name=my app
Implements=org.freedesktop.UriHandler;org.freedesktop.Terminal
[org.freedesktop.UriHandler]
Supports=example.org;
SomeExtraData=foo;bar
EOF

EXPECTED=$(cat <<EOF
[Intent Cache]
org.freedesktop.Terminal=myapp1.desktop;
org.freedesktop.UriHandler=myapp1.desktop;

[org.freedesktop.UriHandler]
example.org=myapp1.desktop;
EOF
)

$update_desktop_database "${TEST_DATA_DIR}/intent1"
assert_file_equals "${TEST_DATA_DIR}/intent1/intent.cache" "${EXPECTED}"


###
# Add another app
cat << EOF > "${TEST_DATA_DIR}/intent1/myapp2.desktop"
[Desktop Entry]
Encoding=UTF-8
Version=1.0
Type=Application
Exec=sleep %f
Name=my app 2
Implements=org.freedesktop.Terminal
EOF

EXPECTED=$(cat <<EOF
[Intent Cache]
org.freedesktop.Terminal=myapp1.desktop;myapp2.desktop;
org.freedesktop.UriHandler=myapp1.desktop;

[org.freedesktop.UriHandler]
example.org=myapp1.desktop;
EOF
)

$update_desktop_database "${TEST_DATA_DIR}/intent1"
assert_file_equals "${TEST_DATA_DIR}/intent1/intent.cache" "${EXPECTED}"