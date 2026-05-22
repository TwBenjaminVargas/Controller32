/**
 * @file SerialOutput.c
 * @brief Interfaz serial para el display.
 */

#include "DisplayInterface.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ANSI_RESET  "\033[0m"
#define ANSI_GREEN  "\033[32m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_RED    "\033[31m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_CLEAR  "\033[2J\033[H"   
#define ANSI_HOME   "\033[H"         
#define ANSI_EOL    "\033[K"         

#define MAX_ELEMENTS   32
#define MAX_TEXT       64
#define BAR_WIDTH      20

typedef enum {
    TITLE,
    SUBTITLE,
    TEXT,
    BAR,
    BIPOLAR,
    STATE,
    ALERT
} element_type_t;

typedef struct {
    bool active;
    element_type_t type;

    char text[MAX_TEXT];
    char value[MAX_TEXT];

    int percentage;
    bool state;
    bool alert;
} element_t;

static element_t elements[MAX_ELEMENTS];


static ui_eid_t new_element(element_type_t type) {

    for (int i = 0; i < MAX_ELEMENTS; i++) {

        if (!elements[i].active) {

            memset(&elements[i], 0, sizeof(element_t));

            elements[i].active = true;
            elements[i].type = type;

            return i;
        }
    }
    return -1;
}

static bool valid(ui_eid_t id) {
    return id >= 0 &&
           id < MAX_ELEMENTS &&
           elements[id].active;
}


static void draw_bar(int percentage) {

    int filled = (percentage * BAR_WIDTH) / 100;

    printf("[");
    for (int i = 0; i < BAR_WIDTH; i++) {

        if (i < filled)
            printf("#");
        else
            printf(" ");
    }
    printf("]");
}

static void draw_bipolar(int percentage) {

    int half = BAR_WIDTH / 2;
    int filled = (abs(percentage) * half) / 100;

    printf("[");

    if (percentage < 0) {

        for (int i = 0; i < half - filled; i++)
            printf(" ");

        for (int i = 0; i < filled; i++)
            printf("#");

        printf("|");

        for (int i = 0; i < half; i++)
            printf(" ");

    } else {

        for (int i = 0; i < half; i++)
            printf(" ");

        printf("|");

        for (int i = 0; i < filled; i++)
            printf("#");

        for (int i = 0; i < half - filled; i++)
            printf(" ");
    }

    printf("]");
}


static void render(element_t *e) {

    switch (e->type) {

        case TITLE:
            printf(ANSI_GREEN);
            printf("\n================================================" ANSI_EOL "\n");
            printf("%s" ANSI_EOL "\n", e->text);
            printf("================================================" ANSI_EOL "\n");
            printf(ANSI_RESET);
            break;

        case SUBTITLE:
            printf(ANSI_CYAN "\n[ %s ]" ANSI_EOL "\n" ANSI_RESET, e->text);
            break;

        case TEXT:
            printf(ANSI_YELLOW "%s" ANSI_EOL "\n" ANSI_RESET, e->text);
            break;

        case BAR:
            printf("%-15s ", e->text);
            draw_bar(e->percentage);
            printf(" %d%% (%s)" ANSI_EOL "\n", e->percentage, e->value);
            break;

        case BIPOLAR:
            printf("%-15s ", e->text);
            draw_bipolar(e->percentage);
            printf(" %+d%%" ANSI_EOL "\n", e->percentage);
            break;

        case STATE:
            if (e->state)
                printf("[ON ] %s" ANSI_EOL "\n", e->text);
            else
                printf("[OFF] %s" ANSI_EOL "\n", e->text);
            break;

        case ALERT:
            if (e->alert) {
                printf(ANSI_RED);
                printf("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" ANSI_EOL "\n");
                printf("%s" ANSI_EOL "\n", e->text);
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" ANSI_EOL "\n");
                printf(ANSI_RESET);
            } else {
                printf(ANSI_EOL "\n" ANSI_EOL "\n" ANSI_EOL "\n" ANSI_EOL "\n");
            }
            break;
    }
}


static int impl_initUi(void *args) {
    (void)args;
    memset(elements, 0, sizeof(elements));
    printf(ANSI_CLEAR); 

    return 0;
}

static int impl_refreshUi(void) {

    printf(ANSI_HOME);

    for (int i = 0; i < MAX_ELEMENTS; i++) {
        if (elements[i].active) {
            render(&elements[i]);
        }
    }

    fflush(stdout);
    return 0;
}

