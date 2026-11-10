#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    unsigned int count = 0;

    while (1) {
        const char *motion = (count % 2 == 0) ? "active" : "motionless";
        const char *button = (count % 4 == 3) ? "pressed" : "released";

        printf("motion=%s, button=%s\n", motion, button);

        count++;
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}