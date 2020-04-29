#!/usr/bin/env bash

git log --no-color -M -C --name-status \
    > "${MESON_DIST_ROOT}/ChangeLog"
