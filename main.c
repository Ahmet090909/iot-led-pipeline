#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

#include <pigpio.h>
#include "lib/cjson/cJSON.h"

/* =========================
   Config (defaults)
   ========================= */
typedef struct {
    /* GPIO pins */
    int ns_r, ns_y, ns_g;
    int ew_r, ew_y, ew_g;
    int btn_night;

    /* timings (seconds) */
    int t_green;
    int t_yellow;
    int t_allred;

    /* night mode */
    int night_blink_ms;
} AppConfig;

static AppConfig cfg = {
    .ns_r = 26, .ns_y = 19, .ns_g = 13,
    .ew_r = 6,  .ew_y = 5,  .ew_g = 22,
    .btn_night = 23,
    .t_green = 5, .t_yellow = 2, .t_allred = 1,
    .night_blink_ms = 500
};

static volatile int running = 1;
static volatile int night_mode = 0;

/* Debounce */
static volatile uint32_t last_tick = 0;
#define DEBOUNCE_MS 250

/* Detect whether we are on real Pi or not */
static int gpio_available = 0;

/* =========================
   Helpers: file read + JSON
   ========================= */
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

static void load_config_json(const char* path) {
    char* text = read_file_all(path);
    if (!text) {
        printf("[CFG] %s not found -> using defaults\n", path);
        return;
    }

    cJSON* root = cJSON_Parse(text);
    free(text);

    if (!root) {
        printf("[CFG] JSON parse failed -> using defaults\n");
        return;
    }

    cJSON* pins = cJSON_GetObjectItemCaseSensitive(root, "pins");
    if (cJSON_IsObject(pins)) {
        cfg.ns_r = json_get_int(pins, "ns_r", cfg.ns_r);
        cfg.ns_y = json_get_int(pins, "ns_y", cfg.ns_y);
        cfg.ns_g = json_get_int(pins, "ns_g", cfg.ns_g);
        cfg.ew_r = json_get_int(pins, "ew_r", cfg.ew_r);
        cfg.ew_y = json_get_int(pins, "ew_y", cfg.ew_y);
        cfg.ew_g = json_get_int(pins, "ew_g", cfg.ew_g);
        cfg.btn_night = json_get_int(pins, "btn_night", cfg.btn_night);
    }

    cJSON* timing = cJSON_GetObjectItemCaseSensitive(root, "timing_seconds");
    if (cJSON_IsObject(timing)) {
        cfg.t_green  = json_get_int(timing, "green", cfg.t_green);
        cfg.t_yellow = json_get_int(timing, "yellow", cfg.t_yellow);
        cfg.t_allred = json_get_int(timing, "all_red", cfg.t_allred);
    }

    cJSON* night = cJSON_GetObjectItemCaseSensitive(root, "night_mode");
    if (cJSON_IsObject(night)) {
        cfg.night_blink_ms = json_get_int(night, "blink_ms", cfg.night_blink_ms);
    }

    cJSON_Delete(root);

    printf("[CFG] Loaded config:\n");
    printf("      PINS: NS(R,Y,G)=(%d,%d,%d) EW(R,Y,G)=(%d,%d,%d) BTN=%d\n",
           cfg.ns_r, cfg.ns_y, cfg.ns_g, cfg.ew_r, cfg.ew_y, cfg.ew_g, cfg.btn_night);
    printf("      TIMING: green=%ds yellow=%ds all_red=%ds\n",
           cfg.t_green, cfg.t_yellow, cfg.t_allred);
    printf("      NIGHT: blink=%dms\n", cfg.night_blink_ms);
}

/* =========================
   State machine
   ========================= */
typedef enum {
    S_NS_GREEN,
    S_NS_YELLOW,
    S_ALLRED_1,
    S_EW_GREEN,
    S_EW_YELLOW,
    S_ALLRED_2
} state_t;

static const char* state_name(state_t s) {
    switch (s) {
        case S_NS_GREEN:  return "NS_GREEN";
        case S_NS_YELLOW: return "NS_YELLOW";
        case S_ALLRED_1:  return "ALL_RED";
        case S_EW_GREEN:  return "EW_GREEN";
        case S_EW_YELLOW: return "EW_YELLOW";
        case S_ALLRED_2:  return "ALL_RED";
        default:          return "UNKNOWN";
    }
}

static int state_duration(state_t s) {
    switch (s) {
        case S_NS_GREEN:  return cfg.t_green;
        case S_NS_YELLOW: return cfg.t_yellow;
        case S_ALLRED_1:  return cfg.t_allred;
        case S_EW_GREEN:  return cfg.t_green;
        case S_EW_YELLOW: return cfg.t_yellow;
        case S_ALLRED_2:  return cfg.t_allred;
        default:          return 1;
    }
}

static state_t next_state(state_t s) {
    switch (s) {
        case S_NS_GREEN:  return S_NS_YELLOW;
        case S_NS_YELLOW: return S_ALLRED_1;
        case S_ALLRED_1:  return S_EW_GREEN;
        case S_EW_GREEN:  return S_EW_YELLOW;
        case S_EW_YELLOW: return S_ALLRED_2;
        case S_ALLRED_2:  return S_NS_GREEN;
        default:          return S_NS_GREEN;
    }
}

/* =========================
   GPIO helpers (real vs sim)
   ========================= */
static void sim_log_leds(int ns_r, int ns_y, int ns_g, int ew_r, int ew_y, int ew_g) {
    printf("[SIM] NS(R,Y,G)=(%d,%d,%d)  EW(R,Y,G)=(%d,%d,%d)\n",
           ns_r, ns_y, ns_g, ew_r, ew_y, ew_g);
}

