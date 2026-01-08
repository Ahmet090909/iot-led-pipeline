#include <pigpio.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

#include "lib/cjson/cJSON.h"

// Raspberry Pi 4 (BCM GPIO numbering)

// NS RGB
static int NS_R = 26;
static int NS_G = 13;
static int NS_B = 19;   // not used (kept OFF)

// EW RGB
static int EW_R = 6;
static int EW_G = 22;
static int EW_B = 5;    // not used (kept OFF)

static int BTN_NIGHT = 23; // button to GND

// Timings (seconds)
static int T_GREEN  = 5;
static int T_YELLOW = 2;
static int T_ALLRED = 1;

// Night mode blink (ms)
static int NIGHT_BLINK_MS = 500;

static volatile int running = 1;
static volatile int night_mode = 0;

// debounce
static volatile uint32_t last_tick = 0;
#define DEBOUNCE_MS 250

typedef enum {
    S_NS_GREEN,
    S_NS_YELLOW,
    S_ALLRED_1,
    S_EW_GREEN,
    S_EW_YELLOW,
    S_ALLRED_2
} state_t;

static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}

/* ---------------- JSON (optional) ---------------- */

static char* read_file_all(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) { fclose(f); return NULL; }

    char* data = (char*)malloc((size_t)len + 1);
    if (!data) { fclose(f); return NULL; }

    size_t n = fread(data, 1, (size_t)len, f);
    fclose(f);
    data[n] = '\0';
    return data;
}

static int json_get_int(cJSON* obj, const char* key, int fallback) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(v)) return v->valueint;
    return fallback;
}

static void load_config(const char* path) {
    char* text = read_file_all(path);
    if (!text) return;

    cJSON* root = cJSON_Parse(text);
    free(text);
    if (!root) return;

    cJSON* pins = cJSON_GetObjectItemCaseSensitive(root, "pins");
    if (cJSON_IsObject(pins)) {
        NS_R = json_get_int(pins, "ns_r", NS_R);
        NS_G = json_get_int(pins, "ns_g", NS_G);
        NS_B = json_get_int(pins, "ns_b", NS_B);

        EW_R = json_get_int(pins, "ew_r", EW_R);
        EW_G = json_get_int(pins, "ew_g", EW_G);
        EW_B = json_get_int(pins, "ew_b", EW_B);

        BTN_NIGHT = json_get_int(pins, "btn_night", BTN_NIGHT);
    }

    cJSON* timing = cJSON_GetObjectItemCaseSensitive(root, "timing_seconds");
    if (cJSON_IsObject(timing)) {
        T_GREEN  = json_get_int(timing, "green", T_GREEN);
        T_YELLOW = json_get_int(timing, "yellow", T_YELLOW);
        T_ALLRED = json_get_int(timing, "all_red", T_ALLRED);
    }

    cJSON* night = cJSON_GetObjectItemCaseSensitive(root, "night_mode");
    if (cJSON_IsObject(night)) {
        NIGHT_BLINK_MS = json_get_int(night, "blink_ms", NIGHT_BLINK_MS);
    }

    cJSON_Delete(root);
}

/* ---------------- LED helpers (common cathode: 1=ON) ---------------- */

static void all_off(void) {
    gpioWrite(NS_R, 0); gpioWrite(NS_G, 0); gpioWrite(NS_B, 0);
    gpioWrite(EW_R, 0); gpioWrite(EW_G, 0); gpioWrite(EW_B, 0);
}

static void ns_red(void)    { gpioWrite(NS_R, 1); gpioWrite(NS_G, 0); gpioWrite(NS_B, 0); }
static void ns_green(void)  { gpioWrite(NS_R, 0); gpioWrite(NS_G, 1); gpioWrite(NS_B, 0); }
static void ns_yellow(void) { gpioWrite(NS_R, 1); gpioWrite(NS_G, 1); gpioWrite(NS_B, 0); } // R+G

static void ew_red(void)    { gpioWrite(EW_R, 1); gpioWrite(EW_G, 0); gpioWrite(EW_B, 0); }
static void ew_green(void)  { gpioWrite(EW_R, 0); gpioWrite(EW_G, 1); gpioWrite(EW_B, 0); }
static void ew_yellow(void) { gpioWrite(EW_R, 1); gpioWrite(EW_G, 1); gpioWrite(EW_B, 0); } // R+G

