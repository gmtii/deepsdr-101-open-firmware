#include "spectrum.h"
#include "gfx.h"
#include <string.h> /* memcpy() - see build_lut()'s comment for the six palettes copied straight from a 256-entry source table */

/* --- palette LUT ---------------------------------------------------- */

static uint16_t s_lut[256];
static uint8_t  s_lut_ready = 0;
static spectrum_palette_t s_palette = SPECTRUM_PALETTE_CLASSIC;

/*
 * *** 08/09/2026 - exact palette data from SDR++'s own
 * (AlexandreRouma/SDRPlusPlus) root/res/colormaps/ directory ***, per
 * the project owner - a first pass at this feature approximated
 * several of these from general knowledge/memory (see git history);
 * the project owner then uploaded the actual JSON files, so
 * everything below is now byte-for-byte from those files (hex ->
 * RGB565/RGB8 conversion only), not an approximation. FIRE is the one
 * remaining exception - it's this project's own invention, not part
 * of SDR++'s set.
 *
 * Two different representations depending on how many stops each
 * source file has:
 *   - Six of SDR++'s maps (GQRX/INFERNO/MAGMA/PLASMA/TURBO/VIRIDIS)
 *     already ship as exactly 256 entries - i.e. already ONE color
 *     per LUT index, no interpolation needed or wanted (interpolating
 *     an already-256-entry map would just be a lossy roundtrip
 *     through the same number of points). These are stored as
 *     ready-to-copy uint16_t[256] RGB565 tables (k_lut_*) and
 *     build_lut() below just memcpy()s the right one straight into
 *     s_lut - cheaper AND more accurate than the general path.
 *   - Everything else (far fewer stops - 2 to 23) uses the existing
 *     palette_lerp_stops() N-stop linear interpolator, same as this
 *     feature's first pass already did for its approximations.
 */

/* GQRX - exact 256-entry LUT, converted from SDR++'s own
 * root/res/colormaps/gqrx.json (author: csete), uploaded
 * by the project owner 08/09/2026 - RGB565, one entry per LUT index,
 * no interpolation needed since this already has exactly 256 stops. */
static const uint16_t k_lut_gqrx[256] = {
    0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U,
    0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0000U, 0x0001U,
    0x0001U, 0x0001U, 0x0002U, 0x0002U, 0x0002U, 0x0003U, 0x0003U, 0x0003U, 0x0004U, 0x0004U, 0x0004U, 0x0005U,
    0x0005U, 0x0005U, 0x0006U, 0x0006U, 0x0007U, 0x0007U, 0x0007U, 0x0008U, 0x0008U, 0x0008U, 0x0009U, 0x0009U,
    0x0009U, 0x000AU, 0x000AU, 0x000AU, 0x000BU, 0x000BU, 0x000BU, 0x000CU, 0x000CU, 0x000CU, 0x000DU, 0x000DU,
    0x000EU, 0x000EU, 0x000EU, 0x000FU, 0x000FU, 0x000FU, 0x0010U, 0x0010U, 0x0010U, 0x0011U, 0x0011U, 0x0031U,
    0x0052U, 0x0072U, 0x0893U, 0x08B3U, 0x08D4U, 0x08F4U, 0x1115U, 0x1135U, 0x1156U, 0x1176U, 0x1997U, 0x19B7U,
    0x19D8U, 0x19F8U, 0x2219U, 0x2239U, 0x225AU, 0x227AU, 0x2A9BU, 0x2ABBU, 0x2ADCU, 0x2AFCU, 0x333DU, 0x335DU,
    0x337DU, 0x339EU, 0x3BBEU, 0x3BDFU, 0x3BFFU, 0x3BFFU, 0x441EU, 0x443EU, 0x4C3DU, 0x4C5CU, 0x547CU, 0x547BU,
    0x5C9AU, 0x5CBAU, 0x64B9U, 0x64D8U, 0x6CF8U, 0x6CF7U, 0x7517U, 0x7536U, 0x7D35U, 0x7D55U, 0x8554U, 0x8573U,
    0x8D93U, 0x8D92U, 0x95B1U, 0x95D1U, 0x9DD0U, 0x9DF0U, 0xA60FU, 0xA60EU, 0xAE2EU, 0xAE4DU, 0xB64CU, 0xB66CU,
    0xBE8BU, 0xBE8AU, 0xC6AAU, 0xC6C9U, 0xCEC9U, 0xCEE8U, 0xD6E7U, 0xD707U, 0xDF26U, 0xDF25U, 0xDF45U, 0xE764U,
    0xE763U, 0xEF83U, 0xEFA2U, 0xF7A2U, 0xF7C1U, 0xFFE0U, 0xFFE0U, 0xFFE0U, 0xFFC0U, 0xFFC0U, 0xFFA0U, 0xFF80U,
    0xFF80U, 0xFF60U, 0xFF40U, 0xFF40U, 0xFF20U, 0xFF00U, 0xFF00U, 0xFEE0U, 0xFEE0U, 0xFEC0U, 0xFEA0U, 0xFEA0U,
    0xFE80U, 0xFE60U, 0xFE60U, 0xFE40U, 0xFE20U, 0xFE20U, 0xFE00U, 0xFE00U, 0xFDE0U, 0xFDC0U, 0xFDC0U, 0xFDA0U,
    0xFD80U, 0xFD80U, 0xFD60U, 0xFD40U, 0xFD40U, 0xFD20U, 0xFD20U, 0xFD00U, 0xFCE0U, 0xFCE0U, 0xFCC0U, 0xFCA0U,
    0xFCA0U, 0xFC80U, 0xFC60U, 0xFC60U, 0xFC40U, 0xFC40U, 0xFC20U, 0xFC00U, 0xFC00U, 0xFBE0U, 0xFBC0U, 0xFBC0U,
    0xFBA0U, 0xFB80U, 0xFB80U, 0xFB60U, 0xFB60U, 0xFB40U, 0xFB20U, 0xFB20U, 0xFB00U, 0xFAE0U, 0xFAE0U, 0xFAC0U,
    0xFAA0U, 0xFAA0U, 0xFA80U, 0xFA80U, 0xFA60U, 0xFA40U, 0xFA40U, 0xFA20U, 0xFA00U, 0xFA00U, 0xF9E0U, 0xF9C0U,
    0xF9C0U, 0xF9A0U, 0xF980U, 0xF980U, 0xF960U, 0xF960U, 0xF940U, 0xF920U, 0xF920U, 0xF900U, 0xF8E0U, 0xF8E0U,
    0xF8C0U, 0xF8A0U, 0xF8A0U, 0xF880U, 0xF880U, 0xF860U, 0xF840U, 0xF840U, 0xF820U, 0xF800U, 0xF800U, 0xF986U,
    0xFB2CU, 0xFCD3U, 0xFE79U, 0xFFFFU,
};

/* Inferno - exact 256-entry LUT, converted from SDR++'s own
 * root/res/colormaps/inferno.json (author: B.I.D.S.), uploaded
 * by the project owner 08/09/2026 - RGB565, one entry per LUT index,
 * no interpolation needed since this already has exactly 256 stops. */
