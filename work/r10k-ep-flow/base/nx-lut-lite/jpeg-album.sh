#!/bin/sh
# CPU-only preview generation and single final publication after native return.
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OPS=$D/bin/nxl-fileops
META=$D/bin/nxl-jpegmeta.arm
REGISTER=$D/bin/nxl-album-register.arm
DISPATCH=$D/bin/album-dispatch-rc682.so
ALBUM_REGISTER_SHA=83d5f13776f7a741be01071e3e7c8f4a00c2625b8e08f2501765170a57848389
ALBUM_DISPATCH_SHA=8ac7fd8c2b8fd908c9a2189f33177cb681e87fa72782ed5e6e6ecb3d59d7e0c1
OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
JPEG_META_SHA=4f0f265a99d4ce0b1a4e161ef12e99e3f3f4667f54c3211fd831911e1b343943
DJPEG_SHA=edc93d2d7f6ad5fba65ae43994bb3e374a058316abb7db4917ea01b186b3e919
CJPEG_SHA=1c08929a9db3c16990aac45cfd2a8c58077c540e004ee6780f5e01258879a120
RUN=/tmp/nx-lut-lite
scratch= cpu_child= phase=IDENTITY
cleanup(){
 if [ -n "$cpu_child" ];then kill -TERM "$cpu_child" 2>/dev/null || :;wait "$cpu_child" 2>/dev/null || :;fi
 if [ -n "$scratch" ];then rm -f "$scratch/scaled.ppm" "$scratch/thumbnail.ppm" "$scratch/screen.ppm" "$scratch/thumbnail.jpg" "$scratch/screen.jpg";rmdir "$scratch" 2>/dev/null || :;fi
}
trap cleanup 0
trap 'exit 1' HUP INT TERM
cpu_run(){ "$@" & cpu_child=$!;cpu_rc=0;wait "$cpu_child" || cpu_rc=$?;cpu_child=;return "$cpu_rc"; }
fail(){ printf 'JPEG_ALBUM_FAILED phase=%s reason=%s\n' "$phase" "$1" >&2;exit 1; }
for pair in "$OPS:$OPS_SHA" "$META:$JPEG_META_SHA" "/usr/bin/djpeg:$DJPEG_SHA" "/usr/bin/cjpeg:$CJPEG_SHA";do
 file=${pair%:*};want=${pair##*:};[ -f "$file" ] && [ ! -L "$file" ] && [ "$("$OPS" sha256 "$file")" = "$want" ] || fail COMPONENT_IDENTITY_FAILED
done
[ "$#" = 3 ] || fail ARGUMENTS
source=$1;expected_source=$2;report=$3
[ ! -e "$RUN/auto-init-held" ] && [ ! -L "$RUN/auto-init-held" ] || fail NATIVE_OPERATION_HELD_RESTART_REQUIRED
if [ ! -e "$report/album-final.json" ] && [ ! -L "$report/album-final.json" ];then
 phase=PREVIEWS
 [ -d "$RUN" ] && [ ! -L "$RUN" ] || fail RUNTIME_DIRECTORY
 scratch=$RUN/jpeg-album.$$
 mkdir "$scratch" || fail SCRATCH_NOT_NEW
 image=$("$META" native-path "$report" "$expected_source") || fail NATIVE_RECEIPT_INVALID
 [ -f "$image" ] && [ ! -L "$image" ] || fail PRIVATE_NATIVE_JPEG_UNAVAILABLE
 # Decode only once; both previews come from this newly rendered image.
 cpu_run /usr/bin/djpeg -scale 1/4 -pnm -outfile "$scratch/scaled.ppm" "$image" >> "$report/album-codec.log" 2>&1 || fail CPU_DECODE
 cpu_run "$META" previews-ppm "$scratch/scaled.ppm" "$scratch/thumbnail.ppm" "$scratch/screen.ppm" >> "$report/album-codec.log" 2>&1 || fail CPU_PREVIEWS
 cpu_run /usr/bin/cjpeg -quality 85 -sample 2x1 -outfile "$scratch/thumbnail.jpg" "$scratch/thumbnail.ppm" >> "$report/album-codec.log" 2>&1 || fail CPU_THUMB_ENCODE
 cpu_run /usr/bin/cjpeg -quality 85 -sample 2x1 -outfile "$scratch/screen.jpg" "$scratch/screen.ppm" >> "$report/album-codec.log" 2>&1 || fail CPU_SCREEN_ENCODE
fi
phase=FINALIZE
cpu_run "$META" finalize2 "$source" "$report" "${scratch:-/nonexistent}/thumbnail.jpg" "${scratch:-/nonexistent}/screen.jpg" "$expected_source" || fail COMPLETE_FILE_PUBLICATION_UNCONFIRMED
# JPEG remains saved even if optional live registration is safely deferred.
phase=ALBUM_REGISTER
if [ -f "$REGISTER" ] && [ ! -L "$REGISTER" ] && [ -f "$DISPATCH" ] && [ ! -L "$DISPATCH" ] && [ "$("$OPS" sha256 "$REGISTER")" = "$ALBUM_REGISTER_SHA" ] && [ "$("$OPS" sha256 "$DISPATCH")" = "$ALBUM_DISPATCH_SHA" ];then
 registration_rc=0
 cpu_run "$REGISTER" "$report" || registration_rc=$?
 printf 'JPEG_ALBUM_REGISTRATION helper_exit=%s JPEG_retained=1\n' "$registration_rc"
else
 printf 'JPEG_ALBUM_REGISTRATION component_identity_failed=1 deferred=1 JPEG_retained=1\n'
fi
# Unknown command returns still require restart, just as RC6.8.1 protects.
[ ! -e "$RUN/auto-init-held" ] && [ ! -L "$RUN/auto-init-held" ] || fail NATIVE_OPERATION_HELD_RESTART_REQUIRED
printf 'JPEG_ALBUM_SAVED single_item_registration=ATTEMPTED full_rescan=0 consult_register_result=1\n'
echo 'OK JPEG_ALBUM_READY FRESH_320_THUMB_AND_1616_MPF PRIVATE_UNTIL_COMPLETE'
