#include <pigpio.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include "lib/cjson/cJSON.h"


#define NS_R 26
#define NS_Y 19
#define NS_G 13

#define EW_R 6
#define EW_Y 5
#define EW_G 22

#define BTN_NIGHT 23   // drukknop naar GND

// Timings (seconden) - demo waarden
#define T_GREEN   5
#define T_YELLOW  2
#define T_ALLRED  1

static volatile int running = 1;

// Night mode flag (toggle met knop)
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

static void all_off(void) {
    gpioWrite(NS_R, 0); gpioWrite(NS_Y, 0); gpioWrite(NS_G, 0);
    gpioWrite(EW_R, 0); gpioWrite(EW_Y, 0); gpioWrite(EW_G, 0);
}

static void set_ns(int r, int y, int g) {
    gpioWrite(NS_R, r); gpioWrite(NS_Y, y); gpioWrite(NS_G, g);
}

static void set_ew(int r, int y, int g) {
    gpioWrite(EW_R, r); gpioWrite(EW_Y, y); gpioWrite(EW_G, g);
}

static void apply_state(state_t s) {
    all_off();
    switch (s) {
        case S_NS_GREEN:
            set_ns(0,0,1);
            set_ew(1,0,0);
            break;
        case S_NS_YELLOW:
            set_ns(0,1,0);
            set_ew(1,0,0);
            break;
        case S_ALLRED_1:
            set_ns(1,0,0);
            set_ew(1,0,0);
            break;
        case S_EW_GREEN:
            set_ns(1,0,0);
            set_ew(0,0,1);
            break;
        case S_EW_YELLOW:
            set_ns(1,0,0);
            set_ew(0,1,0);
            break;
        case S_ALLRED_2:
            set_ns(1,0,0);
            set_ew(1,0,0);
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

// Night mode: beide richtingen knipperend geel
static void run_night_mode_loop(void) {
    // In night mode: rood en groen uit, geel knippert
    all_off();
    set_ns(0,0,0);
    set_ew(0,0,0);

    int on = 0;
    while (running && night_mode) {
        on = !on;
        gpioWrite(NS_Y, on);
        gpioWrite(EW_Y, on);
        // korte sleep met "snelle exit" check
        for (int i = 0; i < 5 && running && night_mode; i++) {
            usleep(100000); // 100ms -> totaal 500ms
        }
    }

    // bij exit: alles uit + safe all-red kort
    all_off();
    set_ns(1,0,0);
    set_ew(1,0,0);
    sleep(1);
}

static void btn_isr(int gpio, int level, uint32_t tick) {
    (void)gpio;
    // we willen toggle op falling edge (druk)
    if (level != 0) return;

    // debounce
    if ((tick - last_tick) < (DEBOUNCE_MS * 1000)) return;
    last_tick = tick;

    night_mode = !night_mode;
}

int main(void) {
    signal(SIGINT, handle_sigint);

    if (gpioInitialise() < 0) {
        fprintf(stderr, "pigpio init failed\n");
        return 1;
    }

    // outputs
    gpioSetMode(NS_R, PI_OUTPUT);
    gpioSetMode(NS_Y, PI_OUTPUT);
    gpioSetMode(NS_G, PI_OUTPUT);
    gpioSetMode(EW_R, PI_OUTPUT);
    gpioSetMode(EW_Y, PI_OUTPUT);
    gpioSetMode(EW_G, PI_OUTPUT);

    // button input
    gpioSetMode(BTN_NIGHT, PI_INPUT);
    gpioSetPullUpDown(BTN_NIGHT, PI_PUD_UP); // knop naar GND
    gpioSetAlertFunc(BTN_NIGHT, btn_isr);

    // start safe: all-red
    all_off();
    set_ns(1,0,0);
    set_ew(1,0,0);
    sleep(1);

    state_t s = S_NS_GREEN;

    while (running) {
        if (night_mode) {
            run_night_mode_loop();
            // als night mode uit gaat, start veilig opnieuw
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