static const uint16_t k_lut_inferno[256] = {
    0x0000U, 0x0000U, 0x0000U, 0x0001U, 0x0001U, 0x0001U, 0x0001U, 0x0002U, 0x0002U, 0x0002U, 0x0022U, 0x0023U,
    0x0023U, 0x0823U, 0x0823U, 0x0824U, 0x0824U, 0x0844U, 0x0845U, 0x0845U, 0x1045U, 0x1046U, 0x1046U, 0x1046U,
    0x1046U, 0x1047U, 0x1867U, 0x1867U, 0x1868U, 0x1868U, 0x1868U, 0x1869U, 0x2069U, 0x2069U, 0x2069U, 0x206AU,
    0x284AU, 0x284AU, 0x284AU, 0x284BU, 0x284BU, 0x304BU, 0x304BU, 0x304BU, 0x304CU, 0x384CU, 0x384CU, 0x384CU,
    0x384CU, 0x384CU, 0x404CU, 0x404DU, 0x404DU, 0x404DU, 0x404DU, 0x484DU, 0x486DU, 0x486DU, 0x486DU, 0x486DU,
    0x506DU, 0x506DU, 0x506DU, 0x506DU, 0x508DU, 0x588DU, 0x588DU, 0x588DU, 0x588DU, 0x588DU, 0x608DU, 0x60ADU,
    0x60ADU, 0x60ADU, 0x60ADU, 0x68ADU, 0x68ADU, 0x68CDU, 0x68CDU, 0x68CDU, 0x70CDU, 0x70CDU, 0x70CDU, 0x70CDU,
    0x70EDU, 0x78EDU, 0x78EDU, 0x78EDU, 0x78EDU, 0x78EDU, 0x80EDU, 0x810DU, 0x810DU, 0x810DU, 0x810DU, 0x890DU,
    0x890DU, 0x890DU, 0x890DU, 0x892DU, 0x912DU, 0x912DU, 0x912CU, 0x912CU, 0x912CU, 0x992CU, 0x994CU, 0x994CU,
    0x994CU, 0x994CU, 0xA14CU, 0xA14CU, 0xA16CU, 0xA16CU, 0xA16CU, 0xA96BU, 0xA96BU, 0xA96BU, 0xA98BU, 0xA98BU,
    0xB18BU, 0xB18BU, 0xB18BU, 0xB18BU, 0xB1ABU, 0xB1AAU, 0xB9AAU, 0xB9AAU, 0xB9AAU, 0xB9CAU, 0xB9CAU, 0xC1CAU,
    0xC1CAU, 0xC1C9U, 0xC1E9U, 0xC1E9U, 0xC1E9U, 0xC9E9U, 0xCA09U, 0xCA09U, 0xCA09U, 0xCA08U, 0xCA28U, 0xD228U,
    0xD228U, 0xD228U, 0xD248U, 0xD248U, 0xD247U, 0xDA67U, 0xDA67U, 0xDA67U, 0xDA87U, 0xDA87U, 0xDA87U, 0xDA86U,
    0xE2A6U, 0xE2A6U, 0xE2A6U, 0xE2C6U, 0xE2C6U, 0xE2E6U, 0xE2E5U, 0xE2E5U, 0xEB05U, 0xEB05U, 0xEB05U, 0xEB25U,
    0xEB25U, 0xEB24U, 0xEB44U, 0xEB44U, 0xEB64U, 0xEB64U, 0xF364U, 0xF383U, 0xF383U, 0xF3A3U, 0xF3A3U, 0xF3C3U,
    0xF3C3U, 0xF3C2U, 0xF3E2U, 0xF3E2U, 0xF402U, 0xF402U, 0xF422U, 0xFC21U, 0xFC21U, 0xFC41U, 0xFC41U, 0xFC61U,
    0xFC61U, 0xFC81U, 0xFC80U, 0xFCA0U, 0xFCA0U, 0xFCA0U, 0xFCC0U, 0xFCC0U, 0xFCE0U, 0xFCE0U, 0xFD01U, 0xFD01U,
    0xFD21U, 0xFD21U, 0xFD41U, 0xFD41U, 0xFD62U, 0xFD62U, 0xFD82U, 0xFD82U, 0xFDA3U, 0xFDA3U, 0xFDC3U, 0xFDC3U,
    0xFDE4U, 0xFDE4U, 0xFE04U, 0xFE05U, 0xFE25U, 0xFE25U, 0xFE25U, 0xFE46U, 0xFE46U, 0xFE66U, 0xFE67U, 0xF687U,
    0xF688U, 0xF6A8U, 0xF6A8U, 0xF6C9U, 0xF6C9U, 0xF6E9U, 0xF6EAU, 0xF70AU, 0xF70BU, 0xF72BU, 0xF72CU, 0xF74CU,
    0xF74DU, 0xF76DU, 0xF76EU, 0xF76EU, 0xF78FU, 0xF78FU, 0xF7B0U, 0xF7B0U, 0xF7B1U, 0xF7D1U, 0xF7D2U, 0xF7D2U,
    0xFFD3U, 0xFFF3U, 0xFFF4U, 0xFFF4U,
};

/* Magma - exact 256-entry LUT, converted from SDR++'s own
 * root/res/colormaps/magma.json (author: B.I.D.S.), uploaded
 * by the project owner 08/09/2026 - RGB565, one entry per LUT index,
 * no interpolation needed since this already has exactly 256 stops. */
static const uint16_t k_lut_magma[256] = {
    0x0000U, 0x0000U, 0x0000U, 0x0001U, 0x0001U, 0x0001U, 0x0001U, 0x0001U, 0x0002U, 0x0022U, 0x0022U, 0x0023U,
    0x0023U, 0x0023U, 0x0823U, 0x0824U, 0x0844U, 0x0844U, 0x0844U, 0x0845U, 0x0845U, 0x1045U, 0x1065U, 0x1066U,
    0x1066U, 0x1066U, 0x1067U, 0x1067U, 0x1867U, 0x1887U, 0x1888U, 0x1888U, 0x1888U, 0x1889U, 0x2089U, 0x2089U,
    0x208AU, 0x208AU, 0x208AU, 0x208BU, 0x288BU, 0x288BU, 0x288BU, 0x288CU, 0x288CU, 0x308CU, 0x308CU, 0x308DU,
    0x308DU, 0x388DU, 0x386DU, 0x386EU, 0x386EU, 0x386EU, 0x406EU, 0x406EU, 0x406EU, 0x408EU, 0x408FU, 0x488FU,
    0x488FU, 0x488FU, 0x488FU, 0x488FU, 0x508FU, 0x508FU, 0x508FU, 0x50AFU, 0x50AFU, 0x58AFU, 0x58AFU, 0x58AFU,
    0x58AFU, 0x58CFU, 0x60D0U, 0x60D0U, 0x60D0U, 0x60D0U, 0x60D0U, 0x68F0U, 0x68F0U, 0x68F0U, 0x68F0U, 0x68F0U,
    0x70F0U, 0x70F0U, 0x7110U, 0x7110U, 0x7110U, 0x7910U, 0x7910U, 0x7910U, 0x7910U, 0x7930U, 0x8130U, 0x8130U,
    0x8130U, 0x8130U, 0x8130U, 0x8930U, 0x8950U, 0x8950U, 0x8950U, 0x8950U, 0x9150U, 0x9150U, 0x9150U, 0x9170U,
    0x9170U, 0x9970U, 0x9970U, 0x996FU, 0x996FU, 0x996FU, 0xA16FU, 0xA18FU, 0xA18FU, 0xA18FU, 0xA18FU, 0xA98FU,
    0xA98FU, 0xA98FU, 0xA9AFU, 0xA9AFU, 0xB1AFU, 0xB1AFU, 0xB1AFU, 0xB1AFU, 0xB1AFU, 0xB9AFU, 0xB9CFU, 0xB9CFU,
    0xB9CEU, 0xB9CEU, 0xC1CEU, 0xC1CEU, 0xC1EEU, 0xC1EEU, 0xC1EEU, 0xC9EEU, 0xC9EEU, 0xC9EEU, 0xCA0EU, 0xCA0EU,
    0xD20DU, 0xD20DU, 0xD20DU, 0xD22DU, 0xD22DU, 0xDA2DU, 0xDA2DU, 0xDA2DU, 0xDA4DU, 0xDA4DU, 0xDA4DU, 0xE26CU,
    0xE26CU, 0xE26CU, 0xE26CU, 0xE28CU, 0xE28CU, 0xEA8CU, 0xEAACU, 0xEAACU, 0xEAACU, 0xEACCU, 0xEACBU, 0xEACBU,
    0xEAEBU, 0xF2EBU, 0xF30BU, 0xF30BU, 0xF32BU, 0xF32BU, 0xF32BU, 0xF34BU, 0xF34BU, 0xF36BU, 0xF36BU, 0xF38BU,
    0xF38BU, 0xFBABU, 0xFBABU, 0xFBCBU, 0xFBCBU, 0xFBCBU, 0xFBEBU, 0xFBEBU, 0xFC0BU, 0xFC0BU, 0xFC2CU, 0xFC2CU,
    0xFC4CU, 0xFC4CU, 0xFC6CU, 0xFC6CU, 0xFC8CU, 0xFC8CU, 0xFCACU, 0xFCADU, 0xFCCDU, 0xFCCDU, 0xFCCDU, 0xFCEDU,
    0xFCEDU, 0xFD0DU, 0xFD0DU, 0xFD2EU, 0xFD2EU, 0xFD4EU, 0xFD4EU, 0xFD6EU, 0xFD6EU, 0xFD8FU, 0xFD8FU, 0xFDAFU,
    0xFDAFU, 0xFDAFU, 0xFDCFU, 0xFDD0U, 0xFDF0U, 0xFDF0U, 0xFE10U, 0xFE10U, 0xFE31U, 0xFE31U, 0xFE51U, 0xFE51U,
    0xFE71U, 0xFE72U, 0xFE72U, 0xFE92U, 0xFE92U, 0xFEB2U, 0xFEB3U, 0xFED3U, 0xFED3U, 0xFEF3U, 0xFEF4U, 0xFF14U,
    0xFF14U, 0xFF14U, 0xFF34U, 0xFF35U, 0xFF55U, 0xFF55U, 0xFF75U, 0xFF76U, 0xFF96U, 0xFF96U, 0xFFB6U, 0xFFB7U,
    0xFFB7U, 0xFFD7U, 0xFFD7U, 0xFFF7U,
};

