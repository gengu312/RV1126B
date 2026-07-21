#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

enum {
    kSourceOffset = 8,
    kSenseOffset = 9,
    kPollMilliseconds = 20,
    kDebounceSamples = 3,
};

static volatile sig_atomic_t keep_running = 1;

static void stop_running(int signal_number)
{
    (void)signal_number;
    keep_running = 0;
}

static int request_input(int chip_fd)
{
    struct gpio_v2_line_request request;
    memset(&request, 0, sizeof(request));
    request.offsets[0] = kSenseOffset;
    request.num_lines = 1;
    request.config.flags = GPIO_V2_LINE_FLAG_INPUT;
    snprintf(request.consumer, sizeof(request.consumer), "pressure-sense");
    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &request) < 0)
        return -1;
    return request.fd;
}

static int request_source_high(int chip_fd)
{
    struct gpio_v2_line_request request;
    memset(&request, 0, sizeof(request));
    request.offsets[0] = kSourceOffset;
    request.num_lines = 1;
    request.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    request.config.num_attrs = 1;
    request.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    request.config.attrs[0].attr.values = 1;
    request.config.attrs[0].mask = 1;
    snprintf(request.consumer, sizeof(request.consumer), "pressure-source");
    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &request) < 0)
        return -1;
    return request.fd;
}

static int read_line(int line_fd, unsigned char *value)
{
    struct gpio_v2_line_values values = {.mask = 1};
    if (ioctl(line_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0)
        return -1;
    *value = (values.bits & 1) != 0;
    return 0;
}

static void release_source_safely(int source_fd)
{
    struct gpio_v2_line_config config;
    memset(&config, 0, sizeof(config));
    config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    config.num_attrs = 1;
    config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    config.attrs[0].attr.values = 0;
    config.attrs[0].mask = 1;
    (void)ioctl(source_fd, GPIO_V2_LINE_SET_CONFIG_IOCTL, &config);

    memset(&config, 0, sizeof(config));
    config.flags = GPIO_V2_LINE_FLAG_INPUT;
    (void)ioctl(source_fd, GPIO_V2_LINE_SET_CONFIG_IOCTL, &config);
}

int main(void)
{
    const char *chip_path = "/dev/gpiochip3";
    int chip_fd = open(chip_path, O_RDONLY | O_CLOEXEC);
    if (chip_fd < 0) {
        fprintf(stderr, "open %s: %s\n", chip_path, strerror(errno));
        return 1;
    }

    const int input_fd = request_input(chip_fd);
    if (input_fd < 0) {
        fprintf(stderr, "request GPIO3_B1 input: %s\n", strerror(errno));
        close(chip_fd);
        return 2;
    }

    const int source_fd = request_source_high(chip_fd);
    if (source_fd < 0) {
        fprintf(stderr, "request GPIO3_B0 output-high: %s\n", strerror(errno));
        close(input_fd);
        close(chip_fd);
        return 3;
    }
    close(chip_fd);

    signal(SIGINT, stop_running);
    signal(SIGTERM, stop_running);

    puts("READY: connect the sensor only between physical pins 31 and 29.");
    puts("Do not connect either sensor lead to 3.3V, 1.8V, 5V, or GND.");
    puts("LOW=RELEASED, HIGH=PRESSED. Press Ctrl+C to stop.");
    fflush(stdout);

    int stable_value = -1;
    int candidate_value = -1;
    int candidate_count = 0;
    const struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = kPollMilliseconds * 1000L * 1000L,
    };

    while (keep_running) {
        unsigned char value = 0;
        if (read_line(input_fd, &value) < 0) {
            fprintf(stderr, "read GPIO3_B1: %s\n", strerror(errno));
            release_source_safely(source_fd);
            close(source_fd);
            close(input_fd);
            return 4;
        }

        if ((int)value != candidate_value) {
            candidate_value = value;
            candidate_count = 1;
        } else if (candidate_count < kDebounceSamples) {
            ++candidate_count;
        }

        if (candidate_count >= kDebounceSamples && candidate_value != stable_value) {
            stable_value = candidate_value;
            puts(stable_value == 0 ? "RELEASED" : "PRESSED");
            fflush(stdout);
        }
        nanosleep(&delay, NULL);
    }

    release_source_safely(source_fd);
    close(source_fd);
    close(input_fd);
    return 0;
}
