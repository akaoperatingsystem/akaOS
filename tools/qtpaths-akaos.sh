#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -eq 2 && "$1" == "--query" ]]; then
    query_name="$2"
elif [[ "$#" -eq 1 ]]; then
    query_name="$1"
else
    echo "Usage: qtpaths-akaos.sh --query QT_INSTALL_VARIABLE" >&2
    exit 2
fi

case "$query_name" in
    QT_INSTALL_PREFIX) echo "${AKAOS_QT_INSTALL_PREFIX:-/usr/local}" ;;
    QT_INSTALL_LIBS) echo "${AKAOS_QT_INSTALL_LIBS:-/usr/local/lib}" ;;
    QT_INSTALL_PLUGINS) echo "${AKAOS_QT_INSTALL_PLUGINS:-/usr/local/plugins}" ;;
    QT_INSTALL_BINS) echo "${AKAOS_QT_INSTALL_BINS:-/usr/local/bin}" ;;
    QT_INSTALL_HEADERS) echo "${AKAOS_QT_INSTALL_HEADERS:-/usr/local/include}" ;;
    QT_INSTALL_DATA|QT_INSTALL_ARCHDATA) echo "${AKAOS_QT_INSTALL_DATA:-/usr/local/share}" ;;
    QT_INSTALL_QML) echo "${AKAOS_QT_INSTALL_QML:-/usr/local/qml}" ;;
    QT_INSTALL_TRANSLATIONS) echo "${AKAOS_QT_INSTALL_TRANSLATIONS:-/usr/local/translations}" ;;
    QT_INSTALL_DOCS) echo "${AKAOS_QT_INSTALL_DOCS:-/usr/local/doc}" ;;
    *)
        echo "Unsupported Qt install query: $query_name" >&2
        exit 2
        ;;
esac