/* Plasma - exact 256-entry LUT, converted from SDR++'s own
 * root/res/colormaps/plasma.json (author: B.I.D.S.), uploaded
 * by the project owner 08/09/2026 - RGB565, one entry per LUT index,
 * no interpolation needed since this already has exactly 256 stops. */
static const uint16_t k_lut_plasma[256] = {
    0x0850U, 0x1031U, 0x1031U, 0x1031U, 0x1831U, 0x1831U, 0x1831U, 0x2031U, 0x2032U, 0x2032U, 0x2032U, 0x2832U,
    0x2832U, 0x2832U, 0x2832U, 0x2832U, 0x3032U, 0x3032U, 0x3033U, 0x3033U, 0x3833U, 0x3833U, 0x3833U, 0x3833U,
    0x3833U, 0x4033U, 0x4013U, 0x4013U, 0x4013U, 0x4813U, 0x4814U, 0x4814U, 0x4814U, 0x4814U, 0x5014U, 0x5014U,
    0x5014U, 0x5014U, 0x5014U, 0x5814U, 0x5814U, 0x5814U, 0x5814U, 0x5814U, 0x6014U, 0x6014U, 0x6014U, 0x6014U,
    0x6014U, 0x6015U, 0x6815U, 0x6815U, 0x6815U, 0x6815U, 0x6815U, 0x7015U, 0x7015U, 0x7015U, 0x7015U, 0x7015U,
    0x7815U, 0x7815U, 0x7815U, 0x7815U, 0x7815U, 0x8035U, 0x8034U, 0x8034U, 0x8034U, 0x8034U, 0x8034U, 0x8854U,
    0x8854U, 0x8854U, 0x8854U, 0x8874U, 0x8874U, 0x9074U, 0x9074U, 0x9094U, 0x9094U, 0x9094U, 0x98B4U, 0x98B3U,
    0x98B3U, 0x98B3U, 0x98D3U, 0x98D3U, 0xA0D3U, 0xA0D3U, 0xA0F3U, 0xA0F3U, 0xA0F3U, 0xA113U, 0xA112U, 0xA912U,
    0xA912U, 0xA932U, 0xA932U, 0xA932U, 0xA952U, 0xB152U, 0xB152U, 0xB151U, 0xB171U, 0xB171U, 0xB171U, 0xB191U,
    0xB191U, 0xB991U, 0xB991U, 0xB9B1U, 0xB9B0U, 0xB9B0U, 0xB9D0U, 0xB9D0U, 0xC1D0U, 0xC1D0U, 0xC1F0U, 0xC1F0U,
    0xC1EFU, 0xC20FU, 0xC20FU, 0xC20FU, 0xCA0FU, 0xCA2FU, 0xCA2FU, 0xCA2FU, 0xCA2FU, 0xCA4EU, 0xCA4EU, 0xCA4EU,
    0xCA6EU, 0xD26EU, 0xD26EU, 0xD26EU, 0xD28EU, 0xD28EU, 0xD28DU, 0xD2ADU, 0xD2ADU, 0xD2ADU, 0xDAADU, 0xDACDU,
    0xDACDU, 0xDACDU, 0xDAEDU, 0xDAECU, 0xDAECU, 0xDAECU, 0xDB0CU, 0xDB0CU, 0xE30CU, 0xE32CU, 0xE32CU, 0xE32CU,
    0xE34BU, 0xE34BU, 0xE34BU, 0xE34BU, 0xE36BU, 0xE36BU, 0xE36BU, 0xEB8BU, 0xEB8BU, 0xEB8AU, 0xEBAAU, 0xEBAAU,
    0xEBAAU, 0xEBAAU, 0xEBCAU, 0xEBCAU, 0xEBCAU, 0xEBEAU, 0xEBEAU, 0xF3E9U, 0xF409U, 0xF409U, 0xF409U, 0xF429U,
    0xF429U, 0xF429U, 0xF449U, 0xF449U, 0xF448U, 0xF468U, 0xF468U, 0xF468U, 0xF488U, 0xF488U, 0xF488U, 0xFCA8U,
    0xFCA8U, 0xFCA7U, 0xFCC7U, 0xFCC7U, 0xFCC7U, 0xFCE7U, 0xFCE7U, 0xFCE7U, 0xFD07U, 0xFD07U, 0xFD07U, 0xFD26U,
    0xFD26U, 0xFD46U, 0xFD46U, 0xFD46U, 0xFD66U, 0xFD66U, 0xFD66U, 0xFD86U, 0xFD85U, 0xFDA5U, 0xFDA5U, 0xFDA5U,
    0xFDC5U, 0xFDC5U, 0xFDC5U, 0xFDE5U, 0xFDE5U, 0xFE05U, 0xFE05U, 0xFE05U, 0xFE24U, 0xFE24U, 0xFE44U, 0xFE44U,
    0xFE44U, 0xFE64U, 0xFE64U, 0xFE84U, 0xFE84U, 0xFE84U, 0xFEA4U, 0xFEA4U, 0xFEC4U, 0xFEC4U, 0xFEE4U, 0xFEE4U,
    0xFEE4U, 0xFF04U, 0xF704U, 0xF724U, 0xF724U, 0xF744U, 0xF744U, 0xF744U, 0xF764U, 0xF764U, 0xF784U, 0xF784U,
    0xF7A4U, 0xF7A4U, 0xF7A4U, 0xF7C4U,
};

/* Turbo - exact 256-entry LUT, converted from SDR++'s own
 * root/res/colormaps/turbo.json (author: Google AI), uploaded
 * by the project owner 08/09/2026 - RGB565, one entry per LUT index,
 * no interpolation needed since this already has exactly 256 stops. */
static const uint16_t k_lut_turbo[256] = {
    0x3087U, 0x30A8U, 0x30C9U, 0x30CAU, 0x30EBU, 0x310BU, 0x312CU, 0x392DU, 0x394EU, 0x396FU, 0x3970U, 0x3990U,
    0x39B1U, 0x39D2U, 0x39D2U, 0x39F3U, 0x4214U, 0x4214U, 0x4235U, 0x4256U, 0x4256U, 0x4277U, 0x4297U, 0x42B8U,
    0x42B8U, 0x42D9U, 0x42F9U, 0x42FAU, 0x431AU, 0x433BU, 0x433BU, 0x435CU, 0x435CU, 0x437CU, 0x439DU, 0x439DU,
    0x43BDU, 0x43DEU, 0x43DEU, 0x43FEU, 0x441EU, 0x441FU, 0x443FU, 0x443FU, 0x445FU, 0x447FU, 0x447FU, 0x449FU,
    0x44BFU, 0x44BFU, 0x44DFU, 0x3CDFU, 0x3CFFU, 0x3D1FU, 0x3D1FU, 0x3D3FU, 0x355FU, 0x355FU, 0x357EU, 0x357EU,
    0x2D9EU, 0x2DBEU, 0x2DBEU, 0x2DDDU, 0x2DFDU, 0x25FDU, 0x261CU, 0x261CU, 0x263CU, 0x263BU, 0x1E5BU, 0x1E5BU,
    0x1E7BU, 0x1E9AU, 0x1E9AU, 0x1EBAU, 0x1EB9U, 0x1EB9U, 0x1ED9U, 0x1ED8U, 0x1EF8U, 0x1EF8U, 0x1F17U, 0x1F17U,
    0x1F17U, 0x1F36U, 0x1F36U, 0x1F36U, 0x1F55U, 0x2755U, 0x2755U, 0x2774U, 0x2774U, 0x2F74U, 0x2F93U, 0x2F93U,
    0x3793U, 0x3792U, 0x3FB2U, 0x3FB1U, 0x3FB1U, 0x47B0U, 0x47D0U, 0x4FD0U, 0x4FCFU, 0x57CFU, 0x57CEU, 0x5FCEU,
    0x5FEDU, 0x67EDU, 0x67EDU, 0x6FECU, 0x6FECU, 0x77EBU, 0x77EBU, 0x7FEBU, 0x7FEAU, 0x87EAU, 0x87EAU, 0x8FE9U,
    0x8FE9U, 0x8FE9U, 0x97E8U, 0x97E8U, 0x9FE8U, 0x9FE8U, 0x9FE7U, 0xA7E7U, 0xA7E7U, 0xA7E7U, 0xAFC7U, 0xAFC7U,
    0xAFC6U, 0xB7C6U, 0xB7C6U, 0xB7A6U, 0xBFA6U, 0xBFA6U, 0xBFA6U, 0xC786U, 0xC786U, 0xC786U, 0xCF66U, 0xCF66U,
    0xCF66U, 0xD746U, 0xD746U, 0xD726U, 0xD726U, 0xDF26U, 0xDF06U, 0xDF06U, 0xDEE6U, 0xE6E6U, 0xE6C7U, 0xE6C7U,
    0xE6A7U, 0xEEA7U, 0xEE87U, 0xEE87U, 0xEE67U, 0xEE67U, 0xF647U, 0xF647U, 0xF627U, 0xF627U, 0xF607U, 0xF607U,
    0xFDE7U, 0xFDE7U, 0xFDC7U, 0xFDC7U, 0xFDA6U, 0xFD86U, 0xFD86U, 0xFD66U, 0xFD66U, 0xFD46U, 0xFD26U, 0xFD26U,
    0xFD06U, 0xFCE5U, 0xFCC5U, 0xFCC5U, 0xFCA5U, 0xFC85U, 0xFC85U, 0xFC64U, 0xFC44U, 0xFC24U, 0xFC24U, 0xFC04U,
    0xFBE4U, 0xFBC3U, 0xFBC3U, 0xFBA3U, 0xFB83U, 0xF363U, 0xF363U, 0xF343U, 0xF322U, 0xF302U, 0xF302U, 0xF2E2U,
    0xF2C2U, 0xEAC2U, 0xEAA2U, 0xEA81U, 0xEA81U, 0xEA61U, 0xEA41U, 0xE241U, 0xE221U, 0xE221U, 0xE201U, 0xE201U,
    0xD9E1U, 0xD9E1U, 0xD9C0U, 0xD9C0U, 0xD9A0U, 0xD1A0U, 0xD180U, 0xD180U, 0xD160U, 0xC960U, 0xC940U, 0xC940U,
    0xC940U, 0xC120U, 0xC120U, 0xC100U, 0xB900U, 0xB900U, 0xB8E0U, 0xB0E0U, 0xB0C0U, 0xB0C0U, 0xA8C0U, 0xA8A0U,
    0xA8A0U, 0xA0A0U, 0xA080U, 0xA080U, 0x9880U, 0x9860U, 0x9860U, 0x9060U, 0x9040U, 0x8840U, 0x8840U, 0x8840U,
    0x8020U, 0x8020U, 0x7820U, 0x7820U,
};

