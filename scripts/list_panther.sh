#!/bin/bash
set -e
curl -sL -H "Cookie: devsite_wall_acks=nexus-image-tos" "https://developers.google.com/android/images" \
  | grep -oE 'https://dl\.google\.com/dl/android/aosp/panther-[^"<> ]+\.zip'
