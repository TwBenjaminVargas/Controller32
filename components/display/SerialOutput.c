/**
 * @file SerialOutput.c
 * @brief Implementación del motor gráfico agnóstico para salida serial.
 */

#include "DisplayInterface.h"
#include <stdio.h>
#include <string.h>

static int impl_initUi(void *optionalArgs) {

    return 0; 
}

static int impl_refreshUi(void) {
    return 0;
}

static void impl_cleanUi(void) {
}

static void impl_deleteElement(ui_eid_t eid) {
}

static ui_eid_t impl_addTitle(const char *text) {
    return -1;
}

static ui_eid_t impl_addSubtitle(const char *text) {

    return -1;
}

static ui_eid_t impl_addTextLine(const char *text) {
    return -1;
}

static void impl_updateTitle(ui_eid_t eid, const char *text) {
}

static void impl_updateSubtitle(ui_eid_t eid, const char *text) {
}

static void impl_updateTextLine(ui_eid_t eid, const char *text) {

}

static ui_eid_t impl_addProgressBar(const char *title, int percentage, const char *value) {
    return -1;
}

static ui_eid_t impl_addBipolarProgressBar(const char *title, int percentage, const char *value) {

    return -1;
}

static void impl_updateProgressBar(ui_eid_t eid, int percentage, const char *value) {
}

static void impl_updateBipolarProgressBar(ui_eid_t eid, int percentage, const char *value) {

}

static ui_eid_t impl_addStateIndicator(const char *title, bool state) {
    return -1;
}

static void impl_updateStateIndicator(ui_eid_t eid, bool state) {

}

static ui_eid_t impl_addAlertIndicator(const char *text) {
    return -1;
}

static void impl_triggerAlertIndicator(ui_eid_t eid, const char *text) {
}

static void impl_clearAlertIndicator(ui_eid_t eid, const char *text) {

}

InterfaceModule UI = {
    .initUi                 = impl_initUi,
    .refreshUi              = impl_refreshUi,
    .cleanUi                = impl_cleanUi,
    .deleteElement          = impl_deleteElement,
    
    .addTitle               = impl_addTitle,
    .addSubtitle            = impl_addSubtitle,
    .addTextLine            = impl_addTextLine,
    .updateTitle            = impl_updateTitle,
    .updateSubtitle         = impl_updateSubtitle,
    .updateTextLine         = impl_updateTextLine,
    
    .addProgressBar         = impl_addProgressBar,
    .addBipolarProgressBar  = impl_addBipolarProgressBar,
    .updateProgressBar      = impl_updateProgressBar,
    .updateBipolarProgressBar = impl_updateBipolarProgressBar,
    
    .addStateIndicator      = impl_addStateIndicator,
    .updateStateIndicator   = impl_updateStateIndicator,
    
    .addAlertIndicator      = impl_addAlertIndicator,
    .triggerAlertIndicator  = impl_triggerAlertIndicator,
    .clearAlertIndicator    = impl_clearAlertIndicator
};