/* Viridis - exact 256-entry LUT, converted from SDR++'s own
 * root/res/colormaps/viridis.json (author: B.I.D.S.), uploaded
 * by the project owner 08/09/2026 - RGB565, one entry per LUT index,
 * no interpolation needed since this already has exactly 256 stops. */
static const uint16_t k_lut_viridis[256] = {
    0x400AU, 0x400AU, 0x402AU, 0x402BU, 0x402BU, 0x404BU, 0x404BU, 0x404BU, 0x406CU, 0x406CU, 0x408CU, 0x408CU,
    0x408CU, 0x48ACU, 0x48ADU, 0x48ADU, 0x48CDU, 0x48CDU, 0x48CDU, 0x48EDU, 0x48EDU, 0x48EEU, 0x490EU, 0x490EU,
    0x490EU, 0x492EU, 0x492EU, 0x492EU, 0x494FU, 0x494FU, 0x414FU, 0x416FU, 0x416FU, 0x416FU, 0x416FU, 0x418FU,
    0x418FU, 0x418FU, 0x41B0U, 0x41B0U, 0x41B0U, 0x41D0U, 0x41D0U, 0x41D0U, 0x41D0U, 0x41F0U, 0x41F0U, 0x41F0U,
    0x4210U, 0x4210U, 0x4210U, 0x4230U, 0x4231U, 0x4231U, 0x3A31U, 0x3A51U, 0x3A51U, 0x3A51U, 0x3A71U, 0x3A71U,
    0x3A71U, 0x3A71U, 0x3A91U, 0x3A91U, 0x3A91U, 0x3A91U, 0x3AB1U, 0x3AB1U, 0x3AB1U, 0x3AD1U, 0x3AD1U, 0x32D1U,
    0x32D1U, 0x32F1U, 0x32F1U, 0x32F1U, 0x32F1U, 0x3311U, 0x3311U, 0x3311U, 0x3311U, 0x3331U, 0x3331U, 0x3331U,
    0x3331U, 0x3351U, 0x3351U, 0x3351U, 0x2B51U, 0x2B71U, 0x2B71U, 0x2B71U, 0x2B71U, 0x2B91U, 0x2B91U, 0x2B91U,
    0x2B91U, 0x2B91U, 0x2BB1U, 0x2BB1U, 0x2BB1U, 0x2BB1U, 0x2BD1U, 0x2BD1U, 0x2BD1U, 0x2BD1U, 0x2BF1U, 0x2BF1U,
    0x23F1U, 0x23F1U, 0x2411U, 0x2411U, 0x2411U, 0x2411U, 0x2411U, 0x2431U, 0x2431U, 0x2431U, 0x2431U, 0x2451U,
    0x2451U, 0x2451U, 0x2451U, 0x2471U, 0x2471U, 0x2471U, 0x2471U, 0x2491U, 0x2491U, 0x2491U, 0x2491U, 0x2491U,
    0x1CB1U, 0x1CB1U, 0x1CB1U, 0x1CB1U, 0x1CD1U, 0x1CD1U, 0x1CD1U, 0x1CD1U, 0x1CF1U, 0x1CF1U, 0x1CF1U, 0x1CF1U,
    0x1D11U, 0x1D11U, 0x1D10U, 0x1D10U, 0x2510U, 0x2530U, 0x2530U, 0x2530U, 0x2530U, 0x2550U, 0x2550U, 0x2550U,
    0x2550U, 0x2570U, 0x2570U, 0x2570U, 0x2D70U, 0x2D6FU, 0x2D8FU, 0x2D8FU, 0x2D8FU, 0x2D8FU, 0x2DAFU, 0x35AFU,
    0x35AFU, 0x35AFU, 0x35AFU, 0x35CFU, 0x3DCEU, 0x3DCEU, 0x3DCEU, 0x3DEEU, 0x3DEEU, 0x45EEU, 0x45EEU, 0x45EEU,
    0x460DU, 0x4E0DU, 0x4E0DU, 0x4E0DU, 0x4E0DU, 0x562DU, 0x562DU, 0x562DU, 0x562CU, 0x5E2CU, 0x5E4CU, 0x5E4CU,
    0x5E4CU, 0x664CU, 0x664BU, 0x664BU, 0x666BU, 0x6E6BU, 0x6E6BU, 0x6E6BU, 0x766AU, 0x768AU, 0x768AU, 0x768AU,
    0x7E8AU, 0x7E8AU, 0x7E89U, 0x8689U, 0x86A9U, 0x86A9U, 0x8EA9U, 0x8EA8U, 0x8EA8U, 0x96A8U, 0x96A8U, 0x96C8U,
    0x9EC7U, 0x9EC7U, 0x9EC7U, 0xA6C7U, 0xA6C6U, 0xA6C6U, 0xAEC6U, 0xAEE6U, 0xAEE6U, 0xB6E5U, 0xB6E5U, 0xB6E5U,
    0xBEE5U, 0xBEE5U, 0xBEE4U, 0xC6E4U, 0xC6E4U, 0xC704U, 0xCF04U, 0xCF03U, 0xCF03U, 0xD703U, 0xD703U, 0xD703U,
    0xDF03U, 0xDF03U, 0xDF03U, 0xDF03U, 0xE723U, 0xE723U, 0xE723U, 0xEF23U, 0xEF23U, 0xEF23U, 0xF723U, 0xF723U,
    0xF724U, 0xFF24U, 0xFF24U, 0xFF24U,
};

/* Classic (15 stops) - exact stops from SDR++'s own
 * root/res/colormaps/classic.json (author: Youssef Touil), uploaded
 * by the project owner 08/09/2026 - linearly interpolated via
 * palette_lerp_stops(), same technique as VIRIDIS originally used. */
static const uint8_t k_stops_classic[15][3] = {
    {0U, 0U, 32U},
    {0U, 0U, 48U},
    {0U, 0U, 80U},
    {0U, 0U, 145U},
    {30U, 144U, 255U},
    {255U, 255U, 255U},
    {255U, 255U, 0U},
    {254U, 109U, 22U},
    {254U, 109U, 22U},
    {255U, 0U, 0U},
    {255U, 0U, 0U},
    {198U, 0U, 0U},
    {159U, 0U, 0U},
    {117U, 0U, 0U},
    {74U, 0U, 0U},
};

/* Classic Green (13 stops) - exact stops from SDR++'s own
 * root/res/colormaps/classic_green.json (author: Paul (PD0SWL)), uploaded
 * by the project owner 08/09/2026 - linearly interpolated via
 * palette_lerp_stops(), same technique as VIRIDIS originally used. */
