/* SPDX-License-Identifier: AGPL-3.0-only.
 * Equivalent float32 coefficients fitted to recorded native tables.
 * Not original EEPROM floats. Order matches lens_profile_catalog.h.
 */
#ifndef NXL_LENS_CURVE_CALIBRATION_H
#define NXL_LENS_CURVE_CALIBRATION_H
#define LC_CALIBRATION_COUNT 12u
static const uint32_t lc_calibration[LC_CALIBRATION_COUNT][2]={
{0xb9ccad60u,0x352b04d0u},
{0xb96aebf3u,0x35125cdcu},
{0xb8b934a9u,0x34c29888u},
{0xb853376fu,0x349b0595u},
{0xb7a80934u,0x3478f89fu},
{0x36990371u,0x3447424du},
{0x37c4d236u,0x341ebac5u},
{0x381c12aeu,0x33fabdcfu},
{0x385c3652u,0x3395ad31u},
{0x386b6c28u,0x33602ddbu},
{0x38746047u,0x33266a7bu},
{0x38783ebcu,0x330c7eb6u},
};
_Static_assert(LC_CALIBRATION_COUNT==sizeof lp_catalog/sizeof *lp_catalog,"calibration/catalog pairing");
#endif
