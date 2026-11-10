/* Generated from the verified NX5001.12 OEM images. */
#ifndef NXL_VIDEO_COUNTER_PROFILE_H
#define NXL_VIDEO_COUNTER_PROFILE_H
#include <stdint.h>
struct vp_library {const char*name;unsigned bytes;const char*sha256;};
static const struct vp_library vp_libraries[]={
 {"libmmfmodules.so",315716u,"e40d3c7fdbab8ea7f67d0e876468c2a31fa8572d602d969d32348f3fd0f45de7"},
 {"libmmfcore.so",340268u,"b3e7cd2987282d856a8f95b8768d44ea2f4f3804684a391de0567c10dada73c8"},
 {"libmmfosal.so",15744u,"66ac8ea0c678a2a8f844bce9f23794e8f76ab95b205354d234072f8b7a00ea3c"},
};
struct vp_rom_guard {uint32_t address,words[8];};
static const struct vp_rom_guard vp_rom_guards[]={
 {0x80023c20u,{0xe59fc86cu,0xe79c2003u,0xe2822001u,0xe78c2003u,0xe59fc860u,0xe79cc003u,0xe12fff3cu,0xe59fc858u}},
 {0x80013494u,{0xe59f0238u,0xebfffa74u,0xe59f6234u,0xe59f5234u,0xe3a04020u,0xe5b62004u,0xe1a01004u,0xe3520000u}},
 {0x801b92b4u,{0xe59f0200u,0xebf962ecu,0xe59f51fcu,0xe3a04005u,0xe5951000u,0xe59f01f4u,0xebf962e7u,0xe59f01bcu}},
 {0x801d9660u,{0xe3a00001u,0xe5933000u,0xe3130401u,0x1a000012u,0xe59f10c4u,0xebfe8b5fu,0xe2505000u,0xba000020u}},
 {0x8049a4b0u,{0xe92d45f8u,0xe2515000u,0x0a0000bbu,0xe1d531d0u,0xe3530000u,0xba0000b2u,0xe31000fdu,0x0a000008u}},
 {0x804b6894u,{0xe92d4ff0u,0xe6ef3070u,0xe3530014u,0xe24dd044u,0xe58d301cu,0x93a03000u,0x83a03001u,0xe3a05000u}},
 {0x804b6b98u,{0xe92d4ff0u,0xe6ef3070u,0xe3530014u,0xe24dd03cu,0xe58d3004u,0x93a03000u,0x83a03001u,0xe3a05000u}},
 {0x8003677cu,{0xe3a02000u,0xe3422081u,0xe5921468u,0xe3a00001u,0xe1813310u,0xe3a00000u,0xe5823468u,0xe12fff1eu}},
 {0x80036890u,{0xe3a02000u,0xe3422081u,0xe5921468u,0xe3a00001u,0xe1c11310u,0xe5821468u,0xe59f200cu,0xe3a01000u}},
 {0x80036bc0u,{0xe3a05000u,0xe3425081u,0xe5953418u,0xe3a06000u,0xe58d3004u,0xe58d6000u,0xe59d3000u,0xe353001eu}},
};
#endif
