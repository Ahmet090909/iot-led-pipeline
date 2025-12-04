#include <stdio.h>
#include <sys/sysinfo.h>
//#include "../workdirectory/pigpio.h"
#include <pigpio.h>


int main() {
    const unsigned int LED_PIN = 26; 

    // Initializeer pigpio
    if (gpioInitialise() < 0) {
        printf("Fout bij initialisatie van pigpio\n");
        return 1;
    }

    gpioSetMode(LED_PIN, PI_OUTPUT);

    printf("LED knipperen op GPIO %d\n", LED_PIN);

    while (1) {
        gpioWrite(LED_PIN, PI_ON);
        gpioDelay(500000);
        gpioWrite(LED_PIN, PI_OFF);
        gpioDelay(500000);
    }

    gpioTerminate(); 
    return 0;
}