static const uint8_t k_stops_classic_green[13][3] = {
    {0U, 0U, 0U},
    {0U, 0U, 48U},
    {0U, 40U, 81U},
    {0U, 73U, 147U},
    {0U, 155U, 230U},
    {128U, 255U, 128U},
    {128U, 255U, 128U},
    {255U, 160U, 66U},
    {255U, 0U, 0U},
    {198U, 0U, 0U},
    {159U, 0U, 0U},
    {117U, 0U, 0U},
    {74U, 0U, 0U},
};

/* Electric (4 stops) - exact stops from SDR++'s own
 * root/res/colormaps/electric.json (author: Ryzerth), uploaded
 * by the project owner 08/09/2026 - linearly interpolated via
 * palette_lerp_stops(), same technique as VIRIDIS originally used. */
static const uint8_t k_stops_electric[4][3] = {
    {0U, 0U, 0U},
    {0U, 0U, 255U},
    {0U, 255U, 255U},
    {255U, 255U, 255U},
};

/* Smoke (8 stops) - exact stops from SDR++'s own
 * root/res/colormaps/smoke.json (author: Yaroslav Andrianov), uploaded
 * by the project owner 08/09/2026 - linearly interpolated via
 * palette_lerp_stops(), same technique as VIRIDIS originally used. */
static const uint8_t k_stops_smoke[8][3] = {
    {255U, 255U, 255U},
    {238U, 238U, 238U},
    {204U, 204U, 204U},
    {119U, 119U, 119U},
    {85U, 85U, 85U},
    {51U, 51U, 51U},
    {17U, 17U, 17U},
    {0U, 0U, 0U},
};

/* Temper Colors (22 stops) - exact stops from SDR++'s own
 * root/res/colormaps/temper_colors.json (author: Yaroslav Andrianov), uploaded
 * by the project owner 08/09/2026 - linearly interpolated via
 * palette_lerp_stops(), same technique as VIRIDIS originally used. */
static const uint8_t k_stops_temper_colors[22][3] = {
    {0U, 0U, 0U},
    {5U, 1U, 31U},
    {15U, 8U, 54U},
    {47U, 20U, 54U},
    {61U, 17U, 77U},
    {78U, 24U, 111U},
    {89U, 42U, 143U},
    {94U, 67U, 165U},
    {95U, 94U, 179U},
    {98U, 118U, 186U},
    {107U, 140U, 191U},
    {123U, 161U, 194U},
    {149U, 181U, 199U},
    {179U, 198U, 206U},
    {212U, 188U, 172U},
    {204U, 163U, 137U},
    {198U, 138U, 109U},
    {190U, 111U, 91U},
    {178U, 86U, 82U},
    {162U, 64U, 80U},
    {142U, 44U, 80U},
    {116U, 30U, 79U},
};

/* Vivid (23 stops) - exact stops from SDR++'s own
 * root/res/colormaps/vivid.json (author: Yaroslav Andrianov), uploaded
 * by the project owner 08/09/2026 - linearly interpolated via
 * palette_lerp_stops(), same technique as VIRIDIS originally used. */
static const uint8_t k_stops_vivid[23][3] = {
    {0U, 0U, 0U},
    {6U, 0U, 28U},
    {9U, 0U, 40U},
    {18U, 0U, 44U},
    {35U, 0U, 57U},
    {54U, 1U, 67U},
    {68U, 1U, 84U},
    {71U, 44U, 122U},
    {59U, 81U, 139U},
    {44U, 113U, 142U},
    {33U, 144U, 141U},
    {39U, 173U, 129U},
    {92U, 200U, 99U},
    {170U, 220U, 50U},
    {246U, 253U, 37U},
    {253U, 222U, 23U},
    {254U, 203U, 49U},
    {254U, 144U, 41U},
    {245U, 105U, 24U},
    {220U, 59U, 7U},
    {206U, 45U, 4U},
    {172U, 23U, 1U},
    {152U, 14U, 1U},
};

/* WebSDR (5 stops) - exact stops from SDR++'s own
 * root/res/colormaps/websdr.json (author: Ryzerth), uploaded
 * by the project owner 08/09/2026 - linearly interpolated via
 * palette_lerp_stops(), same technique as VIRIDIS originally used. */
static const uint8_t k_stops_websdr[5][3] = {
    {0U, 0U, 0U},
    {0U, 0U, 80U},
    {255U, 0U, 255U},
    {255U, 255U, 80U},
    {255U, 255U, 255U},
};

/* GRAYSCALE - plain black -> white, no hue at all (matches SDR++'s
 * own 2-stop "Grey Scale" exactly - simple enough to keep as a direct
 * formula rather than a 2-entry stop table). Useful in bright
 * sunlight (a resistive TFT's colors wash out faster than pure
 * brightness contrast does) or just for a calmer, lower-visual-noise
 * waterfall. */
static uint16_t palette_eval_grayscale(float t)
{
    uint8_t v;

    if (t < 0.0f) { t = 0.0f; }
    if (t > 1.0f) { t = 1.0f; }
    v = (uint8_t)(t * 255.0f);
    return gfx_rgb565(v, v, v);
}

/* FIRE - black -> red -> orange -> yellow -> white. This project's
 * OWN addition (not part of SDR++'s set) - kept from this feature's
 * first pass since it's a distinct, useful "hot" look none of SDR++'s
 * own maps quite duplicate. Independent per-channel ramps (unlike the
 * stop-table palettes below) since a plain 3-segment RGB ramp is all
 * this needs. */
static uint16_t palette_eval_fire(float t)
{
    uint8_t r, g, b;

    if (t < 0.0f) { t = 0.0f; }
    if (t > 1.0f) { t = 1.0f; }

    if (t < (1.0f / 3.0f)) {
        float u = t / (1.0f / 3.0f);
        r = (uint8_t)(u * 255.0f); g = 0U; b = 0U;
    } else if (t < (2.0f / 3.0f)) {
        float u = (t - (1.0f / 3.0f)) / (1.0f / 3.0f);
        r = 255U; g = (uint8_t)(u * 255.0f); b = 0U;
    } else {
        float u = (t - (2.0f / 3.0f)) / (1.0f / 3.0f);
        r = 255U; g = 255U; b = (uint8_t)(u * 255.0f);
    }
    return gfx_rgb565(r, g, b);
}

/*
 * Shared N-stop linear interpolator - pos maps t (0..1) onto the stop
 * array's index range, i0/i1 are the two stops straddling it, u is
 * the fractional position between them - all three channels
 * interpolated together (unlike FIRE's independent-per-channel
 * ramps), since these palettes' hue itself changes across stops, not
 * just brightness.
 */
static uint16_t palette_lerp_stops(const uint8_t stops[][3], uint8_t n_stops, float t)
{
    float pos;
    uint8_t i0, i1;
    float u;
    uint8_t r, g, b;

    if (t < 0.0f) { t = 0.0f; }
    if (t > 1.0f) { t = 1.0f; }

    pos = t * (float)(n_stops - 1U);
    i0 = (uint8_t)pos;
    if (i0 > (uint8_t)(n_stops - 2U)) { i0 = (uint8_t)(n_stops - 2U); } /* guards the t=1.0 exact-edge case */
    i1 = (uint8_t)(i0 + 1U);
    u = pos - (float)i0;

    r = (uint8_t)((float)stops[i0][0] + u * ((float)stops[i1][0] - (float)stops[i0][0]));
    g = (uint8_t)((float)stops[i0][1] + u * ((float)stops[i1][1] - (float)stops[i0][1]));
    b = (uint8_t)((float)stops[i0][2] + u * ((float)stops[i1][2] - (float)stops[i0][2]));
    return gfx_rgb565(r, g, b);
}

static uint16_t palette_eval_classic(float t)       { return palette_lerp_stops(k_stops_classic, 15U, t); }
static uint16_t palette_eval_classic_green(float t) { return palette_lerp_stops(k_stops_classic_green, 13U, t); }
static uint16_t palette_eval_electric(float t)      { return palette_lerp_stops(k_stops_electric, 4U, t); }
static uint16_t palette_eval_smoke(float t)         { return palette_lerp_stops(k_stops_smoke, 8U, t); }
static uint16_t palette_eval_temper_colors(float t) { return palette_lerp_stops(k_stops_temper_colors, 22U, t); }
static uint16_t palette_eval_vivid(float t)         { return palette_lerp_stops(k_stops_vivid, 23U, t); }
static uint16_t palette_eval_websdr(float t)        { return palette_lerp_stops(k_stops_websdr, 5U, t); }

/*
 * Six of SDR++'s palettes ship as exactly 256 stops already - see
 * this file's own header comment above for why those get memcpy()d
 * straight in rather than going through palette_lerp_stops() (or an
 * eval-per-index callback) like everything else. Checked first;
 * falls through to the eval-callback path below for every other
 * palette.
 */
