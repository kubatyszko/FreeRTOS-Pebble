#include "rebbleos.h"
#include "systemapp.h"
#include "menu.h"
#include "status_bar_layer.h"
#include "test_defs.h"

static void _test_test_layer_update_proc(Layer *layer, GContext *ctx);

static n_GColor colors[] = {
    GColorOxfordBlue,
    GColorDarkBlue,
    GColorBlue,
    GColorDarkGreen,
    GColorMidnightGreen,
    GColorCobaltBlue,
    GColorBlueMoon,
    GColorIslamicGreen,
    GColorJaegerGreen,
    GColorTiffanyBlue,
    GColorVividCerulean,
    GColorGreen,
    GColorMalachite,
    GColorMediumSpringGreen,
    GColorCyan,
    GColorBulgarianRose,
    GColorImperialPurple,
    GColorIndigo,
    GColorElectricUltramarine,
    GColorArmyGreen,
    GColorDarkGray,
    GColorLiberty,
    GColorVeryLightBlue,
    GColorKellyGreen,
    GColorMayGreen,
    GColorCadetBlue,
    GColorPictonBlue,
    GColorBrightGreen,
    GColorScreaminGreen,
    GColorMediumAquamarine,
    GColorElectricBlue,
    GColorDarkCandyAppleRed,
    GColorJazzberryJam,
    GColorPurple,
    GColorVividViolet,
    GColorWindsorTan, 
    GColorRoseVale,
    GColorPurpureus,
    GColorLavenderIndigo,
    GColorLimerick,
    GColorBrass,
    GColorLightGray,
    GColorBabyBlueEyes,
    GColorSpringBud,
    GColorInchworm,
    GColorMintGreen,
    GColorCeleste,
    GColorRed,
    GColorFolly,
    GColorFashionMagenta,
    GColorMagenta,
    GColorOrange,
    GColorSunsetOrange,
    GColorBrilliantRose,
    GColorShockingPink,
    GColorChromeYellow,
    GColorRajah,
    GColorMelon,
    GColorRichBrilliantLavender,
    GColorYellow,
    GColorIcterine,
    GColorPastelYellow,
    GColorWhite,
    GColorBlack
};

bool test_test_init(Window *window)
{
    SYS_LOG("test", APP_LOG_LEVEL_ERROR, "Init: Test Test");
    return true;
}

bool test_test_exec(void)
{
    SYS_LOG("test", APP_LOG_LEVEL_ERROR, "Exec: Test Test");
    return true;
}

bool test_test_deinit(void)
{
    SYS_LOG("test", APP_LOG_LEVEL_ERROR, "De-Init: Test Test");
    return true;
}
