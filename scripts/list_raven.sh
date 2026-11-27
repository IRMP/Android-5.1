#!/bin/bash
set -e
curl -sL -H "Cookie: devsite_wall_acks=nexus-image-tos" "https://developers.google.com/android/images" \
  | grep -oE 'https://dl\.google\.com/dl/android/aosp/raven-[^"<> ]+\.zip' \
  | sort -u | grep -E "sd1a|sq|sp2a|tp1a|tq3a"
