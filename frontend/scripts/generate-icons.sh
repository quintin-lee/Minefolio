#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
FRONTEND_DIR="$ROOT_DIR/frontend"
SVG_SRC="$FRONTEND_DIR/public/favicon.svg"
RES_DIR="$FRONTEND_DIR/android/app/src/main/res"

MODE="${1:-all}"
TMP_DIR="$(mktemp -d /tmp/mf-icons-XXXXXX)"
trap 'rm -rf "$TMP_DIR"' EXIT

echo "==> Using SVG source: $SVG_SRC"

generate_web() {
  echo "==> Generating Web & Favicon assets..."
  
  # Standard favicon PNGs
  rsvg-convert -w 16 -h 16 "$SVG_SRC" -o "$FRONTEND_DIR/public/favicon-16x16.png"
  rsvg-convert -w 32 -h 32 "$SVG_SRC" -o "$FRONTEND_DIR/public/favicon-32x32.png"
  
  # Apple Touch Icon (180x180)
  rsvg-convert -w 180 -h 180 "$SVG_SRC" -o "$FRONTEND_DIR/public/apple-touch-icon.png"
  
  # PWA / Chrome Icons (192x192, 512x512)
  rsvg-convert -w 192 -h 192 "$SVG_SRC" -o "$FRONTEND_DIR/public/android-chrome-192x192.png"
  rsvg-convert -w 512 -h 512 "$SVG_SRC" -o "$FRONTEND_DIR/public/android-chrome-512x512.png"
  
  # Multi-resolution favicon.ico (16, 32, 48)
  rsvg-convert -w 16 -h 16 "$SVG_SRC" -o "$TMP_DIR/ico-16.png"
  rsvg-convert -w 32 -h 32 "$SVG_SRC" -o "$TMP_DIR/ico-32.png"
  rsvg-convert -w 48 -h 48 "$SVG_SRC" -o "$TMP_DIR/ico-48.png"
  magick "$TMP_DIR/ico-16.png" "$TMP_DIR/ico-32.png" "$TMP_DIR/ico-48.png" "$FRONTEND_DIR/public/favicon.ico"
  
  echo "✓ Web assets generated successfully in frontend/public/"
}

generate_android() {
  echo "==> Generating Android mipmap and splash assets..."
  
  # Densities: name:launcher_size:fg_size
  DENSITIES=(
    "mdpi:48:108"
    "hdpi:72:162"
    "xhdpi:96:216"
    "xxhdpi:144:324"
    "xxxhdpi:192:432"
  )
  
  for entry in "${DENSITIES[@]}"; do
    IFS=":" read -r density l_size fg_size <<< "$entry"
    target_dir="$RES_DIR/mipmap-$density"
    mkdir -p "$target_dir"
    
    # 1. Standard square launcher icon
    rsvg-convert -w "$l_size" -h "$l_size" "$SVG_SRC" -o "$target_dir/ic_launcher.png"
    
    # 2. Circular launcher icon (ic_launcher_round.png)
    radius=$((l_size / 2))
    center=$radius
    magick "$target_dir/ic_launcher.png" \
      \( -size "${l_size}x${l_size}" xc:none -fill white -draw "circle $center,$center $center,1" \) \
      -alpha set -compose DstIn -composite "$target_dir/ic_launcher_round.png"
      
    # 3. Adaptive foreground icon (ic_launcher_foreground.png)
    # Safe zone for adaptive icons is center 66.7% of the 108dp canvas (i.e. ~72dp inner safe zone)
    inner_size=$((fg_size * 72 / 108))
    rsvg-convert -w "$inner_size" -h "$inner_size" "$SVG_SRC" -o "$TMP_DIR/fg_inner_${density}.png"
    magick -size "${fg_size}x${fg_size}" xc:transparent \
      "$TMP_DIR/fg_inner_${density}.png" -gravity center -composite \
      "$target_dir/ic_launcher_foreground.png"
      
    echo "  ✓ Generated mipmap-$density (launcher: ${l_size}px, fg: ${fg_size}px)"
  done
  
  # 4. Splash screens (centered logo on #060B18)
  # Format: folder:width:height
  SPLASH_SPECS=(
    "drawable:480:320"
    "drawable-land-mdpi:480:320"
    "drawable-land-hdpi:800:480"
    "drawable-land-xhdpi:1280:720"
    "drawable-land-xxhdpi:1600:960"
    "drawable-land-xxxhdpi:1920:1280"
    "drawable-port-mdpi:320:480"
    "drawable-port-hdpi:480:800"
    "drawable-port-xhdpi:720:1280"
    "drawable-port-xxhdpi:960:1600"
    "drawable-port-xxxhdpi:1280:1920"
  )
  
  for s_entry in "${SPLASH_SPECS[@]}"; do
    IFS=":" read -r folder width height <<< "$s_entry"
    s_dir="$RES_DIR/$folder"
    mkdir -p "$s_dir"
    
    # Logo size ~ min(width, height) * 0.35
    min_dim=$((width < height ? width : height))
    logo_sz=$((min_dim * 35 / 100))
    if [ "$logo_sz" -lt 64 ]; then logo_sz=64; fi
    
    rsvg-convert -w "$logo_sz" -h "$logo_sz" "$SVG_SRC" -o "$TMP_DIR/splash_logo_${folder}.png"
    magick -size "${width}x${height}" xc:"#060B18" \
      "$TMP_DIR/splash_logo_${folder}.png" -gravity center -composite \
      "$s_dir/splash.png"
    echo "  ✓ Generated $folder/splash.png (${width}x${height})"
  done
  
  echo "✓ Android assets generated successfully in frontend/android/app/src/main/res/"
}

if [ "$MODE" = "web" ]; then
  generate_web
elif [ "$MODE" = "android" ]; then
  generate_android
elif [ "$MODE" = "all" ]; then
  generate_web
  generate_android
else
  echo "Unknown mode: $MODE (use: web, android, all)"
  exit 1
fi