static void impl_cleanUi(void) {
    memset(elements, 0, sizeof(elements));
    printf(ANSI_CLEAR);  
}

static void impl_deleteElement(ui_eid_t eid) {
    if (valid(eid)) {
        elements[eid].active = false;
    }
}


static ui_eid_t impl_addTitle(const char *text) {

    ui_eid_t id = new_element(TITLE);
    strcpy(elements[id].text, text);

    return id;
}

static ui_eid_t impl_addSubtitle(const char *text) {

    ui_eid_t id = new_element(SUBTITLE);
    strcpy(elements[id].text, text);

    return id;
}

static ui_eid_t impl_addTextLine(const char *text) {

    ui_eid_t id = new_element(TEXT);
    strcpy(elements[id].text, text);

    return id;
}

static ui_eid_t impl_addProgressBar(const char *title,
                                    int percentage,
                                    const char *value) {

    ui_eid_t id = new_element(BAR);

    strcpy(elements[id].text, title);
    strcpy(elements[id].value, value);

    elements[id].percentage = percentage;

    return id;
}

static ui_eid_t impl_addBipolarProgressBar(const char *title,
                                           int percentage,
                                           const char *value) {

    ui_eid_t id = new_element(BIPOLAR);

    strcpy(elements[id].text, title);
    strcpy(elements[id].value, value);

    elements[id].percentage = percentage;

    return id;
}

static ui_eid_t impl_addStateIndicator(const char *title, bool state) {

    ui_eid_t id = new_element(STATE);

    strcpy(elements[id].text, title);
    elements[id].state = state;

    return id;
}

static ui_eid_t impl_addAlertIndicator(const char *text) {

    ui_eid_t id = new_element(ALERT);
    strcpy(elements[id].text, text);

    return id;
}

static void impl_updateTitle(ui_eid_t eid, const char *text) {
    if(valid(eid)) strcpy(elements[eid].text, text);
}

static void impl_updateSubtitle(ui_eid_t eid, const char *text) {
    if(valid(eid)) strcpy(elements[eid].text, text);
}

static void impl_updateTextLine(ui_eid_t eid, const char *text) {
    if(valid(eid)) strcpy(elements[eid].text, text);
}

static void impl_updateProgressBar(ui_eid_t eid,
                                   int percentage,
                                   const char *value) {

    if(valid(eid)) {
        elements[eid].percentage = percentage;
        strcpy(elements[eid].value, value);
    }
}

static void impl_updateBipolarProgressBar(ui_eid_t eid,
                                          int percentage,
                                          const char *value) {

    if(valid(eid)) {
        elements[eid].percentage = percentage;
        strcpy(elements[eid].value, value);
    }
}

static void impl_updateStateIndicator(ui_eid_t eid, bool state) {

    if(valid(eid)) {
        elements[eid].state = state;
    }
}

static void impl_triggerAlertIndicator(ui_eid_t eid, const char *text) {

    if(valid(eid)) {
        strcpy(elements[eid].text, text);
        elements[eid].alert = true;
    }
}

static void impl_clearAlertIndicator(ui_eid_t eid, const char *text) {

    (void)text;
    if(valid(eid)) {
        elements[eid].alert = false;
    }
}


InterfaceModule UI = {

    .initUi = impl_initUi,
    .refreshUi = impl_refreshUi,
    .cleanUi = impl_cleanUi,
    .deleteElement = impl_deleteElement,

    .addTitle = impl_addTitle,
    .addSubtitle = impl_addSubtitle,
    .addTextLine = impl_addTextLine,
    .updateTitle = impl_updateTitle,
    .updateSubtitle = impl_updateSubtitle,
    .updateTextLine = impl_updateTextLine,

    .addProgressBar = impl_addProgressBar,
    .addBipolarProgressBar = impl_addBipolarProgressBar,
    .updateProgressBar = impl_updateProgressBar,
    .updateBipolarProgressBar = impl_updateBipolarProgressBar,

    .addStateIndicator = impl_addStateIndicator,
    .updateStateIndicator = impl_updateStateIndicator,

    .addAlertIndicator = impl_addAlertIndicator,
    .triggerAlertIndicator = impl_triggerAlertIndicator,
    .clearAlertIndicator = impl_clearAlertIndicator
};
