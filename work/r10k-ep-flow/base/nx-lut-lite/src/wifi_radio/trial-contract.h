/* Original NX500 1.12 contracts, generated from stock files on PC. */
#define AP_STOCK_BYTES 396764u
#define AP_STOCK_FNV 0x76b05ce9u
#define US_GET_POINT_GUARD_AT 0x70a4u
#define US_VALUE_GUARD_AT 0x73a0u
#define US_ROOT_GOT 0x37330u
#define US_ROOT_VARIABLE 0x37364u
static const unsigned char us_get_point_guard[]={0x50,0x22,0x1f,0xe5,0x02,0x30,0x93,0xe7,0x00,0x00,0x93,0xe5,0x68,0x00,0x80,0xe2,0x10,0x80,0xbd,0xe8};
static const unsigned char us_value_guard[]={0x08,0x40,0x2d,0xe9,0x08,0xf0,0xff,0xeb,0x00,0x00,0x50,0xe3,0x00,0x00,0x90,0x15,0x08,0x80,0xbd,0xe8};
static const char*const trial_actor_paths[]={"/usr/sbin/net-config","/usr/sbin/connmand","/usr/sbin/wpa_supplicant","/usr/bin/mobileap-agent"};
static const struct{const char*path;unsigned size;uint32_t hash;}trial_files[]={
{"/lib/modules/sd8xxx.ko",575964u,0x140ae8edu},
{"/lib/modules/mlan.ko",459812u,0x8d80f7e5u},
{"/lib/modules/cfg80211.ko",208716u,0xa8ccafd9u},
{"/lib/firmware/mrvl/sd8887_uapsta.bin",653112u,0x2de162e8u},
{"/lib/firmware/mrvl/WlanCalData_ext.conf",1720u,0x4de0f592u},
{"/lib/firmware/mrvl/txpwrlimit_cfg.bin",1324u,0xcb543773u},
{"/usr/lib/libuser-spec.so",194760u,0x17a4cc28u},
{"/usr/bin/mobileap-agent",238804u,0x63852bb7u},
{"/usr/sbin/net-config",117492u,0x299d7cf3u},
{"/usr/sbin/connmand",441048u,0x7031833du},
{"/usr/bin/wlan.sh",7916u,0xf753e40du},
{"/usr/bin/iwlist",26532u,0x0d39984eu},
{"/usr/bin/mlanutl",183816u,0x03796f40u},
{"/opt/usr/nx-lut-lite/bin/nxl-wifi-server",169076u,0x985c03e1u},
{NXL_AP_DAEMON,AP_STOCK_BYTES,AP_STOCK_FNV},
};



/* Original ET_EXEC .rel.dyn R_ARM_COPY for _m_us_data_info, size 4. */
#define US_ROOT_APP_COPY 0x0049d0f8u
#define US_ROOT_INIT_AT 0x4774u
#define US_RTOS_MAP_AT 0x106b4u
static const unsigned char us_root_init_guard[]={0x0b,0xfb,0xff,0xeb,0x50,0x31,0x9f,0xe5,0x03,0x50,0x98,0xe7,0x01,0x07,0x80,0xe2,0x00,0x00,0x85,0xe5};
static const unsigned char us_rtos_map_guard[]={0x00,0x10,0xa0,0xe3,0x02,0x27,0xa0,0xe3,0x40,0x15,0x48,0xe3,0x06,0x30,0xa0,0xe1};

/* RC610 radio data identity */
#define RADIO_MODULE_BYTES 208780u
#define RADIO_MODULE_FNV 0x546a33c5u
#define RADIO_MODULE_SHA "2c8ef0dd538d667af281312a8f6f2241de566bde59b97759f51f177c788789fe"