static void apply_state(state_t s) {
    all_off();
    switch (s) {
        case S_NS_GREEN:
            ns_green();
            ew_red();
            break;
        case S_NS_YELLOW:
            ns_yellow();
            ew_red();
            break;
        case S_ALLRED_1:
            ns_red();
            ew_red();
            break;
        case S_EW_GREEN:
            ns_red();
            ew_green();
            break;
        case S_EW_YELLOW:
            ns_red();
            ew_yellow();
            break;
        case S_ALLRED_2:
            ns_red();
            ew_red();
            break;
    }
}

static int state_duration(state_t s) {
    switch (s) {
        case S_NS_GREEN:   return T_GREEN;
        case S_NS_YELLOW:  return T_YELLOW;
        case S_ALLRED_1:   return T_ALLRED;
        case S_EW_GREEN:   return T_GREEN;
        case S_EW_YELLOW:  return T_YELLOW;
        case S_ALLRED_2:   return T_ALLRED;
        default:           return 1;
    }
}

static state_t next_state(state_t s) {
    switch (s) {
        case S_NS_GREEN:   return S_NS_YELLOW;
        case S_NS_YELLOW:  return S_ALLRED_1;
        case S_ALLRED_1:   return S_EW_GREEN;
        case S_EW_GREEN:   return S_EW_YELLOW;
        case S_EW_YELLOW:  return S_ALLRED_2;
        case S_ALLRED_2:   return S_NS_GREEN;
        default:           return S_NS_GREEN;
    }
}

// Night mode: blink yellow both directions (R+G)
static void run_night_mode_loop(void) {
    all_off();
    int on = 0;

    while (running && night_mode) {
        on = !on;

        gpioWrite(NS_R, on);
        gpioWrite(NS_G, on);
        gpioWrite(NS_B, 0);

        gpioWrite(EW_R, on);
        gpioWrite(EW_G, on);
        gpioWrite(EW_B, 0);

        int chunks = NIGHT_BLINK_MS / 100;
        if (chunks < 1) chunks = 1;

        for (int i = 0; i < chunks && running && night_mode; i++) {
            usleep(100000); // 100ms
        }
    }

    all_off();
    ns_red();
    ew_red();
    sleep(1);
}

/* ---------------- Button ISR ---------------- */

static void btn_isr(int gpio, int level, uint32_t tick) {
    (void)gpio;
    if (level != 0) return; // falling edge only (button to GND)

    if ((tick - last_tick) < (DEBOUNCE_MS * 1000)) return;
    last_tick = tick;

    night_mode = !night_mode;
}

int main(void) {
    signal(SIGINT, handle_sigint);

    // safe if config.json not present
    load_config("config.json");

    if (gpioInitialise() < 0) {
        fprintf(stderr, "pigpio init failed\n");
        return 1;
    }

    // outputs
    gpioSetMode(NS_R, PI_OUTPUT);
    gpioSetMode(NS_G, PI_OUTPUT);
    gpioSetMode(NS_B, PI_OUTPUT);

    gpioSetMode(EW_R, PI_OUTPUT);
    gpioSetMode(EW_G, PI_OUTPUT);
    gpioSetMode(EW_B, PI_OUTPUT);

    // button input with internal pull-up
    gpioSetMode(BTN_NIGHT, PI_INPUT);
    gpioSetPullUpDown(BTN_NIGHT, PI_PUD_UP);
    gpioSetAlertFunc(BTN_NIGHT, btn_isr);

    // start safe: all-red
    all_off();
    ns_red();
    ew_red();
    sleep(1);

    state_t s = S_NS_GREEN;

    while (running) {
        if (night_mode) {
            run_night_mode_loop();
            s = S_NS_GREEN;
            continue;
        }

        apply_state(s);

        int d = state_duration(s);
        for (int i = 0; i < d && running && !night_mode; i++) {
            sleep(1);
        }

        if (!night_mode) {
            s = next_state(s);
        }
    }

    all_off();
    gpioTerminate();
    return 0;
}