static void all_off(void) {
    if (!gpio_available) {
        sim_log_leds(0,0,0,0,0,0);
        return;
    }
    gpioWrite(cfg.ns_r, 0); gpioWrite(cfg.ns_y, 0); gpioWrite(cfg.ns_g, 0);
    gpioWrite(cfg.ew_r, 0); gpioWrite(cfg.ew_y, 0); gpioWrite(cfg.ew_g, 0);
}

static void set_ns(int r, int y, int g) {
    if (!gpio_available) {
        // We'll print in apply_state
        (void)r; (void)y; (void)g;
        return;
    }
    gpioWrite(cfg.ns_r, r); gpioWrite(cfg.ns_y, y); gpioWrite(cfg.ns_g, g);
}

static void set_ew(int r, int y, int g) {
    if (!gpio_available) {
        (void)r; (void)y; (void)g;
        return;
    }
    gpioWrite(cfg.ew_r, r); gpioWrite(cfg.ew_y, y); gpioWrite(cfg.ew_g, g);
}

static void apply_state(state_t s) {
    int ns_r=0, ns_y=0, ns_g=0, ew_r=0, ew_y=0, ew_g=0;

    switch (s) {
        case S_NS_GREEN:
            ns_g=1; ew_r=1;
            break;
        case S_NS_YELLOW:
            ns_y=1; ew_r=1;
            break;
        case S_ALLRED_1:
            ns_r=1; ew_r=1;
            break;
        case S_EW_GREEN:
            ns_r=1; ew_g=1;
            break;
        case S_EW_YELLOW:
            ns_r=1; ew_y=1;
            break;
        case S_ALLRED_2:
            ns_r=1; ew_r=1;
            break;
        default:
            ns_r=1; ew_r=1;
            break;
    }

    if (!gpio_available) {
        printf("[SIM] STATE=%s (for %ds)\n", state_name(s), state_duration(s));
        sim_log_leds(ns_r, ns_y, ns_g, ew_r, ew_y, ew_g);
        return;
    }

    all_off();
    set_ns(ns_r, ns_y, ns_g);
    set_ew(ew_r, ew_y, ew_g);
}

/* Night mode loop */
static void run_night_mode_loop(void) {
    printf("[%s] Night mode ON\n", gpio_available ? "GPIO" : "SIM");

    int on = 0;
    while (running && night_mode) {
        on = !on;

        if (!gpio_available) {
            printf("[SIM] NIGHT BLINK %s\n", on ? "ON" : "OFF");
            sim_log_leds(0,on,0, 0,on,0);
        } else {
            all_off();
            gpioWrite(cfg.ns_y, on);
            gpioWrite(cfg.ew_y, on);
        }

        // sleep in chunks for fast exit
        int remaining = cfg.night_blink_ms;
        while (remaining > 0 && running && night_mode) {
            int step = remaining > 100 ? 100 : remaining;
            usleep(step * 1000);
            remaining -= step;
        }
    }

    printf("[%s] Night mode OFF\n", gpio_available ? "GPIO" : "SIM");

    // safe all-red short
    if (!gpio_available) {
        printf("[SIM] Safe ALL_RED 1s\n");
        sim_log_leds(1,0,0, 1,0,0);
        sleep(1);
        return;
    }

    all_off();
    gpioWrite(cfg.ns_r, 1);
    gpioWrite(cfg.ew_r, 1);
    sleep(1);
}

/* Button ISR (Pi only) */
static void btn_isr(int gpio, int level, uint32_t tick) {
    (void)gpio;
    if (level != 0) return; // falling edge only

    if ((tick - last_tick) < (DEBOUNCE_MS * 1000)) return;
    last_tick = tick;

    night_mode = !night_mode;
}

/* SIGINT handler */
static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
}

int main(void) {
    signal(SIGINT, handle_sigint);

    /* 1) Load JSON config (cJSON really used) */
    load_config_json("config.json");

    /* 2) Init pigpio (may fail in container) */
    int ret = gpioInitialise();
    if (ret < 0) {
        gpio_available = 0;
        printf("[SIM] pigpio init failed (expected without Raspberry Pi)\n");
        printf("[SIM] Running intersection logic in simulation mode.\n");
    } else {
        gpio_available = 1;
        printf("[GPIO] pigpio initialised OK\n");

        /* Setup outputs */
        gpioSetMode(cfg.ns_r, PI_OUTPUT);
        gpioSetMode(cfg.ns_y, PI_OUTPUT);
        gpioSetMode(cfg.ns_g, PI_OUTPUT);
        gpioSetMode(cfg.ew_r, PI_OUTPUT);
        gpioSetMode(cfg.ew_y, PI_OUTPUT);
        gpioSetMode(cfg.ew_g, PI_OUTPUT);

        /* Setup button */
        gpioSetMode(cfg.btn_night, PI_INPUT);
        gpioSetPullUpDown(cfg.btn_night, PI_PUD_UP); // button to GND
        gpioSetAlertFunc(cfg.btn_night, btn_isr);

        /* safe start all-red */
        all_off();
        gpioWrite(cfg.ns_r, 1);
        gpioWrite(cfg.ew_r, 1);
        sleep(1);
    }

    /* 3) Run state machine */
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

    /* 4) Cleanup */
    if (gpio_available) {
        all_off();
        gpioTerminate();
    }
    printf("Stopped.\n");
    return 0;
}