static void build_lut(void)
{
    uint16_t i;
    uint16_t (*eval)(float);

    switch (s_palette) {
    case SPECTRUM_PALETTE_GQRX:    memcpy(s_lut, k_lut_gqrx,    sizeof(s_lut)); s_lut_ready = 1U; return;
    case SPECTRUM_PALETTE_INFERNO: memcpy(s_lut, k_lut_inferno, sizeof(s_lut)); s_lut_ready = 1U; return;
    case SPECTRUM_PALETTE_MAGMA:   memcpy(s_lut, k_lut_magma,   sizeof(s_lut)); s_lut_ready = 1U; return;
    case SPECTRUM_PALETTE_PLASMA:  memcpy(s_lut, k_lut_plasma,  sizeof(s_lut)); s_lut_ready = 1U; return;
    case SPECTRUM_PALETTE_TURBO:   memcpy(s_lut, k_lut_turbo,   sizeof(s_lut)); s_lut_ready = 1U; return;
    case SPECTRUM_PALETTE_VIRIDIS: memcpy(s_lut, k_lut_viridis, sizeof(s_lut)); s_lut_ready = 1U; return;
    default: break;
    }

    switch (s_palette) {
    case SPECTRUM_PALETTE_FIRE:          eval = palette_eval_fire;          break;
    case SPECTRUM_PALETTE_GRAYSCALE:     eval = palette_eval_grayscale;     break;
    case SPECTRUM_PALETTE_ELECTRIC:      eval = palette_eval_electric;      break;
    case SPECTRUM_PALETTE_CLASSIC_GREEN: eval = palette_eval_classic_green; break;
    case SPECTRUM_PALETTE_SMOKE:         eval = palette_eval_smoke;         break;
    case SPECTRUM_PALETTE_TEMPER_COLORS: eval = palette_eval_temper_colors; break;
    case SPECTRUM_PALETTE_VIVID:         eval = palette_eval_vivid;         break;
    case SPECTRUM_PALETTE_WEBSDR:        eval = palette_eval_websdr;        break;
    case SPECTRUM_PALETTE_CLASSIC:
    default:                             eval = palette_eval_classic;       break;
    }

    for (i = 0; i < 256U; i++) {
        s_lut[i] = eval((float)i * (1.0f / 255.0f));
    }
    s_lut_ready = 1;
}

void spectrum_init(void)
{
    build_lut();
}

void spectrum_set_palette(spectrum_palette_t palette)
{
    s_palette = palette;
    build_lut(); /* live - the next spectrum_draw()/spectrum_colormap() call already sees the new colors, no separate re-init needed */
}

spectrum_palette_t spectrum_get_palette(void)
{
    return s_palette;
}

static spectrum_style_t s_style = SPECTRUM_STYLE_HEATMAP;

void spectrum_set_style(spectrum_style_t style)
{
    s_style = style;
}

spectrum_style_t spectrum_get_style(void)
{
    return s_style;
}

/* See this pair's own comment in spectrum.h. Default OFF (0) -
 * color-matched trace, no separate white highlight. */
static uint8_t s_heatmap_trace_white = 0U;

void spectrum_set_heatmap_trace_white(uint8_t white)
{
    s_heatmap_trace_white = white ? 1U : 0U;
}

uint8_t spectrum_get_heatmap_trace_white(void)
{
    return s_heatmap_trace_white;
}

uint16_t spectrum_colormap(float db, float db_min, float db_max)
{
    float t;
    int32_t idx;

    if (db_max <= db_min) {
        return GFX_COLOR_BLACK;
    }
    if (!s_lut_ready) {
        spectrum_init(); /* safety net if someone draws before init */
    }
    t = (db - db_min) / (db_max - db_min);
    idx = (int32_t)(t * 255.0f);
    if (idx < 0)   { idx = 0; }
    if (idx > 255) { idx = 255; }
    return s_lut[idx];
}

/* --- spectrum rendering --------------------------------------------- */

#define SPEC_MAX_W 800
#define SPEC_MAX_H 280 /* raised from 160 (30/07/2026) for the redesigned UI's
                          taller spectrum panel - per-row arrays only, cheap */

/* Extra colors not taken from the palette. */
#define SPEC_COLOR_TRACE  GFX_COLOR_WHITE
#define SPEC_COLOR_PEAK   GFX_COLOR_ORANGE
#define SPEC_COLOR_GRID   0x4A49U /* very dark gray, under everything */
#define SPEC_COLOR_CENTER 0xCE59U /* 08/09/2026: changed from full-bright red to a light gray (~200,200,200), per the project owner ("el rojo queda mal con los nuevos colores") - red was fine against the old fixed palette but clashes with several of the new selectable ones (FIRE/INFERNO/TURBO all have red/orange near the hot end of their own gradient, so a red demod line stopped standing out and started looking like part of the signal). Gray is neutral against every palette instead of matching or clashing with any one hue. */
#define SPEC_CENTER_HALF_WIDTH_PX 1U /* 01/09/2026: was implicitly 0 (a single exact-column pixel) - now +/-1, i.e. 3px total */

/* SPECTRUM_STYLE_LINE's palette - see spectrum_set_style()'s comment
 * in spectrum.h. Computed offline for a dark-navy-background,
 * medium-blue-fill, light-blue-trace look (RGB565): background
 * (6,10,26), fill (28,70,150), trace (110,180,255), gridline
 * (20,26,46) - a lighter navy than the background so the gridlines
 * stay faintly visible against it, same role SPEC_COLOR_GRID plays
 * against black for the HEATMAP style. */
#define SPEC_LINE_BG    0x0043
#define SPEC_LINE_FILL  0x1A32
#define SPEC_LINE_TRACE 0x6D9F
#define SPEC_LINE_GRID  0x10C5

/* Demodulated-bandwidth background tint - see spectrum_draw()'s
 * comment in spectrum.h. Deliberately a hue the rest of each style's
 * palette never uses (dim PURPLE) - the HEATMAP palette's own low end
 * ramps black->BLUE->cyan->green/yellow->red, and the LINE style is
 * all blues (background/fill/trace/grid), so a weak real signal could
 * never accidentally read as "this is the tint", or vice versa.
 * HEATMAP: RGB (40,0,60). LINE: RGB (34,0,52), slightly dimmer since
 * SPEC_LINE_BG is already non-black. */
#define SPEC_COLOR_BAND_TINT      0x632CU /* 08/09/2026: changed from a warm amber to a medium gray (~150,150,150), per the project owner - same "red/warm clashes with the new palettes" reasoning as SPEC_COLOR_CENTER just above (amber sits close to several of the new hot-end palette colors too, e.g. FIRE/INFERNO/TURBO/GQRX). Gray stays neutral against all of them. */
#define SPEC_LINE_BAND_TINT       0x5ACBU /* same reasoning as SPEC_COLOR_BAND_TINT just above, dimmed further (~90,90,90) to match LINE style's own generally darker palette (see SPEC_LINE_BG/GRID/TRACE) */

/* *** 01/09/2026: moved to TCM RAM *** - pure spectrum-rendering
 * working buffers, never DMA targets (only this file's own drawing
 * functions touch them) - see fft.c's fuller TCM comment for the
 * "why" (freed main-RAM headroom for the widened waterfall/spectrum
 * panel - SPEC_MAX_W was already 800, comfortably covering the new
 * width, so no size change was needed here, just relocating where
 * these already-existing buffers live). */
#define TCMRAM_BSS __attribute__((section(".tcmram")))

static float    s_col_ema[SPEC_MAX_W] TCMRAM_BSS;   /* smoothed dB per column      */
#if SPECTRUM_PEAK_HOLD
static float    s_col_peak[SPEC_MAX_W] TCMRAM_BSS;  /* peak-hold dB per column     */
#endif
static uint16_t s_bar_h[SPEC_MAX_W] TCMRAM_BSS;     /* bar height, px from bottom  */
static uint16_t s_bar_h_smooth[SPEC_MAX_W] TCMRAM_BSS; /* scratch buf for spatial smoothing */
static uint16_t s_peak_h[SPEC_MAX_W] TCMRAM_BSS;    /* peak marker height          */
static uint16_t s_bar_lo[SPEC_MAX_W] TCMRAM_BSS;    /* vertical trace bridge to the left neighbor, low end - see Pass 1.6 */
static uint16_t s_bar_hi[SPEC_MAX_W] TCMRAM_BSS;    /* vertical trace bridge to the left neighbor, high end - see Pass 1.6 */
static uint16_t s_row_color[SPEC_MAX_H] TCMRAM_BSS; /* gradient fill color per row */
static uint8_t  s_row_grid[SPEC_MAX_H] TCMRAM_BSS;  /* 1 = horizontal gridline on this row */
static uint8_t  s_col_grid[SPEC_MAX_W] TCMRAM_BSS;  /* 1 = vertical gridline on this column - added 08/09/2026, see SPECTRUM_GRID_ROWS/COLS's comment in spectrum.h */
static uint16_t s_row_buf[SPEC_MAX_W] TCMRAM_BSS;   /* stripe assembled in RAM     */
static uint16_t s_prev_w = 0;            /* detect geometry change      */

/* See spectrum_set_line_smooth()'s comment in spectrum.h. Defaults to
 * 0 (disabled, original bin-sharp look) - main.c's repurposed NB tile
 * sets its own starting value on the first menu draw, this is just
 * the safe fallback if spectrum_draw() ever runs before that. */
static uint8_t s_line_smooth_passes = 3U;

void spectrum_set_line_smooth(uint8_t passes)
{
    if (passes > SPECTRUM_LINE_SMOOTH_MAX) {
        passes = SPECTRUM_LINE_SMOOTH_MAX;
    }
    s_line_smooth_passes = passes;
}

uint8_t spectrum_get_line_smooth(void)
{
    return s_line_smooth_passes;
}

void spectrum_draw(const float *db, uint32_t n_bins,
                    uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                    float db_min, float db_max,
                    int16_t center_mark_offset_px,
                    uint8_t band_active,
                    int16_t band_lo_offset_px, int16_t band_hi_offset_px)
{
    uint16_t col, row;
    float scale_t;
    uint16_t center_mark_col;
    uint16_t center_mark_col_lo, center_mark_col_hi; /* see SPEC_CENTER_HALF_WIDTH_PX below */
    uint16_t band_col_lo = 0, band_col_hi = 0; /* only meaningful when band_active */

    if (db_max <= db_min || w == 0U || h == 0U ||
        w > SPEC_MAX_W || h > SPEC_MAX_H || n_bins == 0U) {
        return;
    }
    if (!s_lut_ready) {
        spectrum_init();
    }

    /* Clamp the (possibly offset) center mark into [0, w-1] - a
     * caller passing an offset close to the panel's edge shouldn't be
     * able to walk the column index out of s_row_buf's bounds. */
    {
        int32_t c = (int32_t)(w / 2U) + (int32_t)center_mark_offset_px;
        if (c < 0) { c = 0; }
        if (c > (int32_t)(w - 1U)) { c = (int32_t)(w - 1U); }
        center_mark_col = (uint16_t)c;
    }

    /* SPEC_CENTER_HALF_WIDTH_PX: half-width, in pixels, of the demod
     * center marker - 01/09/2026, bumped from a single pixel to 3px
     * total, per the project owner ("la linea de demodulacion...
     * mas ancho"). Computed with signed math and re-clamped into
     * [0, w-1] independently (not just center_mark_col +/- 1 as
     * uint16_t, which would underflow to a huge value if center_
     * mark_col is 0) so the marker never walks off either edge of
     * s_row_buf even when the demod point itself is clamped right at
     * the panel's edge. */
    {
        int32_t lo = (int32_t)center_mark_col - (int32_t)SPEC_CENTER_HALF_WIDTH_PX;
        int32_t hi = (int32_t)center_mark_col + (int32_t)SPEC_CENTER_HALF_WIDTH_PX;
        if (lo < 0) { lo = 0; }
        if (hi > (int32_t)(w - 1U)) { hi = (int32_t)(w - 1U); }
        center_mark_col_lo = (uint16_t)lo;
        center_mark_col_hi = (uint16_t)hi;
    }

    /* Same clamp, applied to both band edges independently, then
     * sorted - the caller (main.c) builds these from
     * center_mark_offset_px +/- a bandwidth in pixels, so which one
     * ends up smaller depends on the demod mode (AM straddles both
     * ways, USB/LSB only extend one way - see the call site), not
     * worth requiring the caller to pre-sort them here too. */
    if (band_active) {
        int32_t lo = (int32_t)(w / 2U) + (int32_t)band_lo_offset_px;
        int32_t hi = (int32_t)(w / 2U) + (int32_t)band_hi_offset_px;
        int32_t tmp;

        if (lo > hi) { tmp = lo; lo = hi; hi = tmp; }
        if (lo < 0) { lo = 0; }
        if (hi > (int32_t)(w - 1U)) { hi = (int32_t)(w - 1U); }
        if (lo > (int32_t)(w - 1U)) { lo = (int32_t)(w - 1U); }
        if (hi < 0) { hi = 0; }
        band_col_lo = (uint16_t)lo;
        band_col_hi = (uint16_t)hi;
    }

    /* Reset smoothing state when the geometry changes (first call
     * included): stale EMA values from a different column mapping
     * would be meaningless. */
    if (w != s_prev_w) {
        for (col = 0; col < w; col++) {
            s_col_ema[col] = db_min;
#if SPECTRUM_PEAK_HOLD
            s_col_peak[col] = db_min;
#endif
        }
        s_prev_w = w;
    }

    scale_t = 1.0f / (db_max - db_min);

    /*
     * Pass 1 (per column, float - only ~w iterations): fold bins into
     * the column by AVERAGING every bin whose center lands in it
     * (when n_bins >= w), or nearest bin (when zoomed in, n_bins < w).
     * Then asymmetric EMA and peak-hold, and quantize to pixels.
     */
    for (col = 0; col < w; col++) {
        uint32_t b0 = ((uint32_t)col * n_bins) / w;
        uint32_t b1 = ((uint32_t)(col + 1U) * n_bins) / w;
        float v;
        float t;
        uint32_t bi;

        if (b1 <= b0) {
            b1 = b0 + 1U; /* zoomed in: at least one bin */
        }
        v = 0.0f;
        for (bi = b0; bi < b1; bi++) {
            v += db[bi];
        }
        v = v / (float)(b1 - b0);

        /* Fast attack, slow decay. */
        if (v > s_col_ema[col]) {
            s_col_ema[col] += SPECTRUM_EMA_ATTACK * (v - s_col_ema[col]);
        } else {
            s_col_ema[col] += SPECTRUM_EMA_DECAY * (v - s_col_ema[col]);
        }

#if SPECTRUM_PEAK_HOLD
        if (s_col_ema[col] > s_col_peak[col]) {
            s_col_peak[col] = s_col_ema[col];
        } else {
            s_col_peak[col] -= SPECTRUM_PEAK_DECAY;
            if (s_col_peak[col] < db_min) {
                s_col_peak[col] = db_min;
            }
        }
        t = (s_col_peak[col] - db_min) * scale_t;
        if (t < 0.0f) { t = 0.0f; }
        if (t > 1.0f) { t = 1.0f; }
        s_peak_h[col] = (uint16_t)(t * (float)h);
#else
        s_peak_h[col] = 0;
#endif

        t = (s_col_ema[col] - db_min) * scale_t;
        if (t < 0.0f) { t = 0.0f; }
        if (t > 1.0f) { t = 1.0f; }
        s_bar_h[col] = (uint16_t)(t * (float)h);
    }

    /*
     * Pass 1.5 (per column, integer - only ~w iterations per pass):
     * light spatial smoothing across neighboring columns so the
     * trace/fill edge isn't jagged bin-to-bin. 3-tap (1,2,1)/4
     * average, edges left untouched (clamped), run
     * s_line_smooth_passes times back-to-back (see
     * spectrum_set_line_smooth()'s comment in spectrum.h - repeating
     * the same narrow kernel approximates a wider one without the
     * cost/complexity of a real 5-/7-tap filter). Applied
     * post-quantization, on top of s_bar_h only, so it never touches
     * the persistent s_col_ema / s_col_peak temporal state - it's a
     * purely visual pass, redone from scratch every frame.
     */
    if (w >= 3U) {
        uint8_t pass;
        for (pass = 0; pass < s_line_smooth_passes; pass++) {
            s_bar_h_smooth[0] = s_bar_h[0];
            for (col = 1; col < w - 1U; col++) {
                s_bar_h_smooth[col] = (uint16_t)(((uint32_t)s_bar_h[col - 1U] +
                                                   2U * (uint32_t)s_bar_h[col] +
                                                   (uint32_t)s_bar_h[col + 1U]) / 4U);
            }
            s_bar_h_smooth[w - 1U] = s_bar_h[w - 1U];
            for (col = 0; col < w; col++) {
                s_bar_h[col] = s_bar_h_smooth[col];
            }
        }
    }

    /*
     * Pass 1.6 (per column, integer - only ~w iterations): the single
     * bright trace pixel per column only lands on ONE row (bh ==
     * level_from_bottom). Where a signal edge is steep, adjacent
     * columns' bar heights can differ by more than 1px, so their
     * trace pixels don't share a row and the top edge reads as
     * scattered dots instead of a connected line/contour - most
     * visible in HEATMAP now that different palettes make the
     * fill-vs-background contrast vary a lot more than the old fixed
     * gradient did (reported by the project owner, 08/09/2026: "los
     * puntos del espectro no estan conectados"). Runs for EVERY style
     * now (used to be OUTLINE-only, on the theory that HEATMAP/LINE's
     * fill already hides the gap - true only WITHIN one column's own
     * bar, not BETWEEN two columns of very different heights, which
     * is exactly the steep-edge case this exists for): for each
     * column, connect its height to its LEFT neighbor's with trace
     * color, drawn in the current column's pixel stripe (a
     * stepped/staircase join, same idea as a polyline plot). col 0
     * has no left neighbor, so its bridge collapses to the single dot
     * as before. HEATMAP/LINE's own bar fill is untouched by this -
     * it still only fills up to that column's OWN bh (see Pass 3
     * below) - so on a steep edge the bridge is a connecting line
     * with background showing through beneath it in the shorter
     * column, exactly like OUTLINE's contour, rather than a second
     * filled region.
     */
    for (col = 0; col < w; col++) {
        uint16_t left = (col == 0U) ? s_bar_h[col] : s_bar_h[col - 1U];
        uint16_t cur  = s_bar_h[col];
        if (left < cur) {
            s_bar_lo[col] = left;
            s_bar_hi[col] = cur;
        } else {
            s_bar_lo[col] = cur;
            s_bar_hi[col] = left;
        }
    }

    /*
     * Pass 2 (per row, float - only ~h iterations): the vertical
     * gradient color of each row (HEATMAP), or a single flat fill
     * color for every row (LINE - see spectrum_set_style()'s comment
     * in spectrum.h), and whether a dB gridline lands on it. Row
     * `row` on screen corresponds to level (h - row) px from the
     * bottom.
     */
    for (row = 0; row < h; row++) {
        if (s_style == SPECTRUM_STYLE_HEATMAP) {
            uint32_t level = (uint32_t)(h - row);
            int32_t idx = (int32_t)((level * 255U) / h);
            s_row_color[row] = s_lut[idx];
        } else {
            /* LINE reads this as its flat fill color. OUTLINE never
             * reads it (fill_enabled below is 0 for that style) - left
             * assigned anyway so the two styles share this branch. */
            s_row_color[row] = SPEC_LINE_FILL;
        }
        s_row_grid[row] = 0;
    }
    /*
     * Fixed-pixel reference grid (08/09/2026) - see SPECTRUM_GRID_ROWS/
     * COLS's comment in spectrum.h for why these no longer track
     * db_min/db_max or the frequency span. Horizontal: SPECTRUM_GRID_ROWS
     * lines evenly dividing the panel height into that many+1 bands.
     * Vertical: SPECTRUM_GRID_COLS lines evenly dividing the width -
     * with the default of 3, this lands exactly on the panel's
     * quarter/half/three-quarter columns, matching
     * spec_span_labels_draw()'s own tick positions below the panel.
     * Both are cheap fixed-count loops (a handful of iterations each,
     * not per-row/per-column work), safe to redo every draw call even
     * though the panel width can change with zoom.
     */
#if SPECTRUM_GRID_ROWS > 0
    {
        uint8_t gi;
        for (gi = 1U; gi <= SPECTRUM_GRID_ROWS; gi++) {
            uint16_t row_pos = (uint16_t)(((uint32_t)h * gi) / (SPECTRUM_GRID_ROWS + 1U));
            if (row_pos < h) { s_row_grid[row_pos] = 1U; }
        }
    }
#endif
    for (col = 0; col < w; col++) {
        s_col_grid[col] = 0U;
    }
#if SPECTRUM_GRID_COLS > 0
    {
        uint8_t gi;
        for (gi = 1U; gi <= SPECTRUM_GRID_COLS; gi++) {
            uint16_t col_pos = (uint16_t)(((uint32_t)w * gi) / (SPECTRUM_GRID_COLS + 1U));
            if (col_pos < w) { s_col_grid[col_pos] = 1U; }
        }
    }
#endif

    /*
     * Pass 3 (per pixel, INTEGER ONLY): assemble each row stripe in
     * RAM and push it with one gfx_blit per row. Priority per pixel:
     * trace > peak marker > bar fill > gridline > background.
     * bg_color/grid_color/trace_color picked ONCE here (not per
     * pixel) based on the current style - see spectrum_set_style()'s
     * comment in spectrum.h. Peak-hold and center-mark colors stay
     * the same in both styles (informational overlays, not part of
     * the base heat-vs-line aesthetic).
     */
    {
        /* HEATMAP and LINE fill the bar interior; OUTLINE shares
         * LINE's dark-navy palette but leaves fill_enabled clear -
         * see the loop below, where bh > level_from_bottom then just
         * falls through to peak/center/band/background instead of
         * being painted solid. That's the whole "no fill, only the
         * contour" difference; everything else about OUTLINE (trace
         * color, background, gridlines, band tint) is identical to
         * LINE. */
        uint8_t  fill_enabled = (s_style == SPECTRUM_STYLE_OUTLINE) ? 0U : 1U;
        uint16_t bg_color    = (s_style == SPECTRUM_STYLE_HEATMAP) ? GFX_COLOR_BLACK : SPEC_LINE_BG;
        uint16_t grid_color  = (s_style == SPECTRUM_STYLE_HEATMAP) ? SPEC_COLOR_GRID : SPEC_LINE_GRID;
        uint16_t trace_color = (s_style == SPECTRUM_STYLE_HEATMAP) ? SPEC_COLOR_TRACE : SPEC_LINE_TRACE;
        uint16_t band_color  = (s_style == SPECTRUM_STYLE_HEATMAP) ? SPEC_COLOR_BAND_TINT : SPEC_LINE_BAND_TINT;

        for (row = 0; row < h; row++) {
            uint16_t level_from_bottom = (uint16_t)(h - row);
            uint16_t fill  = s_row_color[row];
            uint8_t  row_has_grid = s_row_grid[row];

            for (col = 0; col < w; col++) {
                uint16_t bh = s_bar_h[col];
                uint16_t px;
                uint8_t  is_trace;

                /* Bridge to the left neighbor (see Pass 1.6) - lit if
                 * this row falls anywhere between this column's own
                 * height and its left neighbor's, not just the exact
                 * bh row - so the trace has no gaps on steep edges,
                 * in every style now (used to be OUTLINE-only; the
                 * fill below (bh > level_from_bottom, HEATMAP/LINE
                 * only) still only covers each column's OWN bar, so
                 * this is purely a connecting line on top, same as
                 * OUTLINE's contour - see Pass 1.6's own comment). */
                is_trace = (level_from_bottom >= s_bar_lo[col] &&
                            level_from_bottom <= s_bar_hi[col]) ? 1U : 0U;

                if (is_trace) {
                    /* HEATMAP default: color-matched, not a separate
                     * white highlight - see
                     * spectrum_set_heatmap_trace_white()'s comment in
                     * spectrum.h for why. `fill` is this ROW's own
                     * palette color (same one the bar interior below
                     * uses), so a color-matched trace/bridge pixel is
                     * visually indistinguishable from the bar simply
                     * extending up to meet its neighbor - which reads
                     * as one continuous, correctly-graded bar chart
                     * with no contour artifact. LINE/OUTLINE always
                     * use their own fixed trace_color regardless - see
                     * that setter's comment for why those two are
                     * exempt. */
                    if (s_style == SPECTRUM_STYLE_HEATMAP && !s_heatmap_trace_white) {
                        px = fill;
                    } else {
                        px = trace_color;
                    }
                } else if (fill_enabled && bh > level_from_bottom) {
                    px = fill;                        /* inside the bar (HEATMAP/LINE only) */
#if SPECTRUM_PEAK_HOLD
                } else if (s_peak_h[col] == level_from_bottom) {
                    px = SPEC_COLOR_PEAK;             /* floating peak dot */
#endif
#if SPECTRUM_CENTER_MARK
                } else if (col >= center_mark_col_lo && col <= center_mark_col_hi) {
                    px = SPEC_COLOR_CENTER;           /* demod point marker, under signals */
#endif
                } else if (band_active && col >= band_col_lo && col <= band_col_hi) {
                    px = band_color;                  /* demodulated-bandwidth tint, under everything else */
                } else {
                    /* Fixed reference grid (see SPECTRUM_GRID_ROWS/COLS's
                     * comment in spectrum.h) - either axis lights this
                     * pixel, same grid_color either way (a row/column
                     * crossing doesn't need to look any different from
                     * a plain row or column line). */
                    px = (row_has_grid || s_col_grid[col]) ? grid_color : bg_color;
                }
                s_row_buf[col] = px;
            }
            gfx_blit(x, (uint16_t)(y + row), w, 1, s_row_buf);
        }
    }
}
